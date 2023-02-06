#include "main.h"
#include "config.h"

#include <set>
#include <vector>

#include <Arduino.h>
#include <Arduino_JSON.h>
#include <rdm6300.h>
#include <Servo.h>
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <SoftTimer.h>

Servo servo;
Rdm6300 rdm6300;
#ifdef USE_SSL
WiFiSSLClient wifiClient;
#endif
#ifndef USE_SSL
WiFiClient wifiClient;
#endif
MqttClient mqttClient(wifiClient);

const static char *brokerHost = MQTT_HOST;
const static int brokerPort = MQTT_PORT;
const static char *brokerUser = MQTT_USER;
const static char *brokerPass = MQTT_PASS;
const static char *wifiSSID = WIFI_SSID;
const static char *wifiPass = WIFI_PASS;

const static char *arduinoStreamTopic = "comps/arduino/stream";
const static char *arduinoWillTopic = "comps/arduino/will";
const static char *serverStreamTopic = "comps/server/stream";
const static char *serverWillTopic = "comps/server/will";
const static JSONVar
        willPayload(R"({"message":"arduino with RFID scanner disconnected","RFID":"","slots":"", status:3})");
const static char *clientID = MQTT_CLIENT_ID;

const static int RDM6300_RX_PIN = 0;
const static int SERVO_PIN = A0;
const static int LED_PIN = LED_BUILTIN;

const static int ROWS = 6;                          // num of rows
const static int COLS = 5;                          // num of columns
const static char *keys[ROWS][COLS] = {             // ID of each key
        {"r1c1",  "r1c2",  "r1c3",  "r1c4",  "r1c5"},
        {"r1c6",  "r1c7",  "r1c8",  "r1c9",  "r1c10"},
        {"r1c11", "r1c12", "r1c13", "r1c14", "r1c15"},
        {"r2c1",  "r2c2",  "r2c3",  "r2c4",  "r2c5"},
        {"r2c6",  "r2c7",  "r2c8",  "r2c9",  "r2c10"},
        {"r2c11", "r2c12", "r2c13", "r2c14", "r2c15"},
};
const static int rowPins[ROWS] = {2, 3, 4, 5, 6, 7};
const static int colPins[COLS] = {8, 9, 10, 11, 12};

char latestRFID[20] = "null";
std::set<const char *> buttonsPressed;
std::set<const char *> buttonsPressedOld;
std::vector<const char *> buttonsToUp;
std::vector<const char *> buttonsToDown;

Task checkWiFiConnectionTask(10000, checkWiFiConnection);
Task checkBrokerConnectionTask(10000, checkBrokerConnection);
Task listenForRFIDTask(10, listenForRFID);
Task listenForButtonsTask(500, listenForButtons);
Task MQTTPollTask(10, MQTTPoll);

__attribute__((unused)) void setup() {
    Serial.begin(9600);

    // init LED --------------------------------------------------------------------------------------------------------
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // init keys listener ----------------------------------------------------------------------------------------------
    for (int rowPin: rowPins) {
        pinMode(rowPin, OUTPUT);
        digitalWrite(rowPin, HIGH);
    }

    for (int colPin: colPins) {
        pinMode(colPin, INPUT_PULLUP);
    }

    // init Servo ------------------------------------------------------------------------------------------------------
    pinMode(SERVO_PIN, OUTPUT);
    servo.attach(SERVO_PIN);

    // connect to the Internet -----------------------------------------------------------------------------------------
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("Wi-Fi status: ");
        Serial.println(WiFi.status());

        while (WiFi.status() != WL_CONNECTED) {
            Serial.print("connecting to \"");
            connectToInternet();
            delay(2000);
        }
    }
    Serial.print("Connected to the Internet. IP address: ");
    Serial.println(WiFi.localIP());

    // init the MQTT client & broker -----------------------------------------------------------------------------------
    mqttClient.setId(clientID);
    mqttClient.setUsernamePassword(brokerUser, brokerPass);

    while (!connectToBroker() && WiFi.status() == WL_CONNECTED) {
        Serial.println("attempting to connect to the broker...");
        delay(1000);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi connection lost during setup. Restarting...");
        NVIC_SystemReset();
    }

    sendWillMessage(willPayload);

    mqttClient.onMessage([](__attribute__((unused)) int messageSize) {
        Serial.print("\nGot a message on topic: ");
        Serial.println(mqttClient.messageTopic());

        while (mqttClient.available()) {
            Serial.println(mqttClient.readString());
        }
    });

    // init RFID scanner -----------------------------------------------------------------------------------------------
    rdm6300.begin(RDM6300_RX_PIN);
    Serial.println("\nlistening for RFID tag nearby...");

    // add Tasks -------------------------------------------------------------------------------------------------------
    for (Task *task: {
            &checkWiFiConnectionTask, &checkBrokerConnectionTask,
            &listenForRFIDTask, &listenForButtonsTask, &MQTTPollTask
    }) {
        SoftTimer.add(task);
    }
}

