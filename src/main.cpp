#include <Arduino.h>
#include <Arduino_JSON.h>
#include <rdm6300.h>
#include <Servo.h>
#include <WiFiNINA.h>
#include <ArduinoMqttClient.h>
#include <SoftTimer.h>

#if USE_SSL
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <utility/ECCX08SelfSignedCert.h>
#endif

#include "main.h"
#include "config.h"

static String statusMessage;

unsigned int (*statusMessageLength)() = []() { return statusMessage.length(); };

static std::set<const char *>    buttonsPressed;
static std::set<const char *>    buttonsPressedOld;
static std::vector<const char *> buttonsToUp;
static std::vector<const char *> buttonsToDown;

Servo      servo;
Rdm6300    rdm6300;
WiFiClient wifiClient;
#if !USE_SSL
MqttClient mqttClient(wifiClient);
#endif
#if USE_SSL
//WiFiSSLClient wifiClient;
BearSSLClient sslClient(wifiClient);
MqttClient mqttClient(sslClient);
const int keySlot = 0;  // Crypto chip slot to pick the key from
const int certSlot = 8;  // Crypto chip slot to pick the certificate from
#endif

Task checkWiFiConnectionTask(10000, checkWiFiConnection);
Task checkBrokerConnectionTask(10000, checkBrokerConnection);
Task listenForRFIDTask(10, listenForRFID);
Task listenForButtonsTask(500, listenForButtons);
Task MQTTPollTask(10, MQTTPoll);

__attribute__((unused)) void setup() {
    // init serial
    Serial.begin(9600);
    while (!Serial);

#if USE_SSL
    Serial.println("Using the certificate from config.h...");
    sslClient.setKey(PRIVATE_KEY, CERTIFICATE);

     Instruct the SSL client to use the chosen ECCX08 slot for picking the private key
     and set the hardcoded certificate as accompanying public certificate.
    //sslClient.setEccCert();  // DER
    //        bearClient.setEccSlot(
    //                keySlot,
    //                CLIENT_CERT);
#endif

    // init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // init keys listener
    for (uint8_t rowPin: ROW_PINS) {
        pinMode(rowPin, OUTPUT);
        digitalWrite(rowPin, HIGH);
    }

    for (uint8_t colPin: COL_PINS) {
        pinMode(colPin, INPUT_PULLUP);
    }

    // init Servo
    pinMode(SERVO_PIN, OUTPUT);
    servo.attach(SERVO_PIN);

    // connect to the Internet
    statusMessage = "# connecting to " + String(wifiSSID);
    while (WiFi.status() != WL_CONNECTED) {
        connectToInternet();
        delay(2000);
    }
    Serial.print("\n## Connected to the Internet. IP address: ");
    Serial.println(WiFi.localIP());

    // init the MQTT broker & client and connect to the broker
    mqttClient.setId(clientID);
    mqttClient.setUsernamePassword(brokerUser, brokerPass);

    statusMessage = "# connecting to the broker";
    while (!connectToBroker() && WiFi.status() == WL_CONNECTED) {
        delay(1000);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("## Wi-Fi connection lost during setup. Restarting...");
        NVIC_SystemReset();
    }

    sendWillMessage(willPayload);

    mqttClient.onMessage([](__attribute__((unused)) int messageSize) {
        Serial.print("## Got a message on topic: ");
        Serial.println(mqttClient.messageTopic());

        while (mqttClient.available()) {
            JSONVar message = JSON.parse(mqttClient.readString());

            switch (static_cast<Status::Value>(int(message["status"]))) {
                case Status::Value::ERROR_OCCUR:
                    Status::handleErrorOccur(message);
                    break;
                case Status::Value::ERROR_RESOLVE:
                    Status::handleErrorResolve(message);
                    break;
                case Status::Value::CONNECT:
                    Status::handleConnect(message);
                    break;
                case Status::Value::DISCONNECT:
                    Status::handleDisconnect(message);
                    break;
                case Status::Value::OPEN:
                    Status::handleOpen(message);
                    break;
                default:
                    Serial.println("[unknown status]: ");
                    Serial.println(message);
                    break;
            }
        }
    });

    // init RFID scanner (RDM6300)
    rdm6300.begin(RDM6300_RX_PIN);
    Serial.println("# listening for RFID tags nearby...");

    // add Tasks to the scheduler (SoftTimer)
    for (Task *task: {
            &checkWiFiConnectionTask, &checkBrokerConnectionTask,
            &listenForRFIDTask, &listenForButtonsTask, &MQTTPollTask
    }) {
        SoftTimer.add(task);
    }
}

