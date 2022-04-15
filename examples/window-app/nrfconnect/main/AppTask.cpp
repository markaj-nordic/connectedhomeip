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
#include "AppConfig.h"
#include "AppEvent.h"
#include "LEDUtil.h"
#include "WindowCovering.h"

#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>

#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/util/attribute-storage.h>

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>

#if CONFIG_CHIP_OTA_REQUESTOR
#include "OTAUtil.h"
#endif

#include <dk_buttons_and_leds.h>
#include <logging/log.h>
#include <zephyr.h>

#define FACTORY_RESET_TRIGGER_TIMEOUT 3000
#define FACTORY_RESET_CANCEL_WINDOW_TIMEOUT 3000
#define MOVEMENT_START_WINDOW_TIMEOUT 2000
#define APP_EVENT_QUEUE_SIZE 10
#define BUTTON_PUSH_EVENT 1
#define BUTTON_RELEASE_EVENT 0

LOG_MODULE_DECLARE(app, CONFIG_MATTER_LOG_LEVEL);
K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), APP_EVENT_QUEUE_SIZE, alignof(AppEvent));

static LEDWidget sStatusLED;
static UnusedLedsWrapper<3> sUnusedLeds{ { DK_LED2, DK_LED3, DK_LED4 } };
static k_timer sFunctionTimer;
static k_timer sMovementTimer;

namespace LedConsts {
constexpr uint32_t kBlinkRate_ms{ 500 };
namespace StatusLed {
namespace Unprovisioned {
constexpr uint32_t kOn_ms{ 100 };
constexpr uint32_t kOff_ms{ kOn_ms };
} // namespace Unprovisioned
namespace Provisioned {
constexpr uint32_t kOn_ms{ 50 };
constexpr uint32_t kOff_ms{ 950 };
} // namespace Provisioned

} // namespace StatusLed
} // namespace LedConsts

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

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
#else
    err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#endif
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("ConnectivityMgr().SetThreadDeviceType() failed");
        return err;
    }

    // Initialize LEDs
    LEDWidget::InitGpio();
    LEDWidget::SetStateUpdateCallback(LEDStateUpdateHandler);

    sStatusLED.Init(SYSTEM_STATE_LED);

    UpdateStatusLED();

    // Initialize buttons
    auto ret = dk_buttons_init(ButtonEventHandler);
    if (ret)
    {
        LOG_ERR("dk_buttons_init() failed");
        return chip::System::MapErrorZephyr(ret);
    }

    // Initialize function timer user data
    k_timer_init(&sFunctionTimer, &AppTask::FunctionTimerTimeoutCallback, nullptr);
    k_timer_user_data_set(&sFunctionTimer, this);

    // Initialize movement timer user data
    k_timer_init(&sMovementTimer, &AppTask::MovementTimerTimeoutCallback, nullptr);
    k_timer_user_data_set(&sMovementTimer, this);

    // Initialize CHIP server
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    static chip::CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    ReturnErrorOnFailure(chip::Server::GetInstance().Init(initParams));
#if CONFIG_CHIP_OTA_REQUESTOR
    InitBasicOTARequestor();
#endif
    ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(chip::RendezvousInformationFlag(chip::RendezvousInformationFlag::kBLE));

    // Add CHIP event handler and start CHIP thread.
    // Note that all the initialization code should happen prior to this point to avoid data races
    // between the main and the CHIP threads
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

void AppTask::ButtonEventHandler(uint32_t aButtonState, uint32_t aHasChanged)
{
    AppEvent event;
    event.Type = AppEvent::Type::Button;

    if (FUNCTION_BUTTON_MASK & aHasChanged)
    {
        event.ButtonEvent.PinNo  = FUNCTION_BUTTON;
        event.ButtonEvent.Action = (FUNCTION_BUTTON_MASK & aButtonState) ? BUTTON_PUSH_EVENT : BUTTON_RELEASE_EVENT;
        event.Handler            = FunctionHandler;
        PostEvent(&event);
    }

    if (BLE_ADVERTISEMENT_START_BUTTON_MASK & aButtonState & aHasChanged)
    {
        event.ButtonEvent.PinNo  = BLE_ADVERTISEMENT_START_BUTTON;
        event.ButtonEvent.Action = BUTTON_PUSH_EVENT;
        event.Handler            = StartBLEAdvertisementHandler;
        PostEvent(&event);
    }

    if (LIFT_BUTTON_MASK & aHasChanged)
    {
        event.ButtonEvent.PinNo  = LIFT_BUTTON;
        event.ButtonEvent.Action = (LIFT_BUTTON_MASK & aButtonState) ? BUTTON_PUSH_EVENT : BUTTON_RELEASE_EVENT;
        event.Handler            = LiftHandler;
        PostEvent(&event);
    }
}

