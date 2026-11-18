// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The car's Wi-Fi access point (SSID, passphrase, BSSID, IP) that the phone is told to join.
#ifndef ENTRY_AAD_PLATFORM_EVO_AP_HPP
#define ENTRY_AAD_PLATFORM_EVO_AP_HPP
#include <string>

#include "wireless_setup.hpp"
namespace evo {
// The content of the Wi-Fi channel file (kWifiChannel): a channel from 1 to 11, 0 when empty or anything else.
inline int parseWifiChannel(const std::string& text) {
    int channel = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9' && channel < 100) {
            channel = channel * 10 + (c - '0');
        } else if (c != '\n' && c != '\r' && c != ' ') {
            return 0;
        }
    }
    return channel >= 1 && channel <= 11 ? channel : 0;
}
// Pure parser for hostapd get_config / ap.conf. Does not log credentials.
bool parseHostapdConfig(const std::string& text, aa::ApInfo& out);
// Read-only hostapd query (bounded child), then ap.conf; IP/MAC from wlan0 ioctls.
bool loadAccessPoint(aa::ApInfo& out, std::string& source);
// Moves the running access point to a 2.4 GHz channel (hostapd CSA, bounded child); false when hostapd refuses.
bool setAccessPointChannel(int channel);
}  // namespace evo
#endif
