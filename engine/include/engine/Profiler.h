#pragma once

#ifdef DS_PROFILE
#include <tracy/Tracy.hpp>

#define DS_ZONE()              ZoneScoped
#define DS_ZONE_N(name)        ZoneNamedN(_ds_zone, name, true)
#define DS_ZONE_C(name, color) ZoneNamedNC(_ds_zone, name, color, true)
#define DS_FRAME_MARK()        FrameMark
#define DS_FRAME_MARK_N(name)  FrameMarkNamed(name)
#define DS_ALLOC(ptr, size)    TracyAlloc(ptr, size)
#define DS_FREE(ptr)           TracyFree(ptr)
#define DS_GPU_ZONE(ctx, name) TracyGpuZone(name)

#else

#define DS_ZONE()
#define DS_ZONE_N(name)
#define DS_ZONE_C(name, color)
#define DS_FRAME_MARK()
#define DS_FRAME_MARK_N(name)
#define DS_ALLOC(ptr, size)
#define DS_FREE(ptr)
#define DS_GPU_ZONE(ctx, name)

#endif
