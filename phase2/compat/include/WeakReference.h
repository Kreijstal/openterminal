#pragma once

// The Windows SDK spells this header "WeakReference.h". mingw-w64 ships the
// same public WinRT interfaces in the lower-case weakreference.h; Linux hosts
// have case-sensitive filesystems.
#include <weakreference.h>
