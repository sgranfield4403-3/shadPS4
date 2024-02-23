// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#define _TIMESPEC_DEFINED

#include <pthread.h>
#include "common/types.h"

namespace Core::Libraries::LibC {

int PS4_SYSV_ABI ps4___cxa_guard_acquire(u64* guard_object);
void PS4_SYSV_ABI ps4___cxa_guard_release(u64* guard_object);
void PS4_SYSV_ABI ps4___cxa_guard_abort(u64* guard_object);

} // namespace Core::Libraries::LibC
