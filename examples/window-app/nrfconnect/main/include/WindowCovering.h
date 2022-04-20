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

#include <app/clusters/window-covering-server/window-covering-server.h>

#include <cstdint>

using namespace chip::app::Clusters::WindowCovering;

class WindowCovering
{
public:
    WindowCovering();
    static WindowCovering & Instance()
    {
        static WindowCovering sInstance;
        return sInstance;
    }

    void LiftMove(OperationalState direction, NPercent100ths target);
    void ScheduleLift();
    void ScheduleOperationalStatusSetWithGlobalUpdate(OperationalStatus opStatus);
    void StartLifting(OperationalState aMoveDirection);
    void StopLifting();
    void CancelTimer();
    void StartTimer(uint32_t aTimeoutInMs);
    void UpdateLiftLED();
    uint8_t LiftToBrightness(uint16_t aLiftPosition);

    static void CallbackPositionSet(intptr_t arg);
    static void CallbackOperationalStatusSetWithGlobalUpdate(intptr_t arg);
    static void ScheduleLiftLEDUpdateCallback(intptr_t arg);
    static void TimerTimeoutCallback(k_timer * aTimer);
    static constexpr chip::EndpointId Endpoint(){ return 1; };
 
private:
    OperationalState mLiftOpState = OperationalState::Stall;
    OperationalState mTiltOpState = OperationalState::Stall;
    bool mContinueWork{ false };
    LEDWidget mLiftLed;
    static constexpr auto sTimeoutMs{ 100 };
};
