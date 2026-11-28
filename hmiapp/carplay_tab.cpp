// The CarPlay tab as a way into Android Auto (carplay_tab.hpp).
#include "carplay_tab.hpp"

#include <dlfcn.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>

#include "../platform/service_contract.hpp"
#include "app_log.hpp"
#include "hmi_thread.hpp"
#include "vtable_check.hpp"

namespace evo {
namespace app {

namespace {

// hmiplugin::get<IHmiCarPlayService>() returns one pointer-sized value in r0, its get() takes that value's address.
typedef uintptr_t (*GetUsageFn)();
typedef void* (*UsageGetFn)(const uintptr_t* usage);
typedef void (*SetVisibleFn)(void* service, bool visible);
typedef bool (*IsConnectedFn)(const void* service);
typedef void (*RequestUiFn)(void* self, const void* url);
typedef void (*ExecuteFn)(void* self, int reason);
typedef void (*OpenEntryFn)(void* self, int reason, unsigned state);

GetUsageFn g_getUsage = nullptr;
UsageGetFn g_usageGet = nullptr;
SetVisibleFn g_setVisible = nullptr;
IsConnectedFn g_isConnected = nullptr;
void (*g_onSelected)() = nullptr;

// A vtable slot to replace: in the F48's HmiMain3D, at index from the vtable's symbol, holding function.
struct Hook {
    const char* vtable;
    const char* function;
    int index;
    void* replacement;
    void** table;
    void* original;
};
void onRequestUi(void* self, const void* url);
void onExecute(void* self, int reason);
void onOpenEntry(void* self, int reason, unsigned state);
Hook g_hooks[] = {
    {"_ZTV18CHmiCarPlayService", "_ZN18CHmiCarPlayService16requestCarPlayUIERK10THmiStringItE", 9,
     reinterpret_cast<void*>(&onRequestUi), nullptr, nullptr},
    {"_ZTV24CHmiCarPlayMainMenuEntry", "_ZN24CHmiCarPlayMainMenuEntry7executeE14EExecuteReason", 4,
     reinterpret_cast<void*>(&onExecute), nullptr, nullptr},
    {"_ZTV22CHmiCarPlayApplication",
     "_ZN22CHmiCarPlayApplication14openEntryStateEN15IHmiApplication22EOpenApplicationReasonEj", 9,
     reinterpret_cast<void*>(&onOpenEntry), nullptr, nullptr},
};

std::atomic<bool> g_ready(false);   // from the status poll
std::atomic<bool> g_active(false);  // the tab leads to Android Auto; false: iDrive's functions are called
bool g_hooked = false;
bool g_visible = false;

bool selected() {
    if (!g_active) {
        return false;
    }
    alog("carplay tab: selected");
    g_onSelected();
    return true;
}
void onRequestUi(void* self, const void* url) {
    if (!selected()) {
        reinterpret_cast<RequestUiFn>(g_hooks[0].original)(self, url);
    }
}
void onExecute(void* self, int reason) {
    if (!selected()) {
        reinterpret_cast<ExecuteFn>(g_hooks[1].original)(self, reason);
    }
}
void onOpenEntry(void* self, int reason, unsigned state) {
    if (!selected()) {
        reinterpret_cast<OpenEntryFn>(g_hooks[2].original)(self, reason, state);
    }
}

// The vtables lie in the program's read-only, executable segment: execution is never taken away from their page.
bool write(void** slot, void* value) {
    const uintptr_t size = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    void* page = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(slot) & ~(size - 1));
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
    *slot = value;
    mprotect(page, size, PROT_READ | PROT_EXEC);
    return true;
}

// Runs on HmiMain3D's thread.
void job() {
    const uintptr_t usage = g_getUsage();
    void* service = g_usageGet(&usage);
    if (!service) {
        return;
    }
    // A phone connected to the car's own CarPlay keeps the tab to itself.
    const bool want = g_ready && !g_isConnected(service);
    if (want && !g_hooked) {
        for (Hook& h : g_hooks) {
            if (!write(&h.table[h.index], h.replacement)) {
                alog("carplay tab: vtable of %s cannot be written", h.vtable);
                return;  // the ones replaced pass through: g_active stays false
            }
        }
        g_hooked = true;
    }
    g_active = want;
    if (want != g_visible) {
        g_setVisible(service, want);
        g_visible = want;
        alog("carplay tab: %s", want ? "visible" : "hidden");
    }
}

template <typename T>
bool find(T& out, const char* name) {
    out = reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
    return out != nullptr;
}

}  // namespace

void captureCarPlayTab(void (*onSelected)()) {
    if (access(contract::kCarPlayTab, F_OK) != 0) {
        return;
    }
    bool ok = find(g_getUsage, "_ZN9hmiplugin3getI18IHmiCarPlayServiceEENS_25CPluginInstanceCacheUsageIT_EEv") &&
              find(g_usageGet, "_ZNK9hmiplugin25CPluginInstanceCacheUsageI18IHmiCarPlayServiceE3getEv") &&
              find(g_setVisible, "_ZN18CHmiCarPlayService25setCarPlayMainMenuVisibleEb") &&
              find(g_isConnected, "_ZNK18CHmiCarPlayService18isCarPlayConnectedEv");
    for (Hook& h : g_hooks) {
        if (ok && find(h.table, h.vtable)) {
            const VtableSlot slot = {h.index, dlsym(RTLD_DEFAULT, h.function)};
            ok = vtableSlotsHold(h.table, &slot, 1);
            h.original = slot.function;
        } else {
            ok = false;
        }
    }
    g_onSelected = onSelected;
    if (!ok) {
        alog("carplay tab: not the iDrive software it was written for: off");
    } else if (captureHmiThread(&job)) {
        alog("carplay tab: on");
    }
}

void carPlayTabPoll(bool ready) {
    g_ready = ready;
    requestHmiThread();  // does nothing unless captured
}

void carPlayTabFinalize() {
    g_ready = false;
    g_active = false;
    // The widgets are left as they are: this runs off HmiMain3D's thread, as iDrive shuts the app down.
    for (Hook& h : g_hooks) {
        if (h.table && h.table[h.index] == h.replacement) {
            write(&h.table[h.index], h.original);
        }
    }
    g_hooked = false;
}

}  // namespace app
}  // namespace evo
