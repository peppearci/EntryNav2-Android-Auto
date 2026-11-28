// Copyright (c) 2026 CallMeLewis
// A job on HmiMain3D's own thread (hmi_thread.hpp).
#include "hmi_thread.hpp"

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>

#include <atomic>

#include "app_log.hpp"
#include "vtable_check.hpp"

namespace evo {
namespace app {

namespace {

const int kSlotSendEvent = 5, kSlotRegisterEvent = 9;  // CHmiProcUnit's vtable
const unsigned short kRunId = 1;

typedef void* (*GetHmiProcUnit)();
typedef unsigned short (*RegisterGroup)(const char* name);
typedef void (*RegisterEvent)(void* unit, void* target, void* fn, int adj, unsigned short group, unsigned short id);
typedef void* (*CreateEvent)(unsigned);
typedef bool (*SendEvent)(void* unit, void* event);

void* g_unit = nullptr;  // HmiMain3D's own proc unit
SendEvent g_send = nullptr;
CreateEvent g_create = nullptr;
HmiThreadJob g_job = nullptr;
uint32_t g_key = 0;
pthread_t g_thread;
std::atomic<bool> g_pending(false);
char g_target;  // the listener's target: nothing is read from it

// iDrive's dispatcher calls this as a member function of g_target.
void onEvent(void* /*target*/, const void* /*event*/) {
    g_pending = false;
    const bool ours = pthread_equal(pthread_self(), g_thread);
    static bool logged = false;
    if (!logged) {
        alog("hmi thread: first job %s", ours ? "run on createApp's thread" : "on another thread: not run");
        logged = true;
    }
    if (ours && g_job) {
        g_job();
    }
}

}  // namespace

bool captureHmiThread(HmiThreadJob job) {
    GetHmiProcUnit get = reinterpret_cast<GetHmiProcUnit>(dlsym(RTLD_DEFAULT, "_Z14getHmiProcUnitv"));
    RegisterGroup group = reinterpret_cast<RegisterGroup>(
        dlsym(RTLD_DEFAULT, "_ZN16CHmiDispatchInfo20registerEventGroupIDEPKc"));
    void* registerFn =
        dlsym(RTLD_DEFAULT, "_ZN12CHmiProcUnit15doRegisterEventEPvM15CDelegateTargetFvPK9CHmiEventEtt");
    void* sendFn = dlsym(RTLD_DEFAULT, "_ZN12CHmiProcUnit9sendEventEP9CHmiEvent");
    g_create = reinterpret_cast<CreateEvent>(dlsym(RTLD_DEFAULT, "_ZN25CHmiModePlayerSelectEvent11createEventEj"));
    void* unit = get ? get() : nullptr;
    // The slots must be the functions read: another iDrive build may lay the vtable out differently.
    const VtableSlot slots[] = {{kSlotSendEvent, sendFn}, {kSlotRegisterEvent, registerFn}};
    if (!unit || !group || !g_create || !vtableSlotsHold(*static_cast<void***>(unit), slots, 2)) {
        alog("hmi thread: not available (unit %p, group %s, event %s, or its vtable not as expected)", unit,
             group ? "found" : "missing", g_create ? "found" : "missing");
        return false;
    }
    const unsigned short id = group("EntryAad");
    if (!id) {
        alog("hmi thread: iDrive gave no event group");
        return false;
    }
    g_job = job;
    g_key = static_cast<uint32_t>(id) << 16 | kRunId;
    g_thread = pthread_self();
    g_send = reinterpret_cast<SendEvent>(sendFn);
    reinterpret_cast<RegisterEvent>(registerFn)(unit, &g_target, reinterpret_cast<void*>(&onEvent), 0, id, kRunId);
    g_unit = unit;
    alog("hmi thread: listening on HmiMain3D's unit %p (event group 0x%04x)", unit, id);
    return true;
}

void requestHmiThread() {
    if (!g_unit || g_pending.exchange(true)) {
        return;
    }
    void* event = g_create(0);
    if (!event) {
        g_pending = false;
        return;
    }
    *reinterpret_cast<uint32_t*>(static_cast<char*>(event) + 4) = g_key;
    g_send(g_unit, event);
}

}  // namespace app
}  // namespace evo
