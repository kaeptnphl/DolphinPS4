// usbpad core: device discovery (hotplug by rescanning), one reader thread per pad.
#include <orbis/Sysmodule.h>
#include <orbis/Usbd.h>
#include <orbis/libkernel.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver.h"
#include "usbpad.h"

// _types/libusb.h only forward-declares this; layout is libusb 1.0's.
struct libusb_endpoint_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
    uint8_t bRefresh;
    uint8_t bSynchAddress;
    const unsigned char* extra;
    int32_t extra_length;
};

// libusb_free_device_list() takes (list, unref_devices); the wrapper header declares one argument.
extern "C" void usbpad_free_device_list(libusb_device** list, int unref) __asm__("sceUsbdFreeDeviceList");

namespace usbpad {
namespace {

// sceUsbd reports libusb error -N as 0x80240000 + N (timeout: 0x80240007).
bool isTimeout(int r) {
    return r == -7 || static_cast<uint32_t>(r) == 0x80240007u;
}
constexpr int kScanIntervalUs = 1000000;
constexpr int kReadTimeoutMs = 8;

const Driver* const kDrivers[] = {&kXInputDriver};

struct Slot {
    bool used = false;
    bool alive = false;  // reader thread running
    bool stop = false;
    uint16_t key = 0;    // bus << 8 | address
    const Driver* driver = nullptr;
    InterfaceInfo itf = {};
    usbpad_info info = {};
    usbpad_state state = {};
    libusb_device_handle* handle = nullptr;
    pthread_t thread = {};
};

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
Slot g_slots[USBPAD_MAX_PADS];
pthread_t g_scanThread;
bool g_running = false;
bool g_inited = false;
void (*g_log)(const char*) = nullptr;

__attribute__((format(printf, 1, 2))) void logf(const char* fmt, ...) {
    if (!g_log)
        return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log(buf);
}

void* readerMain(void* arg) {
    Slot* s = static_cast<Slot*>(arg);
    uint8_t buf[64];
    const int len = s->itf.inMaxPacket > 0 && s->itf.inMaxPacket < sizeof(buf) ? s->itf.inMaxPacket
                                                                              : sizeof(buf);
    for (;;) {
        pthread_mutex_lock(&g_mutex);
        const bool stop = s->stop;
        pthread_mutex_unlock(&g_mutex);
        if (stop)
            break;
        int got = 0;
        int r = sceUsbdInterruptTransfer(s->handle, s->itf.inEndpoint, buf, len, &got, kReadTimeoutMs);
        if (r == 0 && got > 0) {
            pthread_mutex_lock(&g_mutex);
            if (s->driver->parse(buf, got, s->state))
                s->state.sequence++;
            pthread_mutex_unlock(&g_mutex);
        } else if (r != 0 && !isTimeout(r)) {
            logf("usbpad: %04X:%04X read error %d, dropping pad", s->info.vid, s->info.pid, r);
            break;
        }
    }
    sceUsbdReleaseInterface(s->handle, s->itf.number);
    sceUsbdClose(s->handle);
    pthread_mutex_lock(&g_mutex);
    s->alive = false;
    pthread_mutex_unlock(&g_mutex);
    return nullptr;
}

bool describe(libusb_device* dev, DeviceInfo& out) {
    libusb_device_descriptor dd = {};
    if (sceUsbdGetDeviceDescriptor(dev, &dd) < 0)
        return false;
    out = {};
    out.vid = dd.idVendor;
    out.pid = dd.idProduct;
    libusb_config_descriptor* cfg = nullptr;
    if (sceUsbdGetActiveConfigDescriptor(dev, &cfg) < 0 || !cfg)
        return false;
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const libusb_interface& intf = cfg->interface[i];
        if (intf.num_altsetting < 1 || out.numInterfaces >= 8)
            continue;
        const libusb_interface_descriptor& d = intf.altsetting[0];
        InterfaceInfo& ii = out.interfaces[out.numInterfaces++];
        ii = {d.bInterfaceNumber, d.bInterfaceClass, d.bInterfaceSubClass, d.bInterfaceProtocol, 0, 0};
        for (int e = 0; e < d.bNumEndpoints; e++) {
            const libusb_endpoint_descriptor& ep = d.endpoint[e];
            if ((ep.bEndpointAddress & 0x80) && (ep.bmAttributes & 3) == 3) {
                ii.inEndpoint = ep.bEndpointAddress;
                ii.inMaxPacket = ep.wMaxPacketSize & 0x7FF;
                break;
            }
        }
    }
    sceUsbdFreeConfigDescriptor(cfg);
    return true;
}

void tryStart(libusb_device* dev, uint16_t key) {
    DeviceInfo di;
    if (!describe(dev, di))
        return;
    const Driver* driver = nullptr;
    const InterfaceInfo* itf = nullptr;
    for (const Driver* d : kDrivers) {
        if ((itf = d->match(di))) {
            driver = d;
            break;
        }
    }
    if (!driver)
        return;

    int free = -1;
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < USBPAD_MAX_PADS; i++) {
        if (!g_slots[i].used) {
            free = free < 0 ? i : free;
        } else if (g_slots[i].key == key) {
            pthread_mutex_unlock(&g_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    if (free < 0) {
        logf("usbpad: no free slot for %04X:%04X", di.vid, di.pid);
        return;
    }

    libusb_device_handle* h = nullptr;
    int r = sceUsbdOpen(dev, &h);
    if (r < 0 || !h) {
        logf("usbpad: open %04X:%04X failed (%d)", di.vid, di.pid, r);
        return;
    }
    r = sceUsbdClaimInterface(h, itf->number);
    if (r < 0) {
        logf("usbpad: claim %04X:%04X interface %u failed (%d)", di.vid, di.pid, itf->number, r);
        sceUsbdClose(h);
        return;
    }

    Slot& s = g_slots[free];
    pthread_mutex_lock(&g_mutex);
    s = Slot();
    s.used = true;
    s.alive = true;
    s.key = key;
    s.driver = driver;
    s.itf = *itf;
    s.handle = h;
    s.info.vid = di.vid;
    s.info.pid = di.pid;
    snprintf(s.info.driver, sizeof(s.info.driver), "%s", driver->name);
    pthread_mutex_unlock(&g_mutex);
    if (pthread_create(&s.thread, nullptr, readerMain, &s) != 0) {
        logf("usbpad: reader thread for %04X:%04X failed", di.vid, di.pid);
        sceUsbdReleaseInterface(h, itf->number);
        sceUsbdClose(h);
        pthread_mutex_lock(&g_mutex);
        s = Slot();
        pthread_mutex_unlock(&g_mutex);
        return;
    }
    logf("usbpad: slot %d: %04X:%04X (%s)", free, di.vid, di.pid, driver->name);
}

// Frees slots whose reader thread has ended (pad unplugged or read error).
void reap() {
    for (int i = 0; i < USBPAD_MAX_PADS; i++) {
        pthread_mutex_lock(&g_mutex);
        const bool dead = g_slots[i].used && !g_slots[i].alive;
        pthread_mutex_unlock(&g_mutex);
        if (!dead)
            continue;
        pthread_join(g_slots[i].thread, nullptr);
        logf("usbpad: slot %d released", i);
        pthread_mutex_lock(&g_mutex);
        g_slots[i] = Slot();
        pthread_mutex_unlock(&g_mutex);
    }
}

void* scanMain(void*) {
    for (;;) {
        pthread_mutex_lock(&g_mutex);
        const bool running = g_running;
        pthread_mutex_unlock(&g_mutex);
        if (!running)
            break;
        reap();
        libusb_device** list = nullptr;
        const int n = sceUsbdGetDeviceList(&list);
        for (int i = 0; i < n && list; i++) {
            const uint16_t key = static_cast<uint16_t>((sceUsbdGetBusNumber(list[i]) << 8) |
                                                       sceUsbdGetDeviceAddress(list[i]));
            tryStart(list[i], key);
        }
        // A pad that vanished from the bus ends through its read error.
        if (list && n >= 0)
            usbpad_free_device_list(list, 0);
        sceKernelUsleep(kScanIntervalUs);
    }
    return nullptr;
}

}  // namespace
}  // namespace usbpad

extern "C" {

void usbpad_set_log(void (*fn)(const char*)) {
    usbpad::g_log = fn;
}

int usbpad_init(void) {
    using namespace usbpad;
    if (g_inited)
        return 0;
    int r = sceSysmoduleLoadModule(ORBIS_SYSMODULE_USBD);
    logf("usbpad: load USBD module = 0x%X", r);
    r = sceUsbdInit();
    if (r < 0) {
        logf("usbpad: sceUsbdInit = %d", r);
        return r;
    }
    g_running = true;
    if (pthread_create(&g_scanThread, nullptr, scanMain, nullptr) != 0) {
        g_running = false;
        return -1;
    }
    g_inited = true;
    return 0;
}

void usbpad_shutdown(void) {
    using namespace usbpad;
    if (!g_inited)
        return;
    pthread_mutex_lock(&g_mutex);
    g_running = false;
    for (Slot& s : g_slots)
        s.stop = true;
    pthread_mutex_unlock(&g_mutex);
    pthread_join(g_scanThread, nullptr);
    for (Slot& s : g_slots)
        if (s.used)
            pthread_join(s.thread, nullptr);
    for (Slot& s : g_slots)
        s = Slot();
    sceUsbdExit();
    g_inited = false;
}

int usbpad_get(int slot, usbpad_info* info, usbpad_state* state) {
    using namespace usbpad;
    if (slot < 0 || slot >= USBPAD_MAX_PADS)
        return 0;
    pthread_mutex_lock(&g_mutex);
    const Slot& s = g_slots[slot];
    const bool ok = s.used && s.alive;
    if (ok) {
        if (info)
            *info = s.info;
        if (state)
            *state = s.state;
    }
    pthread_mutex_unlock(&g_mutex);
    return ok ? 1 : 0;
}

}  // extern "C"
