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

#include "WindowCovering.h"
#include "AppConfig.h"
#include "PWMManager.h"

#include <dk_buttons_and_leds.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/util/af.h>
#include <logging/log.h>
#include <platform/CHIPDeviceLayer.h>
#include <zephyr.h>

LOG_MODULE_DECLARE(app, CONFIG_MATTER_LOG_LEVEL);

using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;
using namespace chip::app::Clusters::WindowCovering;

static k_timer sActionTimer;

WindowCovering::WindowCovering()
{
    k_timer_init(&sActionTimer, &WindowCovering::TimerTimeoutCallback, nullptr);
    k_timer_user_data_set(&sActionTimer, this);

    mLiftLed.Init(LIFT_STATE_LED);
    (void)PWMMgr().Init(LIFT_PWM_DEVICE, LIFT_PWM_CHANNEL, 0, 255);
}

void WindowCovering::ScheduleLift()
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CallbackPositionSet);
}

void WindowCovering::CallbackPositionSet(intptr_t arg)
{
    EmberAfStatus status;
    chip::Percent100ths percent100ths{};
    NPercent100ths current;

    status = Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current);

    if ((status == EMBER_ZCL_STATUS_SUCCESS) && !current.IsNull())
    {
        static constexpr auto sLiftPercentDelta{ WC_PERCENT100THS_MAX_CLOSED / 100 };
        percent100ths = ComputePercent100thsStep(Instance().mLiftOpState, current.Value(), sLiftPercentDelta);
    }
    else
    {
        // TODO: rather do nothing and inform about the problem
    }

    NPercent100ths position;
    position.SetNonNull(percent100ths);

    LiftPositionSet(Endpoint(), position);
}

void WindowCovering::ScheduleOperationalStatusSetWithGlobalUpdate(OperationalStatus opStatus)
{
    // auto * data = chip::Platform::New<OperationalStatus>();
    // VerifyOrReturn(data != nullptr, emberAfWindowCoveringClusterPrint("Out of Memory for WorkData"));

    // chip::DeviceLayer::PlatformMgr().ScheduleWork(CallbackOperationalStatusSetWithGlobalUpdate,
    // reinterpret_cast<intptr_t>(data));
}

void WindowCovering::CallbackOperationalStatusSetWithGlobalUpdate(intptr_t arg)
{
    // auto * data = reinterpret_cast<OperationalStatus *>(arg);
    // OperationalStatusSetWithGlobalUpdated(Endpoint(), *data);

    // chip::Platform::Delete(data);
}

void WindowCovering::StartLifting(OperationalState aMoveDirection)
{
    mLiftOpState = aMoveDirection;
    mContinueWork = true;
    Instance().StartTimer(sTimeoutMs);
}

void WindowCovering::StopLifting()
{
    mContinueWork = false;
    CancelTimer();
}

void WindowCovering::CancelTimer()
{
    k_timer_stop(&sActionTimer);
}

void WindowCovering::StartTimer(uint32_t aTimeoutInMs)
{
    k_timer_start(&sActionTimer, K_MSEC(aTimeoutInMs), K_NO_WAIT);
}

void WindowCovering::TimerTimeoutCallback(k_timer * aTimer)
{
    Instance().ScheduleLift();

    if (Instance().mContinueWork)
    {
        Instance().StartTimer(sTimeoutMs);
    }
}

void WindowCovering::UpdateLiftLED()
{   
    chip::DeviceLayer::PlatformMgr().ScheduleWork(ScheduleLiftLEDUpdateCallback);
}

void WindowCovering::ScheduleLiftLEDUpdateCallback(intptr_t)
{
    EmberAfStatus status;
    chip::app::DataModel::Nullable<uint16_t> currentPosition;

    status = Attributes::CurrentPositionLift::Get(Endpoint(), currentPosition);

    if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
    {   
        uint8_t brightness = Instance().LiftToBrightness(currentPosition.Value());
        LOG_INF("brightness: %d", brightness);
        PWMMgr().InitiateAction(PWMManager::LEVEL_ACTION, 0, 0, &brightness);
    }
}

uint8_t WindowCovering::LiftToBrightness(uint16_t aLiftPosition)
{   
    uint16_t installedClosedLimit;
    uint16_t installedOpenLimit;
    uint8_t result{ 0 };
    EmberAfStatus status = Attributes::InstalledOpenLimitLift::Get(Endpoint(), &installedOpenLimit);
    status = Attributes::InstalledClosedLimitLift::Get(Endpoint(), &installedClosedLimit);

    if (EMBER_ZCL_STATUS_SUCCESS == status)
    {
        LOG_INF("Lift open limit: %d", installedOpenLimit);
        LOG_INF("Lift close limit: %d", installedClosedLimit);
        float value = 255.0f/65535.0f * aLiftPosition;
        LOG_INF("value: %d", (int)value);
        return value;
    }
    return result;
}
