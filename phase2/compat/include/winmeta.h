#pragma once

// MinGW does not currently provide the Windows SDK's ETW metadata header.
// The open build makes TraceLogging optional, so only the level constants
// consumed by Terminal's no-op provider shim are needed here.
#ifndef WINEVENT_LEVEL_ERROR
#define WINEVENT_LEVEL_ERROR 2
#endif
#ifndef WINEVENT_LEVEL_VERBOSE
#define WINEVENT_LEVEL_VERBOSE 5
#endif
