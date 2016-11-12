# What is DoorBell20?

Door Bell 2.0 (or DoorBell20 for short) is a Bluetooth Low Energy (BLE) appliance to monitor a door bell and send notifications whenever the door bell rings. It turns a conventional door bell into a smart door bell that can be connected to the Internet of Things (IoT). Thus, DoorBell20 is the modern version of a door bell, or, as the name suggests, the door bell version 2.0 for the IoT era.

DoorBell20 consists for two major parts:

* The DoorBell20 monitoring device, which is connected in parallel to the door bell and wirelessly via BLE to a client running on a remote IoT gateway, e.g., a Raspberry Pi with Bluetooth stick.
* A DoorBell20 client running on the IoT gateway passing on notifications received via BLE to a remote cloud service. Different clients can be implemented for different IoT cloud services. So far, DoorBell20 includes a client for [If This Then That (IFTTT)](https://ifttt.com), which makes it very easy to trigger different actions when a door bell event is detected. For instance, a notification can be sent to a mobile phone or trigger an IP camera installed at the door to take pictures.

```
                  [IoT Cloud Service]
                  [  (e.g., IFTTT)  ]
                           | ^
                 Internet  | | Door Bell Event Notifications
                           |
                [      IoT Gateway      ]
                [ w/ DoorBell20 Client  ]
                [ (e.g., IFTTT Trigger) ]
                           |  ^
           BLE Connection  |  | Door Bell Event Notifications  
                           |
|-----------[DoorBell20 Monitoring Device]---------|
|                                                  |
|--------------------[Door Bell]-------------------|
|                                                  |
|                                                  |
|                                                 \   Door Bell Push Button
|                                                  \
|                                                  |
|----------------(Voltage Source)------------------|
                 (    12 VAC    )
```  

The following images show the DoorBell20 monitoring device, and its connection to a door bell.

![DoorBell20 Monitoring Device](/images/doorbell20_monitoring_device2.jpg)
![DoorBell20 Monitoring Device](/images/door_bell_and_doorbell20.jpg)

The main features of DoorBell20 are:

* Open-source software and hardware. Source code for the app and door bell monitoring device as well as Eagle files (schematic and board layout) are provided.
* Maker-friendly: using easily available cheap standard components (nRF51822 BLE chip, standard electronic parts), easy to manufacture circuit board, and open-source software and hardware design.
* Includes a client for the popular and versatile If This Then That (IFTTT) service to facilitate the development of IoT applications integrating DoorBell20. 
* Liberal licensing of software and hardware under the Apache License 2.0 and the CERN Open Hardware License 1.0, respectively.

# DoorBell20 Monitoring Device

## Hardware

The following images show the DoorBell20 hardware and schematic.

![DoorBell20 Monitoring Device](/images/doorbell20_monitoring_device1.jpg)
![DoorBell20 Monitoring Device](/images/doorbell20_monitoring_device2.jpg)
![DoorBell20 Schematic](/images/doorbell20_monitoring_device_schematic.png)

The DoorBell20 monitoring device is based on the BLE chip [nRF51822](https://www.nordicsemi.com/eng/Products/Bluetooth-low-energy/nRF51822) by Nordic Semiconductors. The nRF51822 features an ARM Cortex M0 processor implementing both, the application logic and the BLE stack (so-called softdevice). DoorBell20 uses the S110 softdevice version 8.0. See next sub-section on how to flash the softdevice and the application code. We use a so-called "Bluetooth 4.0" breakout boards with an nRF51822 (version 3, variant AA w/ 16 kB of RAM and 256 kB flash memory) and two 2x9 connectors (2 mm pitch), which you can buy over the Internet for about 6 US$ including shipping.

We isolate the 12 VAC door bell circuit from the microcontroller using an opto-isolator. A rectifier and 5 V voltage regulater is used to power the LED of the opto-isolator whenever the door bell is ringing. A GPIO pin of the nRF51822 connected to the other side of the opto-isolator is then detecting the event. In addition to the integrate protection mechanisms of the LM2940 voltage regulator (short circuit and thermal overload protection, shutdown during transients), a varistor protects from voltage transients since many door bells are inductive loads inducing voltage spikes when switched off. Since varistors age with every voltage transient, a fuse is added to protect the door bell circuit from a short circuit of the varistor.

The nRF51822 is powered by two AA batteries. No additional voltage regulator is required, which increased the energy efficiency, and the monitoring device is expected to run for years from a pair of AA batteries. Note that we did not implement a reverse polarity protection, so be careful to insert the batteries correctly.    

The circuit board layout (PCB) of the DoorBell20 monitoring device for Eagle can be found in folder ```pcb/doorbell20```. We deliberately used a simple single-sided through-hole design to help makers producing their own boards.

## Software/Firmware

The software for the DoorBell20 monitoring device can be found in directory ```nrf51/doorbell20```. 

### Prerequisites for Building the Software

No heavy-weight IDE is required to build the code. However, still a few tools are required to build and flash the code (tested with the given version numbers):

* [nRF51 SDK, version 10.0.0](http://developer.nordicsemi.com/nRF5_SDK/)
* [nRF5x Command Line Tools, version 8.2.0](https://www.nordicsemi.com/eng/Products/nRF51-DK)
* [Softdevice S110, version 8.0.0](https://www.nordicsemi.com/eng/Products/nRF51-DK)
* Flashing and debugging hardware, e.g., [Segger J-Link EDU](https://www.segger.com/j-link-edu.html) (about 50 US$, only for non-commerical use!). The nRF51 Developers Kit (DK) comes with an on-board Segger programmer, but for flashing the DoorBell20 board, you need an external programmer like the J-Link EDU. 
* [Tool chain (compiler, linker, etc.) for ARM](https://launchpad.net/gcc-arm-embedded/+download)

### Building and Flashing the Software

First, adapt the Makefile by defining the following variables:

* `NRF51_SDK`: path to the nRF51 SDK directory
* `CROSS`: path to the compiler tools
* `CFLAGS`: add `-DTARGET_BOARD_NRF51DK` if you compile for the nRF51 Development Kit (DK); if this definition is not set, you compile for the DoorBell20 board.
* `LINKER_SCRIPT`: set to `nrf51422_ac_s110.ld` for the nRF51 DK (nRF51422, variant AC) or to `nrf51822_aa_s110.ld` for the DoorBell20 board (nRF51822, variant AA).

Compiling the code:

```
$ cd nrf51
$ make
```

Flashing the softdevice:

```
$ nrfjprog --family NRF51 --program s110_nrf51_8.0.0_softdevice.hex --chiperase --verify
```

Flashing the application:

```
nrfjprog --family NRF51 --program doorbell20.hex --verify --sectorerase
```

Rebooting after flashing:

```
nrfjprog -r

# IFTTT DoorBell20 Client

DoorBell20 can be connected to any BLE client running on a remote machine. After receiveing a BLE notification about a door bell event, the client can then trigger local actions or---as done here---can forward the event to a remote IoT cloud service. Here we forward events to the popular [If This Then That (IFTTT)](https://ifttt.com/) cloud service.

You find the [nodejs](https://nodejs.org/) implementation of the IFTTT client in directory `client/ifttt`. With this client, you can use door bell alarm events as triggers for IFTTT. IFTTT offers you a broad choice of actions such as showing a notification on your smartphone when the bell rings, playing a sound on your phone, or sending a tweet to Twitter (if that makes sense to you). You could also trigger other IoT devices like an IP camera making a picture of the one standing at the door. 

The IFTTT DoorBell20 client subscribes for BLE notifications using the nodejs library [noble](https://github.com/sandeepmistry/noble). Whenever a notification for a door bell alarm is received, a web request is sent to the IFTTT maker channel triggering an event with a pre-defined name (we use the event name `door_bell_alarm` in our example). You can then define your own IFTTT recipes to decide what to do with this event like showing a notification on your smartphone through the IFTTT app. 

The following screenshot shows an IFTTT notification on an Android phone triggered by a door bell event that was delivered over the IFTTT Maker Channel.

![IFTTT Door Bell Notification](/images/DoorBell20-IFTTTClient.png)

## Prerequisites

The IFTTT client relies on the following software to be installed on the BLE client machine (your IoT gateway):

* nodejs (tested with nodejs version 4.6.0)
* noble
* C++ compiler with C++11 support for installing noble with npm as described below (tested with gcc/g++ 4.8.4). If you use Raspbian: gcc/g++ 4.8.4 included with Raspbian Jessie will work, but gcc/g++ 4.6.3 included with Raspbian Wheezy won't.  

Nodejs can be installed as root like this on a Raspberry Pi A/A+, B/B+, and Zero (ARMv6):

```
$ cd /usr/local
$ wget http://nodejs.org/dist/latest-v4.x/node-v4.6.0-linux-armv6l.tar.xz
$ tar axf node-v4.6.0-linux-armv6l.tar.xz
```

For other platforms, you have to replace `node-v4.6.0-linux-armv6l.tar.xz` by the corresponding file, e.g., `node-v4.6.0-linux-armv7l.tar.xz` for Raspberry Pi 2 (ARMv7) or `node-v4.6.0-linux-x64.tar.xz` for 64 bit Linux.

Make sure to add the nodejs `bin` directory to your path, so the commands `node`and `npm` can be found.

With nodejs installed, install the noble library for implementing BLE centrals:

```
$ sudo apt-get install bluetooth bluez libbluetooth-dev libudev-dev
$ cd doorbell20/client/ifttt
$ npm install noble
```

This should create the sub-directory `node_modules` in directory `doorbell20/client/ifttt`.

The first line installs Bluetooth libraries required by noble on Debian-based systems including Ubuntu and Raspbian. You might need to modify this depending on your platform, e.g., for Fedora:

```
$ sudo yum install bluez bluez-libs bluez-libs-devel
```

Please consult the [noble page](https://github.com/sandeepmistry/noble) for other platforms.

## Executing the IFTTT Client

The client can be started like this:

```
sudo node doorbell20-client-ifttt.js 'ABCDEFGHIJK1234567890' 'f3:23:0d:4c:ce:1b' 'doorbell_alarm' 'iot_failure'
```

Argument `ABCDEFGHIJK1234567890` is the key of your IFTTT Maker channel (obviously here a bogus one), which you get when you register with the Maker channel through the IFTTT web interface or app.

Argument `doorbell_alarm` is the name of the generated events. You can send many different events to your Maker channel, e.g., from your door bell, from your smoke detector (hopefully not so often), etc. The event name defines, which event has been fired.

A second event called `iot_failure` is defined that will be triggerer when the client could not connect to the DoorBell20 monitoring device over BLE to signal a permanent failure, e.g., when the battery of the monitoring device is empty. 

Argument `f3:23:0d:4c:ce:1b` is the MAC address of your DoorBell20 BLE device. You might have installed different DoorBell20 devices for different door bells, so the DoorBell20 service UUID is not enough to distinguish between different devices. The MAC address is unique for each device.

## Source Code

The source code of the IFTTT client can be found in file `doorbell20-client-ifttt.js`. It should be pretty self-explaining.

Event notifications are sent to the IFTTT channel through web requests using HTTPS. The URL defines the triggered event:

```
https://maker.ifttt.com/trigger/{event}/with/key/{key}
```

```{event}``` and ```{key}``` are replaced by the above command line arguments to define the event type and Maker channel. 

A POST request is used to send a simple JSON document with this format:

```
{ "value1" : "2016-10-09 18:37:34", "value2" : "", "value3" : "" }
```

In our application, ```value1``` defines the time when the door bell alarm event has been detected. The other two values are unused.

The client subscribes to BLE/GATT notifications of the door bell alarm characteristic of the DoorBell20 service using noble. When a BLE/GATT notification is sent, the client determines the local time of the client machine. Note that the BLE device has no wall clock time available (although is sends the local up-time of the device with every notification and allows for querying the local device time time through another characteristic). The time of the client machine is sent as timestamp of the IFTTT event notification as `value1`.

# License and Acknowledgments

The DoorBell20 software (contents of folders `nrf51` and `client`) is licensed under the Apache License, Version 2.0.

The DoorBell20 hardware documentation (contents of folder `pcb`) is licensed under the CERN Open Hardware Licence, Version 1.2

Both licenses are included in the repository in the files `LICENSE-SOFTWARE` and `LICENSE-HARDWARE`, respectively.
