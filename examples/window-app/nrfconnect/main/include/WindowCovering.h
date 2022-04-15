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

    // struct CoverWorkData
    // {
    //     chip::EndpointId mEndpointId;
    //     bool isTilt;

    //     union
    //     {
    //         chip::Percent100ths percent100ths;
    //         OperationalStatus opStatus;
    //     };
    // };

    void LiftMove(OperationalState direction, NPercent100ths target);
    void ScheduleLift();
    void ScheduleOperationalStatusSetWithGlobalUpdate(OperationalStatus opStatus);
    static void CallbackPositionSet(intptr_t arg);
    static void CallbackOperationalStatusSetWithGlobalUpdate(intptr_t arg);
    void StartLifting(OperationalState aMoveDirection);
    void StopLifting();
    void CancelTimer();
    void StartTimer(uint32_t aTimeoutInMs);
    static void TimerTimeoutCallback(k_timer * aTimer);

private:
    OperationalState mLiftOpState = OperationalState::Stall;
    OperationalState mTiltOpState = OperationalState::Stall;
    bool mContinueWork{ false };
    static constexpr chip::EndpointId sEndpoint{ 1 };
    static constexpr auto sTimeoutMs{ 100 };
};
