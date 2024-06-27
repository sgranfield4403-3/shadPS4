// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include "common/assert.h"
#include "common/config.h"
#include "common/version.h"
#include "core/libraries/pad/pad.h"
#include "input/controller.h"
#include "sdl_window.h"

namespace Frontend {

WindowSDL::WindowSDL(s32 width_, s32 height_, Input::GameController* controller_)
    : width{width_}, height{height_}, controller{controller_} {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        UNREACHABLE_MSG("Failed to initialize SDL video subsystem: {}", SDL_GetError());
    }
    SDL_InitSubSystem(SDL_INIT_AUDIO);

    const std::string title = "shadPS4 v" + std::string(Common::VERSION);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(props, "flags", SDL_WINDOW_VULKAN);
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window == nullptr) {
        UNREACHABLE_MSG("Failed to create window handle: {}", SDL_GetError());
    }

    SDL_SetWindowFullscreen(window, Config::isFullscreenMode());

#if defined(SDL_PLATFORM_WIN32)
    window_info.type = WindowSystemType::Windows;
    window_info.render_surface =
        SDL_GetProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetProperty(SDL_GetWindowProperties(window),
                                                         SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        window_info.render_surface = (void*)SDL_GetNumberProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
        window_info.render_surface = SDL_GetProperty(SDL_GetWindowProperties(window),
                                                     SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    }
#endif
}

WindowSDL::~WindowSDL() = default;

void WindowSDL::waitEvent() {
    // Called on main thread
    SDL_Event event;

    if (!SDL_PollEvent(&event)) {
        return;
    }

    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        onResize();
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_EXPOSED:
        is_shown = event.type == SDL_EVENT_WINDOW_EXPOSED;
        onResize();
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        onKeyPress(&event);
        break;
    case SDL_EVENT_QUIT:
        is_open = false;
        break;
    default:
        break;
    }
}

void WindowSDL::onResize() {
    SDL_GetWindowSizeInPixels(window, &width, &height);
}

void WindowSDL::onKeyPress(const SDL_Event* event) {
    using Libraries::Pad::OrbisPadButtonDataOffset;

    u32 button = 0;
    Input::Axis axis = Input::Axis::AxisMax;
    int axisvalue = 0;
    int ax = 0;
    switch (event->key.key) {
    case SDLK_UP:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_UP;
        break;
    case SDLK_DOWN:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_DOWN;
        break;
    case SDLK_LEFT:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_LEFT;
        break;
    case SDLK_RIGHT:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_RIGHT;
        break;
    case SDLK_KP_8:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_TRIANGLE;
        break;
    case SDLK_KP_6:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CIRCLE;
        break;
    case SDLK_KP_2:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_CROSS;
        break;
    case SDLK_KP_4:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_SQUARE;
        break;
    case SDLK_RETURN:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_OPTIONS;
        break;
    case SDLK_a:
        axis = Input::Axis::LeftX;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += -127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_d:
        axis = Input::Axis::LeftX;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_w:
        axis = Input::Axis::LeftY;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += -127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_s:
        axis = Input::Axis::LeftY;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_j:
        axis = Input::Axis::RightX;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += -127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_l:
        axis = Input::Axis::RightX;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_i:
        axis = Input::Axis::RightY;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += -127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_k:
        axis = Input::Axis::RightY;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 127;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(-0x80, 0x80, axisvalue);
        break;
    case SDLK_x:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L3;
        break;
    case SDLK_m:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R3;
        break;
    case SDLK_q:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L1;
        break;
    case SDLK_u:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R1;
        break;
    case SDLK_e:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L2;
        axis = Input::Axis::TriggerLeft;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 255;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(0, 0x80, axisvalue);
        break;
    case SDLK_o:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R2;
        axis = Input::Axis::TriggerRight;
        if (event->type == SDL_EVENT_KEY_DOWN) {
            axisvalue += 255;
        } else {
            axisvalue = 0;
        }
        ax = Input::GetAxis(0, 0x80, axisvalue);
        break;
    case SDLK_SPACE:
        button = OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_TOUCH_PAD;
        break;
    default:
        break;
    }
    if (button != 0) {
        controller->CheckButton(0, button, event->type == SDL_EVENT_KEY_DOWN);
    }
    if (axis != Input::Axis::AxisMax) {
        controller->Axis(0, axis, ax);
    }
}

} // namespace Frontend
