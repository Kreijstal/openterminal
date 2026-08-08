// Loading a compiled page.
//
// XBF is not a serialized tree: it is a recording of the calls the object
// writer made while the compiler walked the markup -- create this type, push a
// scope, set that property, pop. This turns that recording back into the
// document it was made from, and hands it to the same markup engine the text
// path uses. Two consequences, both deliberate:
//
//   * the text path is not touched, so nothing here can move a corpus number;
//   * anything the recording contains that the reconstruction cannot express
//     fails by name rather than quietly producing a smaller tree.
//
// The gate this is held to is phase4/scripts/xbf_equivalence.py: the corpus
// markup compiled by the real genxbf, loaded through here, must measure exactly
// as the same markup measures through the text path -- for every case the
// compiler accepts.

#ifndef OPENXAML_XBF_MARKUP_H
#define OPENXAML_XBF_MARKUP_H

#include <memory>
#include <string>

#include "element.h"
#include "markup_tree.h"
#include "xbf.h"

namespace openxaml {

// The document a compiled page was made from, written back out as XAML text.
// This is the seam: everything above it is XBF, everything below it is the
// markup engine that the text path already goes through.
//
// Throws xbf::XbfError for a malformed file and MarkupError for a well-formed
// one this runtime cannot express -- deferred custom-writer sections, x:Bind
// connection ids, types from a metadata provider we do not have.
std::string XbfToMarkup(const std::string& bytes);
std::string XbfToMarkup(const xbf::Document& document);

// The same, taken all the way to a tree.
MarkupNode ParseXbf(const std::string& bytes);
std::unique_ptr<Element> LoadXbf(const std::string& bytes);

}  // namespace openxaml

#endif  // OPENXAML_XBF_MARKUP_H
