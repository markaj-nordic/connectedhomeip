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
 *          Provides the wrapper for Zephyr WiFi API
 */

#include "WiFiManager.h"

#include <cstdlib>

#include "lib/core/CHIPError.h"

#include "common.h"
#include <lib/support/logging/CHIPLogging.h>
#include <net/net_event.h>
#include <net/net_if.h>
#include <net/wifi_mgmt.h>
#include <zephyr.h>

extern "C" {
#include "utils/common.h"
#include "config.h"
#include "ctrl_iface.h"
#include "wpa_supplicant_i.h"
}

#define MAX_SSID_LEN 32

extern struct wpa_supplicant * wpa_s_0;

namespace chip {
namespace DeviceLayer {

CHIP_ERROR WiFiManager::Init()
{
    ChipLogError(NotSpecified, "Init called");
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::AddNetwork(ByteSpan ssid, ByteSpan credentials)
{
    ChipLogError(NotSpecified, "AddNetwork called");
    mSsid = wpa_supplicant_add_network(wpa_s_0);
    if (mSsid)
    {
        mSsid->ssid = (u8 *) k_malloc(MAX_SSID_LEN);

        if (mSsid->ssid)
        {
            memcpy(mSsid->ssid, ssid.data(), ssid.size());
            mSsid->ssid_len             = ssid.size();
            mSsid->key_mgmt             = WPA_KEY_MGMT_NONE;
            mSsid->disabled             = 1;
            wpa_s_0->conf->filter_ssids = 1;

            AddPsk(credentials);

            return CHIP_NO_ERROR;
        }
    }

    return CHIP_ERROR_INTERNAL;
}

CHIP_ERROR WiFiManager::Connect()
{
    ChipLogError(NotSpecified, "AddNetWiFiManager::Connect() called");
    wpa_supplicant_enable_network(wpa_s_0, mSsid);
    wpa_supplicant_select_network(wpa_s_0, mSsid);

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::AddPsk(ByteSpan credentials)
{
    mSsid->key_mgmt = WPA_KEY_MGMT_PSK;
    str_clear_free(mSsid->passphrase);
    mSsid->passphrase = dup_binstr(credentials.data(), credentials.size());

    if (mSsid->passphrase)
    {
        wpa_config_update_psk(mSsid);
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INTERNAL;
}

WiFiManager::StationStatus WiFiManager::NetworkStatus()
{
    static const char * kQueryKeyword{ "STATUS" };
    static constexpr uint8_t kKeywordLength{ 6 };
    static constexpr uint16_t kMaxReplySize{ 512 };
    char reply[kMaxReplySize];
    const auto replySize = wpa_supplicant_ctrl_iface_status(wpa_s_0, kQueryKeyword + kKeywordLength, reply, kMaxReplySize);
    return NetworkStatusStringToEnumCode(reply);
}

WiFiManager::StationStatus WiFiManager::NetworkStatusStringToEnumCode(const std::string & aFullStringStatus)
{
    const std::string extractedStatusString = ExtractNetworkStatusString(aFullStringStatus);

    if (0 == extractedStatusString.compare("DISCONNECTED"))
        return StationStatus::DISCONNECTED;
    else if (0 == extractedStatusString.compare("SCANNING"))
        return StationStatus::SCANNING;
    else if (0 == extractedStatusString.compare("COMPLETED"))
        return StationStatus::CONNECTED;
    else
        return StationStatus::NONE;
}

std::string WiFiManager::ExtractNetworkStatusString(const std::string & aFullStringStatus)
{
    // this parser is implemented according to the wpa_supplicant status string format
    static constexpr const char * pattern{ "wpa_state=" };
    auto pos                       = aFullStringStatus.find(pattern);
    const auto wpaStateSubStr      = aFullStringStatus.substr(pos);
    pos                            = wpaStateSubStr.find("=");
    const auto wpaStateValueSubStr = wpaStateSubStr.substr(pos + 1);
    pos                            = wpaStateValueSubStr.find("\n");
    return wpaStateValueSubStr.substr(0, pos);
}

} // namespace DeviceLayer
} // namespace chip
