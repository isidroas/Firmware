# Automated Tiltrotor Birotor Proyect

Steps to reproduce simulation in Ubuntu (tested in 18.04):


To prepare the develoment environment, follow the instructions

Dowload script:

```
wget https://raw.githubusercontent.com/isidroas/ubuntu_sim.sh -O 
```

Script execution

`source ubuntu_sim.sh` 

Move to source folder 

`cd ~/src/Firmware` 

Run the simulation 

`make make px4_sitl gazebo_tiltrotor_birotor`