void checkWiFiConnection(__attribute__((unused)) Task *me) {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("Lost connection to the Internet");
    Serial.print("Wi-Fi status: ");
    Serial.println(WiFi.status());

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print("reconnecting to \"");
        connectToInternet();
        delay(2000);
    }

    Serial.print("Reconnected to the Internet. IP address: ");
    Serial.println(WiFi.localIP());
}

void checkBrokerConnection(__attribute__((unused)) Task *me) {
    if (mqttClient.connected()) return;

    Serial.println("Lost connection to the broker");

    while (!connectToBroker() && WiFi.status() == WL_CONNECTED) {
        Serial.println("reconnecting...");
        delay(1000);
    }

    if (WiFi.status() != WL_CONNECTED) return;

    Serial.print("Reconnected to the broker. ClientID is \"");
    Serial.print(clientID);
    Serial.println("\"");
}

void listenForRFID(__attribute__((unused)) Task *me) {
    if (rdm6300.get_new_tag_id()) {
        strcpy(latestRFID, itoa(rdm6300.get_tag_id(), latestRFID, 16));
        Serial.println(latestRFID);

        sendWillMessage(createMessage(latestRFID, "", Status::DISCONNECTED));
        sendMessage(arduinoStreamTopic, createMessage(latestRFID, "", Status::SCANNED));

        openDoor(true);
    }

    digitalWrite(LED_PIN, int(rdm6300.get_tag_id()));
}

void listenForButtons(__attribute__((unused)) Task *me) {
    using namespace SetSub;


    for (int i = 0; i < ROWS; i++) {
        digitalWrite(rowPins[i], LOW);

        for (int j = 0; j < COLS; j++) {
            if (!digitalRead(colPins[j])) {
                buttonsPressed.emplace(keys[i][j]);
            }
        }

        digitalWrite(rowPins[i], HIGH);
    }

    buttonsToUp = buttonsPressedOld - buttonsPressed;
    buttonsToDown = buttonsPressed - buttonsPressedOld;

    if (!buttonsToDown.empty()) {
        String slots;
        for (const char *button: buttonsToDown) {
            slots += button + String(";");
        }

        sendMessage(arduinoStreamTopic, createMessage(latestRFID, slots, Status::PLACED));
    }

    if (!buttonsToUp.empty()) {
        String slots;
        for (const char *button: buttonsToUp) {
            slots += button + String(";");
        }

        sendMessage(arduinoStreamTopic, createMessage(latestRFID, slots, Status::TAKEN));
    }

    buttonsPressedOld = buttonsPressed;
    buttonsPressed.clear();
    buttonsToUp.clear();
    buttonsToDown.clear();
}

void MQTTPoll(__attribute__((unused)) Task *me) { mqttClient.poll(); }

bool connectToBroker() {
    if (!mqttClient.connect(brokerHost, brokerPort)) {
        Serial.print("MOTT connection failed. Error no: ");
        Serial.println(mqttClient.connectError());

        return false;
    }
    Serial.print("Connected to the broker. ClientID: \"");
    Serial.print(clientID);
    Serial.println("\"");

    for (const char *const &topic: {arduinoStreamTopic, serverStreamTopic, serverWillTopic}) {
        if (mqttClient.subscribe(topic)) {
            Serial.print("Subscribed to \"");
            Serial.print(topic);
            Serial.println("\"");
        } else {
            Serial.print("Failed to subscribe to \"");
            Serial.print(topic);
            Serial.println("\"");
        }
    }

    return true;
}

void connectToInternet() {
    Serial.print(wifiSSID);
    Serial.println("\"...");

    WiFi.begin(wifiSSID, wifiPass);
}

JSONVar createMessage(const char *tag, const String &slots, Status status) {
    JSONVar messageObject;
    messageObject["message"] = as_string(status);
    messageObject["RFID"] = tag;
    messageObject["slots"] = slots;
    messageObject["status"] = as_integer(status);

    return messageObject;
}

int sendMessage(const char *topic, const JSONVar &message) {
    if (!mqttClient.beginMessage(topic)) {
        Serial.println("Failed to begin message");
        return 0;
    }

    mqttClient.print(JSON.stringify(message));
    return mqttClient.endMessage();
}

int sendWillMessage(const JSONVar &message) {
    if (mqttClient.beginWill(arduinoWillTopic, true, 2)) {
        Serial.println("Failed to begin will message");
        return 0;
    }

    mqttClient.print(JSON.stringify(message));
    return mqttClient.endWill();
}

void openDoor(bool isOpen) {
    if (isOpen)
        servo.write(0);
    else
        servo.write(90);
}
