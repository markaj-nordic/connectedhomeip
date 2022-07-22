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

extern "C" {
#include <common/defs.h>
#include <utils/common.h>
}

struct net_if;
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
        DISABLED,
        SCANNING,
        CONNECTING,
        CONNECTED,
        COMPLETED,
        NONE
    };

    class StatusMap
    {
    public:
        static StatusMap & GetMap()
        {
            static StatusMap sInstance;
            return sInstance;
        }
        StationStatus operator[](enum wpa_states aWpaState);

    private:
        struct StatusPair
        {
            wpa_states mWpaStatus;
            WiFiManager::StationStatus mStatus;
        };

        static const StatusPair sStatusMap[];
    };

    static WiFiManager & Instance()
    {
        static WiFiManager sInstance;
        return sInstance;
    }

    CHIP_ERROR Init();
    CHIP_ERROR AddNetwork(const ByteSpan & ssid, const ByteSpan & credentials);
    CHIP_ERROR Connect();
    CHIP_ERROR GetMACAddress(uint8_t * buf);
    StationStatus GetStationStatus();
    CHIP_ERROR EnableStation(bool aEnable);

private:
    CHIP_ERROR AddPsk(const ByteSpan & credentials);
    StationStatus StatusFromWpaStatus(wpa_states aStatus);

    WpaNetwork * mpWpaNetwork{ nullptr };
};

} // namespace DeviceLayer
} // namespace chip
