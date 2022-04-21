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

#pragma once

#include "LEDWidget.h"
#include "PWMDevice.h"

#include <app/clusters/window-covering-server/window-covering-server.h>

#include <cstdint>

using namespace chip::app::Clusters::WindowCovering;

class WindowCovering
{
public:
    enum class MoveType : uint8_t
    {
        LIFT = 0,
        TILT,
        NONE
    };

    WindowCovering();
    static WindowCovering & Instance()
    {
        static WindowCovering sInstance;
        return sInstance;
    }

    void ScheduleMove(const OperationalState & aDirection);
    void SetMoveType(MoveType aMoveType) { mCurrentMoveType = aMoveType; }
    MoveType GetMoveType() { return mCurrentMoveType; }
    void UpdatePositionLED(MoveType aMoveType);

    static constexpr chip::EndpointId Endpoint() { return 1; };

private:
    void SchedulePositionSet();
    void ScheduleOperationalStatusSetWithGlobalUpdate();
    static uint8_t PositionToBrightness(uint16_t aLiftPosition, MoveType aMoveType);
    void SetBrightness(MoveType aMoveType, uint16_t aPosition);

    static void SetOperationalStatus(const OperationalStatus & aStatus);
    static uint8_t OperationalStateToValue(const OperationalState & aState);
    static void CallbackPositionSet(intptr_t arg);
    static void OperationalStatusSetWithGlobalUpdate(intptr_t aArg);
    static void PositionLEDUpdate(intptr_t aArg);

    OperationalStatus mOperationalStatus{};
    MoveType mCurrentMoveType;
    LEDWidget mLiftLED;
    LEDWidget mTiltLED;
    PWMDevice mLiftIndicator;
    PWMDevice mTiltIndicator;
};
