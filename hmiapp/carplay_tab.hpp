// The CarPlay tab of iDrive's main menu as a way into Android Auto. Off unless the file contract::kCarPlayTab exists.
// It calls iDrive's own CarPlay service and replaces three slots of its vtables: all of it on HmiMain3D's thread
// (hmi_thread.hpp), and nothing at all when a symbol or a slot is not the one read on the F48.
#ifndef ENTRY_AAD_HMIAPP_CARPLAY_TAB_HPP
#define ENTRY_AAD_HMIAPP_CARPLAY_TAB_HPP

namespace evo {
namespace app {

// In createApp, on HmiMain3D's thread. onSelected is called on that thread when the tab is selected.
void captureCarPlayTab(void (*onSelected)());
// From the status poll. ready: Android Auto can be shown.
void carPlayTabPoll(bool ready);
// As the app ends: iDrive gets its own functions back.
void carPlayTabFinalize();

}  // namespace app
}  // namespace evo

#endif
