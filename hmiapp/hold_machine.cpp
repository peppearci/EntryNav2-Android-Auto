// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The hold's decisions (hmiapp/hold_machine.hpp): which iDrive event, which control message, at which event or poll.
#include "hold_machine.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rhmi_ids.hpp"

namespace evo {
namespace app {

HoldMachine::HoldMachine(HoldIo& io)
    : io_(io),
      menuFocused_(false),
      aaVisible_(false),
      aaFocused_(false),
      holding_(false),
      hideSent_(false),
      aaSeen_(false),
      holdGaveUp_(false),
      prevOverlay_(false),
      holdTime_(0),
      holdShownAt_(0),
      holdTries_(0),
      menuRouteTries_(0),
      popupTried_(false),
      unfocusedPolls_(0),
      leftSeen_(0),
      menuHoldMs_(-1000),
      optionPanel_(false),
      waiting_(false),
      unheld_(false),
      popupOpen_(false),
      underPopup_(false),
      waitSince_(0),
      askedAt_(0),
      asks_(0),
      unheldPopups_(0),
      inCall_(false),
      callEnd_(0),
      gear_(kGearUnknown),
      reverseHeld_(false),
      gearPending_(false),
      gearPendingSince_(0),
      pdc_(kPdcUnknown),
      parkDismissed_(false),
      driveModeAt_(0),
      screen_(kScreenUnknown),
      radioBacks_(0),
      radioSince_(0),
      servicePid_(0) {
    memset(prevState_, 0, sizeof(prevState_));
    strcpy(prevState_, "idle");
}

void HoldMachine::say(const char* fmt, ...) {
    char line[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    io_.log(line);
}

// ---- the car --------------------------------------------------------------------------------------------------------
void HoldMachine::callChanged(bool inCall, long now) {
    if (inCall_ && !inCall) {
        callEnd_ = now;
    }
    inCall_ = inCall;
}

bool HoldMachine::gearChanged(int gear, long now) {
    const bool wasInReverse = gear_ == kReverseGear;
    gear_ = gear;
    if (gear == kReverseGear) {
        if (!wasInReverse) {
            reverseHeld_ = holding_ || gearPending_;
        }
        gearPending_ = false;
        return true;
    }
    if (wasInReverse && reverseHeld_ && !aaFocused_) {
        gearPending_ = true;
        gearPendingSince_ = now;
        say("hold: out of reverse, waiting for camera focus to return");
    }
    return !gearPending_;
}

void HoldMachine::flushGear(const char* why) {
    if (gearPending_) {
        gearPending_ = false;
        char msg[32];
        snprintf(msg, sizeof(msg), "gear %d", gear_);
        io_.send(msg);
        say("hold: %s: service told \"%s\"", why, msg);
    }
}

// Park assist starting or ending is a new episode: a choice of Android Auto made during the previous one is
// forgotten. The service is told whether park assist owns the screen, only when that changes.
void HoldMachine::pdcChanged(int status) {
    if (status == pdc_) {
        return;  // the same value again is not a new episode
    }
    const bool owned = parkAssistOwns();
    pdc_ = status;
    parkDismissed_ = false;
    if (parkAssistOwns() != owned) {
        io_.send(owned ? "pdc 0" : "pdc 1");
        say("park assist %s: Android Auto picture %s", owned ? "ended" : "on", owned ? "back" : "aside");
    }
}

// The picture comes back over a park assist that is still on, until park assist changes.
void HoldMachine::leaveParkAssist(const char* why) {
    if (!parkAssistOwns()) {
        return;
    }
    parkDismissed_ = true;
    io_.send("pdc 0");
    say("park assist: %s, kept until park assist changes", why);
}

void HoldMachine::driveModeChanged(long now) {
    driveModeAt_ = now;
}

// Only kept for a departure from the blank state: forgotten each time it shows again, so a screen seen before Android
// Auto was opened cannot pass for the one iDrive raises later.
void HoldMachine::screenChanged(int screen) {
    screen_ = screen;
}

// ---- events 52 and 51 -----------------------------------------------------------------------------------------------
void HoldMachine::fireHold(const char* why, long now) {
    if (holdTries_ >= kMaxHoldTries) {
        say("hold: not entering state %u (%s): already tried %d times", kAaState, why, holdTries_);
        return;
    }
    ++holdTries_;
    aaSeen_ = false;
    holdTime_ = now;
    io_.triggerEvent(kEvHoldAa);
    say("hold: event %u -> state %u (%s, try %d)", kEvHoldAa, kAaState, why, holdTries_);
}

// Event 52 works from our own states (menu, Help, the blank state). Over one of iDrive's own screens it can be
// refused: Android Auto that comes up by itself (start automatically, phone reconnected) then either stays over that
// screen with the knob reaching both, or steps aside after three refusals. Both were seen on the car, and the same is
// reported on NBT Evo after an automatic start. What the driver does by hand works: our menu first (event 51), whose
// focus asks for the blank state at once (menuFocus).
void HoldMachine::holdViaMenu(const char* why, long now) {
    ++menuRouteTries_;
    aaSeen_ = false;
    holdTime_ = now;
    io_.triggerEvent(kEvOpenMenu);
    say("hold: event %u -> our menu, then state %u (%s, try %d)", kEvOpenMenu, kAaState, why, menuRouteTries_);
}

// ---- our own popup --------------------------------------------------------------------------------------------------
// It only serves to ask for the blank state under it at the next poll. Closed as soon as that is pointless: one of our
// states shows or has the focus, Android Auto is brought to the front another way, it steps aside, the session ends.
// Left open, it kept the focus over the blank state, which then looked covered by an iDrive popup: Android Auto was
// hidden behind an empty popup that nothing closed (code review of 25/11, H10).
void HoldMachine::closeOwnPopup() {
    if (popupOpen_) {
        popupOpen_ = false;
        io_.ownPopup(false);
    }
}

// iDrive's call screen or the camera came up before it was used: not left over them. Nothing was asked under it, so
// the step that opened it comes again once they are gone (while waiting aside, from our menu: iDrive may take it now).
void HoldMachine::dropOwnPopup() {
    if (!popupOpen_) {
        return;
    }
    closeOwnPopup();
    popupTried_ = false;
    if (waiting_) {
        asks_ = 0;
    }
    say("hold: our own popup closed: %s on top", inCall_ ? "iDrive's call screen" : "the camera");
}

// ---- the app's own requests -----------------------------------------------------------------------------------------
// Android Auto comes to the front: iDrive goes to the blank state at once, not at the next status poll, so the knob
// never reaches the screen underneath (entry button and the menu's Open row).
void HoldMachine::showAndHold(const char* why, long now) {
    flushGear(why);
    // Park assist stays on a while after reverse: the driver who picks Android Auto over it gets it.
    leaveParkAssist("Android Auto chosen over it");
    waiting_ = false;
    unheld_ = false;
    optionPanel_ = false;
    closeOwnPopup();  // our menu opened by itself meanwhile
    io_.send("show");
    prevOverlay_ = false;  // the coming overlay is new
    holding_ = true;
    hideSent_ = false;
    holdTries_ = 0;
    holdShownAt_ = now;
    fireHold(why, now);
}

void HoldMachine::requestShow() {
    waiting_ = false;
    unheld_ = false;
    optionPanel_ = false;
    closeOwnPopup();
    io_.send("show");
    prevOverlay_ = false;  // the coming overlay is new: hold iDrive on the blank state
}

// ---- Keys that keep Android Auto on screen ------------------------------------------------------------------------
// NAV / MAP / MEDIA / COM / steering wheel phone opened an Android Auto app, or BACK / tilt left went to it (service
// file, written within the last 2 s). iDrive switched to its own navigation / media / phone screen underneath Android
// Auto, or walked back its history to the screen one of those keys opened (F30, #43: Android Auto stepped aside after
// BACK in its map); put it back on our blank state at once, so the knob does not act on that hidden screen. A key that
// leaves Android Auto (MENU, OPTION) removes the file in the service first.
// A file dated after now (the car's date moved back since it was written) is not recent: its age is unknown. Taken as
// recent, an old app key file kept Android Auto on screen after a hard key.
bool HoldMachine::appKeyRecent() {
    long age = 0;
    return io_.appKeyAge(&age) && age >= 0 && age <= kAppKeyS;
}

// iDrive left the blank state for its FM station page with no key left to iDrive just before: the tuner followed the
// station to another frequency and iDrive raised that page by itself (#138). Back to the blank state at once, a
// bounded number of times per window so a page raised over and over cannot start a fight. A key pressed just before
// (the service's file) is the driver's choice: the hard key step aside.
// iDrive left the blank state without a key for it: it raised its radio page by itself (#138), or it read a touch meant
// for Android Auto (it gets them too: its header buttons and swipes act under the picture). Back to the blank state.
bool HoldMachine::winBack(long now) {
    const bool radio = screen_ == kRadioStationScreen;
    bool touched = false;
    long age = 0;
    for (size_t i = 0; i < sizeof(kTouchScreens) / sizeof(kTouchScreens[0]); ++i) {
        if (screen_ == kTouchScreens[i]) {
            touched = io_.touchAge(&age) && age >= 0 && age <= kTouchS;
        }
    }
    if (!radio && !touched) {
        return false;
    }
    if (io_.hardKeyAge(&age) && age >= 0 && age <= kHardKeyS) {  // dated after now: not recent (appKeyRecent)
        if (!leftSeen_) {
            say("hold: iDrive's %s %ld s after a key left to iDrive: the driver's choice",
                radio ? "radio page" : "screen", age);
        }
        return false;
    }
    // Only the radio page is counted: iDrive raises it by itself. A touched screen needs a touch under 3 s old each
    // time, and quick taps are how Android Auto is used.
    if (radio) {
        if (!radioSince_ || now - radioSince_ >= kRadioWindowS) {
            radioSince_ = now;
            radioBacks_ = 0;
        }
        if (radioBacks_ >= kRadioBacksMax) {
            if (!leftSeen_) {
                say("hold: iDrive's radio page again, %d times in %d s: Android Auto steps aside this time",
                    radioBacks_, kRadioWindowS);
            }
            return false;
        }
        ++radioBacks_;
    }
    leftSeen_ = 0;
    holdTries_ = 0;
    menuRouteTries_ = 0;
    popupTried_ = false;
    if (radio) {
        say("hold: iDrive raised its radio page (screen %d) without a key: back to state %u, %d of %d in %d s", screen_,
            kAaState, radioBacks_, kRadioBacksMax, kRadioWindowS);
    } else {
        say("hold: iDrive screen %d under Android Auto after a touch: back to state %u", screen_, kAaState);
    }
    fireHold(radio ? "iDrive raised its radio page" : "a touch opened an iDrive screen", now);
    return true;
}

bool HoldMachine::keepForAppKey(const char* why, long now) {
    if (!appKeyRecent() || inCall_ || cameraWins()) {
        return false;
    }
    const ServiceState d = io_.readService();
    if (!d.onScreen()) {
        return false;
    }
    io_.removeAppKey();
    holdTries_ = 0;
    fireHold(why, now);
    return true;
}

// ---- iDrive's focus -------------------------------------------------------------------------------------------------
void HoldMachine::aaFocus(bool focused) {
    aaFocused_ = focused;
    if (focused) {
        closeOwnPopup();
        screen_ = kScreenUnknown;
        // The blank state has the focus: it is shown, whatever visibility notification was or was not sent. The hold
        // worked, so its tries start over (a focus lost and given back without "visible" used them up for good, and
        // the next BACK then left the menu under Android Auto for the rest of the session).
        menuFocused_ = false;
        aaVisible_ = true;
        aaSeen_ = true;
        holdTries_ = 0;
        menuRouteTries_ = 0;
        popupTried_ = false;
        holdGaveUp_ = false;
        underPopup_ = false;
        flushGear("blank state regained focus");
        // Out of reverse, park assist still on: iDrive gives the focus back to the blank state when its view closes
        // (BACK, or by itself). The picture stayed hidden over a blank page until park assist ended (#88).
        if (gear_ != kReverseGear) {
            leaveParkAssist("its view closed, iDrive back on the Android Auto page");
        }
    }
    say("hold: state %u focus -> %d", kAaState, static_cast<int>(focused));
    if (focused && optionPanel_) {
        backFromOptionPanel();
    }
}

// OPTION under Android Auto: the service stepped aside, iDrive opened its option panel over the blank state, and the
// app asked for its menu (event 51, poll). iDrive ignores that event for as long as the panel is open (MINI F54, #137,
// 14 times out of 14); BACK, tilt left or a touch at the left edge close it and give the focus back to the blank state,
// still visible under it, with nothing on it. That focus is the answer: Android Auto to the front, as CarPlay does and
// as when iDrive goes back to the blank state by itself (aaVisible). The phone gone meanwhile: our menu, accepted now
// that the panel is closed, rather than a blank page.
void HoldMachine::backFromOptionPanel() {
    optionPanel_ = false;
    if (inCall_ || cameraWins()) {
        return;  // iDrive's call screen or the camera: nothing over them
    }
    const ServiceState d = io_.readService();
    if (d.pid > 0 && d.is("connected")) {
        if (!d.onScreen()) {
            say("hold: OPTION panel closed, iDrive back on the Android Auto page: asking the service to come to the "
                "front");
            requestShow();
        }
        return;
    }
    io_.triggerEvent(kEvOpenMenu);
    say("hold: OPTION panel closed, no phone connected (%s): event %u -> Android Auto menu", d.state, kEvOpenMenu);
}

void HoldMachine::aaVisible(bool visible, long now) {
    aaVisible_ = visible;
    if (!visible) {
        optionPanel_ = false;  // a choice in the option panel took iDrive elsewhere: Android Auto stays aside
    } else {
        if (popupOpen_) {
            // Shown under our popup, which has the focus: what refused our pages may still be on top of the blank
            // state once it is closed, as after event 52 under it.
            closeOwnPopup();
            underPopup_ = true;
        }
        screen_ = kScreenUnknown;
        aaSeen_ = true;
        holdTries_ = 0;
        menuRouteTries_ = 0;
        popupTried_ = false;
    }
    say("hold: state %u visible -> %d", kAaState, static_cast<int>(visible));
    if (!visible && keepForAppKey("a key kept Android Auto on screen", now)) {
        return;
    }
    // iDrive came back to our blank state by itself while Android Auto is minimized (BACK walks its history): bring
    // Android Auto to the front, like CarPlay. Not in reverse or during a call.
    if (visible && !holding_ && !inCall_ && !cameraWins()) {
        const ServiceState d = io_.readService();
        if (d.pid > 0 && !d.onScreen() && d.is("connected")) {
            say("hold: iDrive went back to the Android Auto screen: asking the service to come to the front");
            requestShow();
        }
    }
}

void HoldMachine::menuFocus(unsigned state, bool focused, long now, int64_t nowMs) {
    menuFocused_ = focused;
    if (focused) {
        aaFocused_ = false;
        optionPanel_ = false;  // event 51 was performed: the menu is where the driver is
        closeOwnPopup();
        flushGear("our menu has the focus");
    }
    say("menu state %u focus -> %d", state, static_cast<int>(focused));
    if (focused && unheld_ && !waiting_) {
        say("hold: iDrive opened our menu after %d request(s): taking the hold", asks_);
    }
    if (focused && waiting_ && !inCall_ && !cameraWins()) {
        // Our menu has the focus: iDrive performs our events. Android Auto was waiting for exactly that.
        say("hold: our menu is open after %ld s and %d request(s): Android Auto comes up", now - waitSince_, asks_);
        showAndHold("our menu opened for Android Auto", now);
        return;
    }
    // BACK / tilt-left pop iDrive from the blank state to our menu, or a stray push opened Help under Android Auto:
    // put it straight back (the knob goes to the phone).
    if (focused && holding_ && !inCall_) {
        const ServiceState d = io_.readService();
        if (d.onScreen() && nowMs - menuHoldMs_ >= kMenuRefireMs) {
            menuHoldMs_ = nowMs;
            fireHold("BACK/tilt popped to our menu (immediate)", now);
        }
    }
}

// ---- the status poll ------------------------------------------------------------------------------------------------
// A (re)started service knows neither the call, the gear nor park assist: started in reverse, Android Auto would
// come up over the camera. They are sent when the service becomes reachable, and again when the phone connects.
void HoldMachine::tellService(const ServiceState& service) {
    const bool reachable = service.pid > 0;
    const bool newService = reachable && service.pid != servicePid_;
    if (newService) {
        // From now on a phone that connects waits aside for our "show": iDrive is brought to our pages first. A
        // service that does not know the word shows Android Auto by itself, as before.
        io_.send("await");
    }
    if (newService || (service.is("connected") && strcmp(prevState_, "connected"))) {
        io_.send(inCall_ ? "call 1" : "call 0");
        if (gear_ != kGearUnknown) {
            char msg[32];
            snprintf(msg, sizeof(msg), "gear %d", gearPending_ ? kReverseGear : gear_);
            io_.send(msg);
        }
        if (pdc_ != kPdcUnknown) {
            io_.send(parkAssistOwns() ? "pdc 1" : "pdc 0");
        }
    }
    servicePid_ = reachable ? service.pid : 0;
    strncpy(prevState_, service.state, sizeof(prevState_) - 1);
}

// iDrive popup over our blank state: the service steps aside so the driver sees it (hmiapp/popup_peek.hpp).
void HoldMachine::updatePopupPeek(bool covered, long now) {
    const PopupPeek::Action a = popup_.step(covered, now, driveModeAt_);
    if (a == PopupPeek::kStart) {
        io_.send("popup 1");
        say("popup: iDrive took the focus over state %u (gear %d): Android Auto picture removed", kAaState, gear_);
    } else if (a == PopupPeek::kEnd) {
        io_.send("popup 0");
        say("popup: focus given back: Android Auto picture back");
    }
}

// ---- Android Auto and an iDrive that refuses our pages -------------------------------------------------------------
// HmiMain3D performs no focus event of an app while one of its own popups is the topmost one
// (CFocusEvent::isCurrentlyAllowed: neither a remote state nor CarPlay's; the event returns 215 and nothing else
// happens, it is not even counted). On the car, opened but not started, that lasted until a key was pressed. The
// answer never reaches the app (CLocalCommAdapter::sendEvent drops it) and nothing tells an app when it ends, so the
// only way to know is to ask: our menu (event 51) once a second. Its focus is the answer (menuFocus).
void HoldMachine::beginWait(long now) {
    waiting_ = true;
    waitSince_ = now;
    askedAt_ = 0;
    asks_ = 0;
}

// The service keeps Android Auto aside for us: iDrive on our pages first, when it lets us.
void HoldMachine::bringIdrive(const ServiceState& service, long now) {
    if (service.onScreen() || service.pid <= 0 || !service.is("connected") || !service.asks()) {
        waiting_ = false;  // shown another way, the session is over, or the service no longer waits
        closeOwnPopup();
        return;
    }
    if (inCall_ || cameraWins()) {
        return;  // iDrive's call screen or the camera: not now
    }
    if (menuFocused_ || aaFocused_) {
        say("hold: one of our pages has the focus: Android Auto comes up");
        showAndHold("Android Auto was waiting aside", now);
        return;
    }
    if (askedAt_ && now - askedAt_ < 1) {
        return;
    }
    if (asks_ == 1) {
        // Refused, and iDrive may refuse until a key is pressed. With a popup of ours on top it performs our
        // focus events: opened now, used at the next poll.
        askedAt_ = now;
        ++asks_;
        popupOpen_ = true;
        io_.ownPopup(true);
        say("hold: iDrive refused our menu: our own popup opened (event %u) to ask under it", kEvOwnPopup);
        return;
    }
    if (asks_ >= 2) {
        io_.triggerEvent(kEvHoldAa);
        closeOwnPopup();
        underPopup_ = true;
        askedAt_ = now;
        waiting_ = false;
        unheld_ = true;
        unheldPopups_ = 0;
        holding_ = true;
        hideSent_ = false;
        holdGaveUp_ = false;
        prevOverlay_ = false;
        io_.send("show");
        say("hold: event %u -> state %u asked under our popup, popup closed: Android Auto comes up after %ld s",
            kEvHoldAa, kAaState, now - waitSince_);
        return;
    }
    askedAt_ = now;
    ++asks_;
    io_.triggerEvent(kEvOpenMenu);
    if (asks_ == 1) {
        say("hold: Android Auto waits aside: event %u -> our menu", kEvOpenMenu);
    }
}

// Android Auto is on screen over an iDrive screen that refused our pages: asked again, once a second.
void HoldMachine::askWithoutHold(long now) {
    if (inCall_ || cameraWins() || (askedAt_ && now - askedAt_ < 1)) {
        return;
    }
    askedAt_ = now;
    ++asks_;
    if (menuFocused_) {
        holdTries_ = 0;
        fireHold("our menu opened under Android Auto", now);
        return;
    }
    // On the car our menu never reports focus 1 and the radio may take the screen as our popup closes (app.log,
    // 21/11): whatever lies under Android Auto takes its touches (USB row toggled, FM list). Own popup again.
    if (unheldPopups_ < kUnheldPopupTries) {
        if (!popupOpen_) {
            popupOpen_ = true;
            io_.ownPopup(true);
            say("hold: iDrive screen %d under Android Auto: our own popup opened (event %u) to ask under it", screen_,
                kEvOwnPopup);
            return;
        }
        ++unheldPopups_;
        popupOpen_ = false;
        io_.triggerEvent(kEvHoldAa);
        io_.ownPopup(false);
        say("hold: event %u -> state %u asked under our popup while unheld (try %d), popup closed", kEvHoldAa,
            kAaState, unheldPopups_);
        return;
    }
    io_.triggerEvent(kEvOpenMenu);
}

// Status poll (1 s). service.onScreen(): Android Auto is on screen (knob and touch go to the phone).
void HoldMachine::poll(const ServiceState& service, long now) {
    if (gearPending_ && now - gearPendingSince_ >= kCameraWaitS) {
        flushGear("camera focus wait timed out");
    }
    if (inCall_ || cameraWins()) {
        dropOwnPopup();
    }
    const bool overlay = service.onScreen();
    const bool rose = overlay && !prevOverlay_, fell = !overlay && prevOverlay_;
    prevOverlay_ = overlay;
    const bool blank = blankShown();
    if (blank) {
        // iDrive is back on the blank state: an earlier departure was not a hard key. The next one needs its own
        // second poll, or two short departures within 3 s would add up to a "hide" with no key pressed.
        leftSeen_ = 0;
    }
    // Covered: the blank state is still shown but none of our states has the focus (an iDrive popup over it).
    // Not while the blank state, taken under our own popup, has not had the focus yet: what refused our pages is
    // still on top of it, and it is not a popup the driver can see. Never our own popup itself.
    updatePopupPeek(!rose && !fell && overlay && holding_ && blank && !aaFocused_ && !inCall_ && !cameraWins() &&
                        !underPopup_ && !popupOpen_,
                    now);
    if (rose) {
        waiting_ = false;
        if (unheld_) {
            holding_ = true;  // shown without the hold: our menu goes on being asked for below, from the next poll
            return;
        }
        holdTries_ = 0;
        menuRouteTries_ = 0;
        popupTried_ = false;
        hideSent_ = false;
        holdGaveUp_ = false;
        holding_ = true;
        // The blank state shown but unfocused is an iDrive popup: the peek handles it, no event over it.
        if (menuFocused_) {
            fireHold("Android Auto came on screen over our menu", now);
        } else if (!aaVisible_ && inCall_) {
            fireHold("Android Auto came on screen during a call", now);
        } else if (!aaVisible_) {
            holdViaMenu("Android Auto came on screen over an iDrive screen", now);
        }
        return;
    }
    if (fell) {
        closeOwnPopup();  // opened to hold Android Auto, which is gone
        // Android Auto stepped aside by itself (phone's exit icon, signal lost) while iDrive shows our blank state:
        // give the driver the iDrive main menu. Not in reverse, not when a hard key already took iDrive elsewhere.
        if (blank && !aaFocused_ && !cameraWins()) {
            // OPTION while Android Auto is on screen (the service stepped aside, iDrive opened its option panel over
            // our blank state): the app's menu (Open / Stop / start automatically). iDrive may ignore it under its
            // panel: the focus back on the blank state then brings Android Auto back (backFromOptionPanel).
            io_.triggerEvent(kEvOpenMenu);
            optionPanel_ = true;
            say("hold: OPTION: event %u -> Android Auto menu", kEvOpenMenu);
        } else if (blank && !hideSent_ && !cameraWins()) {
            io_.triggerEvent(kEvGoHome);
            say("hold: Android Auto stepped aside: event %u -> iDrive main menu", kEvGoHome);
        } else {
            say("hold: Android Auto stepped aside (state %u visible %d, hard key %d, gear %d)", kAaState,
                static_cast<int>(aaVisible_), static_cast<int>(hideSent_), gear_);
        }
        holding_ = false;
        unheld_ = false;
        underPopup_ = false;
        return;
    }
    if (unheld_) {
        if (service.pid <= 0 || !service.is("connected")) {
            unheld_ = false;  // the session is over
        } else if (blank || aaFocused_) {
            unheld_ = false;
            say("hold: iDrive is on our blank state after %d request(s): Android Auto is held", asks_);
        } else {
            if (overlay) {
                askWithoutHold(now);
            }
            return;
        }
    }
    if (service.asks() && !waiting_) {
        beginWait(now);
    }
    if (waiting_) {
        bringIdrive(service, now);
        return;
    }
    if (blank) {
        callEnd_ = 0;
    }
    if (overlay && holding_ && blank && !aaFocused_ && !inCall_) {
        if (underPopup_) {
            return;  // shown, and the focus still with what refused our pages: held, nothing to ask again
        }
        if (popup_.active() || (driveModeAt_ && now - driveModeAt_ < kDriveModeQuietS)) {
            return;  // an iDrive popup or the driving mode display: Android Auto steps aside, no event over it
        }
        if (cameraWins()) {
            unfocusedPolls_ = 0;
            return;  // the camera took the focus before Android Auto stepped aside: no event over it
        }
        if (++unfocusedPolls_ >= 2 && now - holdTime_ >= 1) {
            unfocusedPolls_ = 0;
            fireHold("state lost focus", now);
        }
        return;
    }
    unfocusedPolls_ = 0;
    // Event 52 fired for the Open row or the entry button, but Android Auto never came up (session dropped meanwhile):
    // give the driver the menu back instead of a blank screen.
    if (!overlay && holding_ && blank && holdShownAt_ && now - holdShownAt_ >= 3) {
        holdShownAt_ = 0;
        holding_ = false;
        io_.triggerEvent(kEvOpenMenu);
        say("hold: Android Auto did not come up after the blank state: event %u -> menu", kEvOpenMenu);
        return;
    }
    if (overlay) {
        holdShownAt_ = 0;
    }
    if (!overlay || !holding_ || blank) {
        return;
    }
    if (now - holdTime_ < 1) {
        return;
    }
    if (menuFocused_) {
        fireHold("BACK returned to our menu", now);
        return;
    }
    if (inCall_) {
        aaSeen_ = true;
        return;
    }
    if (callEnd_ && now - callEnd_ < kCallEndS) {
        callEnd_ = 0;
        fireHold("call ended", now);
        return;
    }
    if (cameraWins()) {
        return;  // the camera or the park assist view: nothing is asked over it, not even our own popup
    }
    if (!aaSeen_) {
        // Still on an iDrive screen a second after asking: through our menu (holdViaMenu), then event 52 once if the
        // menu never opened. After the first refused menu, our own popup, as for a phone waiting aside
        // (bringIdrive): iDrive performs no focus event of an app under one of its own popups, but does under an
        // app's. Seen on the car after a double MEDIA: Android Auto back over iDrive's screen, every request
        // refused, and the knob driving that screen underneath.
        if (popupOpen_) {
            io_.triggerEvent(kEvHoldAa);
            closeOwnPopup();
            underPopup_ = true;
            aaSeen_ = false;
            holdTime_ = now;
            say("hold: event %u -> state %u asked under our popup over an iDrive screen, popup closed", kEvHoldAa,
                kAaState);
        } else if (menuRouteTries_ == 1 && !popupTried_) {
            popupTried_ = true;
            popupOpen_ = true;
            holdTime_ = now;
            io_.ownPopup(true);
            say("hold: iDrive refused our menu over its screen: our own popup opened (event %u) to ask under it",
                kEvOwnPopup);
        } else if (menuRouteTries_ < kMenuRouteTries) {
            holdViaMenu("state not shown over an iDrive screen", now);
        } else if (holdTries_ == 0) {
            fireHold("our menu did not open: state directly", now);
        } else if (!holdGaveUp_) {
            // iDrive may refuse until a key is pressed: Android Auto stays, and our menu is asked for every second.
            holdGaveUp_ = true;
            unheld_ = true;
            unheldPopups_ = 0;
            askedAt_ = now;
            asks_ = 0;
            say("hold: iDrive did not show state %u, even through our menu: Android Auto stays, without the hold",
                kAaState);
        }
        return;
    }
    // A hard key (MENU / MEDIA / NAV / COM / MAP / OPTION) took iDrive elsewhere, the way MENU leaves native CarPlay:
    // Android Auto steps aside; the Android Auto entry brings it back. Confirmed on a second poll.
    if (keepForAppKey("a key kept Android Auto on screen (poll)", now)) {
        return;
    }
    if (winBack(now)) {
        return;
    }
    if (!leftSeen_ || now - leftSeen_ > 3) {
        leftSeen_ = now;
        return;
    }
    leftSeen_ = 0;
    hideSent_ = true;
    holding_ = false;
    io_.send("hide");
    say("hold: iDrive left state %u (hard key): asked the service to step aside", kAaState);
}

}  // namespace app
}  // namespace evo
