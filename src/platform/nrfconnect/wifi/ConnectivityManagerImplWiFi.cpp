/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
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
/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/CommissionableDataProvider.h>
#include <platform/ConnectivityManager.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/internal/BLEManager.h>

#include "ConnectivityManagerImplWiFi.h"
#include <platform/Zephyr/WiFiManager.h>

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::TLV;

namespace chip {
namespace DeviceLayer {

ConnectivityManager::WiFiStationMode ConnectivityManagerImplWiFi::_GetWiFiStationMode(void)
{
    return ConnectivityManager::WiFiStationMode::kWiFiStationMode_Disabled;
}

CHIP_ERROR ConnectivityManagerImplWiFi::_SetWiFiStationMode(ConnectivityManager::WiFiStationMode val)
{
    return CHIP_NO_ERROR;
}

bool ConnectivityManagerImplWiFi::_IsWiFiStationEnabled(void)
{
    return false;
}

bool ConnectivityManagerImplWiFi::_IsWiFiStationApplicationControlled(void)
{
    return false;
}

bool ConnectivityManagerImplWiFi::_IsWiFiStationConnected(void)
{
    return false;
}

System::Clock::Timeout ConnectivityManagerImplWiFi::_GetWiFiStationReconnectInterval(void)
{
    return System::Clock::kZero;
}

CHIP_ERROR ConnectivityManagerImplWiFi::_SetWiFiStationReconnectInterval(System::Clock::Timeout val)
{
    return CHIP_NO_ERROR;
}

bool ConnectivityManagerImplWiFi::_IsWiFiStationProvisioned(void)
{
    return false;
}

void ConnectivityManagerImplWiFi::_ClearWiFiStationProvision(void) {}

ConnectivityManager::WiFiAPMode ConnectivityManagerImplWiFi::_GetWiFiAPMode(void)
{
    return ConnectivityManager::WiFiAPMode::kWiFiAPMode_NotSupported;
}

CHIP_ERROR ConnectivityManagerImplWiFi::_SetWiFiAPMode(ConnectivityManager::WiFiAPMode val)
{
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

bool ConnectivityManagerImplWiFi::_IsWiFiAPActive(void)
{
    /* AP mode is unsupported */
    return false;
}

bool ConnectivityManagerImplWiFi::_IsWiFiAPApplicationControlled(void)
{
    /* AP mode is unsupported */
    return false;
}

void ConnectivityManagerImplWiFi::_DemandStartWiFiAP(void)
{ /* AP mode is unsupported */
}

void ConnectivityManagerImplWiFi::_StopOnDemandWiFiAP(void)
{ /* AP mode is unsupported */
}

void ConnectivityManagerImplWiFi::_MaintainOnDemandWiFiAP(void)
{ /* AP mode is unsupported */
}

System::Clock::Timeout ConnectivityManagerImplWiFi::_GetWiFiAPIdleTimeout(void)
{
    /* AP mode is unsupported */
    return System::Clock::kZero;
}

void ConnectivityManagerImplWiFi::_SetWiFiAPIdleTimeout(System::Clock::Timeout val)
{ /* AP mode is unsupported */
}

CHIP_ERROR ConnectivityManagerImplWiFi::_GetAndLogWiFiStatsCounters(void)
{
    return CHIP_NO_ERROR;
}

bool ConnectivityManagerImplWiFi::_CanStartWiFiScan()
{
    return false;
}

void ConnectivityManagerImplWiFi::_OnWiFiScanDone() {}

void ConnectivityManagerImplWiFi::_OnWiFiStationProvisionChange() {}

CHIP_ERROR ConnectivityManagerImplWiFi::InitWiFi()
{
    return WiFiManager::Instance().Init();
}

void ConnectivityManagerImplWiFi::OnWiFiPlatformEvent(const ChipDeviceEvent * event)
{
    return;
}

// helpers
const char * ConnectivityManagerImplWiFi::_WiFiStationModeToStr(ConnectivityManager::WiFiStationMode mode)
{
    return nullptr;
}

const char * ConnectivityManagerImplWiFi::_WiFiStationStateToStr(ConnectivityManager::WiFiStationState state)
{
    return nullptr;
}

} // namespace DeviceLayer
} // namespace chip

#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI
