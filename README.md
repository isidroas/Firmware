# Autonomous Tiltrotor Birotor Proyect

[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/9isC3tmvcgc/0.jpg)](https://www.youtube.com/watch?v=9isC3tmvcgc)


Steps to reproduce simulation in Ubuntu (tested in 18.04):




Dowload script:

```
wget https://raw.githubusercontent.com/isidroas/Firmware/tiltrotor-birotor/ubuntu_sim.sh
```

Script execution

```
source ubuntu_sim.sh
```

Clone source code

```
git clone https://github.com/isidroas/Firmware ~/src/Firmware
```

Download the GCS 
```
wget -P ~/ https://github.com/mavlink/qgroundcontrol/releases/download/v4.0.6/QGroundControl.AppImage
```

Move to source folder 

```
cd ~/src/Firmware
```

Run the simulation 

```
make -j 4 px4_sitl gazebo_tiltrotor_birotor
```

Open another terminal and start the GCS (instructions from https://docs.qgroundcontrol.com/en/getting_started/download_and_install.html#ubuntu)

```
cd ~/
sudo usermod -a -G dialout $USER
sudo apt-get remove modemmanager -y
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-libav -y
chmod +x ./QGroundControl.AppImage
./QGroundControl.AppImage
```

You can now control the tiltrotor through the GCS spawned terminal as shown is the next video

[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/rLd2zTMaQHI/0.jpg)](https://www.youtube.com/watch?v=rLd2zTMaQHI)


