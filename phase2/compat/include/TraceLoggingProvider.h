#pragma once

// TraceLoggingProvider.h is distributed with the Windows SDK and is absent
// from mingw-w64. ETW is diagnostic-only for the Terminal parser, so the open
// build keeps the call sites intact while compiling them to no-ops.
using TraceLoggingHProvider = void*;

#define TRACELOGGING_DECLARE_PROVIDER(provider) extern TraceLoggingHProvider provider
#define TRACELOGGING_DEFINE_PROVIDER(provider, ...) TraceLoggingHProvider provider = nullptr
#define TraceLoggingRegister(provider) ((void)(provider))
#define TraceLoggingUnregister(provider) ((void)(provider))
#define TraceLoggingProviderEnabled(...) false
#define TraceLoggingWrite(...) ((void)0)
