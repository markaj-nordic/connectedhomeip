/*
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

#include "AppTask.h"
#include "AppEvent.h"
#include "AppConfig.h"
#include "LEDWidget.h"
#include "ThreadUtil.h"

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ErrorStr.h>
#include <system/SystemClock.h>

#if CONFIG_CHIP_OTA_REQUESTOR
#include "OTAUtil.h"
#endif

#include <dk_buttons_and_leds.h>
#include <logging/log.h>
#include <zephyr.h>

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

#define FACTORY_RESET_TRIGGER_TIMEOUT 3000
#define FACTORY_RESET_CANCEL_WINDOW_TIMEOUT 3000
#define APP_EVENT_QUEUE_SIZE 10
#define BUTTON_PUSH_EVENT 1
#define BUTTON_RELEASE_EVENT 0

namespace {
constexpr EndpointId kWindowShutterEndpointId = 1;

LOG_MODULE_DECLARE(app, CONFIG_MATTER_LOG_LEVEL);
K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), APP_EVENT_QUEUE_SIZE, alignof(AppEvent));

bool sIsThreadProvisioned = false;
bool sIsThreadEnabled     = false;
bool sHaveBLEConnections  = false;

} // namespace

CHIP_ERROR AppTask::Init()
{
    // Initialize CHIP stack
    LOG_INF("Init CHIP stack");

    CHIP_ERROR err = chip::Platform::MemoryInit();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("Platform::MemoryInit() failed");
        return err;
    }

    err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("PlatformMgr().InitChipStack() failed");
        return err;
    }

    err = ThreadStackMgr().InitThreadStack();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("ThreadStackMgr().InitThreadStack() failed");
        return err;
    }

#ifdef CONFIG_OPENTHREAD_MTD_SED
    err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_SleepyEndDevice);
#elif CONFIG_OPENTHREAD_MTD
    err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#else
    err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_FullEndDevice);
#endif
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("ConnectivityMgr().SetThreadDeviceType() failed");
        return err;
    }

    // Initialize LEDs
    // TODO

    // Initialize buttons
    int ret = dk_buttons_init(ButtonEventHandler);
    if (ret)
    {
        LOG_ERR("dk_buttons_init() failed");
        return chip::System::MapErrorZephyr(ret);
    }

#ifdef CONFIG_MCUMGR_SMP_BT
    // Initialize DFU over SMP
    GetDFUOverSMP().Init(RequestSMPAdvertisingStart);
    GetDFUOverSMP().ConfirmNewImage();
#endif

    // Initialize CHIP server
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
    static chip::CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();

    ReturnErrorOnFailure(chip::Server::GetInstance().Init(initParams));
#if CONFIG_CHIP_OTA_REQUESTOR
    InitBasicOTARequestor();
#endif
    ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    // Add CHIP event handler and start CHIP thread.
    // Note that all the initialization code should happen prior to this point to avoid data races
    // between the main and the CHIP threads.
    PlatformMgr().AddEventHandler(ChipEventHandler, 0);

    err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("PlatformMgr().StartEventLoopTask() failed");
    }

    return err;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());

    AppEvent event{};

    while (true)
    {
        k_msgq_get(&sAppEventQueue, &event, K_FOREVER);
        DispatchEvent(&event);
    }

    return CHIP_NO_ERROR;
}

void AppTask::ButtonEventHandler(uint32_t button_state, uint32_t has_changed)
{
    AppEvent button_event;
    button_event.Type = AppEvent::Type::Button;

    if (BLE_ADVERTISEMENT_START_BUTTON_MASK & button_state & has_changed)
    {
        button_event.ButtonEvent.PinNo  = BLE_ADVERTISEMENT_START_BUTTON;
        button_event.ButtonEvent.Action = BUTTON_PUSH_EVENT;
        button_event.Handler            = StartBLEAdvertisementHandler;
    }

    if (FACTORY_RESET_BUTTON_MASK & button_state & has_changed)
    {
        button_event.ButtonEvent.PinNo  = FACTORY_RESET_BUTTON;
        button_event.ButtonEvent.Action = BUTTON_PUSH_EVENT;
        button_event.Handler            = FactoryResetTimerEventHandler;
    }

    Instance().PostEvent(&button_event);
}

void AppTask::TimerEventHandler(k_timer * timer)
{
    AppEvent event;
    event.Type               = AppEvent::Type::Timer;
    event.TimerEvent.Context = k_timer_user_data_get(timer);
    event.Handler            = FactoryResetTimerEventHandler;
    Instance().PostEvent(&event);
}

void AppTask::FactoryResetTimerEventHandler(AppEvent * aEvent)
{
    if (aEvent->ButtonEvent.PinNo != FACTORY_RESET_BUTTON)
        return;

    if (true /* TODO */)
    {
        chip::Server::GetInstance().ScheduleFactoryReset();
    }
}

#ifdef CONFIG_MCUMGR_SMP_BT
void AppTask::RequestSMPAdvertisingStart(void)
{

}
#endif

void AppTask::StartBLEAdvertisementHandler(AppEvent * aEvent)
{
    if (aEvent->ButtonEvent.PinNo != BLE_ADVERTISEMENT_START_BUTTON)
        return;

    if (Server::GetInstance().GetFabricTable().FabricCount() != 0)
    {
        LOG_INF("Matter service BLE advertising not started - device is already commissioned");
        return;
    }

    if (ConnectivityMgr().IsBLEAdvertisingEnabled())
    {
        LOG_INF("BLE advertising is already enabled");
        return;
    }

    if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow() != CHIP_NO_ERROR)
    {
        LOG_ERR("OpenBasicCommissioningWindow() failed");
    }
}

void AppTask::ChipEventHandler(const ChipDeviceEvent * event, intptr_t /* arg */)
{
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
#ifdef CONFIG_CHIP_NFC_COMMISSIONING
        if (event->CHIPoBLEAdvertisingChange.Result == kActivity_Started)
        {
            if (NFCMgr().IsTagEmulationStarted())
            {
                LOG_INF("NFC Tag emulation is already started");
            }
            else
            {
                ShareQRCodeOverNFC(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
            }
        }
        else if (event->CHIPoBLEAdvertisingChange.Result == kActivity_Stopped)
        {
            NFCMgr().StopTagEmulation();
        }
#endif
        sHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
        break;
    case DeviceEventType::kThreadStateChange:
        sIsThreadProvisioned = ConnectivityMgr().IsThreadProvisioned();
        sIsThreadEnabled     = ConnectivityMgr().IsThreadEnabled();
        break;
    default:
        break;
    }
}

void AppTask::CancelTimer()
{

}

void AppTask::StartTimer(uint32_t aTimeoutInMs)
{

}

void AppTask::PostEvent(AppEvent * aEvent)
{
    if (k_msgq_put(&sAppEventQueue, aEvent, K_NO_WAIT))
    {
        LOG_INF("Failed to post event to app task event queue");
    }
}

void AppTask::DispatchEvent(AppEvent * aEvent)
{
    if (aEvent->Handler)
    {
        aEvent->Handler(aEvent);
    }
    else
    {
        LOG_INF("Event received with no handler. Dropping event.");
    }
}
