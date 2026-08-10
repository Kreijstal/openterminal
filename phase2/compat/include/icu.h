#pragma once

// The Windows SDK flattens the public ICU C surface into <icu.h>. Upstream ICU
// keeps it in focused headers; expose the subset used by Terminal under the SDK
// spelling while linking the open vcpkg build.
#include <unicode/ubrk.h>
#include <unicode/uregex.h>
#include <unicode/utext.h>
#include <unicode/ustring.h>
#include <unicode/utf16.h>
#include <unicode/utypes.h>
