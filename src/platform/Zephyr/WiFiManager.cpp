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

#include <inet/InetInterface.h>
#include <inet/UDPEndPointImplSockets.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>

#include <zephyr.h>

extern "C" {
#include <wpa_supplicant/config.h>
#include <wpa_supplicant/scan.h>
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

net_if * GetInterface(Inet::InterfaceId ifaceId = Inet::InterfaceId::Null())
{
    return ifaceId.IsPresent() ? net_if_get_by_index(ifaceId.GetPlatformInterface()) : net_if_get_default();
}

} // namespace

// These enums shall reflect the overall ordered disconnected->connected flow
// NOTE: it's NOT a unique keyed map
const WiFiManager::StatusMap::StatusPair WiFiManager::StatusMap::sStatusMap[] = {
    { WPA_DISCONNECTED, WiFiManager::StationStatus::DISCONNECTED },
    { WPA_INTERFACE_DISABLED, WiFiManager::StationStatus::DISABLED },
    { WPA_INACTIVE, WiFiManager::StationStatus::DISABLED },
    { WPA_SCANNING, WiFiManager::StationStatus::SCANNING },
    { WPA_AUTHENTICATING, WiFiManager::StationStatus::CONNECTING },
    { WPA_ASSOCIATING, WiFiManager::StationStatus::CONNECTING },
    { WPA_ASSOCIATED, WiFiManager::StationStatus::CONNECTED },
    { WPA_4WAY_HANDSHAKE, WiFiManager::StationStatus::PROVISIONING },
    { WPA_GROUP_HANDSHAKE, WiFiManager::StationStatus::PROVISIONING },
    { WPA_COMPLETED, WiFiManager::StationStatus::FULLY_PROVISIONED }
};

WiFiManager::StationStatus WiFiManager::StatusMap::operator[](wpa_states wpaState)
{
    for (const auto & it : sStatusMap)
    {
        if (wpaState == it.mWpaStatus)
            return it.mStatus;
    }

    return WiFiManager::StationStatus::NONE;
}

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

CHIP_ERROR WiFiManager::Connect(const ByteSpan & ssid, const ByteSpan & credentials, const ConnectionHandling & handling)
{
    ChipLogDetail(DeviceLayer, "Connecting to WiFi network");

    mConnectionSuccessClbk = handling.mOnConnectionSuccess;
    mConnectionFailedClbk  = handling.mOnConnectionFailed;
    mConnectionTimeoutMs   = handling.mConnectionTimeoutMs;

    CHIP_ERROR err = AddNetwork(ssid, credentials);
    if (CHIP_NO_ERROR == err)
    {
        EnableStation(true);
        wpa_supplicant_select_network(wpa_s_0, mpWpaNetwork);
        WaitForConnectionAsync();
    }
    else
    {
        OnConnectionFailed();
    }
    return err;
}

void WiFiManager::OnConnectionSuccess()
{
    if (mConnectionSuccessClbk)
        mConnectionSuccessClbk();
}

void WiFiManager::OnConnectionFailed()
{
    if (mConnectionFailedClbk)
        mConnectionFailedClbk();
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

CHIP_ERROR WiFiManager::GetMACAddress(uint8_t * buf)
{
    const net_if * const iface = GetInterface();
    VerifyOrReturnError(iface != nullptr && iface->if_dev != nullptr, CHIP_ERROR_INTERNAL);

    const auto linkAddrStruct = iface->if_dev->link_addr;
    memcpy(buf, linkAddrStruct.addr, linkAddrStruct.len);

    return CHIP_NO_ERROR;
}

WiFiManager::StationStatus WiFiManager::GetStationStatus()
{
    if (wpa_s_0)
    {
        return StatusFromWpaStatus(wpa_s_0->wpa_state);
    }
    else
    {
        ChipLogError(DeviceLayer, "wpa_supplicant is not initialized!");
        return StationStatus::NONE;
    }
}

WiFiManager::StationStatus WiFiManager::StatusFromWpaStatus(wpa_states status)
{
    ChipLogDetail(DeviceLayer, "WPA internal status: %d", static_cast<int>(status));
    return WiFiManager::StatusMap::GetMap()[status];
}

CHIP_ERROR WiFiManager::EnableStation(bool enable)
{
    VerifyOrReturnError(nullptr != wpa_s_0 && nullptr != mpWpaNetwork, CHIP_ERROR_INTERNAL);
    if (enable)
    {
        wpa_supplicant_enable_network(wpa_s_0, mpWpaNetwork);
    }
    else
    {
        wpa_supplicant_disable_network(wpa_s_0, mpWpaNetwork);
        // TODO: wpa_supplicant_remove_network(wpa_s, ssid->id)??
        // TODO: DisconnectStation()??
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::ClearStationProvisioningData()
{
    VerifyOrReturnError(nullptr != wpa_s_0 && nullptr != mpWpaNetwork, CHIP_ERROR_INTERNAL);
    // TODO(?): wpa_supplicant_deauthenticate(wpa_s_0, 1); // 1 - unspecified reason (IEEE 802.11)
    wpa_supplicant_cancel_scan(wpa_s_0);
    wpa_clear_keys(wpa_s_0, mpWpaNetwork->bssid);
    str_clear_free(mpWpaNetwork->passphrase);
    wpa_config_update_psk(mpWpaNetwork);
    wpa_supplicant_set_state(wpa_s_0, WPA_INACTIVE);

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiManager::DisconnectStation()
{
    VerifyOrReturnError(nullptr != wpa_s_0, CHIP_ERROR_INTERNAL);
    wpa_supplicant_cancel_scan(wpa_s_0);
    wpas_request_disconnection(wpa_s_0);

    return CHIP_NO_ERROR;
}

void WiFiManager::WaitForConnectionAsync()
{
    chip::DeviceLayer::SystemLayer().StartTimer(
        static_cast<System::Clock::Timeout>(1000), [](System::Layer *, void *) { Instance().PollTimerCallback(); }, nullptr);
}

void WiFiManager::PollTimerCallback()
{
    const uint32_t kMaxRetriesNumber{ mConnectionTimeoutMs.count() / 1000 };
    static uint32_t retriesNumber{ 0 };

    if (WiFiManager::StationStatus::FULLY_PROVISIONED == GetStationStatus())
    {
        OnConnectionSuccess();
    }
    else
    {
        if (retriesNumber++ < kMaxRetriesNumber)
        {
            // wait more time
            WaitForConnectionAsync();
        }
        else
        {
            // connection timeout
            OnConnectionFailed();
        }
    }
}

} // namespace DeviceLayer
} // namespace chip
