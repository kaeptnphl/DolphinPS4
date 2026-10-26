// usbpad demo: logs pad connect/disconnect and input changes to /data/DolphinPS4/usbpad-demo.log.
#include <orbis/libkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "usbpad.h"

static FILE* g_log;

static void logLine(const char* line) {
    if (g_log) {
        fputs(line, g_log);
        fputc('\n', g_log);
        fflush(g_log);
    }
    char buf[300];
    snprintf(buf, sizeof(buf), "[usbpad] %s\n", line);
    sceKernelDebugOutText(0, buf);
}

static bool moved(int a, int b, int threshold) {
    return abs(a - b) >= threshold;
}

int main() {
    mkdir("/data/DolphinPS4", 0777);
    g_log = fopen("/data/DolphinPS4/usbpad-demo.log", "w");
    logLine("usbpad demo (built " __DATE__ " " __TIME__ ")");
    usbpad_set_log(logLine);
    if (usbpad_init() != 0)
        logLine("usbpad_init failed");

    bool connected[USBPAD_MAX_PADS] = {};
    usbpad_state last[USBPAD_MAX_PADS] = {};
    for (;;) {
        for (int i = 0; i < USBPAD_MAX_PADS; i++) {
            usbpad_info info;
            usbpad_state s;
            const bool ok = usbpad_get(i, &info, &s) != 0;
            char line[200];
            if (ok != connected[i]) {
                connected[i] = ok;
                last[i] = {};
                if (ok)
                    snprintf(line, sizeof(line), "pad %d connected: %04X:%04X driver %s", i, info.vid,
                             info.pid, info.driver);
                else
                    snprintf(line, sizeof(line), "pad %d disconnected", i);
                logLine(line);
            }
            if (!ok)
                continue;
            // Log only real changes; resting sticks with drift would otherwise flood the log.
            if (s.buttons != last[i].buttons || moved(s.lx, last[i].lx, 4096) ||
                moved(s.ly, last[i].ly, 4096) || moved(s.rx, last[i].rx, 4096) ||
                moved(s.ry, last[i].ry, 4096) || moved(s.lt, last[i].lt, 16) ||
                moved(s.rt, last[i].rt, 16)) {
                snprintf(line, sizeof(line),
                         "pad %d btn %04X  L %6d %6d  R %6d %6d  LT %3u RT %3u  seq %u", i, s.buttons,
                         s.lx, s.ly, s.rx, s.ry, s.lt, s.rt, s.sequence);
                logLine(line);
                last[i] = s;
            }
        }
        sceKernelUsleep(16000);
    }
}
