// DolphinPS4 USB diagnostic probe (standalone; not part of Dolphin).
//
// Lists every USB device the OpenOrbis sceUsbd wrappers (libSceUsbd.sprx, a libusb 1.0
// build) can see and logs VID/PID, class codes, bus/address/speed, product/manufacturer/
// serial strings, and for every interface its class and endpoint descriptors.
//
// Read-only by design:
//   - never calls SetConfiguration, ResetDevice, Detach/AttachKernelDriver, SetInterfaceAltSetting
//   - never sends output or class/vendor control requests. The only control traffic is the
//     standard GET_DESCRIPTOR (string descriptors) issued by sceUsbdGetStringDescriptorAscii,
//     which needs the device open (disable with the "nostrings" option below).
//   - the optional HID read (opt-in, see below) only does interrupt-IN reads with short
//     timeouts on a HID interface. libusb requires the interface to be claimed for that; it is
//     released again afterwards. Nothing is ever written to the device.
//
// Options: /data/DolphinPS4/usb-probe.cfg, one word per line:
//   nostrings      do not open devices (no product/manufacturer/serial)
//   read           attempt a bounded interrupt-IN read on the first HID or vendor-specific (XInput) interface (claims it)
//   read=VVVV:PPPP same, but only for that VID:PID (hex), e.g. read=0079:1844
//   read_ms=N      total read window per interface, default 5000 (100..30000)
// Results: /data/DolphinPS4/usb-probe.log (and klog). usb-probe_state records the step in
// progress; if the app dies, that step is skipped on the next launch (usb-probe_skip).

#include <errno.h>
#include <orbis/Sysmodule.h>
#include <orbis/Usbd.h>
#include <orbis/libkernel.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <set>
#include <string>

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

namespace {

constexpr const char* kDir = "/data/DolphinPS4";
constexpr const char* kLogPath = "/data/DolphinPS4/usb-probe.log";
constexpr const char* kCfgPath = "/data/DolphinPS4/usb-probe.cfg";
constexpr const char* kStatePath = "/data/DolphinPS4/usb-probe_state";
constexpr const char* kSkipPath = "/data/DolphinPS4/usb-probe_skip";
constexpr uint8_t kClassHid = 3;
// sceUsbd reports libusb error -N as 0x80240000 + N (timeout: 0x80240007).
bool isTimeout(int r) {
    return r == -7 || static_cast<uint32_t>(r) == 0x80240007u;
}

FILE* g_log;
std::set<std::string> g_skip;
bool g_strings = true;
bool g_read = false;
bool g_readAny = true;
uint16_t g_readVid, g_readPid;
int g_readMs = 5000;

__attribute__((format(printf, 1, 2))) void log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log) {
        fputs(buf, g_log);
        fputc('\n', g_log);
        fflush(g_log);
    }
    std::string line = std::string("[usb-probe] ") + buf + "\n";
    sceKernelDebugOutText(0, line.c_str());
}

std::string readFile(const char* path) {
    std::string out;
    if (FILE* f = fopen(path, "r")) {
        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
    }
    return out;
}

void writeFile(const char* path, const std::string& s, const char* mode = "w") {
    if (FILE* f = fopen(path, mode)) {
        fputs(s.c_str(), f);
        fclose(f);
    }
}

template <typename F>
void forEachLine(const std::string& text, F&& fn) {
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string l = text.substr(start, end - start);
        while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
            l.pop_back();
        if (!l.empty())
            fn(l);
        start = end + 1;
    }
}

void parseConfig() {
    forEachLine(readFile(kCfgPath), [](const std::string& l) {
        unsigned vid, pid;
        int ms;
        if (l == "nostrings") {
            g_strings = false;
        } else if (l == "read") {
            g_read = true;
        } else if (sscanf(l.c_str(), "read=%x:%x", &vid, &pid) == 2) {
            g_read = true;
            g_readAny = false;
            g_readVid = static_cast<uint16_t>(vid);
            g_readPid = static_cast<uint16_t>(pid);
        } else if (sscanf(l.c_str(), "read_ms=%d", &ms) == 1) {
            g_readMs = ms < 100 ? 100 : (ms > 30000 ? 30000 : ms);
        } else {
            log("config: ignoring unknown line '%s'", l.c_str());
        }
    });
}

// Runs fn unless it killed the app on a previous launch. The state file always names the
// innermost running step; nested steps restore their parent's name when they finish.
template <typename F>
void guarded(const std::string& step, F&& fn) {
    if (g_skip.count(step)) {
        log("  [skip] %s (crashed the app on a previous launch; delete %s to retry)", step.c_str(),
            kSkipPath);
        return;
    }
    std::string parent = readFile(kStatePath);
    writeFile(kStatePath, step);
    fn();
    if (parent.empty())
        remove(kStatePath);
    else
        writeFile(kStatePath, parent);
}

