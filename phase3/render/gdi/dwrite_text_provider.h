#ifndef OPENXAML_RENDER_GDI_DWRITE_TEXT_PROVIDER_H
#define OPENXAML_RENDER_GDI_DWRITE_TEXT_PROVIDER_H

#include <memory>
#include <string>

#include "display_list.h"
#include "surface.h"
#include "text.h"

namespace openxaml::render {

// Adds one font file to the private DirectWrite collection this process
// resolves families out of, in addition to whatever
// OPENXAML_FONT_ALIAS_MANIFEST named. Nothing is installed on the machine and
// the face is gone when the process ends. The file's own family names resolve;
// no alias is invented for it.
//
// Must be called before the provider is installed, because the private
// collection is built once, at initialization: a later call is a named refusal
// rather than a font that silently never arrives.
bool AddPrivateDirectWriteFontFile(const std::string& path, std::string& diagnostic);

// Installs the Windows-only provider behind TextBlock. Native/oracle programs
// that do not call this keep using harvested FontLibrary data.
bool InstallDirectWriteRuntimeTextProvider(std::string& diagnostic);

// Shapes and rasterizes one retained text run through the same provider.
// Failure is named and leaves the surface unchanged.
bool DrawDirectWriteTextRun(Surface& surface, const TextOp& run, Color ink,
                            std::string& diagnostic);

}  // namespace openxaml::render

#endif
