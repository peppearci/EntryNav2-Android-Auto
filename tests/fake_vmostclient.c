// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
/* PC-only stand-in for the HU's libvmostclient.so.1 (tests/test_evo_wlan.cpp, tests/test_evo_reboot.cpp,
 * tests/test_evo_input.cpp). Records every call into $FAKE_VMOST_LOG and answers like:
 *   - wland: WLANState3 Get -> Status($FAKE_WLAN_STATE); SelectWLANState3 StartResultAck -> ResultAck(handle) then
 *     Status(1). $FAKE_WLAN_SILENT=1: no answer at all.
 *   - the Cortex-M4 to ECUReset (0100.FD.04.C00 Set F4 63 11 01), as $FAKE_ECURESET_MODE says: ok (51 01), pending_ok
 *     (7F 11 78 then 51 01), alt_inst (51 01 on FD.41), nrc22 (7F 11 22), most_error (operation 0xF), silent,
 *     other_tester (51 01 for tester F1), other_address (51 01 from address 0x0200), echo (a copy of the request),
 *     send_refused (the write of the request fails: see below). Unset: ok.
 * $FAKE_VMOST_FAIL_REGISTER=1: vmostd refuses vmostFblockShadowRegister (an answer, the connection stays).
 *
 * Like the car's library (libvmostclient.so.1.6.0, platform/vmost_client.hpp):
 *   - it exports 'connected', 1 from vmostConnect to vmostDisconnect;
 *   - a read or a write that fails runs its own vmostDisconnect, and the call returns an error;
 *   - vmostMostMsgSend and vmostProcessMessages return 26 without a connection, and do nothing else;
 *   - vmostFblockShadowRegister and Deregister do not look: once disconnected they fail on the closed socket and
 *     disconnect again;
 *   - vmostDisconnect does not look either: a second one after a connection frees its message queue twice, which
 *     glibc aborts on the car (free(): invalid pointer). Here: a line in the log and on stderr, then abort().
 *   - vmostFileDescriptorGet still gives the closed socket's number after the disconnection.
 * vmostd leaves after it took $FAKE_VMOST_GONE_AFTER messages (vmostMostMsgSend; 0: at once), as
 * $FAKE_VMOST_GONE_HOW says, and every later write or read fails:
 *   hangup  (the default) it closes its end: the descriptor hangs up;
 *   broken  the connection breaks while a message comes: the descriptor is readable, the read fails;
 *   deaf    its socket takes nothing more: the next write fails before the descriptor says anything.
 * Built with FAKE_VMOST_NO_CONNECTED: a library without the 'connected' symbol, otherwise the same.
 * ConnectionMaster route requests (tests/test_audio_route.cpp): see $FAKE_ROUTE_REFUSALS below. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef FAKE_VMOST_NO_CONNECTED
static int connected;
#else
int connected; /* as the car's library exports it */
#endif

typedef void (*cb_t)(void*, void*);
static cb_t g_cb;
static void* g_user;
static int g_fd = -1;     /* the library's end; its number stays after the disconnection, as on the car */
static int g_daemon = -1; /* vmostd's end */
static int g_queue;       /* the library's message queue: 0 none, 1 allocated by vmostConnect, 2 freed */
static int g_taken;       /* messages vmostd took on this connection */
static int g_goneAfter;   /* -1: vmostd stays */
static int g_gone;        /* vmostd left: every write and read fails */
static int g_brokenRead;  /* the next read fails */
struct out_msg {
    uint16_t addr;
    uint8_t fblock, inst;
    uint16_t fkt, r0;
    uint32_t op;
    uint16_t len, r1;
    const uint8_t* data;
    uint32_t channel;
};
struct queued {
    uint16_t addr;
    uint8_t fblock, inst;
    uint16_t fkt;
    uint32_t op;
    uint16_t len;
    uint8_t data[8];
};
static struct queued g_q[16];
static int g_nq;
static int g_refused; /* route requests refused since the last connect */

