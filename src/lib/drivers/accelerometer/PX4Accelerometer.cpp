/****************************************************************************
 *
 *   Copyright (c) 2018-2020 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/


#include "PX4Accelerometer.hpp"

#include <lib/drivers/device/Device.hpp>

using namespace time_literals;
using matrix::Vector3f;

static constexpr int32_t sum(const int16_t samples[16], uint8_t len)
{
	int32_t sum = 0;

	for (int n = 0; n < len; n++) {
		sum += samples[n];
	}

	return sum;
}

static constexpr unsigned clipping(const int16_t samples[16], int16_t clip_limit, uint8_t len)
{
	unsigned clip_count = 0;

	for (int n = 0; n < len; n++) {
		if (abs(samples[n]) >= clip_limit) {
			clip_count++;
		}
	}

	return clip_count;
}

PX4Accelerometer::PX4Accelerometer(uint32_t device_id, uint8_t priority, enum Rotation rotation) :
	_sensor_pub{ORB_ID(sensor_accel), priority},
	_sensor_fifo_pub{ORB_ID(sensor_accel_fifo), priority},
	_sensor_integrated_pub{ORB_ID(sensor_accel_integrated), priority},
	_sensor_status_pub{ORB_ID(sensor_accel_status), priority},
	_device_id{device_id},
	_rotation{rotation},
	_rotation_dcm{get_rot_matrix(rotation)}
{
}

void PX4Accelerometer::set_device_type(uint8_t devtype)
{
	// current DeviceStructure
	union device::Device::DeviceId device_id;
	device_id.devid = _device_id;

	// update to new device type
	device_id.devid_s.devtype = devtype;

	// copy back
	_device_id = device_id.devid;
}

void PX4Accelerometer::set_update_rate(uint16_t rate)
{
	_update_rate = rate;
	const uint32_t update_interval = 1000000 / rate;

	// TODO: set this intelligently
	_integrator_reset_samples = 2500 / update_interval;
}

void PX4Accelerometer::update(const hrt_abstime &timestamp_sample, float x, float y, float z)
{
	// Apply rotation (before scaling)
	rotate_3f(_rotation, x, y, z);

	const Vector3f raw{x, y, z};

	// Clipping (check unscaled raw values)
	for (int i = 0; i < 3; i++) {
		if (fabsf(raw(i)) > _clip_limit) {
			_clipping_total[i]++;
			_integrator_clipping(i)++;
		}
	}

	// Apply range scale
	const Vector3f val{raw * _scale};

	// publish raw data immediately
	{
		sensor_accel_s report;

		report.timestamp_sample = timestamp_sample;
		report.device_id = _device_id;
		report.temperature = _temperature;
		report.x = val(0);
		report.y = val(1);
		report.z = val(2);
		report.timestamp = hrt_absolute_time();

		_sensor_pub.publish(report);
	}

	// Integrated values
	Vector3f delta_velocity;
	uint32_t integral_dt = 0;

	_integrator_samples++;

	if (_integrator.put(timestamp_sample, val, delta_velocity, integral_dt)) {

		// fill sensor_accel_integrated and publish
		sensor_accel_integrated_s report;

		report.timestamp_sample = timestamp_sample;
		report.error_count = _error_count;
		report.device_id = _device_id;
		delta_velocity.copyTo(report.delta_velocity);
		report.dt = integral_dt;
		report.samples = _integrator_samples;

		for (int i = 0; i < 3; i++) {
			report.clip_counter[i] = fabsf(roundf(_integrator_clipping(i)));
		}

		report.timestamp = hrt_absolute_time();

		_sensor_integrated_pub.publish(report);


		// reset integrator
		ResetIntegrator();

		// update vibration metrics
		UpdateVibrationMetrics(delta_velocity);
	}

	PublishStatus();
}

void PX4Accelerometer::updateFIFO(const FIFOSample &sample)
{
	const uint8_t N = sample.samples;
	const float dt = sample.dt;

	// publish raw data immediately
	{
		// average
		float x = (float)sum(sample.x, N) / (float)N;
		float y = (float)sum(sample.y, N) / (float)N;
		float z = (float)sum(sample.z, N) / (float)N;

		// Apply rotation (before scaling)
		rotate_3f(_rotation, x, y, z);

		// Apply range scale
		const Vector3f val{Vector3f{x, y, z} *_scale};

		sensor_accel_s report;

		report.timestamp_sample = sample.timestamp_sample;
		report.device_id = _device_id;
		report.temperature = _temperature;
		report.x = val(0);
		report.y = val(1);
		report.z = val(2);
		report.timestamp = hrt_absolute_time();

		_sensor_pub.publish(report);
	}


	// clipping
	unsigned clip_count_x = clipping(sample.x, _clip_limit, N);
	unsigned clip_count_y = clipping(sample.y, _clip_limit, N);
	unsigned clip_count_z = clipping(sample.z, _clip_limit, N);

	_clipping_total[0] += clip_count_x;
	_clipping_total[1] += clip_count_y;
	_clipping_total[2] += clip_count_z;

	_integrator_clipping(0) += clip_count_x;
	_integrator_clipping(1) += clip_count_y;
	_integrator_clipping(2) += clip_count_z;

	// integrated data (INS)
	{
		// reset integrator if previous sample was too long ago
		if ((sample.timestamp_sample > _timestamp_sample_prev)
		    && ((sample.timestamp_sample - _timestamp_sample_prev) > (N * dt * 2.0f))) {

			ResetIntegrator();
		}

		// integrate
		_integrator_samples += 1;
		_integrator_fifo_samples += N;

		// trapezoidal integration (equally spaced, scaled by dt later)
		_integration_raw(0) += (0.5f * (_last_sample[0] + sample.x[N - 1]) + sum(sample.x, N - 1));
		_integration_raw(1) += (0.5f * (_last_sample[1] + sample.y[N - 1]) + sum(sample.y, N - 1));
		_integration_raw(2) += (0.5f * (_last_sample[2] + sample.z[N - 1]) + sum(sample.z, N - 1));
		_last_sample[0] = sample.x[N - 1];
		_last_sample[1] = sample.y[N - 1];
		_last_sample[2] = sample.z[N - 1];


		if (_integrator_fifo_samples > 0 && (_integrator_samples >= _integrator_reset_samples)) {

			// Apply rotation and scale
			// integrated in microseconds, convert to seconds
			const Vector3f delta_velocity{_rotation_dcm *_integration_raw *_scale * 1e-6f * dt};

			// fill sensor_accel_integrated and publish
			sensor_accel_integrated_s report;

			report.timestamp_sample = sample.timestamp_sample;
			report.error_count = _error_count;
			report.device_id = _device_id;
			delta_velocity.copyTo(report.delta_velocity);
			report.dt = _integrator_fifo_samples * dt; // time span in microseconds
			report.samples = _integrator_fifo_samples;

			const Vector3f clipping{_rotation_dcm * _integrator_clipping};

			for (int i = 0; i < 3; i++) {
				report.clip_counter[i] = fabsf(roundf(clipping(i)));
			}

			report.timestamp = hrt_absolute_time();
			_sensor_integrated_pub.publish(report);

			// update vibration metrics
			UpdateVibrationMetrics(delta_velocity);

			// reset integrator
			ResetIntegrator();
		}

		_timestamp_sample_prev = sample.timestamp_sample;
	}

	// publish sensor fifo
	sensor_accel_fifo_s fifo{};

	fifo.device_id = _device_id;
	fifo.timestamp_sample = sample.timestamp_sample;
	fifo.dt = dt;
	fifo.scale = _scale;
	fifo.samples = N;

	memcpy(fifo.x, sample.x, sizeof(sample.x[0]) * N);
	memcpy(fifo.y, sample.y, sizeof(sample.y[0]) * N);
	memcpy(fifo.z, sample.z, sizeof(sample.z[0]) * N);

	fifo.timestamp = hrt_absolute_time();
	_sensor_fifo_pub.publish(fifo);


	PublishStatus();
}

void PX4Accelerometer::PublishStatus()
{
	// publish sensor status
	if (hrt_elapsed_time(&_status_last_publish) >= 100_ms) {
		sensor_accel_status_s status;

		status.device_id = _device_id;
		status.error_count = _error_count;
		status.full_scale_range = _range;
		status.rotation = _rotation;
		status.measure_rate_hz = _update_rate;
		status.temperature = _temperature;
		status.vibration_metric = _vibration_metric;
		status.clipping[0] = _clipping_total[0];
		status.clipping[1] = _clipping_total[1];
		status.clipping[2] = _clipping_total[2];
		status.timestamp = hrt_absolute_time();
		_sensor_status_pub.publish(status);

		_status_last_publish = status.timestamp;
	}
}

void PX4Accelerometer::ResetIntegrator()
{
	_integrator_samples = 0;
	_integrator_fifo_samples = 0;
	_integration_raw.zero();
	_integrator_clipping.zero();

	_timestamp_sample_prev = 0;
}

void PX4Accelerometer::UpdateClipLimit()
{
	// 99.9% of potential max
	_clip_limit = fmaxf((_range / _scale) * 0.999f, INT16_MAX);
}

void PX4Accelerometer::UpdateVibrationMetrics(const Vector3f &delta_velocity)
{
	// Accel high frequency vibe = filtered length of (delta_velocity - prev_delta_velocity)
	const Vector3f delta_velocity_diff = delta_velocity - _delta_velocity_prev;
	_vibration_metric = 0.99f * _vibration_metric + 0.01f * delta_velocity_diff.norm();

	_delta_velocity_prev = delta_velocity;
}

void PX4Accelerometer::print_status()
{
	char device_id_buffer[80] {};
	device::Device::device_id_print_buffer(device_id_buffer, sizeof(device_id_buffer), _device_id);
	PX4_INFO("device id: %d (%s)", _device_id, device_id_buffer);
	PX4_INFO("rotation: %d", _rotation);
}