const char* className(uint8_t c) {
    switch (c) {
    case 0x00: return "per-interface";
    case 0x01: return "Audio";
    case 0x02: return "CDC";
    case 0x03: return "HID";
    case 0x05: return "Physical";
    case 0x06: return "Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC-Data";
    case 0x0B: return "Smart Card";
    case 0x0E: return "Video";
    case 0x0F: return "Healthcare";
    case 0xE0: return "Wireless";
    case 0xEF: return "Miscellaneous";
    case 0xFE: return "Application";
    case 0xFF: return "Vendor-specific";
    default: return "?";
    }
}

const char* speedName(int s) {
    switch (s) {
    case 1: return "low (1.5M)";
    case 2: return "full (12M)";
    case 3: return "high (480M)";
    case 4: return "super (5G)";
    default: return "unknown";
    }
}

const char* xferName(uint8_t attr) {
    switch (attr & 3) {
    case 0: return "control";
    case 1: return "isochronous";
    case 2: return "bulk";
    default: return "interrupt";
    }
}

std::string getString(libusb_device_handle* h, uint8_t index) {
    if (index == 0)
        return "(none)";
    unsigned char buf[256] = {};
    int r = sceUsbdGetStringDescriptorAscii(h, index, 0x0409, buf, sizeof(buf) - 1);
    if (r < 0) {
        char e[32];
        snprintf(e, sizeof(e), "(error %d)", r);
        return e;
    }
    return std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(r));
}

struct HidEndpoint {
    uint8_t iface;
    uint8_t addr;
    uint16_t maxPacket;
};

// Bounded interrupt-IN read; short timeouts so this never blocks for long.
void readHid(libusb_device* dev, uint16_t vid, uint16_t pid, const HidEndpoint& ep) {
    libusb_device_handle* h = nullptr;
    int r = sceUsbdOpen(dev, &h);
    if (r < 0 || !h) {
        log("    read: open failed (%d)", r);
        return;
    }
    r = sceUsbdClaimInterface(h, ep.iface);
    if (r < 0) {
        log("    read: claim interface %u failed (%d); the system probably owns it", ep.iface, r);
        sceUsbdClose(h);
        return;
    }
    unsigned char buf[64];
    const int len = ep.maxPacket > 0 && ep.maxPacket < sizeof(buf) ? ep.maxPacket : sizeof(buf);
    int reports = 0, timeouts = 0;
    unsigned char last[64] = {};
    int lastLen = -1;
    constexpr int kSliceMs = 50;
    timespec start = {};
    for (;;) {
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (start.tv_sec == 0)
            start = now;
        long elapsed = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed >= g_readMs || reports >= 400)
            break;
        int got = 0;
        r = sceUsbdInterruptTransfer(h, ep.addr, buf, len, &got, kSliceMs);
        if (r == 0 && got > 0 && (got != lastLen || memcmp(buf, last, got) != 0)) {
            lastLen = got;
            memcpy(last, buf, got);
            char hex[3 * sizeof(buf) + 1] = {};
            for (int i = 0; i < got && i < static_cast<int>(sizeof(buf)); i++)
                snprintf(hex + i * 3, 4, "%02X ", buf[i]);
            log("    read %04X:%04X ep 0x%02X: %d bytes: %s", vid, pid, ep.addr, got, hex);
            reports++;
        } else if (isTimeout(r)) {
            timeouts++;
        } else if (r < 0) {
            log("    read: transfer error %d, stopping", r);
            break;
        }
    }
    log("    read done: %d reports, %d idle slices of %d ms", reports, timeouts, kSliceMs);
    sceUsbdReleaseInterface(h, ep.iface);
    sceUsbdClose(h);
}

void describeConfig(libusb_device* dev, HidEndpoint& hidIn, bool& haveHidIn) {
    libusb_config_descriptor* cfg = nullptr;
    int r = sceUsbdGetActiveConfigDescriptor(dev, &cfg);
    if (r < 0 || !cfg) {
        log("  active config descriptor: error %d", r);
        return;
    }
    log("  config %u: %u interface(s), attrs 0x%02X, max power %u mA", cfg->bConfigurationValue,
        cfg->bNumInterfaces, cfg->bmAttributes, cfg->MaxPower * 2u);
    for (int i = 0; i < cfg->bNumInterfaces; i++) {
        const libusb_interface& intf = cfg->interface[i];
        for (int a = 0; a < intf.num_altsetting; a++) {
            const libusb_interface_descriptor& d = intf.altsetting[a];
            log("  interface %u alt %u: class 0x%02X (%s) sub 0x%02X proto 0x%02X, %u endpoint(s)",
                d.bInterfaceNumber, d.bAlternateSetting, d.bInterfaceClass,
                className(d.bInterfaceClass), d.bInterfaceSubClass, d.bInterfaceProtocol,
                d.bNumEndpoints);
            for (int e = 0; e < d.bNumEndpoints; e++) {
                const libusb_endpoint_descriptor& ep = d.endpoint[e];
                log("    endpoint 0x%02X %s %s, max packet %u, interval %u", ep.bEndpointAddress,
                    (ep.bEndpointAddress & 0x80) ? "IN" : "OUT", xferName(ep.bmAttributes),
                    ep.wMaxPacketSize & 0x7FF, ep.bInterval);
                if ((d.bInterfaceClass == kClassHid || d.bInterfaceClass == 0xFF) && d.bAlternateSetting == 0 &&
                    (ep.bEndpointAddress & 0x80) && (ep.bmAttributes & 3) == 3 && !haveHidIn) {
                    hidIn = {d.bInterfaceNumber, ep.bEndpointAddress,
                             static_cast<uint16_t>(ep.wMaxPacketSize & 0x7FF)};
                    haveHidIn = true;
                }
            }
        }
    }
    sceUsbdFreeConfigDescriptor(cfg);
}

