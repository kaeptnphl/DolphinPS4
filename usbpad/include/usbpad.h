/* usbpad: third-party USB game controllers on PS4 homebrew (OpenOrbis), through sceUsbd.
 *
 * Independent of Dolphin: a plain C API any OpenOrbis project can use. Devices are found and
 * dropped again while running (hotplug), each is served by a small driver that turns the
 * device's input reports into one common state. Read-only: nothing is ever sent to a pad.
 *
 * Drivers so far: XInput (Xbox 360 class pads, 8BitDo GBros. in XInput mode, ...).
 * Thread-safe; usbpad_get() may be called from any thread.
 */
#ifndef USBPAD_H
#define USBPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USBPAD_MAX_PADS 8

/* Button bits (Xbox layout names; A is the bottom face button). */
enum {
    USBPAD_BTN_A = 1u << 0,
    USBPAD_BTN_B = 1u << 1,
    USBPAD_BTN_X = 1u << 2,
    USBPAD_BTN_Y = 1u << 3,
    USBPAD_BTN_LB = 1u << 4,
    USBPAD_BTN_RB = 1u << 5,
    USBPAD_BTN_BACK = 1u << 6,
    USBPAD_BTN_START = 1u << 7,
    USBPAD_BTN_GUIDE = 1u << 8,
    USBPAD_BTN_L3 = 1u << 9,
    USBPAD_BTN_R3 = 1u << 10,
    USBPAD_BTN_DPAD_UP = 1u << 11,
    USBPAD_BTN_DPAD_DOWN = 1u << 12,
    USBPAD_BTN_DPAD_LEFT = 1u << 13,
    USBPAD_BTN_DPAD_RIGHT = 1u << 14,
};

typedef struct {
    uint32_t buttons;       /* USBPAD_BTN_* */
    int16_t lx, ly, rx, ry; /* -32768..32767, up and right are positive */
    uint8_t lt, rt;         /* analog triggers, 0..255 */
    uint32_t sequence;      /* increments with every report received */
} usbpad_state;

typedef struct {
    uint16_t vid, pid;
    char driver[16];        /* name of the driver serving this pad, e.g. "xinput" */
} usbpad_info;

/* Optional; called from library threads. Set before usbpad_init(). */
void usbpad_set_log(void (*fn)(const char* line));

/* Loads the USB module and starts device scanning. Returns 0 on success. */
int usbpad_init(void);
void usbpad_shutdown(void);

/* Slot 0..USBPAD_MAX_PADS-1; a pad keeps its slot while it stays plugged in.
 * Returns 1 and fills the outputs (either may be NULL) if a pad is connected there, else 0. */
int usbpad_get(int slot, usbpad_info* info, usbpad_state* state);

#ifdef __cplusplus
}
#endif

#endif
