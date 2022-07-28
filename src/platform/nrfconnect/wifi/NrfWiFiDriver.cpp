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

#include "NrfWiFiDriver.h"

#include <platform/KeyValueStoreManager.h>
#include <platform/Zephyr/WiFiManager.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/SafeInt.h>
#include <platform/CHIPDeviceLayer.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::PersistedStorage;

namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

CHIP_ERROR NrfWiFiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    LoadFromStorage();

    if (mStagingNetwork.IsConfigured())
    {
        ReturnErrorOnFailure(WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan()));
    }

    return CHIP_NO_ERROR;
}

void NrfWiFiDriver::Shutdown() {}

CHIP_ERROR NrfWiFiDriver::CommitConfiguration()
{
    ReturnErrorOnFailure(KeyValueStoreMgr().Put(kPassKey, mStagingNetwork.pass, mStagingNetwork.passLen));
    ReturnErrorOnFailure(KeyValueStoreMgr().Put(kSsidKey, mStagingNetwork.ssid, mStagingNetwork.ssidLen));

    return CHIP_NO_ERROR;
}

CHIP_ERROR NrfWiFiDriver::RevertConfiguration()
{
    // Disconnection should happen automatically when WiFiManager::Connect() is called.
    // Here we do it also explicitly to ping the Connectivity Manager which
    // will send the disconnection event as a result.
    // TODO: refactor when the callback-based supplicant API is incorporated
    WiFiManager::Instance().DisconnectStation();
    ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Disabled);

    LoadFromStorage();

    if (mStagingNetwork.IsConfigured())
    {
        ReturnErrorOnFailure(WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan()));
    }

    return CHIP_NO_ERROR;
}

Status NrfWiFiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                         uint8_t & outNetworkIndex)
{
    outDebugText    = {};
    outNetworkIndex = 0;

    VerifyOrReturnError(!mStagingNetwork.IsConfigured() || ssid.data_equal(mStagingNetwork.GetSsidSpan()), Status::kBoundsExceeded);
    VerifyOrReturnError(ssid.size() <= sizeof(mStagingNetwork.ssid), Status::kOutOfRange);
    VerifyOrReturnError(credentials.size() <= sizeof(mStagingNetwork.pass), Status::kOutOfRange);

    memcpy(mStagingNetwork.ssid, ssid.data(), ssid.size());
    memcpy(mStagingNetwork.pass, credentials.data(), credentials.size());
    mStagingNetwork.ssidLen = ssid.size();
    mStagingNetwork.passLen = credentials.size();

    return Status::kSuccess;
}

Status NrfWiFiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText, uint8_t & outNetworkIndex)
{
    outDebugText    = {};
    outNetworkIndex = 0;

    VerifyOrReturnError(networkId.data_equal(mStagingNetwork.GetSsidSpan()), Status::kNetworkIDNotFound);
    mStagingNetwork.Clear();

    return Status::kSuccess;
}

Status NrfWiFiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    outDebugText = {};

    // Only one network is supported for now
    VerifyOrReturnError(index == 0, Status::kOutOfRange);
    VerifyOrReturnError(networkId.data_equal(mStagingNetwork.GetSsidSpan()), Status::kNetworkIDNotFound);

    return Status::kSuccess;
}

void NrfWiFiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    Status status = Status::kSuccess;

    VerifyOrExit(networkId.data_equal(mStagingNetwork.GetSsidSpan()), status = Status::kNetworkIDNotFound);
    VerifyOrExit(mpConnectCallback == nullptr, status = Status::kUnknownError);

    mpConnectCallback = callback;
    WiFiManager::Instance().Connect(mStagingNetwork.GetSsidSpan(), mStagingNetwork.GetPassSpan());
    WaitForConnectionAsync();

exit:
    if (status != Status::kSuccess)
    {
        mpConnectCallback = nullptr;
        callback->OnResult(status, CharSpan(), 0);
    }
}

void NrfWiFiDriver::WaitForConnectionAsync()
{
    DeviceLayer::SystemLayer().StartTimer(
        static_cast<System::Clock::Timeout>(2000), [](System::Layer *, void *) { NrfWiFiDriver::PollTimerCallback(); }, nullptr);
}

void NrfWiFiDriver::PollTimerCallback()
{
    static constexpr uint8_t kMaxRetriesNumber{ 60 };
    static uint8_t retriesNumber;

    WiFiManager::StationStatus status = WiFiManager::Instance().GetStationStatus();

    if (WiFiManager::StationStatus::FULLY_PROVISIONED == status)
    {
        ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Enabled);
        Instance().OnConnectWiFiNetwork();
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
            Instance().OnConnectWiFiNetworkFailed();
        }
    }
}

CHIP_ERROR GetConfiguredNetwork(Network & network)
{
    return CHIP_NO_ERROR;
}

void NrfWiFiDriver::OnConnectWiFiNetwork()
{
    if (mpConnectCallback)
    {
        mpConnectCallback->OnResult(Status::kSuccess, CharSpan(), 0);
        mpConnectCallback = nullptr;
    }
}

void NrfWiFiDriver::OnConnectWiFiNetworkFailed()
{
    if (mpConnectCallback)
    {
        mpConnectCallback->OnResult(Status::kNetworkNotFound, CharSpan(), 0);
        mpConnectCallback = nullptr;
    }
}

void NrfWiFiDriver::ScanNetworks(ByteSpan ssid, WiFiDriver::ScanCallback * callback) {}

void NrfWiFiDriver::LoadFromStorage()
{
    WiFiNetwork network;

    mStagingNetwork = {};
    ReturnOnFailure(KeyValueStoreMgr().Get(kSsidKey, network.ssid, sizeof(network.ssid), &network.ssidLen));
    ReturnOnFailure(KeyValueStoreMgr().Get(kPassKey, network.pass, sizeof(network.pass), &network.passLen));
    mStagingNetwork = network;
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
