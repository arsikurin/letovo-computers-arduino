#ifndef LETOVO_COMPUTERS_ARDUINO_MAIN_H
#define LETOVO_COMPUTERS_ARDUINO_MAIN_H

#include <set>
#include <vector>
#include <type_traits>
#include <algorithm>

#include <Arduino_JSON.h>
#include <SoftTimer.h>

#include "config.h"


namespace Status {
    static bool SERVER_CONNECTED = false;
    static bool ERROR_OCCURRED   = false;

    enum class Value : uint8_t {
        PLACE         = 0,
        TAKE          = 1,
        SCAN          = 2,
        DISCONNECT    = 3,
        CONNECT       = 4,
        OPEN          = 5,
        ERROR_OCCUR   = 7,
        ERROR_RESOLVE = 8
    };

    const char *as_string(Value status) {
        switch (status) {
            case Value::PLACE:
                return "computer placed";
            case Value::TAKE:
                return "computer taken";
            case Value::SCAN:
                return "new tag scanned";
            case Value::DISCONNECT:
                // only for outgoing messages
                return "arduino with RFID scanner disconnected";
            case Value::CONNECT:
                // only for outgoing messages
                return "arduino with RFID scanner connected";
            case Value::OPEN:
                return "opening the door";
            case Value::ERROR_OCCUR:
                // only for incoming messages
                return "server error occurred";
            case Value::ERROR_RESOLVE:
                // only for incoming messages
                return "server error resolved";
            default:
                return "unknown status";
        }
    }

    void handleErrorOccur(const JSONVar &MQTTMessage);

    void handleErrorResolve(const JSONVar &MQTTMessage);

    void handleDisconnect(const JSONVar &MQTTMessage);

    void handleConnect(const JSONVar &MQTTMessage);

    void handleOpen(const JSONVar &MQTTMessage);
}

namespace SetSubtract {
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

#if USE_UNSAFE_POINTER_CAST
// check if _field is a member of _struct and return its name as a string if it is
#define verifyField(_field, _struct) ((_struct){}._field, #_field)

struct MQTTMessagePayload {
    const char *message;
    const char *RFID;
    const char *slots;
    const char *status;
} __attribute__((packed));

const char *memberNames[] = {
        verifyField(message, MQTTMessagePayload),
        verifyField(RFID, MQTTMessagePayload),
        verifyField(slots, MQTTMessagePayload),
        verifyField(status, MQTTMessagePayload),
};
#endif

class RepeatedString : public Printable {
public:
    RepeatedString(const char *str, unsigned int count)
            : _str(str), _count(count) {}

    virtual ~RepeatedString() = default;

    size_t printTo(Print &p) const override {
        size_t sz = 0;

        for (unsigned int i = 0; i < _count; ++i)
            sz += p.print(_str);

        return sz;
    }

private:
    const char   *_str;
    unsigned int _count;
};

JSONVar createMessage(Status::Value status, const char *slots = "", const char *tag = latestRFID);

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
