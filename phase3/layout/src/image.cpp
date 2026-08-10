#include "image.h"

namespace openxaml {
namespace {

// Source and Stretch are retained by the live ABI object. They remain absent
// from the harvested dependency-property table until markup/XBF decoding can
// also retain an ImageSource resource identity rather than only its runtime
// class; see image.h.
const std::vector<std::string> kOwners = {"Image", "FrameworkElement", "UIElement"};

}  // namespace

const std::vector<std::string>& Image::Owners() { return kOwners; }

}  // namespace openxaml