static void logLine(const char* text) {
    const char* p = getenv("FAKE_VMOST_LOG");
    FILE* f = p ? fopen(p, "a") : 0;
    if (f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

static void enqueueFrom(uint16_t addr, uint8_t fb, uint8_t in, uint16_t fkt, uint32_t op, const uint8_t* d,
                        uint16_t len) {
    if (g_nq >= 16 || g_gone || g_daemon < 0) {
        return;
    }
    struct queued* q = &g_q[g_nq++];
    q->addr = addr;
    q->fblock = fb;
    q->inst = in;
    q->fkt = fkt;
    q->op = op;
    q->len = len;
    memcpy(q->data, d, len);
    if (write(g_daemon, "x", 1) != 1) {
    }
}
static void enqueue(uint8_t fb, uint8_t in, uint16_t fkt, uint32_t op, const uint8_t* d, uint16_t len) {
    enqueueFrom(0x0100, fb, in, fkt, op, d, len);
}

static void leave(void) {
    const char* how = getenv("FAKE_VMOST_GONE_HOW");
    char line[64];
    how = how ? how : "hangup";
    snprintf(line, sizeof(line), "vmostd leaves (%s)", how);
    logLine(line);
    g_gone = 1;
    g_nq = 0;
    if (strcmp(how, "broken") == 0) {
        g_brokenRead = 1;
        if (write(g_daemon, "x", 1) != 1) {
        }
    } else if (strcmp(how, "deaf") != 0) {
        close(g_daemon);
        g_daemon = -1;
    }
}

int vmostDisconnect(void) {
    logLine("disconnect");
    if (g_queue == 2) {
        logLine("abort: vmostDisconnect again, the message queue freed twice");
        fprintf(stderr, "[fake libvmostclient] vmostDisconnect again: free(): invalid pointer, as on the car\n");
        abort();
    }
    if (g_queue == 1) {
        close(g_fd);
        if (g_daemon >= 0) {
            close(g_daemon);
        }
        g_daemon = -1;
        g_queue = 2;
    }
    connected = 0;
    return 0;
}

/* The library's own vmostDisconnect after a failed read or write. */
static void dropConnection(const char* what) {
    char line[96];
    snprintf(line, sizeof(line), "the library disconnects itself (%s failed)", what);
    logLine(line);
    vmostDisconnect();
}

int vmostConnect(cb_t cb, void* user, int flags) {
    int p[2];
    const char* after = getenv("FAKE_VMOST_GONE_AFTER");
    (void)flags;
    if (pipe(p) != 0) {
        return -1;
    }
    logLine("connect");
    g_cb = cb;
    g_user = user;
    g_nq = 0;
    g_refused = 0;
    g_fd = p[0];
    g_daemon = p[1];
    g_queue = 1;
    g_taken = 0;
    g_gone = 0;
    g_brokenRead = 0;
    g_goneAfter = after ? atoi(after) : -1;
    connected = 1;
    if (g_goneAfter == 0) {
        leave();
    }
    return 0;
}

/* The exchange a (de)registration waits on: written whether connected or not, as the car's library does. */
static int exchange(const char* what, uint8_t fb, uint8_t in) {
    char line[48];
    snprintf(line, sizeof(line), "%s %02x.%02x", what, fb, in);
    logLine(line);
    if (g_queue == 2 || g_gone) {
        dropConnection(strcmp(what, "register") == 0 ? "a registration" : "a deregistration");
        return -1;
    }
    return 0;
}
int vmostFblockShadowRegister(uint8_t fb, uint8_t in) {
    const int rc = exchange("register", fb, in);
    return rc != 0 ? rc : getenv("FAKE_VMOST_FAIL_REGISTER") ? -1 : 0;
}
int vmostFblockShadowDeregister(uint8_t fb, uint8_t in) {
    return exchange("deregister", fb, in);
}
int vmostFileDescriptorGet(int* fd) {
    *fd = g_fd;
    return 0;
}
// The M4's answer to ECUReset, as $FAKE_ECURESET_MODE says.
static void answerEcuReset(const struct out_msg* m) {
    static const uint8_t ok[4] = {0xF4, 0x63, 0x51, 0x01};
    static const uint8_t pending[5] = {0xF4, 0x63, 0x7F, 0x11, 0x78};
    static const uint8_t refused[5] = {0xF4, 0x63, 0x7F, 0x11, 0x22};
    static const uint8_t mostError[2] = {0x02, 0x01};
    static const uint8_t otherTester[4] = {0xF1, 0x63, 0x51, 0x01};
    const char* mode = getenv("FAKE_ECURESET_MODE");
    if (!mode || strcmp(mode, "ok") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "pending_ok") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, pending, 5);
        enqueue(0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "alt_inst") == 0) {
        enqueue(0xFD, 0x41, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "nrc22") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, refused, 5);
    } else if (strcmp(mode, "most_error") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xF, mostError, 2);
    } else if (strcmp(mode, "other_tester") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0xC, otherTester, 4);
    } else if (strcmp(mode, "other_address") == 0) {
        enqueueFrom(0x0200, 0xFD, 0x04, 0xC00, 0xC, ok, 4);
    } else if (strcmp(mode, "echo") == 0) {
        enqueue(0xFD, 0x04, 0xC00, 0x0, m->data, m->len);
    }
}
static int isEcuReset(const struct out_msg* m) {
    return m->addr == 0x0100 && m->fblock == 0xFD && m->inst == 4 && m->fkt == 0xC00 && m->op == 0 && m->len == 4 &&
           m->data[0] == 0xF4 && m->data[1] == 0x63 && m->data[2] == 0x11 && m->data[3] == 0x01;
}
int vmostMostMsgSend(void* msg) {
    const struct out_msg* m = (const struct out_msg*)msg;
    const char* mode = getenv("FAKE_ECURESET_MODE");
    char line[160];
    int o = snprintf(line, sizeof(line), "send addr=%04x %02x.%02x.%03x op=%u len=%u :", m->addr, m->fblock, m->inst,
                     m->fkt, m->op, m->len);
    for (unsigned i = 0; i < m->len && o > 0 && o < (int)sizeof(line) - 4; ++i) {
        o += snprintf(line + o, sizeof(line) - o, " %02x", m->data[i]);
    }
    logLine(line);
    if (!connected) {
        return 26;
    }
    if (g_gone || (isEcuReset(m) && mode && strcmp(mode, "send_refused") == 0)) {
        dropConnection("a write");
        return 5;
    }
    ++g_taken;
    if (g_goneAfter >= 0 && g_taken >= g_goneAfter) {
        leave();
        return 0;
    }
    if (getenv("FAKE_WLAN_SILENT")) {
        return 0;
    }
    if (m->fblock == 0x03 && m->inst == 0 && (m->fkt == 0x053 || m->fkt == 0x054) && m->op == 6 && m->len == 4) {
        /* ConnectionMaster: ErrorAck (op 9) to the first $FAKE_ROUTE_REFUSALS requests, then ResultAck (op 0xD). */
        const char* r = getenv("FAKE_ROUTE_REFUSALS");
        const int ok = g_refused >= (r ? atoi(r) : 0);
        if (!ok) {
            ++g_refused;
        }
        enqueue(0x03, 0, m->fkt, ok ? 0xD : 0x9, m->data, 2);
    } else if (m->fblock == 0xA0 && m->inst == 1 && m->fkt == 0xC10 && m->op == 1) {
        const char* s = getenv("FAKE_WLAN_STATE");
        uint8_t st = (uint8_t)(s ? atoi(s) : 0);
        enqueue(0xA0, 1, 0xC10, 0xC, &st, 1);
    } else if (m->fblock == 0xA0 && m->inst == 1 && m->fkt == 0xC11 && m->op == 6 && m->len == 3) {
        uint8_t one = 1;
        enqueue(0xA0, 1, 0xC11, 0xD, m->data, 2);
        enqueue(0xA0, 1, 0xC10, 0xC, &one, 1);
    } else if (isEcuReset(m)) {
        answerEcuReset(m);
    }
    return 0;
}
int vmostProcessMessages(void) {
    char b[16];
    if (!connected) {
        return 26;
    }
    const ssize_t got = read(g_fd, b, sizeof(b));
    if (got <= 0 || g_brokenRead) {
        dropConnection("a read");
        return 24;
    }
    int n = g_nq;
    g_nq = 0;
    for (int i = 0; i < n; ++i) {
        unsigned char raw[32];
        memset(raw, 0, sizeof(raw));
        const uint8_t* dp = g_q[i].data;
        memcpy(raw, &g_q[i].addr, 2);
        raw[2] = g_q[i].fblock;
        raw[3] = g_q[i].inst;
        memcpy(raw + 4, &g_q[i].fkt, 2);
        memcpy(raw + 8, &g_q[i].op, 4);
        memcpy(raw + 12, &g_q[i].len, 2);
        memcpy(raw + 16, &dp, sizeof(dp));
        g_cb(raw, g_user);
    }
    return 0;
}
