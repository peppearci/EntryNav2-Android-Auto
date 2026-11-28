// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Keeps iDrive on the app's blank state while Android Auto is on screen ("hold"), and decides when it steps aside.
//
// Android Auto is drawn in its own layer above iDrive, but the knob and the keys still act on whatever iDrive shows
// underneath. While Android Auto is on screen, iDrive must therefore sit on state 13, an inert blank state of the app,
// the way native CarPlay holds its own. Event 52 asks for it, event 51 opens the app's menu, event 50 the iDrive main
// menu (hmiapp/EntryAad.xml). What the machine does, case by case:
// - Open row, entry button: "show" and event 52 at once, not at the next status poll.
// - Android Auto comes up by itself (start automatically, phone reconnected, double MEDIA): over our menu, event 52;
//   during a call, event 52 over iDrive's call screen; over one of iDrive's own screens, where event 52 can be refused,
//   our menu first (event 51), whose focus asks for event 52 at once. After the first refused menu, our own popup
//   (below) and event 52 under it; then up to 2 more menu tries a second apart, then event 52 once.
// - A phone connects: the service keeps Android Auto aside and says so (it was told "await" when it started). Our
//   menu is asked for (event 51) once a second; its focus sends "show" and event 52, so iDrive is on our pages
//   before Android Auto shows.
// - iDrive refuses: it performs no focus event of an app while one of its own popups is the topmost one
//   (CFocusEvent::isCurrentlyAllowed in HmiMain3D: the event answers 215 and does nothing else, it is not even
//   counted). Seen on the car, opened but not started: refused for as long as no key was pressed (64 s, until
//   MENU). Nothing tells an app when that ends, and the answer itself is dropped before it reaches the app. So
//   after one refusal the app opens its own empty popup (popupEvent 53, what RHMI gives an app to come to the
//   front: iDrive lets an app's focus event through when the topmost popup is an app's), asks for the blank
//   state under it a second later, closes it and sends "show". Should iDrive refuse even that, Android Auto
//   is on screen without the hold: the touchscreen is the phone's, the knob also reaches the iDrive screen
//   underneath, and our menu is asked for once a second until it opens (event 52 then).
// - Our own popup is never left open for anything else: closed as soon as the blank state shows or has the focus, our
//   menu has the focus, Android Auto is asked to the front, a call or the camera comes up (opened again once they are
//   gone), and never taken for an iDrive popup over the blank state.
// - A service that did not wait (an older one) shows Android Auto by itself: the same events, then the same
//   asking without the hold.
// - BACK or tilt to our menu: event 52 at once (at most every 300 ms), and at each poll while the menu keeps the focus;
//   at most 8 events 52 until the blank state shows again.
// - Focus given back to the blank state: the hold worked, its tries start over (a focus lost and given back without
//   "visible" used them up for good, and the next BACK left the menu under Android Auto).
// - iDrive popup: the blank state stays visible but loses the focus. Android Auto steps aside for it ("popup 1", then
//   "popup 0" when it closes, hmiapp/popup_peek.hpp) however long it stays, and no event 52 is fired over it.
// - Driving mode change: iDrive keeps its own display over the blank state. Its focus loss is not fought for 15 s, then
//   event 52 every two polls without focus.
// - Hard key (MENU, MEDIA, NAV, COM, MAP, OPTION): iDrive left the blank state. Confirmed on a second poll within 3 s,
//   Android Auto steps aside ("hide"), the way MENU leaves native CarPlay; the Android Auto entry brings it back.
// - iDrive's FM station page (screen 523) instead, with no key left to iDrive in the last 6 s (the service's file):
//   iDrive raised it by itself, the tuner following the station to another frequency (#138, X1 F48 once moving, up to
//   about 20 times a drive). Event 52 at the first poll, no step aside; at most 4 times in 10 s, past that the hard
//   key step aside above. Only that screen, seen in a car's log: any other iDrive screen without a key (a warning,
//   BMW navigation) still makes Android Auto step aside.
// - The same for a screen a touch opens (kTouchScreens): iDrive reads the touchscreen too, under Android Auto. With a
//   finger sent to the phone in the last 3 s (the service's file) and no key left to iDrive in the last 6 s, event 52
//   at the first poll. No limit of its own: each one needs a new touch.
// - NAV, MEDIA or COM opened an Android Auto app, or BACK / tilt left went to it (the service wrote its app key file
//   within the last 2 s): iDrive switched to its own screen underneath, or went back to the one NAV / MEDIA / COM
//   opened, event 52 at once so the knob does not act on that hidden screen.
// - Android Auto steps aside by itself (exit icon, signal lost) over the blank state: event 50 (iDrive main menu), or
//   event 51 when OPTION opened iDrive's option panel over it. Nothing in reverse or after a hard key.
// - iDrive ignores that event 51 for as long as its option panel is open (seen on a MINI F54, the same software as the
//   BMWs, #137): BACK, tilt left or a touch at the left edge then close the panel and give the focus back to the blank
//   state, with nothing on it. Android Auto is asked to the front at that moment ("show"), as when iDrive goes back to
//   the blank state by itself; event 51 instead if the session is gone meanwhile. Our menu taking the focus (event 51
//   performed) or a choice in the panel (the blank state hidden) cancels it. Not during a call or in reverse.
// - Open row, but Android Auto never came up (session dropped): after 3 s on the blank state, event 51 (menu back).
// - iDrive back on the blank state by itself while Android Auto is minimized (BACK walks its history): "show", like
//   CarPlay.
// - Reverse gear: the camera wins. No popup peek, no "show", no hard key step aside, no app key hold, no event 52
//   for a lost focus. After reverse, keep the picture aside until our app regains focus or the driver chooses
//   Android Auto, with a timeout.
// - Park assist (sensors.PDCStatus 1, also opened by its button without reverse): the same, the service told
//   "pdc 1" hides the picture, "pdc 0" when it ends brings it back. It stays on a while after reverse: the
//   driver choosing Android Auto meanwhile (Open row, entry button) keeps it until park assist changes (#26). So
//   does its view closing out of reverse (BACK, or by itself): iDrive gives the focus back to the blank state (#88).
// - Phone call: iDrive's call screen is not fought; when the call ends, event 52 within 10 s.
// - Service (re)started, or phone connected: it is told the call, the gear and park assist again (started in
//   reverse, Android Auto would come up over the camera).
//
// Pure: no BMW call, no file, no clock. Events and the time come in; iDrive events, control messages and log lines go
// out through HoldIo. Every time is in seconds since the head unit started (hmiapp/mono_clock.hpp), never the date:
// the car sets its date from the GPS, which would stretch or cut the delays below. Single-threaded (the app thread).
// tests/test_hold_machine.cpp.
#ifndef ENTRY_AAD_HMIAPP_HOLD_MACHINE_HPP
#define ENTRY_AAD_HMIAPP_HOLD_MACHINE_HPP