void checkWiFiConnection(__attribute__((unused)) Task *me) {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.println("## Lost connection to the Internet");

    statusMessage = "# reconnecting to " + String(wifiSSID);
    while (WiFi.status() != WL_CONNECTED) {
        connectToInternet();
        delay(2000);
    }

    Serial.print("\n## Reconnected to the Internet. IP address: ");
    Serial.println(WiFi.localIP());
}

void checkBrokerConnection(__attribute__((unused)) Task *me) {
    if (mqttClient.connected()) return;

    Serial.println("## Lost connection to the broker");

    statusMessage = "# reconnecting to the broker";
    while (!connectToBroker() && WiFi.status() == WL_CONNECTED) {
        delay(1000);
    }

    if (WiFi.status() != WL_CONNECTED) return;

    Serial.print("## Reconnected to the broker. Client ID: ");
    Serial.println(clientID);
}

void listenForRFID(__attribute__((unused)) Task *me) {
    if (uint32_t newTag = rdm6300.get_new_tag_id()) {
        itoa(int(newTag), latestRFID, 16);

        Serial.print("## New tag scanned: ");
        Serial.println(latestRFID);

        sendWillMessage(createMessage(Status::Value::DISCONNECT));
        sendMessage(arduinoStreamTopic, createMessage(Status::Value::SCAN));
    }

    digitalWrite(LED_PIN, int(rdm6300.get_tag_id()));
}

void listenForButtons(__attribute__((unused)) Task *me) {
    using namespace SetSubtract;

    for (uint8_t row = 0; row < ROWS; ++row) {
        digitalWrite(ROW_PINS[row], LOW);

        for (uint8_t col = 0; col < COLS; ++col) {
            if (!digitalRead(COL_PINS[col])) {
                buttonsPressed.emplace(SLOT_IDS[row][col]);
            }
        }

        digitalWrite(ROW_PINS[row], HIGH);
    }

    buttonsToUp   = buttonsPressedOld - buttonsPressed;
    buttonsToDown = buttonsPressed - buttonsPressedOld;

    if (!buttonsToDown.empty()) {
        String          slots;
        for (const char *button: buttonsToDown) {
            slots += button + String(';');
        }

        Serial.print("## Buttons pressed: ");
        Serial.println(slots);

        sendMessage(arduinoStreamTopic, createMessage(Status::Value::PLACE, slots.c_str()));
    }

    if (!buttonsToUp.empty()) {
        String          slots;
        for (const char *button: buttonsToUp) {
            slots += button + String(';');
        }

        Serial.print("## Buttons released: ");
        Serial.println(slots);

        sendMessage(arduinoStreamTopic, createMessage(Status::Value::TAKE, slots.c_str()));
    }

    buttonsPressedOld = buttonsPressed;
    buttonsPressed.clear();
    buttonsToUp.clear();
    buttonsToDown.clear();
}

void MQTTPoll(__attribute__((unused)) Task *me) { mqttClient.poll(); }

