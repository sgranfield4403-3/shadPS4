#pragma once
constexpr int SCE_OK = 0;

constexpr int SCE_KERNEL_ERROR_EBADF = 0x80020009;
constexpr int SCE_KERNEL_ERROR_ENOMEM = 0x8002000c;        // Insufficient memory
constexpr int SCE_KERNEL_ERROR_EFAULT = 0x8002000e;        // Invalid address pointer
constexpr int SCE_KERNEL_ERROR_EINVAL = 0x80020016;        // null or invalid states
constexpr int SCE_KERNEL_ERROR_EAGAIN = 0x80020023;        // Memory cannot be allocated
constexpr int SCE_KERNEL_ERROR_ENAMETOOLONG = 0x8002003f;  // character strings exceeds valid size

// videoOut
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_VALUE = 0x80290001;         // invalid argument
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_ADDRESS = 0x80290002;       // invalid addresses
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_TILING_MODE = 0x80290007;   // invalid tiling mode
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO = 0x80290008;  // invalid aspect ration
constexpr int SCE_VIDEO_OUT_ERROR_RESOURCE_BUSY = 0x80290009;         // already opened
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_INDEX = 0x8029000A;         // invalid buffer index
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_HANDLE = 0x8029000B;        // invalid handle
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_EVENT_QUEUE = 0x8029000C;   // Invalid event queue
constexpr int SCE_VIDEO_OUT_ERROR_SLOT_OCCUPIED = 0x80290010;         // slot already used
constexpr int SCE_VIDEO_OUT_ERROR_FLIP_QUEUE_FULL = 0x80290012;       // flip queue is full
constexpr int SCE_VIDEO_OUT_ERROR_INVALID_OPTION = 0x8029001A;        // Invalid buffer attribute option