void AppTask::FunctionTimerTimeoutCallback(k_timer * aTimer)
{
    if (!aTimer)
        return;

    AppEvent event;
    event.Type               = AppEvent::Type::Timer;
    event.TimerEvent.Context = k_timer_user_data_get(aTimer);
    event.Handler            = FunctionTimerEventHandler;
    PostEvent(&event);
}

void AppTask::MovementTimerTimeoutCallback(k_timer * aTimer)
{
    if (!aTimer)
        return;

    AppEvent event;
    event.Type               = AppEvent::Type::Timer;
    event.TimerEvent.Context = k_timer_user_data_get(aTimer);
    event.Handler            = MovementTimerEventHandler;
    PostEvent(&event);
}

void AppTask::FunctionTimerEventHandler(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->Type != AppEvent::Type::Timer)
        return;

    // If we reached here, the button was held past FACTORY_RESET_TRIGGER_TIMEOUT, initiate factory reset
    if (Instance().mFunctionTimerActive && Instance().mMode == OperatingMode::Normal)
    {
        LOG_INF("Factory Reset Triggered. Release button within %ums to cancel.", FACTORY_RESET_TRIGGER_TIMEOUT);

        // Start timer for FACTORY_RESET_CANCEL_WINDOW_TIMEOUT to allow user to cancel, if required.
        StartTimer(TimerType::Function, FACTORY_RESET_CANCEL_WINDOW_TIMEOUT);
        Instance().mMode = OperatingMode::FactoryReset;

#ifdef CONFIG_STATE_LEDS
        // Turn off all LEDs before starting blink to make sure blink is co-ordinated.
        sStatusLED.Set(false);
        sUnusedLeds.Set(false);

        sStatusLED.Blink(LedConsts::kBlinkRate_ms);
        sUnusedLeds.Blink(LedConsts::kBlinkRate_ms);
#endif
    }
    else if (Instance().mFunctionTimerActive && Instance().mMode == OperatingMode::FactoryReset)
    {
        // Actually trigger Factory Reset
        Instance().mMode = OperatingMode::Normal;
        ConfigurationMgr().InitiateFactoryReset();
    }
}

void AppTask::MovementTimerEventHandler(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->Type != AppEvent::Type::Timer || Instance().mMode != OperatingMode::Normal)
        return;

    if (Instance().mMovementTimerActive)
    {
        CancelTimer(TimerType::Movement);
        LOG_INF("Starting lifting in timer timeout handler");
        WindowCovering::Instance().StartLifting(Instance().mMoveType);
    }
}

void AppTask::FunctionHandler(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->ButtonEvent.PinNo != FUNCTION_BUTTON)
        return;

    // To initiate factory reset: press the FUNCTION_BUTTON for FACTORY_RESET_TRIGGER_TIMEOUT + FACTORY_RESET_CANCEL_WINDOW_TIMEOUT
    // All LEDs start blinking after FACTORY_RESET_TRIGGER_TIMEOUT to signal factory reset has been initiated.
    // To cancel factory reset: release the FUNCTION_BUTTON once all LEDs start blinking within the
    // FACTORY_RESET_CANCEL_WINDOW_TIMEOUT
    if (aEvent->ButtonEvent.Action == BUTTON_PUSH_EVENT)
    {
        if (!Instance().mFunctionTimerActive && Instance().mMode == OperatingMode::Normal)
        {
            StartTimer(TimerType::Function, FACTORY_RESET_TRIGGER_TIMEOUT);
        }
    }
    else
    {
        if (Instance().mFunctionTimerActive && Instance().mMode == OperatingMode::FactoryReset)
        {
            sUnusedLeds.Set(false);

            UpdateStatusLED();
            CancelTimer(TimerType::Function);

            // Change the function to none selected since factory reset has been canceled.
            Instance().mMode = OperatingMode::Normal;

            LOG_INF("Factory Reset has been Canceled");
        }
        else if (Instance().mFunctionTimerActive)
        {
            CancelTimer(TimerType::Function);
            Instance().mMode = OperatingMode::Normal;
        }
    }
}

