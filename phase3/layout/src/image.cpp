#include "image.h"

namespace openxaml {
namespace {

// No Source, no Stretch. Both are the Image's own properties and both are
// deliberately unregistered: markup carrying either must be refused by name
// rather than measured as though it had said nothing -- see image.h.
const std::vector<std::string> kOwners = {"Image", "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& Image::Owners() { return kOwners; }

}  // namespace openxaml
