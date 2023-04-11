# letovo-computers-arduino

## Description

This code sets up an Arduino-based system that connects to the internet and communicates with a MQTT broker. It listens
for RFID tags and button presses, and sends data to the broker when it detects an event. The system controls a servo and
has a keypad with 30 buttons arranged in a 15x2.
Overall, the device monitors the availability of the computers and reports the status to the server.

### Configuration

The device can be configured via the serial interface. The following commands are supported:

* `WIFI_SSID` - SSID of the Wi-Fi network to connect to
* `WIFI_PASS` - password for the Wi-Fi network to connect to
* `MQTT_USER` - username for the MQTT broker
* `MQTT_PASS` - password for the MQTT broker
* `MQTT_HOST` - hostname or IP address of the MQTT broker
* `MQTT_PORT` - port of the MQTT broker
* `MQTT_CLIENT_ID` - client ID for the MQTT connection
* `USE_SSL` - whether to use SSL for the MQTT connection
* `USE_UNSAFE_POINTER_CAST` - whether to use unsafe pointer casts for the struct iteration
* `ARDUINO_STREAM_TOPIC` - topic to publish the Arduino stream to
* `ARDUINO_WILL_TOPIC` - topic to publish the Arduino will to
* `SERVER_STREAM_TOPIC` - topic to publish the server stream to
* `SERVER_WILL_TOPIC` - topic to publish the server will to

### Hardware

The device uses the following hardware:

* Arduino Nano 33 IoT with WiFiNINA module
* RDM6300 RFID reader
* 30x push buttons
* 1x LED (built-in to the Arduino Nano 33 IoT)

### Schematics

The device schematics can be viewed in the [schematics](schematics) directory.

### Libraries

The following libraries are used:

* [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)
* [rdm6300](https://github.com/arduino12/rdm6300)
* [Servo](https://github.com/arduino-libraries/Servo)
* [WiFiNINA](https://github.com/arduino-libraries/WiFiNINA)
* [ArduinoMqttClient](https://github.com/arduino-libraries/ArduinoMqttClient)
