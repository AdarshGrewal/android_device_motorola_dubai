/*
 * Copyright (C) 2020 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.lights-service.dubai"
#define MAXIMUM_DISPLAY_BRIGHTNESS 3514

#include "Lights.h"
#include <cmath>
#include <fstream>
#include <log/log.h>
#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace light {

const static std::map<LightType, const char*> kLogicalLights = {
    {LightType::BACKLIGHT,     LIGHT_ID_BACKLIGHT},
    {LightType::KEYBOARD,      LIGHT_ID_KEYBOARD},
    {LightType::BUTTONS,       LIGHT_ID_BUTTONS},
    {LightType::BATTERY,       LIGHT_ID_BATTERY},
    {LightType::NOTIFICATIONS, LIGHT_ID_NOTIFICATIONS},
    {LightType::ATTENTION,     LIGHT_ID_ATTENTION},
    {LightType::BLUETOOTH,     LIGHT_ID_BLUETOOTH},
    {LightType::WIFI,          LIGHT_ID_WIFI}
};

light_device_t* getLightDevice(const char* name) {
    light_device_t* lightDevice;
    const hw_module_t* hwModule = NULL;
    int ret = hw_get_module (LIGHTS_HARDWARE_MODULE_ID, &hwModule);
    if (ret == 0) {
        ret = hwModule->methods->open(hwModule, name,
            reinterpret_cast<hw_device_t**>(&lightDevice));
        if (ret != 0) {
            ALOGE("light_open %s %s failed: %d", LIGHTS_HARDWARE_MODULE_ID, name, ret);
        }
    } else {
        ALOGE("hw_get_module %s %s failed: %d", LIGHTS_HARDWARE_MODULE_ID, name, ret);
    }
    if (ret == 0) {
        return lightDevice;
    } else {
        ALOGE("Light passthrough failed to load legacy HAL.");
        return nullptr;
    }
}

Lights::Lights() {
    std::map<int, light_device_t*> lights;
    std::vector<HwLight> availableLights;
    int lightCount =0;
    for(auto const &pair : kLogicalLights) {
        LightType type = pair.first;
        const char* name = pair.second;
        light_device_t* lightDevice = getLightDevice(name);
        lightCount++;
        if (lightDevice != nullptr) {
            HwLight hwLight{};
            hwLight.id = (int)type;
            hwLight.type = type;
            hwLight.ordinal = 0;
            lights[hwLight.id] = lightDevice;
            availableLights.emplace_back(hwLight);
        }
    }
    mAvailableLights = availableLights;
    mLights = lights;
    maxLights = lightCount;
}

ndk::ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {
    if (id >= maxLights) {
        ALOGE("Invalid Light id : %d", id);
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    auto it = mLights.find(id);
    if (it == mLights.end()) {
        ALOGE("Light not supported");
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    light_device_t* hwLight = it->second;
    light_state_t legacyState {
        .color = static_cast<unsigned int>(state.color),
        .flashMode = static_cast<int>(state.flashMode),
        .flashOnMS = state.flashOnMs,
        .flashOffMS = state.flashOffMs,
        .brightnessMode = static_cast<int>(state.brightnessMode),
    };

    // Scale display brightness.
    if (id == (int)LightType::BACKLIGHT) {
        legacyState.color = (state.color & 0xFF) * MAXIMUM_DISPLAY_BRIGHTNESS / 0xFF;
    }

    int ret = hwLight->set_light(hwLight, &legacyState);
    switch (ret) {
        case -ENOSYS:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        case 0:
            return ndk::ScopedAStatus::ok();
        default:
            return ndk::ScopedAStatus::fromServiceSpecificError(ret);
    }
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    for (auto i = mAvailableLights.begin(); i != mAvailableLights.end(); i++) {
        lights->push_back(*i);
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl