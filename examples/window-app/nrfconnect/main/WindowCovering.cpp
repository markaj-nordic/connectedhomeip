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

static k_timer sLiftTimer;
static k_timer sTiltTimer;
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

    k_timer_init(&sLiftTimer, MoveTimerTimeoutCallback, nullptr);
    k_timer_init(&sTiltTimer, MoveTimerTimeoutCallback, nullptr);
}

void WindowCovering::MoveTimerTimeoutCallback(k_timer * aTimer)
{
    if (!aTimer)
        return;

    if (aTimer == &sLiftTimer)
    {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(DriveCurrentLiftPosition);
    }
    else if (aTimer == &sTiltTimer)
    {
        chip::DeviceLayer::PlatformMgr().ScheduleWork(DriveCurrentTiltPosition);
    }
}

void WindowCovering::DriveCurrentLiftPosition(intptr_t)
{
    NPercent100ths current{};
    NPercent100ths target{};
    NPercent100ths positionToSet{};

    VerifyOrReturn(Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS);
    VerifyOrReturn(Attributes::TargetPositionLiftPercent100ths::Get(Endpoint(), target) == EMBER_ZCL_STATUS_SUCCESS);

    UpdateOperationalStatus(MoveType::LIFT, ComputeOperationalState(target, current));

    positionToSet.SetNonNull(CalculateSingleStep(MoveType::LIFT));
    LiftPositionSet(Endpoint(), positionToSet);

    // assume single move completed
    Instance().mInLiftMove = false;

    VerifyOrReturn(Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS);

    if (!TargetCompleted(MoveType::LIFT, current, target))
    {
        // continue to move
        StartTimer(MoveType::LIFT, sMoveTimeoutMs);
    }
    else
    {
        // the OperationalStatus should indicate no-lift-movement after the target is completed
        UpdateOperationalStatus(MoveType::LIFT, ComputeOperationalState(target, current));
    }
}

chip::Percent100ths WindowCovering::CalculateSingleStep(MoveType aMoveType)
{
    EmberAfStatus status{};
    chip::Percent100ths percent100ths{};
    NPercent100ths current{};
    OperationalState opState{};

    OperationalStatus opStatus = OperationalStatusGet(Endpoint());

    if (aMoveType == MoveType::LIFT)
    {
        status  = Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), current);
        opState = opStatus.lift;
    }
    else if (aMoveType == MoveType::TILT)
    {
        status  = Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), current);
        opState = opStatus.tilt;
    }

    if ((status == EMBER_ZCL_STATUS_SUCCESS) && !current.IsNull())
    {
        static constexpr auto sPercentDelta{ WC_PERCENT100THS_MAX_CLOSED / 20 };
        percent100ths = ComputePercent100thsStep(opState, current.Value(), sPercentDelta);
    }
    else
    {
        LOG_ERR("Cannot read the current lift position. Error: %d", static_cast<uint8_t>(status));
    }

    return percent100ths;
}

bool WindowCovering::TargetCompleted(MoveType aMoveType, NPercent100ths aCurrent, NPercent100ths aTarget)
{
    OperationalStatus currentOpStatus = OperationalStatusGet(Endpoint());
    OperationalState currentOpState   = (aMoveType == MoveType::LIFT) ? currentOpStatus.lift : currentOpStatus.tilt;

    if (!aCurrent.IsNull() && !aTarget.IsNull())
    {
        switch (currentOpState)
        {
        case OperationalState::MovingDownOrClose:
            return (aCurrent.Value() >= aTarget.Value());
        case OperationalState::MovingUpOrOpen:
            return (aCurrent.Value() <= aTarget.Value());
        default:
            return true;
        }
    }
    else
    {
        LOG_ERR("Invalid target/current positions");
    }
    return false;
}

void WindowCovering::StartTimer(MoveType aMoveType, uint32_t aTimeoutMs)
{
    if (aMoveType == MoveType::LIFT)
    {
        k_timer_start(&sLiftTimer, K_MSEC(sMoveTimeoutMs), K_NO_WAIT);
    }
    else if (aMoveType == MoveType::TILT)
    {
        k_timer_start(&sTiltTimer, K_MSEC(sMoveTimeoutMs), K_NO_WAIT);
    }
}

