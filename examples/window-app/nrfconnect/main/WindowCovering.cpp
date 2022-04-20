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

WindowCovering::WindowCovering() :
    mLiftIndicator(LIFT_PWM_DEVICE, LIFT_PWM_CHANNEL, 0, 255), mTiltIndicator(TILT_PWM_DEVICE, TILT_PWM_CHANNEL, 0, 255)
{
    mLiftLED.Init(LIFT_STATE_LED);
    mTiltLED.Init(TILT_STATE_LED);
    mLiftIndicator.Init();
    mTiltIndicator.Init();
    // PWMManager::Instance().RegisterDevice(mLiftIndicator);
    // PWMManager::Instance().RegisterDevice(mTiltIndicator);
}

void WindowCovering::ScheduleMove(const OperationalState & aDirection)
{
    switch (mCurrentMoveType)
    {
    case MoveType::LIFT:
        mOperationalStatus.lift   = aDirection;
        mOperationalStatus.global = mOperationalStatus.lift;
        mOperationalStatus.tilt   = OperationalState::Stall;
        break;
    case MoveType::TILT:
        mOperationalStatus.tilt   = aDirection;
        mOperationalStatus.global = mOperationalStatus.tilt;
        mOperationalStatus.lift   = OperationalState::Stall;
        break;
    default:
        break;
    }
    SchedulePositionSet();
    ScheduleOperationalStatusSetWithGlobalUpdate();
}

void WindowCovering::SchedulePositionSet()
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CallbackPositionSet);
}

void WindowCovering::CallbackPositionSet(intptr_t)
{
    EmberAfStatus status{};
    chip::Percent100ths percent100ths{};
    NPercent100ths current{};

    if (Instance().mCurrentMoveType == MoveType::LIFT)
    {
        status = Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current);
    }
    else if (Instance().mCurrentMoveType == MoveType::TILT)
    {
        status = Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), current);
    }

    if ((status == EMBER_ZCL_STATUS_SUCCESS) && !current.IsNull())
    {
        static constexpr auto sPercentDelta{ WC_PERCENT100THS_MAX_CLOSED / 50 };
        percent100ths = ComputePercent100thsStep(Instance().mOperationalStatus.global, current.Value(), sPercentDelta);
    }
    else
    {
        LOG_ERR("Cannot read the current lift position. Error: %d", static_cast<uint8_t>(status));
    }

    NPercent100ths position{};
    position.SetNonNull(percent100ths);
    if (Instance().mCurrentMoveType == MoveType::LIFT)
    {
        LiftPositionSet(Endpoint(), position);
    }
    else if (Instance().mCurrentMoveType == MoveType::TILT)
    {
        TiltPositionSet(Endpoint(), position);
    }
}

void WindowCovering::ScheduleOperationalStatusSetWithGlobalUpdate()
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(CallbackOperationalStatusSetWithGlobalUpdate);
}

void WindowCovering::CallbackOperationalStatusSetWithGlobalUpdate(intptr_t)
{
    OperationalStatusSet(Endpoint(), Instance().mOperationalStatus);
}

void WindowCovering::SetOperationalStatus(const OperationalStatus & aStatus)
{
    uint8_t global = OperationalStateToValue(aStatus.global);
    uint8_t lift   = OperationalStateToValue(aStatus.lift);
    uint8_t tilt   = OperationalStateToValue(aStatus.tilt);
    uint8_t value  = (global & 0x03) | static_cast<uint8_t>((lift & 0x03) << 2) | static_cast<uint8_t>((tilt & 0x03) << 4);
    Attributes::OperationalStatus::Set(Endpoint(), value);
}

uint8_t WindowCovering::OperationalStateToValue(const OperationalState & state)
{
    switch (state)
    {
    case OperationalState::Stall:
        return 0x00;
    case OperationalState::MovingUpOrOpen:
        return 0x01;
    case OperationalState::MovingDownOrClose:
        return 0x02;
    case OperationalState::Reserved:
    default:
        return 0x03;
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
        Instance().mLiftIndicator.InitiateAction(PWMDevice::LEVEL_ACTION, 0, 0, &brightness);
    }
}

uint8_t WindowCovering::LiftToBrightness(uint16_t aLiftPosition)
{
    uint16_t installedClosedLimit;
    uint16_t installedOpenLimit;
    uint8_t result{ 0 };
    EmberAfStatus status = Attributes::InstalledOpenLimitLift::Get(Endpoint(), &installedOpenLimit);
    status               = Attributes::InstalledClosedLimitLift::Get(Endpoint(), &installedClosedLimit);

    if (EMBER_ZCL_STATUS_SUCCESS == status)
    {
        // TODO: use the actual limits
        LOG_INF("Lift open limit: %d", installedOpenLimit);
        LOG_INF("Lift close limit: %d", installedClosedLimit);
        float value = 255.0f / 65535.0f * aLiftPosition;
        LOG_INF("value: %d", (int) value);
        result = value;
    }
    return result;
}

void WindowCovering::UpdateTiltLED()
{
    chip::DeviceLayer::PlatformMgr().ScheduleWork(ScheduleTiltLEDUpdateCallback);
}

void WindowCovering::ScheduleTiltLEDUpdateCallback(intptr_t)
{
    EmberAfStatus status;
    chip::app::DataModel::Nullable<uint16_t> currentPosition;

    status = Attributes::CurrentPositionTilt::Get(Endpoint(), currentPosition);

    if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
    {
        uint8_t brightness = Instance().TiltToBrightness(currentPosition.Value());
        LOG_INF("brightness: %d", brightness);
        Instance().mTiltIndicator.InitiateAction(PWMDevice::LEVEL_ACTION, 0, 0, &brightness);
    }
}

uint8_t WindowCovering::TiltToBrightness(uint16_t aTiltPosition)
{
    uint16_t installedClosedLimit;
    uint16_t installedOpenLimit;
    uint8_t result{ 0 };
    EmberAfStatus status = Attributes::InstalledOpenLimitTilt::Get(Endpoint(), &installedOpenLimit);
    status               = Attributes::InstalledClosedLimitTilt::Get(Endpoint(), &installedClosedLimit);

    if (EMBER_ZCL_STATUS_SUCCESS == status)
    {
        // TODO: use the actual limits
        LOG_INF("Tilt open limit: %d", installedOpenLimit);
        LOG_INF("Tilt close limit: %d", installedClosedLimit);
        float value = 255.0f / 65535.0f * aTiltPosition;
        LOG_INF("value: %d", (int) value);
        result = value;
    }
    return result;
}
