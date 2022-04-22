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
#include "PWMDevice.h"

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

static k_timer sMoveTimer;
static constexpr uint32_t sMoveTimeoutMs{ 200 };

static constexpr uint32_t FromOneRangeToAnother(uint32_t aInMin, uint32_t aInMax, uint32_t aOutMin, uint32_t aOutMax,
                                                uint32_t aInput)
{
    const auto diffInput  = aInMax - aInMin;
    const auto diffOutput = aOutMax - aOutMin;
    if (diffInput > 0)
    {
        return static_cast<uint32_t>(aOutMin + static_cast<uint64_t>(aInput - aInMin) * diffOutput / diffInput);
    }
    return 0;
}

WindowCovering::WindowCovering()
{
    mLiftLED.Init(LIFT_STATE_LED);
    mTiltLED.Init(TILT_STATE_LED);

    if (mLiftIndicator.Init(LIFT_PWM_DEVICE, LIFT_PWM_CHANNEL, 0, 255) != 0)
    {
        LOG_ERR("Cannot initialize the lift indicator");
    }
    if (mTiltIndicator.Init(TILT_PWM_DEVICE, TILT_PWM_CHANNEL, 0, 255) != 0)
    {
        LOG_ERR("Cannot initialize the tilt indicator");
    }

    k_timer_init(&sMoveTimer, MoveTimerTimeoutCallback, nullptr);
}

void WindowCovering::MoveTimerTimeoutCallback(k_timer * aTimer)
{
    if (!aTimer)
        return;

    chip::DeviceLayer::PlatformMgr().ScheduleWork(DriveCurrentPosition);
}

void WindowCovering::DriveCurrentPosition(intptr_t)
{
    NPercent100ths position{};
    position.SetNonNull(CalculateSingleStep());

    if (Instance().mCurrentMoveType == MoveType::LIFT)
    {
        LiftPositionSet(Endpoint(), position);
    }
    else if (Instance().mCurrentMoveType == MoveType::TILT)
    {
        TiltPositionSet(Endpoint(), position);
    }

    // assume single move completed
    Instance().mInMove = false;

    if (!TargetCompleted())
    {
        // continue to move
        Instance().StartTimer(sMoveTimeoutMs);
    }
}

chip::Percent100ths WindowCovering::CalculateSingleStep()
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
        static constexpr auto sPercentDelta{ WC_PERCENT100THS_MAX_CLOSED / 25 };
        percent100ths = ComputePercent100thsStep(OperationalStatusGet(Endpoint()).global, current.Value(), sPercentDelta);
    }
    else
    {
        LOG_ERR("Cannot read the current lift position. Error: %d", static_cast<uint8_t>(status));
    }

    return percent100ths;
}

bool WindowCovering::TargetCompleted()
{
    NPercent100ths current{};
    NPercent100ths target{};

    if (Instance().mCurrentMoveType == MoveType::LIFT)
    {
        VerifyOrReturnError(Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS,
                            false);
        VerifyOrReturnError(Attributes::TargetPositionLiftPercent100ths::Get(Endpoint(), target) == EMBER_ZCL_STATUS_SUCCESS,
                            false);
    }
    else if (Instance().mCurrentMoveType == MoveType::TILT)
    {
        VerifyOrReturnError(Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS,
                            false);
        VerifyOrReturnError(Attributes::TargetPositionTiltPercent100ths::Get(Endpoint(), target) == EMBER_ZCL_STATUS_SUCCESS,
                            false);
    }

    if (!current.IsNull())
    {
        return (current == target);
    }
    else
    {
        LOG_ERR("Cannot read the shutter position");
    }
    return false;
}

void WindowCovering::StartTimer(uint32_t aTimeoutMs)
{
    k_timer_start(&sMoveTimer, K_MSEC(sMoveTimeoutMs), K_NO_WAIT);
}

void WindowCovering::StartMove(MoveType aMoveType)
{
    // drop if already in move
    if (!mInMove)
    {
        mCurrentMoveType = aMoveType;
        mInMove          = true;
        StartTimer(sMoveTimeoutMs);
    }
}

