# Automated Tiltrotor Birotor Proyect

Steps to reproduce simulation in Ubuntu (tested in 18.04):


To prepare the develoment environment, follow the instructions

Dowload script:

```
wget https://raw.githubusercontent.com/isidroas/Firmware/tiltrotor-birotor/ubuntu_sim.sh
```

Script execution

```
source ubuntu_sim.sh
```


```
git clone https://github.com/isidroas/Firmware ~/src/Firmware
```

Move to source folder 

```
cd ~/src/Firmware
```

Download the GCS 
```
https://github.com/mavlink/qgroundcontrol/releases/download/v4.0.6/QGroundControl.AppImage
```

Run the simulation 

```
make px4_sitl gazebo_tiltrotor_birotor
```

Open the GCS (instructions from https://docs.qgroundcontrol.com/en/getting_started/download_and_install.html#ubuntu)
```
sudo usermod -a -G dialout $USER
sudo apt-get remove modemmanager -y
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-libav -y
chmod +x ./QGroundControl.AppImage
./QGroundControl.AppImage
```

You can now control the tiltrotor through the GCS spawned terminal