#include <stdint.h>

#include "popup_peek.hpp"
#include "service_state.hpp"

namespace evo {
namespace app {

const int kReverseGear = 2;      // driving.gear
const int kGearUnknown = -1000;  // no driving.gear value received yet
const int kParkAssistOn = 1;     // sensors.PDCStatus
const int kPdcUnknown = -1000;   // no sensors.PDCStatus value received yet
const int kScreenUnknown = -1;   // hmi.graphicalContext: no screen since the blank state last showed
// hmi.graphicalContext visibleContext of iDrive's FM station page (LT_Button_Ent_Radio_StationEntry), the one iDrive
// raises by itself when the tuner follows the station (#138, X1 F48; the same number on NBT Evo). Its saved stations
// page (1325 on NBT Evo) has not been seen on EntryNav2.
const int kRadioStationScreen = 523;
// The screens a touch under Android Auto opened (F48, 26/11): main menu, media / radio, communication, DAB.
const int kTouchScreens[] = {3, 20, 30, 883};

// What the machine does and reads: the app (iDrive, the service's control port, files, app.log), a recorder in tests.
class HoldIo {
public:
    virtual void triggerEvent(unsigned event) = 0;  // a focusEvent of EntryAad.xml: 50, 51 or 52
    virtual void ownPopup(bool open) = 0;           // the app's empty popup (popupEvent 53)
    virtual void send(const char* message) = 0;     // a control message to the service ("show", "hide", ...)
    virtual void log(const char* line) = 0;         // one line of the app's log
    // Read only when a decision needs them, at that moment: the service's state (reading it may ask its control port),
    // and the app key file the service writes when NAV / MEDIA / COM opened an Android Auto app, or BACK / tilt left
    // went to it.
    virtual ServiceState readService() = 0;
    // How long ago the file was written, from its date: negative when the car's date moved back since, an age the
    // machine then takes as unknown, not recent. False: no app key file.
    virtual bool appKeyAge(long* seconds) = 0;
    virtual void removeAppKey() = 0;
    virtual bool hardKeyAge(long* seconds) = 0;  // the same for the file of the last key left to iDrive
    virtual bool touchAge(long* seconds) = 0;    // the same for the file of the last touch sent to the phone

protected:
    // Never deleted through this interface: no virtual destructor, so the app's instance has nothing to run at exit
    // inside HmiMain3D.
    ~HoldIo() = default;
};

class HoldMachine {
public:
    static const int kMaxHoldTries = 8;      // events 52 until the blank state shows again
    static const int kMenuRouteTries = 3;    // events 51 to reach it through our menu
    static const int kUnheldPopupTries = 3;  // own-popup routes while unheld
    static const int kDriveModeQuietS = 15;  // after a driving mode change, its display's focus loss is not fought
    static const int kAppKeyS = 2;           // an app key file this recent means NAV / MEDIA / COM was just pressed
    static const int kCallEndS = 10;         // a call that ended this recently takes iDrive back to the blank state
    static const int kCameraWaitS = 15;      // fall back if iDrive never restores focus to our blank state
    static const long kMenuRefireMs = 300;   // our menu's focus fires event 52 at most this often
    static const int kHardKeyS = 6;          // a key left to iDrive this recently explains iDrive's radio page
    static const int kRadioBacksMax = 4;     // radio pages won back per window; past that, the hard key step aside
    static const int kRadioWindowS = 10;
    static const int kTouchS = 3;            // a touch this recent explains one of kTouchScreens; not counted

