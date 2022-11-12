#include <iostream>
#include <set>
#include <algorithm>
#include <vector>

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <rdm6300.h>
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <SoftTimer.h>

#include "arduino_secrets.h"

Rdm6300 rdm6300;
WiFiClient wifi;
MqttClient mqttClient(wifi);
IPAddress broker = IPAddress(194, 87, 214, 108);

const int RDM6300_RX_PIN = 0;
const int port = 58138;
const char topic[] = "comps-mng";
const char willTopic[] = "comps-mng/arduino/will";
const String willPayload = "arduino with RFID scanner disconnected";
const char clientID[] = "RFIDScanner";

uint32_t currentRFID;
std::set<char> buttonsPressed;
std::set<char> buttonsPressedOld;
std::vector<int> buttonsUP;
std::vector<int> buttonsDOWN;

const int ROWS = 4; // num of rows
const int COLS = 3; // num of cols
char keys[ROWS][COLS] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'},
        {'*', '%', '&'},
};
int rowPins[ROWS] = {9, 8, 7, 6};
int colPins[COLS] = {12, 11, 10};


bool connectToBroker();

void connectToInternet();

void checkWiFiConnection(Task *me);

Task checkWiFiConnectionTask(10000, checkWiFiConnection);

void checkBrokerConnection(Task *me);

Task checkBrokerConnectionTask(10000, checkBrokerConnection);

void listenForRFID(Task *me);

Task listenForRFIDTask(10, listenForRFID);

void listenForButtons(Task *me);

Task listenForButtonsTask(500, listenForButtons);

void MQTTPoll(__attribute__((unused)) Task *me) { mqttClient.poll(); }

Task MQTTPollTask(10, MQTTPoll);


__attribute__((unused)) void setup() {
    Serial.begin(9600);

    // init LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    // init keys listener
    for (int rowPin: rowPins) {
        pinMode(rowPin, OUTPUT);
        digitalWrite(rowPin, HIGH);
    }

    for (int colPin: colPins) {
        pinMode(colPin, INPUT_PULLUP);
    }

    // connect to the Internet
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("Wi-Fi status: ");
        Serial.println(WiFi.status());

        while (WiFi.status() != WL_CONNECTED) {
            Serial.print("connecting to \"");
            connectToInternet();
            delay(2000);
        }
    }
    Serial.print("Connected. IP address: ");
    Serial.println(WiFi.localIP());

    // init the MQTT client & broker:
    mqttClient.setId(clientID);
    mqttClient.setUsernamePassword(SECRET_MQTT_USER, SECRET_MQTT_PASS);

    while (!connectToBroker()) {
        Serial.println("attempting to connect to broker...");
        delay(1000);
    }
    Serial.println("Connected to broker");

    mqttClient.beginWill(willTopic, willPayload.length(), true, 1);
    mqttClient.print(willPayload);
    mqttClient.endWill();

    mqttClient.onMessage([](__attribute__((unused)) int messageSize) {
        Serial.print("Got a message on topic: ");
        Serial.println(mqttClient.messageTopic());

        while (mqttClient.available()) {
            Serial.println(mqttClient.readString());
        }
    });

    // init RFID scanner
    rdm6300.begin(RDM6300_RX_PIN);
    Serial.println("\nlistening for RFID tag nearby...");

    // add Tasks
    SoftTimer.add(&checkWiFiConnectionTask);
    SoftTimer.add(&checkBrokerConnectionTask);
    SoftTimer.add(&listenForRFIDTask);
    SoftTimer.add(&listenForButtonsTask);
    SoftTimer.add(&MQTTPollTask);
}

void listenForButtons(__attribute__((unused)) Task *me) {
    for (int i = 0; i < ROWS; i++) {
        digitalWrite(rowPins[i], LOW);

        for (int j = 0; j < COLS; j++) {
            if (!digitalRead(colPins[j])) {
                buttonsPressed.emplace(keys[i][j]);
                Serial.print(keys[i][j]);
            }
        }

        digitalWrite(rowPins[i], HIGH);
    }

    Serial.print(" ");
    Serial.print(currentRFID);
    Serial.println();

    std::set_difference(
            buttonsPressedOld.begin(), buttonsPressedOld.end(),
            buttonsPressed.begin(), buttonsPressed.end(),
            std::inserter(buttonsUP, buttonsUP.begin())
    );

    std::set_difference(
            buttonsPressed.begin(), buttonsPressed.end(),
            buttonsPressedOld.begin(), buttonsPressedOld.end(),
            std::inserter(buttonsDOWN, buttonsDOWN.begin())
    );

    Serial.print("UP ");
    for (char i: buttonsUP) {
        Serial.print(i);
    }
    Serial.println(" endUP");

    Serial.print("DOWN ");
    for (char i: buttonsDOWN) {
        Serial.print(i);
    }
    Serial.println(" endDOWN");

    buttonsPressedOld = buttonsPressed;
    buttonsPressed.clear();
    buttonsUP.clear();
    buttonsDOWN.clear();
}

void listenForRFID(__attribute__((unused)) Task *me) {
    if (rdm6300.get_new_tag_id()) {
        uint32_t new_tag = rdm6300.get_tag_id();
        currentRFID = new_tag;
        Serial.println(new_tag, HEX);

        JSONVar willObject;
        willObject["message"] = "arduino with RFID scanner disconnected";
        willObject["RFID"] = new_tag;

        mqttClient.beginWill(willTopic, true, 1);
        mqttClient.print(JSON.stringify(willObject));
        mqttClient.endWill();


        JSONVar messageObject;
        messageObject["message"] = "scanned new tag";
        messageObject["RFID"] = new_tag;

        mqttClient.beginMessage("comps-mng");
        mqttClient.print(JSON.stringify(messageObject));
        mqttClient.endMessage();

        // Servo opens the door
    }

    digitalWrite(LED_BUILTIN, int(rdm6300.get_tag_id()));
}

void checkWiFiConnection(__attribute__((unused)) Task *me) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Lost connection to the Internet");
        Serial.print("Wi-Fi status: ");
        Serial.println(WiFi.status());

        while (WiFi.status() != WL_CONNECTED) {
            Serial.print("reconnecting to \"");
            connectToInternet();
            delay(2000);
        }

        Serial.print("Reconnected. IP address: ");
        Serial.println(WiFi.localIP());
    }
}

void checkBrokerConnection(__attribute__((unused)) Task *me) {
    if (!mqttClient.connected()) {
        Serial.println("Lost connection to the broker");

        while (!connectToBroker()) {
            Serial.println("reconnecting...");
            delay(1000);
        }

        Serial.println("Reconnected to broker");
    }
}

bool connectToBroker() {
    if (!mqttClient.connect(broker, port)) {
        Serial.print("MOTT connection failed. Error no: ");
        Serial.println(mqttClient.connectError());

        return false;
    }
    mqttClient.subscribe(topic);
    mqttClient.subscribe(willTopic);

    return true;
}

void connectToInternet() {
    Serial.print(SECRET_SSID);
    Serial.println("\"...");

    WiFi.begin(SECRET_SSID, SECRET_PASS);
}