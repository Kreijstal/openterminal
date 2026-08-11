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

#include <windows.h>

// winbase.h defines GetCurrentTime() as a no-argument macro. Storyboard
// declares a method of the same name, and the macro swallows its parameter
// list -- so the macro has to go before any XAML header is read.
#undef GetCurrentTime

#include <activation.h>
#include <asyncinfo.h>
#include <inspectable.h>
#include <weakreference.h>
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
