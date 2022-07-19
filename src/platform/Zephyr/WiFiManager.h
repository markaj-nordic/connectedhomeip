/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Provides the wrapper for Zephyr wpa_supplicant API
 */

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <string>

struct wpa_ssid;
using WpaNetwork = struct wpa_ssid;

namespace chip {
namespace DeviceLayer {

class WiFiManager
{

public:
    enum class StationStatus : uint8_t
    {
        DISCONNECTED,
        SCANNING,
        COMPLETED,
        CONNECTED = COMPLETED,
        NONE
    };

    static WiFiManager & Instance()
    {
        static WiFiManager sInstance;
        return sInstance;
    }

    CHIP_ERROR Init();
    CHIP_ERROR AddNetwork(const ByteSpan & ssid, const ByteSpan & credentials);
    CHIP_ERROR Connect();
    StationStatus NetworkStatus();
    CHIP_ERROR GetMACAddress(uint8_t * buf);

private:
    CHIP_ERROR AddPsk(const ByteSpan & credentials);

    static StationStatus NetworkStatusStringToEnumCode(const std::string & aFullStringStatus);
    static std::string ExtractNetworkStatusString(const std::string & aFullStringStatus);

    WpaNetwork * mpWpaNetwork{ nullptr };
};

} // namespace DeviceLayer
} // namespace chip