    explicit HoldMachine(HoldIo& io);

    // ---- the car (CDS properties) ----
    void callChanged(bool inCall, long now);  // after each update of either phone line
    // True if the caller should forward the gear now; otherwise the hold sends it when the camera lets go.
    bool gearChanged(int gear, long now);
    void pdcChanged(int status);      // sensors.PDCStatus
    void driveModeChanged(long now);  // a real change between two valid driving modes
    void screenChanged(int screen);   // hmi.graphicalContext visibleContext, the iDrive screen shown

    // ---- the app's own requests ----
    void showAndHold(const char* why, long now);  // Open row, entry button: Android Auto to the front, blank at once
    void requestShow();  // Android Auto to the front; the overlay that follows is new and held at the next poll

    // ---- iDrive's focus on the app's states ----
    void aaFocus(bool focused);                                             // the blank state
    void aaVisible(bool visible, long now);                                 // the blank state
    void menuFocus(unsigned state, bool focused, long now, int64_t nowMs);  // the menu or Help; nowMs: milliseconds

    // ---- the status poll, every second, with the service's state just read ----
    // First: tells a (re)started service, or one whose phone just connected, the call and the gear.
    void tellService(const ServiceState& service);
    // Then, once the menu rows are up to date: the hold itself.
    void poll(const ServiceState& service, long now);

