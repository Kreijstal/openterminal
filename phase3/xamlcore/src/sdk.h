// The SDK's generated WinRT headers, in the order they have to be included.
//
// These are the authority on vtable layout. Everything the DLL implements
// derives from an interface declared here, so a method lands in the slot the
// SDK says it does rather than in the slot we guessed.
//
// The headers come from the pinned Microsoft.Windows.SDK.CPP package by way of
// phase3/scripts/prepare_sdk_headers.py, which makes them consumable by GCC.

#ifndef OPENXAML_SDK_H
#define OPENXAML_SDK_H

// The mingw-w64 11 on ubuntu-24.04 leaves NTDDI_VERSION at bare WIN10
// (0x0A000000), and the SDK's hosting header gates the
// IDesktopWindowXamlSourceNative this DLL implements behind RS5. Current
// mingw-w64 already defaults past that; 19H1 is the newest gate in any
// header sdk.h consumes, so that is what the code declares it assumes.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000007 /* NTDDI_WIN10_19H1 */
#endif

#include <windows.h>

// winbase.h defines GetCurrentTime() as a no-argument macro. Storyboard
// declares a method of the same name, and the macro swallows its parameter
// list -- so the macro has to go before any XAML header is read.
#undef GetCurrentTime

// The WinRT out-of-bounds HRESULT. The SDK's collection templates raise it
// and so do this DLL's own collections, but the mingw-w64 11 winerror.h on
// ubuntu-24.04 predates it. Newer mingw-w64 defines it with exactly this
// value, hence the guard.
#ifndef E_BOUNDS
#define E_BOUNDS _HRESULT_TYPEDEF_(0x8000000B)
#endif

#include <activation.h>
#include <asyncinfo.h>
#include <inspectable.h>
// Named the way the SDK package ships it. Current mingw-w64 also carries a
// lowercase weakreference.h that used to satisfy this include by accident;
// the mingw-w64 11 on ubuntu-24.04 has none, and the shadow of the pinned
// SDK -- the declared authority here -- only answers to the SDK's own case.
#include <WeakReference.h>
#include <winstring.h>

#include <windows.ui.xaml.h>
// The pointer, key and tap handler types an element's add_/remove_ pairs take.
// windows.ui.xaml.h only forward-declares them, and a forward declaration is
// enough to name one in a signature but not to call AddRef on it.
#include <windows.ui.xaml.input.h>
#include <windows.ui.xaml.media.h>
#include <windows.ui.xaml.media.animation.h>
#include <windows.ui.xaml.shapes.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.controls.primitives.h>
#include <windows.ui.xaml.automation.h>
#include <windows.ui.text.h>
#include <windows.ui.viewmanagement.h>
// The thread's CoreWindow: what a XAML island gives its thread on Windows, and
// what Windows Terminal reads modifier keys from.
#include <windows.ui.core.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.hosting.h>
#include <windows.system.h>
#include <windows.applicationmodel.resources.core.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#endif  // OPENXAML_SDK_H
