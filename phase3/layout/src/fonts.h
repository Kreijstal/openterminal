// Loading harvested font metrics into a FontLibrary.
//
// Kept apart from text.cpp because reading files is not layout. The metrics
// themselves are produced by phase3/scripts/harvest_font_metrics.py on a
// machine that has the font; what arrives here is JSON.

#ifndef OPENXAML_FONTS_H
#define OPENXAML_FONTS_H

#include <string>

#include "text.h"

namespace openxaml {

// Parses one harvested metrics document.
FontMetrics ParseFontMetrics(const std::string& json, const std::string& where);

// Adds every *.json under `directory` to `library`, keyed by the family each
// one declares. Returns how many were loaded. A missing directory loads
// nothing and is not an error -- the levels that need a font say so when they
// measure, which names the actual problem instead of failing every case.
int LoadFontDirectory(FontLibrary& library, const std::string& directory);

// Installs the pair adjustments a *derived* metrics file states, onto a family
// whose advances came from a harvest. Returns how many pairs were installed.
//
// Kerning is the one metric that cannot travel with the harvest. The runner's
// Segoe UI kerns pairs the recorded runs prove the runtime did not apply, so
// which pairs survive is a measurement rather than a reading, and the file that
// states it is committed for the same reason the oracle digest is. See
// phase3/xaml-db/fonts/README.md.
int LoadImpliedKerning(FontLibrary& library, const std::string& path);

}  // namespace openxaml

#endif  // OPENXAML_FONTS_H
