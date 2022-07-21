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

#include <inet/InetInterface.h>
#include <inet/UDPEndPointImplSockets.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>

#include "common.h"
#include <net/net_event.h>
#include <net/net_if.h>
#include <net/wifi_mgmt.h>
#include <zephyr.h>

extern "C" {
#include <utils/common.h>

#include <config.h>
#include <ctrl_iface.h>
#include <wpa_supplicant_i.h>
}

extern struct wpa_supplicant * wpa_s_0;

namespace chip {
namespace DeviceLayer {

namespace {

in6_addr ToZephyrAddr(const Inet::IPAddress & address)
{
    in6_addr zephyrAddr;

    static_assert(sizeof(zephyrAddr.s6_addr) == sizeof(address.Addr), "Unexpected address size");
    memcpy(zephyrAddr.s6_addr, address.Addr, sizeof(address.Addr));

    return zephyrAddr;
}

net_if * GetInterface(Inet::InterfaceId interfaceId = Inet::InterfaceId::Null())
{
    return interfaceId.IsPresent() ? net_if_get_by_index(interfaceId.GetPlatformInterface()) : net_if_get_default();
}

} // namespace

CHIP_ERROR WiFiManager::Init()
{
    // wpa_supplicant instance should be initialized in dedicated supplicant thread
    if (!wpa_s_0)
    {
        ChipLogError(DeviceLayer, "wpa_supplicant is not initialized!");
        return CHIP_ERROR_INTERNAL;
    }

    // TODO: consider moving these to ConnectivityManagerImpl to be prepared for handling multiple interfaces on a single device.
    Inet::UDPEndPointImplSockets::SetJoinMulticastGroupHandler([](Inet::InterfaceId interfaceId, const Inet::IPAddress & address) {
        const in6_addr addr = ToZephyrAddr(address);
        net_if * iface      = GetInterface(interfaceId);
        VerifyOrReturnError(iface != nullptr, INET_ERROR_UNKNOWN_INTERFACE);

        net_if_mcast_addr * maddr = net_if_ipv6_maddr_add(iface, &addr);

        if (maddr && !net_if_ipv6_maddr_is_joined(maddr) && !net_ipv6_is_addr_mcast_link_all_nodes(&addr))
        {
            net_if_ipv6_maddr_join(maddr);
        }

        return CHIP_NO_ERROR;
    });

    Inet::UDPEndPointImplSockets::SetLeaveMulticastGroupHandler([](Inet::InterfaceId interfaceId, const Inet::IPAddress & address) {
        const in6_addr addr = ToZephyrAddr(address);
        net_if * iface      = GetInterface(interfaceId);
        VerifyOrReturnError(iface != nullptr, INET_ERROR_UNKNOWN_INTERFACE);

        if (!net_ipv6_is_addr_mcast_link_all_nodes(&addr) && !net_if_ipv6_maddr_rm(iface, &addr))
        {
            return CHIP_ERROR_INVALID_ADDRESS;
        }

        return CHIP_NO_ERROR;
    });

    ChipLogDetail(DeviceLayer, "wpa_supplicant has been initialized");
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::AddNetwork(const ByteSpan & ssid, const ByteSpan & credentials)
{
    ChipLogDetail(DeviceLayer, "Adding WiFi network");
    mpWpaNetwork = wpa_supplicant_add_network(wpa_s_0);
    if (mpWpaNetwork)
    {
        static constexpr size_t kMaxSsidLen{ 32 };
        mpWpaNetwork->ssid = (u8 *) k_malloc(kMaxSsidLen);

        if (mpWpaNetwork->ssid)
        {
            memcpy(mpWpaNetwork->ssid, ssid.data(), ssid.size());
            mpWpaNetwork->ssid_len      = ssid.size();
            mpWpaNetwork->key_mgmt      = WPA_KEY_MGMT_NONE;
            mpWpaNetwork->disabled      = 1;
            wpa_s_0->conf->filter_ssids = 1;

            return AddPsk(credentials);
        }
    }

    return CHIP_ERROR_INTERNAL;
}

CHIP_ERROR WiFiManager::Connect()
{
    ChipLogDetail(DeviceLayer, "Connecting to WiFi network");
    wpa_supplicant_enable_network(wpa_s_0, mpWpaNetwork);
    wpa_supplicant_select_network(wpa_s_0, mpWpaNetwork);
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::AddPsk(const ByteSpan & credentials)
{
    mpWpaNetwork->key_mgmt = WPA_KEY_MGMT_PSK;
    str_clear_free(mpWpaNetwork->passphrase);
    mpWpaNetwork->passphrase = dup_binstr(credentials.data(), credentials.size());

    if (mpWpaNetwork->passphrase)
    {
        wpa_config_update_psk(mpWpaNetwork);
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

CHIP_ERROR WiFiManager::GetMACAddress(uint8_t * buf)
{
    const net_if * const iface = GetInterface();
    VerifyOrReturnError(iface != nullptr && iface->if_dev != nullptr, CHIP_ERROR_INTERNAL);

    const auto linkAddrStruct = iface->if_dev->link_addr;
    memcpy(buf, linkAddrStruct.addr, linkAddrStruct.len);
    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip
