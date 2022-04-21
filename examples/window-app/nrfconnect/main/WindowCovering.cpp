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

static constexpr float FromOneRangeToAnother(uint32_t aInMin, uint32_t aInMax, uint32_t aOutMin, uint32_t aOutMax, uint32_t aInput)
{
    const auto diffInput  = aInMax - aInMin;
    const auto diffOutput = aOutMax - aOutMin;
    if (diffInput > 0)
    {
        return aOutMin + ((float) diffOutput / (float) diffInput) * (aInput - aInMin);
    }
    return 0.0f;
}

WindowCovering::WindowCovering() :
    mLiftIndicator(LIFT_PWM_DEVICE, LIFT_PWM_CHANNEL, 0, 255), mTiltIndicator(TILT_PWM_DEVICE, TILT_PWM_CHANNEL, 0, 255)
{
    mLiftLED.Init(LIFT_STATE_LED);
    mTiltLED.Init(TILT_STATE_LED);
    mLiftIndicator.Init();
    mTiltIndicator.Init();
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
    chip::DeviceLayer::PlatformMgr().ScheduleWork(OperationalStatusSetWithGlobalUpdate);
}

void WindowCovering::OperationalStatusSetWithGlobalUpdate(intptr_t)
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

void WindowCovering::UpdatePositionLED(MoveType aMoveType)
{
    MoveType * moveType = chip::Platform::New<MoveType>();
    VerifyOrReturn(moveType != nullptr, emberAfWindowCoveringClusterPrint("No memory for async call data"));

    *moveType = aMoveType;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(PositionLEDUpdate, reinterpret_cast<intptr_t>(moveType));
}

void WindowCovering::PositionLEDUpdate(intptr_t aArg)
{
    MoveType * moveType = reinterpret_cast<MoveType *>(aArg);

    EmberAfStatus status;
    chip::app::DataModel::Nullable<uint16_t> currentPosition;

    if (*moveType == MoveType::LIFT)
    {
        status = Attributes::CurrentPositionLift::Get(Endpoint(), currentPosition);
        if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
        {
            Instance().SetBrightness(MoveType::LIFT, currentPosition.Value());
        }
    }
    else if (*moveType == MoveType::TILT)
    {
        status = Attributes::CurrentPositionTilt::Get(Endpoint(), currentPosition);
        if (EMBER_ZCL_STATUS_SUCCESS == status && !currentPosition.IsNull())
        {
            Instance().SetBrightness(MoveType::TILT, currentPosition.Value());
        }
    }

    chip::Platform::Delete(moveType);
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
    EmberAfStatus status{};

    if (aMoveType == MoveType::LIFT)
    {
        status = Attributes::InstalledOpenLimitLift::Get(Endpoint(), &installedOpenLimit);
        status = Attributes::InstalledClosedLimitLift::Get(Endpoint(), &installedClosedLimit);
        pwmMin = Instance().mLiftIndicator.GetMinLevel();
        pwmMax = Instance().mLiftIndicator.GetMaxLevel();
    }
    else if (aMoveType == MoveType::TILT)
    {
        status = Attributes::InstalledOpenLimitTilt::Get(Endpoint(), &installedOpenLimit);
        status = Attributes::InstalledClosedLimitTilt::Get(Endpoint(), &installedClosedLimit);
        pwmMin = Instance().mTiltIndicator.GetMinLevel();
        pwmMax = Instance().mTiltIndicator.GetMaxLevel();
    }

    if (EMBER_ZCL_STATUS_SUCCESS == status)
    {
        return FromOneRangeToAnother(installedOpenLimit, installedClosedLimit, pwmMin, pwmMax, aLiftPosition);
    }
    return 0;
}
