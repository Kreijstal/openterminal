// Win32 message-loop backed projection of Windows.UI.Core.CoreDispatcher.

#ifndef OPENXAML_CORE_DISPATCHER_H
#define OPENXAML_CORE_DISPATCHER_H

#include "sdk.h"

namespace openxaml::winrt {

// Returns the dispatcher for the calling Win32 thread.  Its RunAsync queue is
// serviced by that thread's ordinary GetMessage/DispatchMessage loop.
HRESULT GetCoreDispatcherForCurrentThread(
    ABI::Windows::UI::Core::ICoreDispatcher** value);

}  // namespace openxaml::winrt

#endif  // OPENXAML_CORE_DISPATCHER_H
