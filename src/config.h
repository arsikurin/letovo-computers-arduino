#ifndef LETOVO_COMPUTERS_ARDUINO_CONFIG_H
#define LETOVO_COMPUTERS_ARDUINO_CONFIG_H

#include <set>
#include <vector>

#include <Arduino_JSON.h>

#include "secrets.h"

static const char     *brokerHost = MQTT_HOST;
static const uint16_t brokerPort  = MQTT_PORT;
static const char     *brokerUser = MQTT_USER;
static const char     *brokerPass = MQTT_PASS;
static const char     *clientID   = MQTT_CLIENT_ID;

static const char *wifiSSID = WIFI_SSID;
static const char *wifiPass = WIFI_PASS;

static const char *arduinoStreamTopic = ARDUINO_STREAM_TOPIC;
static const char *arduinoWillTopic = ARDUINO_WILL_TOPIC;
static const char *serverStreamTopic = SERVER_STREAM_TOPIC;
static const char *serverWillTopic = SERVER_WILL_TOPIC;

static const JSONVar willPayload(  // NOLINT
        R"({"message":"arduino with RFID reader disconnected","RFID":"","slots":"", status:3})");

// num of rows
static const uint8_t ROWS                  = 6;
// num of columns
static const uint8_t COLS                  = 5;
// ID of each key
static const char    *SLOT_IDS[ROWS][COLS] = {
        {"r1c1",  "r1c2",  "r1c3",  "r1c4",  "r1c5"},
        {"r1c6",  "r1c7",  "r1c8",  "r1c9",  "r1c10"},
        {"r1c11", "r1c12", "r1c13", "r1c14", "r1c15"},
        {"r2c1",  "r2c2",  "r2c3",  "r2c4",  "r2c5"},
        {"r2c6",  "r2c7",  "r2c8",  "r2c9",  "r2c10"},
        {"r2c11", "r2c12", "r2c13", "r2c14", "r2c15"},
};
static const uint8_t ROW_PINS[ROWS]        = {2, 3, 4, 5, 6, 7};
static const uint8_t COL_PINS[COLS]        = {8, 9, 10, 11, 12};
static const uint8_t RDM6300_RX_PIN        = 0;
static const uint8_t SERVO_PIN             = A0;
static const uint8_t LED_PIN               = LED_BUILTIN;

static char latestRFID[20] = "null";

#endif //LETOVO_COMPUTERS_ARDUINO_CONFIG_H