bool connectToBroker() {
    Serial.print('\r');
    Serial.print(RepeatedString(" ", statusMessageLength()));
    Serial.print("\rMOTT connection failed. Error no: ");
    Serial.println(mqttClient.connectError());
    Serial.print(statusMessage);
    Serial.flush();

    if (!mqttClient.connect(brokerHost, brokerPort)) {
        return false;
    }

    Serial.print("\n## Connected to the broker. Client ID: ");
    Serial.println(clientID);

    for (const char *const &topic: {serverStreamTopic, serverWillTopic}) {
        if (mqttClient.subscribe(topic)) {
            Serial.print("## Subscribed to ");
            Serial.println(topic);
        } else {
            Serial.print("## Failed to subscribe to ");
            Serial.println(topic);
        }
    }

    return true;
}

void connectToInternet() {
    Serial.print('\r');
    Serial.print(RepeatedString(" ", statusMessageLength()));
    Serial.print("\rWi-Fi status: ");
    Serial.println(WiFi.status());
    Serial.print(statusMessage);
    Serial.flush();

    WiFi.begin(wifiSSID, wifiPass);
}

JSONVar createMessage(Status::Value status, const char *slots, const char *tag) {
    JSONVar payloadObject;

#if USE_UNSAFE_POINTER_CAST
    MQTTMessagePayload payload{
            .message = Status::as_string(status),
            .RFID    = tag,
            .slots   = slots,
            .status  = String(int(status)).c_str(),
    };

    // used to iterate over the members of the struct dynamically
    // WARNING: could cause undefined behavior if the order of the members is changed
    auto            pField = reinterpret_cast<const char **>(&payload);
    for (const char *fieldName: memberNames) {
        Serial.println(*pField);
        payloadObject[fieldName] = *pField++;
    }
#endif
#if !USE_UNSAFE_POINTER_CAST
    payloadObject["status"]  = int(status);
    payloadObject["message"] = Status::as_string(status);
    payloadObject["RFID"]    = tag;
    payloadObject["slots"]   = slots;
#endif

    return payloadObject;
}

int sendMessage(const char *topic, const JSONVar &message) {
    if (!mqttClient.beginMessage(topic, true, 2)) {
        Serial.println("## Failed to begin message");

        return 0;
    }

    mqttClient.print(message);
    return mqttClient.endMessage();
}

int sendWillMessage(const JSONVar &message) {
    if (mqttClient.beginWill(arduinoWillTopic, true, 2)) {
        Serial.println("## Failed to begin will message");

        return 0;
    }

    mqttClient.print(JSON.stringify(message));
    return mqttClient.endWill();
}

// if isOpen is true, open the door, else close it
void openDoor(bool isOpen) {
    if (isOpen)
        servo.write(0);
    else
        servo.write(90);
}

void Status::handleErrorOccur(const JSONVar &MQTTMessage) {
    Status::ERROR_OCCURRED = true;

    Serial.print("[");
    Serial.print(Status::as_string(Status::Value::ERROR_OCCUR));
    Serial.print("]: ");
    Serial.println(MQTTMessage["message"]);
}

void Status::handleErrorResolve(const JSONVar &MQTTMessage) {
    Status::ERROR_OCCURRED = false;

    Serial.print("[");
    Serial.print(Status::as_string(Status::Value::ERROR_RESOLVE));
    Serial.print("]: ");
    Serial.println(MQTTMessage["message"]);
}

void Status::handleOpen(const JSONVar &MQTTMessage) {
    if (!Status::SERVER_CONNECTED || Status::ERROR_OCCURRED) return;

    Serial.print("[");
    Serial.print(Status::as_string(Status::Value::OPEN));
    Serial.print("]: ");
    Serial.println(MQTTMessage["message"]);

    openDoor(true);
}

void Status::handleConnect(const JSONVar &MQTTMessage) {
    Status::SERVER_CONNECTED = true;

    Serial.println("[server connected]: ");
    Serial.println(MQTTMessage["message"]);
}

void Status::handleDisconnect(const JSONVar &MQTTMessage) {
    Status::SERVER_CONNECTED = false;

    Serial.println("[server disconnected]: ");
    Serial.println(MQTTMessage["message"]);
}