void AppTask::StartBLEAdvertisementHandler(AppEvent *)
{
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

void AppTask::LiftHandler(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->ButtonEvent.PinNo != LIFT_BUTTON || Instance().mMode != OperatingMode::Normal)
        return;

    if (aEvent->ButtonEvent.Action == BUTTON_PUSH_EVENT)
    {
        if (!Instance().mMovementTimerActive)
        {
            LOG_INF("Starting movement timer");
            StartTimer(TimerType::Movement, MOVEMENT_START_WINDOW_TIMEOUT);
        }
    }
    else if (aEvent->ButtonEvent.Action == BUTTON_RELEASE_EVENT)
    {
        if (Instance().mMovementTimerActive)
        {
            LOG_INF("Stopping movement timer, toggling the direction");
            CancelTimer(TimerType::Movement);
            Instance().ToggleLiftMoveDirection();
        }
        // release happened more than 2 seconds after pressing the button,
        // we assume that the flag was cleared in MovementTimerEventHandler
        else if (!Instance().mMovementTimerActive)
        {
            LOG_INF("Stopping movement");
            WindowCovering::Instance().StopLifting();
        }
    }
}

void AppTask::ToggleLiftMoveDirection()
{
    if (Instance().mMoveType == OperationalState::MovingUpOrOpen)
    {
        Instance().mMoveType = OperationalState::MovingDownOrClose;
    }
    else
    {
        Instance().mMoveType = OperationalState::MovingUpOrOpen;
    }
}

void AppTask::UpdateLedStateEventHandler(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->Type == AppEvent::Type::UpdateLedState)
    {
        aEvent->UpdateLedStateEvent.LedWidget->UpdateState();
    }
}

void AppTask::LEDStateUpdateHandler(LEDWidget & aLedWidget)
{
    AppEvent event;
    event.Type                          = AppEvent::Type::UpdateLedState;
    event.Handler                       = UpdateLedStateEventHandler;
    event.UpdateLedStateEvent.LedWidget = &aLedWidget;
    PostEvent(&event);
}

void AppTask::UpdateStatusLED()
{
#ifdef CONFIG_STATE_LEDS
    /* Update the status LED.
     *
     * If thread and service provisioned, keep the LED On constantly.
     *
     * If the system has ble connection(s) uptill the stage above, THEN blink the LED at an even
     * rate of 100ms.
     *
     * Otherwise, blink the LED On for a very short time. */
    if (Instance().mIsThreadProvisioned && Instance().mIsThreadEnabled)
    {
        sStatusLED.Set(true);
    }
    else if (Instance().mHaveBLEConnections)
    {
        sStatusLED.Blink(LedConsts::StatusLed::Unprovisioned::kOn_ms, LedConsts::StatusLed::Unprovisioned::kOff_ms);
    }
    else
    {
        sStatusLED.Blink(LedConsts::StatusLed::Provisioned::kOn_ms, LedConsts::StatusLed::Provisioned::kOff_ms);
    }
#endif
}

void AppTask::ChipEventHandler(const ChipDeviceEvent * aEvent, intptr_t /* aArg */)
{
    if (!aEvent)
        return;
    switch (aEvent->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        Instance().mHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
        UpdateStatusLED();
        break;
    case DeviceEventType::kThreadStateChange:
        Instance().mIsThreadProvisioned = ConnectivityMgr().IsThreadProvisioned();
        Instance().mIsThreadEnabled     = ConnectivityMgr().IsThreadEnabled();
        UpdateStatusLED();
        break;
    default:
        break;
    }
}

void AppTask::CancelTimer(TimerType aTimerType)
{
    if (aTimerType == TimerType::Function)
    {
        k_timer_stop(&sFunctionTimer);
        Instance().mFunctionTimerActive = false;
    }
    else if (aTimerType == TimerType::Movement)
    {
        k_timer_stop(&sMovementTimer);
        Instance().mMovementTimerActive = false;
    }
}

void AppTask::StartTimer(TimerType aTimerType, uint32_t aTimeoutInMs)
{
    if (aTimerType == TimerType::Function)
    {
        k_timer_start(&sFunctionTimer, K_MSEC(aTimeoutInMs), K_NO_WAIT);
        Instance().mFunctionTimerActive = true;
    }
    else if (aTimerType == TimerType::Movement)
    {
        k_timer_start(&sMovementTimer, K_MSEC(aTimeoutInMs), K_NO_WAIT);
        Instance().mMovementTimerActive = true;
    }
}

void AppTask::PostEvent(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (k_msgq_put(&sAppEventQueue, aEvent, K_NO_WAIT))
    {
        LOG_INF("Failed to post event to app task event queue");
    }
}

void AppTask::DispatchEvent(AppEvent * aEvent)
{
    if (!aEvent)
        return;
    if (aEvent->Handler)
    {
        aEvent->Handler(aEvent);
    }
    else
    {
        LOG_INF("Event received with no handler. Dropping event.");
    }
}
