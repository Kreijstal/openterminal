#ifndef OPENXAML_RENDER_GDI_DWRITE_TEXT_PROVIDER_H
#define OPENXAML_RENDER_GDI_DWRITE_TEXT_PROVIDER_H

#include <memory>
#include <string>

#include "display_list.h"
#include "surface.h"
#include "text.h"

namespace openxaml::render {

// Installs the Windows-only provider behind TextBlock. Native/oracle programs
// that do not call this keep using harvested FontLibrary data.
bool InstallDirectWriteRuntimeTextProvider(std::string& diagnostic);

// Shapes and rasterizes one retained text run through the same provider.
// Failure is named and leaves the surface unchanged.
bool DrawDirectWriteTextRun(Surface& surface, const TextOp& run, Color ink,
                            std::string& diagnostic);

}  // namespace openxaml::render

#endif
