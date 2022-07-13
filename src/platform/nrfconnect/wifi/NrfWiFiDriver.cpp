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
#include <platform/Zephyr/WiFiManager.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/SafeInt.h>
#include <platform/CHIPDeviceLayer.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

namespace {
constexpr char kWiFiSSIDKeyName[]        = "wifi-ssid";
constexpr char kWiFiCredentialsKeyName[] = "wifi-pass";
static uint8_t WiFiSSIDStr[DeviceLayer::Internal::kMaxWiFiSSIDLength];
} // namespace

CHIP_ERROR NrfWiFiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR NrfWiFiDriver::Shutdown()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR NrfWiFiDriver::CommitConfiguration()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR NrfWiFiDriver::RevertConfiguration()
{
    return CHIP_NO_ERROR;
}

Status NrfWiFiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                         uint8_t & outNetworkIndex)
{
    WiFiManager::Instance().AddNetwork(ssid, credentials);
    return Status::kSuccess;
}

Status NrfWiFiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText, uint8_t & outNetworkIndex)
{
    return Status::kSuccess;
}

Status NrfWiFiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    // Only one network is supported now
    return Status::kSuccess;
}

void NrfWiFiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    Status networkingStatus = Status::kUnknownError;
    mConnectCallback        = callback;
    WiFiManager::Instance().Connect();
    WaitForConnectionAsync();
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

    WiFiManager::StationStatus status = WiFiManager::Instance().NetworkStatus();

    if (WiFiManager::StationStatus::CONNECTED == status)
    {
        GetInstance().OnConnectWiFiNetwork();
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
            GetInstance().OnConnectWiFiNetworkFailed();
        }
    }
}

CHIP_ERROR GetConfiguredNetwork(Network & network)
{
    return CHIP_NO_ERROR;
}

void NrfWiFiDriver::OnConnectWiFiNetwork()
{
    if (mConnectCallback)
    {
        mConnectCallback->OnResult(Status::kSuccess, CharSpan(), 0);
        mConnectCallback = nullptr;
    }
}

void NrfWiFiDriver::OnConnectWiFiNetworkFailed()
{
    if (mConnectCallback)
    {
        mConnectCallback->OnResult(Status::kNetworkNotFound, CharSpan(), 0);
        mConnectCallback = nullptr;
    }
}

void NrfWiFiDriver::ScanNetworks(ByteSpan ssid, WiFiDriver::ScanCallback * callback) {}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
