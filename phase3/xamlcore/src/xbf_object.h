#pragma once

#include "xbf.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openxaml::xbf {

struct Object;

struct Value {
    enum class Kind { Empty, Constant, Object, Type, Property, Resource };
    Kind kind = Kind::Empty;
    Constant constant;
    std::shared_ptr<Object> object;
    std::string text;

    static Value FromConstant(Constant value);
    static Value FromObject(std::shared_ptr<Object> value);
    static Value Named(Kind kind, std::string value);
};

struct Object {
    std::string type;
    std::string x_class;
    std::map<std::string, Value> properties;
    std::vector<Value> items;
    std::map<std::string, Value> dictionary;
    std::map<std::string, std::shared_ptr<Object>> names;
};

// Materializes the ordinary XBF node stream into a platform-neutral object
// graph. Runtime activation is a separate adapter over this graph, which lets
// parsing, namescopes, collections and diagnostics remain testable on Linux.
std::shared_ptr<Object> WriteObjectGraph(const Document& document,
                                         std::size_t stream_index = 0);

std::size_t CountObjects(const std::shared_ptr<Object>& root);

}  // namespace openxaml::xbf