    bool holding() const { return holding_; }
    // The blank state is shown: iDrive gives the focus to one of our states at a time, the last one to take it wins,
    // so a notification that never came for the other one cannot leave a stale flag behind.
    bool blankShown() const { return aaVisible_ && !menuFocused_; }
    bool popupPeek() const { return popup_.active(); }
    // Park assist shows its view, and the driver has not chosen Android Auto over it.
    bool parkAssistOwns() const { return pdc_ == kParkAssistOn && !parkDismissed_; }
    // The reverse camera or the park assist view is up: nothing is done over it.
    bool cameraWins() const { return gear_ == kReverseGear || gearPending_ || parkAssistOwns(); }

private:
    void fireHold(const char* why, long now);
    void holdViaMenu(const char* why, long now);
    void closeOwnPopup();
    void dropOwnPopup();
    bool appKeyRecent();
    bool keepForAppKey(const char* why, long now);
    bool winBack(long now);
    void backFromOptionPanel();
    void flushGear(const char* why);
    void leaveParkAssist(const char* why);
    void updatePopupPeek(bool covered, long now);
    void beginWait(long now);
    void askWithoutHold(long now);
    void bringIdrive(const ServiceState& service, long now);
    void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    HoldIo& io_;
    PopupPeek popup_;

    // iDrive's focus
    bool menuFocused_;  // our menu (state 10) or its Help page (12) has the focus
    bool aaVisible_;    // the blank state is visible
    bool aaFocused_;    // the blank state has the focus

    // The hold
    bool holding_;        // Android Auto is (or is coming) on screen: iDrive is kept on the blank state
    bool hideSent_;       // the service was asked to step aside (hard key, or iDrive never showed the blank state)
    bool aaSeen_;         // the blank state was seen since the last event 51 or 52
    bool holdGaveUp_;     // Android Auto stepped aside because iDrive never showed the blank state
    bool prevOverlay_;    // Android Auto was on screen at the previous poll
    long holdTime_;       // last event 51 or 52
    long holdShownAt_;    // when showAndHold asked for the blank state before Android Auto confirmed
    int holdTries_;       // events 52 since the blank state last showed
    int menuRouteTries_;  // events 51 since the blank state last showed
    bool popupTried_;     // our own popup was opened for it since the blank state last showed
    int unfocusedPolls_;  // polls with the blank state shown but unfocused
    long leftSeen_;       // first poll that saw iDrive away from the blank state (hard key), 0: none
    int64_t menuHoldMs_;  // last event 52 fired by our menu's focus
    bool optionPanel_;    // Android Auto stepped aside for iDrive's option panel: its event 51 may have been ignored

    // Android Auto aside until iDrive opens our menu
    bool waiting_;
    bool unheld_;      // Android Auto is shown although iDrive refused our pages: they are still asked for
    bool popupOpen_;   // our own popup is open, for the blank state to be asked for under it
    bool underPopup_;  // the blank state was asked for under it: no popup peek until it gets the focus
    long waitSince_;   // when the wait began (a call or the reverse gear start it again)
    long askedAt_;     // last event 51 fired for it, 0: none yet
    int asks_;         // events 51 fired for it
    int unheldPopups_;  // own-popup routes tried while unheld

    // The car
    bool inCall_;
    long callEnd_;  // when the last call ended, 0: handled
    int gear_;
    bool reverseHeld_;  // the reverse camera interrupted Android Auto
    bool gearPending_;  // the service still sees reverse until the camera lets go
    long gearPendingSince_;
    int pdc_;             // sensors.PDCStatus, kPdcUnknown before the first value
    bool parkDismissed_;  // the driver chose Android Auto during this park assist
    long driveModeAt_;    // last driving mode change, 0: none
    int screen_;          // iDrive screen reported since the blank state last showed, kScreenUnknown: none
    int radioBacks_;      // radio pages won back since radioSince_
    long radioSince_;     // first radio page won back in the current window, 0: none

    // The service, as told about the car
    int servicePid_;
    char prevState_[24];
};

}  // namespace app
}  // namespace evo

#endif