void WindowCovering::SetSingleStepTarget(OperationalState aDirection)
{
    UpdateOperationalStatus(mCurrentMoveType, aDirection);
    SetTargetPosition(aDirection, CalculateSingleStep());
}

void WindowCovering::UpdateOperationalStatus(MoveType aMoveType, OperationalState aDirection)
{
    switch (aMoveType)
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
    OperationalStatusSetWithGlobalUpdate();
}

void WindowCovering::OperationalStatusSetWithGlobalUpdate()
{
    OperationalStatusSet(Endpoint(), Instance().mOperationalStatus);
}

void WindowCovering::SetTargetPosition(OperationalState aDirection, chip::Percent100ths aPosition)
{
    EmberAfStatus status{};
    if (Instance().mCurrentMoveType == MoveType::LIFT)
    {
        status = Attributes::TargetPositionLiftPercent100ths::Set(Endpoint(), aPosition);
    }
    else if (Instance().mCurrentMoveType == MoveType::TILT)
    {
        status = Attributes::TargetPositionTiltPercent100ths::Set(Endpoint(), aPosition);
    }

    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        LOG_ERR("Cannot set the target position. Error: %d", static_cast<uint8_t>(status));
    }
}

void WindowCovering::SetMoveType(MoveType aMoveType)
{
    mCurrentMoveType = aMoveType;
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

void WindowCovering::PositionLEDUpdate(MoveType aMoveType)
{
    EmberAfStatus status;
    chip::app::DataModel::Nullable<uint16_t> currentPosition;

    if (aMoveType == MoveType::LIFT)
    {
        status = Attributes::CurrentPositionLift::Get(Endpoint(), currentPosition);
        if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
        {
            Instance().SetBrightness(MoveType::LIFT, currentPosition.Value());
        }
    }
    else if (aMoveType == MoveType::TILT)
    {
        status = Attributes::CurrentPositionTilt::Get(Endpoint(), currentPosition);
        if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
        {
            Instance().SetBrightness(MoveType::TILT, currentPosition.Value());
        }
    }
}

void WindowCovering::SetBrightness(MoveType aMoveType, uint16_t aPosition)
{
    uint8_t brightness = PositionToBrightness(aPosition, aMoveType);
    if (aMoveType == MoveType::LIFT)
    {
        mLiftIndicator.InitiateAction(PWMDevice::LEVEL_ACTION, 0, &brightness);
    }
    else if (aMoveType == MoveType::TILT)
    {
        mTiltIndicator.InitiateAction(PWMDevice::LEVEL_ACTION, 0, &brightness);
    }
}

uint8_t WindowCovering::PositionToBrightness(uint16_t aLiftPosition, MoveType aMoveType)
{
    uint16_t installedClosedLimit{};
    uint16_t installedOpenLimit{};
    uint8_t pwmMin{};
    uint8_t pwmMax{};

    if (aMoveType == MoveType::LIFT)
    {
        VerifyOrReturnError(Attributes::InstalledOpenLimitLift::Get(Endpoint(), &installedOpenLimit) == EMBER_ZCL_STATUS_SUCCESS,
                            0);
        VerifyOrReturnError(
            Attributes::InstalledClosedLimitLift::Get(Endpoint(), &installedClosedLimit) == EMBER_ZCL_STATUS_SUCCESS, 0);
        pwmMin = Instance().mLiftIndicator.GetMinLevel();
        pwmMax = Instance().mLiftIndicator.GetMaxLevel();
    }
    else if (aMoveType == MoveType::TILT)
    {
        VerifyOrReturnError(Attributes::InstalledOpenLimitTilt::Get(Endpoint(), &installedOpenLimit) == EMBER_ZCL_STATUS_SUCCESS,
                            0);
        VerifyOrReturnError(
            Attributes::InstalledClosedLimitTilt::Get(Endpoint(), &installedClosedLimit) == EMBER_ZCL_STATUS_SUCCESS, 0);
        pwmMin = Instance().mTiltIndicator.GetMinLevel();
        pwmMax = Instance().mTiltIndicator.GetMaxLevel();
    }

    return FromOneRangeToAnother(installedOpenLimit, installedClosedLimit, pwmMin, pwmMax, aLiftPosition);
}
