// Copyright (c) 2026 CallMeLewis (CallMeLewis/AA-EntryNav, hmiapp/hmi_thread, reduced to HmiMain3D's own proc unit)
// Runs a job on HmiMain3D's own thread, asked for from any thread. The app's posted tasks (the status poll) run on a
// bmw::Dispatcher thread, where iDrive's per-thread lookups and its widgets must not be touched. An event of the app's
// own is routed to the unit of createApp's thread:
//   - CHmiDispatchInfo::registerEventGroupID(name) gives an event group (a 16-bit hash).
//   - CHmiProcUnit::doRegisterEvent(target, member function, group, id), vtable slot 9, adds the listener; the member
//     function pointer is {fn, adj}, so a plain function with adj 0 is a listener.
//   - CHmiProcUnit::sendEvent (slot 5, from any thread) hands an event to the main dispatcher, which owns it from there.
// The event carried is one of iDrive's own (CHmiModePlayerSelectEvent) with its key (the word at +4) replaced by the
// app's. Symbols and slots checked in the F48's libhmicom.so and HmiMain3D.
#ifndef ENTRY_AAD_HMIAPP_HMI_THREAD_HPP
#define ENTRY_AAD_HMIAPP_HMI_THREAD_HPP

namespace evo {
namespace app {

using HmiThreadJob = void (*)();

// In createApp, on HmiMain3D's thread. False (logged) when something is not as expected: requests then do nothing.
bool captureHmiThread(HmiThreadJob job);

// From any thread: job runs on HmiMain3D's thread soon. Requests made while one is pending are one.
void requestHmiThread();

}  // namespace app
}  // namespace evo

#endif
