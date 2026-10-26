// Internal: what a controller driver has to provide.
#pragma once

#include <stdint.h>

#include "usbpad.h"

namespace usbpad {

struct InterfaceInfo {
    uint8_t number;
    uint8_t cls, sub, proto;
    uint8_t inEndpoint;  // first interrupt-IN endpoint of alt setting 0, 0 if none
    uint16_t inMaxPacket;
};

struct DeviceInfo {
    uint16_t vid, pid;
    uint8_t numInterfaces;
    InterfaceInfo interfaces[8];
};

struct Driver {
    const char* name;
    // Returns the interface to claim and read from, or nullptr if this driver does not fit.
    const InterfaceInfo* (*match)(const DeviceInfo& dev);
    // Applies one input report to state; false if the report is not an input report.
    bool (*parse)(const uint8_t* data, int len, usbpad_state& state);
};

extern const Driver kXInputDriver;

}  // namespace usbpad
