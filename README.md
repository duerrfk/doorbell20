# IFTTT Client

In directory `client/ifttt` you find a [nodejs](https://nodejs.org/) client for the [If This Then That (IFTTT)](https://ifttt.com/) platform. With this client, you can use door bell alarms as event triggers in IFTTT. 

This client subscribes for BLE notifications using the nodejs [noble library](https://github.com/sandeepmistry/noble). Whenever a notification for a door bell alarm is received, a web request is sent to the IFTTT maker channel triggering an event named `door_bell_alarm`. You can then define your own IFTTT recipes to decide what to do with this event, for instance, showing a notification on your smartphone through the IFTTT app. 

## Prerequisites

The IFTTT client relies on the following software to be installed:

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
$ sudo node doorbell20-client-ifttt.js 'ABCDEFGHIJK1234567890' 'doorbell_home' 'f3:23:0d:4c:ce:1b'
```

Argument `ABCDEFGHIJK1234567890` is the key of your IFTTT Maker channelm which you get when you register with this channel through the IFTTT web interface or app.

Argument `doorbell_home` is the name of the generated events. You can send many different events to your Maker channel, e.g., from your door bell, from your smoke detector (hopefully not so often), etc. The event name defines, which event has been fired.

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

The following image shows a notification displayed by the IFTTT Android app.

![IFTTT Door Bell Notification](/images/DoorBell20-IFTTTClient.png)