void probeDevice(libusb_device* dev, int index) {
    libusb_device_descriptor dd = {};
    int r = sceUsbdGetDeviceDescriptor(dev, &dd);
    if (r < 0) {
        log("device %d: GetDeviceDescriptor error %d", index, r);
        return;
    }
    log("device %d: bus %u addr %u speed %s", index, sceUsbdGetBusNumber(dev),
        sceUsbdGetDeviceAddress(dev), speedName(sceUsbdGetDeviceSpeed(dev)));
    log("  VID:PID %04X:%04X  USB %X.%02X  bcdDevice %04X", dd.idVendor, dd.idProduct,
        dd.bcdUSB >> 8, dd.bcdUSB & 0xFF, dd.bcdDevice);
    log("  device class 0x%02X (%s) sub 0x%02X proto 0x%02X, %u configuration(s)",
        dd.bDeviceClass, className(dd.bDeviceClass), dd.bDeviceSubClass, dd.bDeviceProtocol,
        dd.bNumConfigurations);

    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04X:%04X", dd.idVendor, dd.idProduct);
    if (g_strings) {
        guarded(std::string("open:") + vidpid, [&] {
            libusb_device_handle* h = nullptr;
            int o = sceUsbdOpen(dev, &h);
            if (o < 0 || !h) {
                log("  open failed (%d): strings unavailable", o);
                return;
            }
            log("  manufacturer: %s", getString(h, dd.iManufacturer).c_str());
            log("  product:      %s", getString(h, dd.iProduct).c_str());
            log("  serial:       %s", getString(h, dd.iSerialNumber).c_str());
            sceUsbdClose(h);
        });
    }

    HidEndpoint hid = {};
    bool haveHid = false;
    describeConfig(dev, hid, haveHid);
    if (!haveHid)
        return;
    log("  HID/vendor interface %u with interrupt-IN endpoint 0x%02X found", hid.iface, hid.addr);
    if (!g_read) {
        log("  (read disabled; add 'read' to %s to try a passive interrupt read)", kCfgPath);
    } else if (!g_readAny && (g_readVid != dd.idVendor || g_readPid != dd.idProduct)) {
        log("  (read filtered out by VID:PID)");
    } else {
        guarded(std::string("read:") + vidpid, [&] { readHid(dev, dd.idVendor, dd.idProduct, hid); });
    }
}

}  // namespace

int main() {
    mkdir(kDir, 0777);
    g_log = fopen(kLogPath, "w");
    if (g_log)
        setvbuf(g_log, nullptr, _IONBF, 0);
    log("DolphinPS4 USB probe (built " __DATE__ " " __TIME__ ")");

    std::string crashed = readFile(kStatePath);
    while (!crashed.empty() && (crashed.back() == '\n' || crashed.back() == '\r'))
        crashed.pop_back();
    if (!crashed.empty()) {
        log("previous run died in step '%s': skipping it this time", crashed.c_str());
        writeFile(kSkipPath, crashed + "\n", "a");
        remove(kStatePath);
    }
    forEachLine(readFile(kSkipPath), [](const std::string& l) { g_skip.insert(l); });
    parseConfig();
    log("options: strings=%d read=%d read_ms=%d", g_strings, g_read, g_readMs);

    guarded("usbd", [] {
        int r = sceSysmoduleLoadModule(ORBIS_SYSMODULE_USBD);
        log("sceSysmoduleLoadModule(USBD 0x%X) = 0x%X", ORBIS_SYSMODULE_USBD, r);
        r = sceUsbdInit();
        log("sceUsbdInit = %d", r);
        if (r < 0)
            return;
        libusb_device** list = nullptr;
        int n = sceUsbdGetDeviceList(&list);
        log("sceUsbdGetDeviceList = %d device(s)", n);
        for (int i = 0; i < n && list; i++)
            guarded("device" + std::to_string(i), [&] { probeDevice(list[i], i); });
        // The list is deliberately not freed: the wrapper header takes one argument, but
        // libusb_free_device_list() takes an unref flag, so the second argument is unknown.
        sceUsbdExit();
    });
    log("done. Close the app with the PS button.");
    for (;;)
        sceKernelUsleep(1000000);
}
