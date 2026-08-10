#pragma once

// TraceLoggingActivity.h is a convenience wrapper from the Windows SDK's ETW
// toolchain. Terminal's current sources include it for telemetry setup but do
// not instantiate its activity helpers. The MinGW build compiles ETW to no-ops
// in TraceLoggingProvider.h, so no additional declarations are required here.
