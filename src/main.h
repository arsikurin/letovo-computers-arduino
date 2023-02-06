#ifndef LETOVO_COMPUTERS_ARDUINO_MAIN_H
#define LETOVO_COMPUTERS_ARDUINO_MAIN_H

#include <set>
#include <vector>
#include <type_traits>
#include <algorithm>

#include <Arduino_JSON.h>
#include <SoftTimer.h>

enum class Status {
    PLACED = 0, TAKEN = 1, SCANNED = 2, DISCONNECTED = 3
};

auto as_integer(Status const value)
-> typename std::underlying_type<Status>::type {
    return static_cast<typename std::underlying_type<Status>::type>(value);
}

const char *as_string(Status const value) {
    switch (value) {
        case Status::PLACED:
            return "placed the computer";
        case Status::TAKEN:
            return "taken the computer";
        case Status::SCANNED:
            return "scanned new tag";
        case Status::DISCONNECTED:
            return "arduino with RFID scanner disconnected";
    }
}


namespace SetSub {
    auto operator-(const std::set<const char *> &_first, const std::set<const char *> &_last)
    -> std::vector<const char *> {
        std::vector<const char *> _res;
        std::set_difference(
                _first.begin(), _first.end(),
                _last.begin(), _last.end(),
                std::inserter(_res, _res.begin())
        );

        return _res;
    }
}

JSONVar createMessage(const char *tag, const String &slots, Status status);

int sendMessage(const char *topic, const JSONVar &message);

int sendWillMessage(const JSONVar &message);

bool connectToBroker();

void connectToInternet();

void openDoor(bool isOpen);

void checkWiFiConnection(Task *me);

void checkBrokerConnection(Task *me);

void listenForRFID(Task *me);

void listenForButtons(Task *me);

void MQTTPoll(__attribute__((unused)) Task *me);

#endif //LETOVO_COMPUTERS_ARDUINO_MAIN_H
