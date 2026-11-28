// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// parseWifiChannel (platform/evo_ap.hpp): the Wi-Fi channel file. Only 1 to 11 is accepted.
#include <cstdio>
#include <cstdlib>

#include "../platform/evo_ap.hpp"

#define CHECK(c)                                          \
    do {                                                  \
        if (!(c)) {                                       \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            return 1;                                     \
        }                                                 \
    } while (0)

int main() {
    CHECK(evo::parseWifiChannel("1") == 1);
    CHECK(evo::parseWifiChannel("11\n") == 11);
    CHECK(evo::parseWifiChannel(" 6 \r\n") == 6);
    CHECK(evo::parseWifiChannel("") == 0);
    CHECK(evo::parseWifiChannel("0") == 0);
    CHECK(evo::parseWifiChannel("12") == 0);
    CHECK(evo::parseWifiChannel("1x") == 0);
    CHECK(evo::parseWifiChannel("-1") == 0);
    CHECK(evo::parseWifiChannel("99999999999") == 0);
    puts("PASS wifi channel : 1 a 11 accepte, le reste ignore");
    return 0;
}
