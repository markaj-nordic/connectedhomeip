/*
 *
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

#include <array>
#include <cstdint>
#include <drivers/gpio.h>

class PWMDevice
{
public:
    enum Action_t : uint8_t
    {
        ON_ACTION = 0,
        OFF_ACTION,
        LEVEL_ACTION,

        INVALID_ACTION
    };

    enum State_t : uint8_t
    {
        kState_On = 0,
        kState_Off,
    };

    using LightingCallback_fn = void (*)(Action_t, int32_t);

    PWMDevice(const device * pwmDevice, uint32_t pwmChannel, uint8_t minLevel, uint8_t maxLevel);
    int Init();
    bool IsTurnedOn() const { return mState == kState_On; }
    uint8_t GetLevel() const { return mLevel; }
    bool InitiateAction(Action_t aAction, int32_t aActor, uint16_t size, uint8_t * value);
    void SetCallbacks(LightingCallback_fn aActionInitiated_CB, LightingCallback_fn aActionCompleted_CB);

private:
    State_t mState;
    uint8_t mMinLevel;
    uint8_t mMaxLevel;
    uint8_t mLevel;
    const device * mPwmDevice;
    uint32_t mPwmChannel;

    LightingCallback_fn mActionInitiated_CB;
    LightingCallback_fn mActionCompleted_CB;

    void Set(bool aOn);
    void SetLevel(uint8_t aLevel);
    void UpdateLight();
};

template <int size>
class PWMManager
{
public:
    static PWMManager & Instance()
    {
        static PWMManager<2> sInstance;
        return sInstance;
    }
    void RegisterDevice(const PWMDevice & device)
    {
        if (CanAddElement())
        {
            mDevices[mIndex++] = device;
        }
    }
    bool InitiateAction(const device * aPwmDevice, PWMDevice::Action_t aAction, int32_t aActor, uint16_t aSize, uint8_t * aValue)
    {
        for (auto it = mDevices.begin(); it != mDevices.end(); ++it)
        {
            if (it->mPwmDevice == aPwmDevice)
            {
                return it->InitiateAction(aAction, aActor, aSize, aValue);
            }
        }
        return false;
    }

private:
    bool CanAddElement() { return (mIndex + 1 <= mDevices.size()); }
    std::array<PWMDevice, size> mDevices;
    uint8_t mIndex{ 0 };
};