void WindowCovering::DriveCurrentTiltPosition(intptr_t)
{
    NPercent100ths current{};
    NPercent100ths target{};
    NPercent100ths positionToSet{};

    VerifyOrReturn(Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS);
    VerifyOrReturn(Attributes::TargetPositionTiltPercent100ths::Get(Endpoint(), target) == EMBER_ZCL_STATUS_SUCCESS);

    UpdateOperationalStatus(MoveType::TILT, ComputeOperationalState(target, current));

    positionToSet.SetNonNull(CalculateSingleStep(MoveType::TILT));
    TiltPositionSet(Endpoint(), positionToSet);

    // assume single move completed
    Instance().mInTiltMove = false;

    VerifyOrReturn(Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), current) == EMBER_ZCL_STATUS_SUCCESS);

    if (!TargetCompleted(MoveType::TILT, current, target))
    {
        // continue to move
        StartTimer(MoveType::TILT, sMoveTimeoutMs);
    }
    else
    {
        // the OperationalStatus should indicate no-tilt-movement after the target is completed
        UpdateOperationalStatus(MoveType::TILT, ComputeOperationalState(target, current));
    }
}

void WindowCovering::StartMove(MoveType aMoveType)
{
    switch (aMoveType)
    {
    case MoveType::LIFT:
        if (!mInLiftMove)
        {
            mInLiftMove = true;
            StartTimer(aMoveType, sMoveTimeoutMs);
        }
        break;
    case MoveType::TILT:
        if (!mInTiltMove)
        {
            mInTiltMove = true;
            StartTimer(aMoveType, sMoveTimeoutMs);
        }
        break;
    default:
        break;
    };
}

void WindowCovering::SetSingleStepTarget(OperationalState aDirection)
{
    UpdateOperationalStatus(mCurrentUIMoveType, aDirection);
    SetTargetPosition(aDirection, CalculateSingleStep(mCurrentUIMoveType));
}

void WindowCovering::UpdateOperationalStatus(MoveType aMoveType, OperationalState aDirection)
{
    OperationalStatus currentOpStatus = OperationalStatusGet(Endpoint());

    switch (aMoveType)
    {
    case MoveType::LIFT:
        currentOpStatus.lift = aDirection;
        break;
    case MoveType::TILT:
        currentOpStatus.tilt = aDirection;
        break;
    case MoveType::NONE:
        break;
    default:
        break;
    }

    OperationalStatusSetWithGlobalUpdated(Endpoint(), currentOpStatus);
}

void WindowCovering::SetTargetPosition(OperationalState aDirection, chip::Percent100ths aPosition)
{
    EmberAfStatus status{};
    if (Instance().mCurrentUIMoveType == MoveType::LIFT)
    {
        status = Attributes::TargetPositionLiftPercent100ths::Set(Endpoint(), aPosition);
    }
    else if (Instance().mCurrentUIMoveType == MoveType::TILT)
    {
        status = Attributes::TargetPositionTiltPercent100ths::Set(Endpoint(), aPosition);
    }

    if (status != EMBER_ZCL_STATUS_SUCCESS)
    {
        LOG_ERR("Cannot set the target position. Error: %d", static_cast<uint8_t>(status));
    }
}

void WindowCovering::PositionLEDUpdate(MoveType aMoveType)
{
    EmberAfStatus status{};
    NPercent100ths currentPosition{};

    if (aMoveType == MoveType::LIFT)
    {
        status = Attributes::CurrentPositionLiftPercent100ths::Get(Endpoint(), currentPosition);
        if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
        {
            Instance().SetBrightness(MoveType::LIFT, currentPosition.Value());
        }
    }
    else if (aMoveType == MoveType::TILT)
    {
        status = Attributes::CurrentPositionTiltPercent100ths::Get(Endpoint(), currentPosition);
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

uint8_t WindowCovering::PositionToBrightness(uint16_t aPosition, MoveType aMoveType)
{
    static constexpr uint32_t sPercent100thsMin{ 0 };
    static constexpr uint32_t sPercent100thsMax{ 10000 };

    uint8_t pwmMin{};
    uint8_t pwmMax{};

    if (aMoveType == MoveType::LIFT)
    {
        pwmMin = mLiftIndicator.GetMinLevel();
        pwmMax = mLiftIndicator.GetMaxLevel();
    }
    else if (aMoveType == MoveType::TILT)
    {
        pwmMin = mTiltIndicator.GetMinLevel();
        pwmMax = mTiltIndicator.GetMaxLevel();
    }

    return FromOneRangeToAnother(sPercent100thsMin, sPercent100thsMax, pwmMin, pwmMax, aPosition);
}
