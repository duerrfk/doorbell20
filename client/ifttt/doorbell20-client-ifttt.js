/**
 * This file is part of DoorBell20.
 *
 * Copyright 2016 Frank Duerr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

var noble = require('noble');
var https = require('https');

// The key identifying our IFTTT Maker channel.
// We read the key as a command line argument. Note that the first
// argument has the index 2 (0 is always 'node', 1 is the script name).
var iftttKey = process.argv[2];

// The name of the event sent to the IFTTT Maker channel.
// We read the event name as a command line argument. Note that the first
// argument has the index 2 (0 is always 'node', 1 is the script name).
var iftttEventName = process.argv[3];

// We use the device MAC address to identify the right door bell, just in case 
// there are multiple DoorBell20 devices implementing the door bell service.
// We read the event name as a command line argument. 
// Format is like this: 'f3:23:0d:4c:ce:1b';
// Note that the first argument has the index 2 (0 is always 'node', 1 is the 
// script name).
var doorBellDeviceMAC = process.argv[4];

// BLE/GATT UUIDs.
var doorBellServiceUUID = '451e0001dd1c4f20a42eff91a53d2992';
var doorBellAlarmCharUUID = '451e0002dd1c4f20a42eff91a53d2992';
var localtimeCharUUID = '451e0003dd1c4f20a42eff91a53d2992';


// BLE/GATT objects. 
var doorBellPeripheral = null;
var doorBellService = null;
var doorBellAlarmChar = null;
var doorBellLocaltimeChar = null;

noble.on('stateChange', function(state) {
    if (state === 'poweredOn') {
	// Before scanning, we must for the device to be powered on. 
	console.log('Scanning started.');
	noble.startScanning([doorBellServiceUUID], false);
    } else {
	console.log('Scanning stopped.');
	noble.stopScanning();
    }
})

function notifyIFTTT(dateStr) {
    // Can send up to three values formated as JSON document in request body.
    var body = JSON.stringify({ "value1" : dateStr, "value2" : "", 
				"value3" : "" });

    // Request is sent as HTTP Post request.
    // The URL has the format:
    // https://maker.ifttt.com/trigger/{event}/with/key/{key}
    var httpOptions = {
	hostname: 'maker.ifttt.com',
	port: 443,
	path: '/trigger/' + iftttEventName + "/with/key/" + iftttKey,
	method: 'POST',
	headers: {
	    'Content-Type': 'application/json',
	    'Content-Length': Buffer.byteLength(body)
	}
    };

    var req = https.request(httpOptions, function(res) {
	console.log('HTTP request to IFTTT Maker channel. Request status: ' + 
		    res.statusCode);
    });
    req.on('error', function(e) {
	console.log('HTTP request error: ' + e.message);
    });
    req.write(body);
    req.end();
}

function onDoorBellAlarmCharNotification(data, isNotification) {
    console.log('Door bell alarm notification.');
    // Send event notification to IFTTT via HTTP request.
    var now = new Date();
    var dateStr = now.toLocaleString();
    console.log(dateStr);
    notifyIFTTT(dateStr);
}

function onCharDiscovered(err, characteristics) {
    characteristics.forEach(function(characteristic) {
	if (characteristic.uuid === doorBellAlarmCharUUID) {
	    doorBellAlarmChar = characteristic;
	} else if (characteristic.uuid === localtimeCharUUID) {
	    doorBellLocaltimeChar = characteristic;
	}
    });
    
    if (!doorBellAlarmChar || !doorBellLocaltimeChar) {
	console.log('Missing characteristic.');
	process.exit(-1);
    }

    // Required characteristcs are all available.
    console.log('Discovered all required characteristics.');

    // Subscribe for door bell alarm notifications.
    doorBellAlarmChar.on('read', onDoorBellAlarmCharNotification);
    doorBellAlarmChar.subscribe(function(err) {
	if (err) {
	    console.log('Could not subscribe to door bell alarm ' +
			'characteristic.');
	} else {
	    console.log('Subscribed to door bell alarm characteristic.');
	}
    });
}

function onServicesDiscovered(err, services) {
    if (err) {
	console.log('Could not discover services.');
    } else {
	console.log('Discovered DoorBell20 service');
	// There is exactly one service matching the requested UUID.
	doorBellService = services[0];
	doorBellService.discoverCharacteristics([], onCharDiscovered);
    }
}

function onPeripheralDisconnected() {
    console.log('Disconnected');
    process.exit(-1);
}

function onPeripheralConnected(err)  {
    if (err) {
	console.log('Could not connect to DoorBell20 device.');
    } else {
	console.log('Connected to peripheral.');
	doorBellPeripheral.once('disconnect', onPeripheralDisconnected);
	doorBellPeripheral.discoverServices([doorBellServiceUUID], 
					    onServicesDiscovered);
    }
}

noble.on('discover', function(peripheral) {
    console.log('found peripheral: ', peripheral.advertisement);

    if (peripheral.address === doorBellDeviceMAC) {
	// We found our DoorBell20 device implementing the DoorBell20 service.
	console.log('Found DoorBell20 device.');
	doorBellPeripheral = peripheral;

	noble.stopScanning();

	peripheral.connect(onPeripheralConnected);
    }
})
   
