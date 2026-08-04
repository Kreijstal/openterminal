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

}  // namespace openxaml

#endif  // OPENXAML_FONTS_H
