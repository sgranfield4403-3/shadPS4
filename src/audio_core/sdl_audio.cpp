// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>
#include <common/assert.h>
#include <core/libraries/error_codes.h>
#include "sdl_audio.h"

namespace Audio {

int SDLAudio::AudioOutOpen(int type, u32 samples_num, u32 freq,
                           Libraries::AudioOut::OrbisAudioOutParam format) {
    using Libraries::AudioOut::OrbisAudioOutParam;
    std::scoped_lock lock{m_mutex};
    for (int id = 0; id < portsOut.size(); id++) {
        auto& port = portsOut[id];
        if (!port.isOpen) {
            port.isOpen = true;
            port.type = type;
            port.samples_num = samples_num;
            port.freq = freq;
            port.format = format;
            SDL_AudioFormat sampleFormat;
            switch (format) {
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_MONO:
                sampleFormat = SDL_AUDIO_S16;
                port.channels_num = 1;
                port.sample_size = 2;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_MONO:
                sampleFormat = SDL_AUDIO_F32;
                port.channels_num = 1;
                port.sample_size = 4;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO:
                sampleFormat = SDL_AUDIO_S16;
                port.channels_num = 2;
                port.sample_size = 2;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_STEREO:
                sampleFormat = SDL_AUDIO_F32;
                port.channels_num = 2;
                port.sample_size = 4;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH:
                sampleFormat = SDL_AUDIO_S16;
                port.channels_num = 8;
                port.sample_size = 2;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_8CH:
                sampleFormat = SDL_AUDIO_F32;
                port.channels_num = 8;
                port.sample_size = 4;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH_STD:
                sampleFormat = SDL_AUDIO_S16;
                port.channels_num = 8;
                port.sample_size = 2;
                break;
            case OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_8CH_STD:
                sampleFormat = SDL_AUDIO_F32;
                port.channels_num = 8;
                port.sample_size = 4;
                break;
            default:
                UNREACHABLE_MSG("Unknown format");
            }

            for (int i = 0; i < port.channels_num; i++) {
                port.volume[i] = Libraries::AudioOut::SCE_AUDIO_OUT_VOLUME_0DB;
            }

            SDL_AudioSpec fmt;
            SDL_zero(fmt);
            fmt.format = sampleFormat;
            fmt.channels = port.channels_num;
            fmt.freq = 48000;
            port.stream =
                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_OUTPUT, &fmt, NULL, NULL);
            SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(port.stream));
            return id + 1;
        }
    }

    return -1; // all ports are used
}

s32 SDLAudio::AudioOutOutput(s32 handle, const void* ptr) {
    std::scoped_lock lock{m_mutex};
    auto& port = portsOut[handle - 1];
    if (!port.isOpen) {
        return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
    }
    if (ptr == nullptr) {
        return 0;
    }
    // TODO mixing channels
    int result = SDL_PutAudioStreamData(port.stream, ptr,
                                        port.samples_num * port.sample_size * port.channels_num);
    // TODO find a correct value 8192 is estimated
    while (SDL_GetAudioStreamAvailable(port.stream) > 8192) {
        SDL_Delay(0);
    }

    return result;
}

bool SDLAudio::AudioOutSetVolume(s32 handle, s32 bitflag, s32* volume) {
    using Libraries::AudioOut::OrbisAudioOutParam;
    std::scoped_lock lock{m_mutex};
    auto& port = portsOut[handle - 1];
    if (!port.isOpen) {
        return ORBIS_AUDIO_OUT_ERROR_INVALID_PORT;
    }
    for (int i = 0; i < port.channels_num; i++, bitflag >>= 1u) {
        auto bit = bitflag & 0x1u;

        if (bit == 1) {
            int src_index = i;
            if (port.format == OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_FLOAT_8CH_STD ||
                port.format == OrbisAudioOutParam::ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_8CH_STD) {
                switch (i) {
                case 4:
                    src_index = 6;
                    break;
                case 5:
                    src_index = 7;
                    break;
                case 6:
                    src_index = 4;
                    break;
                case 7:
                    src_index = 5;
                    break;
                default:
                    break;
                }
            }
            port.volume[i] = volume[src_index];
        }
    }

    return true;
}

bool SDLAudio::AudioOutGetStatus(s32 handle, int* type, int* channels_num) {
    std::scoped_lock lock{m_mutex};
    auto& port = portsOut[handle - 1];
    *type = port.type;
    *channels_num = port.channels_num;

    return true;
}

} // namespace Audio
