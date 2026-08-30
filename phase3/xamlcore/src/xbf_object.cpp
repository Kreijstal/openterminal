#include "xbf_object.h"

#include <set>
#include <utility>

namespace openxaml::xbf {
namespace {

using ObjectPtr = std::shared_ptr<Object>;

bool IsInterior(NodeType type) {
    switch (type) {
    case NodeType::PushScope:
    case NodeType::PushScopeAddNamespace:
    case NodeType::PushScopeGetValue:
    case NodeType::PushScopeCreateTypeBeginInit:
    case NodeType::PushScopeCreateTypeWithConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
    case NodeType::BeginConditionalScope:
        return true;
    default:
        return false;
    }
}

bool IsTerminator(NodeType type) {
    return type == NodeType::EndInitPopScope ||
           type == NodeType::EndInitProvideValuePopScope ||
           type == NodeType::PopScope;
}

std::string ConstantText(const Constant& value) {
    return value.string_value;
}

class Writer {
public:
    Writer(const Document& document, std::size_t stream_index,
           ObjectPtr existing_root = nullptr)
        : document_(document), stream_index_(stream_index) {
        if (stream_index >= document.substreams.size())
            throw Error(0, "object writer substream index is out of range");
        if (existing_root) objects_.push_back(Value::FromObject(std::move(existing_root)));
    }

    ObjectPtr Run() {
        Visit(0);
        if (objects_.size() != 1 || objects_.back().kind != Value::Kind::Object)
            throw Error(0, "object writer did not finish with exactly one root object");
        auto root = objects_.back().object;
        root->x_class = root_class_;
        root->names = names_;
        return root;
    }

private:
    std::size_t Visit(std::size_t index) {
        const auto& nodes = document_.substreams[stream_index_].nodes;
        const auto starting_scopes = scopes_;
        do {
            if (index >= nodes.size()) throw Error(0, "unterminated object-writer scope");
            const Node& node = nodes[index];
            std::size_t next = index + 1;
            Pre(node);
            if (IsInterior(node.type)) {
                while (next < nodes.size() && !IsTerminator(nodes[next].type))
                    next = Visit(next);
            }
            Post(node);
            index = next;
        } while (scopes_ > starting_scopes && index < nodes.size());
        return index;
    }

    ObjectPtr TopObject(const Node& node) {
        if (objects_.empty() || objects_.back().kind != Value::Kind::Object)
            throw Error(node.offset, "node requires an object on the stack");
        return objects_.back().object;
    }

    Value Pop(const Node& node) {
        if (objects_.empty()) throw Error(node.offset, "object stack underflow");
        Value value = std::move(objects_.back());
        objects_.pop_back();
        return value;
    }

    void PushScope() {
        ++scopes_;
        ++scopes_created_;
    }
    void PopScope(const Node& node) {
        if (scopes_ == 0) throw Error(node.offset, "namespace stack underflow");
        --scopes_;
    }

    void Create(const Node& node) {
        auto object = std::make_shared<Object>();
        object->type = document_.type_name(node.object);
        if (object->type.empty()) throw Error(node.offset, "unresolved XBF type");
        objects_.push_back(Value::FromObject(std::move(object)));
    }

    void Pre(const Node& node) {
        switch (node.type) {
        case NodeType::PushScope:
        case NodeType::PushScopeAddNamespace:
            PushScope();
            break;
        case NodeType::PushScopeGetValue: {
            PushScope();
            const auto property = document_.property_name(node.object);
            auto owner = TopObject(node);
            auto found = owner->properties.find(property);
            if (found == owner->properties.end()) {
                auto collection = std::make_shared<Object>();
                collection->type = property + "#value";
                found = owner->properties.emplace(property, Value::FromObject(collection)).first;
            }
            objects_.push_back(found->second);
            break;
        }
        case NodeType::PushScopeCreateTypeBeginInit:
            PushScope();
            Create(node);
            break;
        case NodeType::PushScopeCreateTypeWithConstantBeginInit:
        case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
            PushScope();
            objects_.push_back(Value::FromConstant(node.constant));
            break;
        case NodeType::BeginConditionalScope:
            PushScope();
            break;
        default:
            break;
        }
    }

    void SetProperty(const Node& node, Value value) {
        auto object = TopObject(node);
        const auto property = document_.property_name(node.object);
        if (property.empty()) throw Error(node.offset, "unresolved XBF property");
        object->properties[property] = std::move(value);
    }

    void Post(const Node& node) {
        switch (node.type) {
        case NodeType::PushScope:
        case NodeType::PushScopeAddNamespace:
        case NodeType::BeginConditionalScope:
        case NodeType::AddNamespace:
            break;
        case NodeType::EndInitPopScope:
        case NodeType::EndInitProvideValuePopScope:
        case NodeType::PopScope:
        case NodeType::EndConditionalScope:
            PopScope(node);
            break;
        case NodeType::PushScopeGetValue:
            (void)Pop(node);
            break;
        case NodeType::PushScopeCreateTypeBeginInit:
        case NodeType::PushScopeCreateTypeWithConstantBeginInit:
        case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
            break;
        case NodeType::CreateTypeBeginInit:
            Create(node);
            break;
        case NodeType::CreateTypeWithConstantBeginInit:
        case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
            objects_.push_back(Value::FromConstant(node.constant));
            break;
        case NodeType::CheckPeerType:
            root_class_ = node.text;
            break;
        case NodeType::PushConstant:
            if (scopes_created_ == 1 && objects_.size() == 1)
                root_class_ = ConstantText(node.constant);
            else
                last_constant_ = Value::FromConstant(node.constant);
            break;
        case NodeType::SetValue:
        case NodeType::SetValueFromMarkupExtension:
            SetProperty(node, Pop(node));
            break;
        case NodeType::SetConnectionId:
            TopObject(node)->properties["x:ConnectionId"] = Value::FromConstant(node.constant);
            break;
        case NodeType::SetName: {
            const auto name = ConstantText(node.constant);
            auto object = TopObject(node);
            object->properties["Windows.UI.Xaml.DependencyObject.Name"] =
                Value::FromConstant(node.constant);
            names_[name] = object;
            break;
        }
        case NodeType::GetResourcePropertyBag:
            TopObject(node)->properties["x:Uid"] = Value::FromConstant(node.constant);
            break;
        case NodeType::SetValueConstant:
        case NodeType::SetValueTypeConvertedConstant:
            SetProperty(node, Value::FromConstant(node.constant));
            break;
        case NodeType::SetValueTypeConvertedResolvedType:
            SetProperty(node, Value::Named(Value::Kind::Type,
                                            document_.type_name(node.second_object)));
            break;
        case NodeType::SetValueTypeConvertedResolvedProperty:
            SetProperty(node, Value::Named(Value::Kind::Property,
                                            document_.property_name(node.second_object)));
            break;
        case NodeType::SetValueFromStaticResource:
        case NodeType::SetValueFromThemeResource:
            SetProperty(node, Value::Named(Value::Kind::Resource,
                                            ConstantText(node.constant)));
            break;
        case NodeType::SetValueFromTemplateBinding:
            SetProperty(node, Value::Named(Value::Kind::Property,
                                            document_.property_name(node.second_object)));
            break;
        case NodeType::ProvideStaticResourceValue:
        case NodeType::ProvideThemeResourceValue:
            objects_.push_back(Value::Named(Value::Kind::Resource,
                                             ConstantText(node.constant)));
            break;
        case NodeType::AddToCollection: {
            Value value;
            if (last_constant_) {
                value = std::move(*last_constant_);
                last_constant_.reset();
            } else {
                value = Pop(node);
            }
            TopObject(node)->items.push_back(std::move(value));
            break;
        }
        case NodeType::AddToDictionaryWithKey: {
            const auto key = ConstantText(node.constant);
            Value value = Pop(node);
            TopObject(node)->dictionary[key] = std::move(value);
            break;
        }
        case NodeType::AddToDictionary: {
            Value value = Pop(node);
            std::string key;
            if (value.kind == Value::Kind::Object) {
                const auto named = value.object->properties.find(
                    "Windows.UI.Xaml.DependencyObject.Name");
                if (named != value.object->properties.end()) key = ConstantText(named->second.constant);
                const auto target = value.object->properties.find("Windows.UI.Xaml.Style.TargetType");
                if (key.empty() && target != value.object->properties.end()) key = target->second.text;
            }
            if (key.empty()) key = "#implicit-" + std::to_string(TopObject(node)->dictionary.size());
            TopObject(node)->dictionary[key] = std::move(value);
            break;
        }
        case NodeType::SetDeferredProperty:
            SetProperty(node, Value::Named(Value::Kind::Object,
                                            "deferred-substream:" + std::to_string(node.substream)));
            break;
        case NodeType::SetCustomRuntimeData:
        {
            auto target = TopObject(node);
            // Optimized ResourceDictionary entries are ordinary XBF object
            // graphs stored in the referenced node substream.  They are not a
            // deferred template: the dictionary must expose them immediately
            // to Lookup/ThemeDictionaries.  Style and visual-state custom data
            // use context-dependent streams, so those keep their marker until
            // the corresponding layer consumes them.
            const bool resource_dictionary =
                target->type == "Windows.UI.Xaml.ResourceDictionary" ||
                target->type == "Windows.UI.Xaml.FrameworkElement.Resources#value";
            if (resource_dictionary &&
                node.substream >= 0 &&
                static_cast<std::size_t>(node.substream) != stream_index_) {
                if (target->type == "Windows.UI.Xaml.ResourceDictionary") {
                    // A ResourceDictionary node's optimized stream contains
                    // its own root object, as the original XBF writer expects.
                    auto payload =
                        Writer(document_, static_cast<std::size_t>(node.substream)).Run();
                    if (payload && payload->type == target->type) {
                        for (auto& [key, value] : payload->dictionary)
                            target->dictionary[key] = std::move(value);
                        for (auto& value : payload->items)
                            target->items.push_back(std::move(value));
                    }
                } else {
                    // FrameworkElement.Resources custom streams instead start
                    // with AddToDictionary nodes: their root is the synthetic
                    // collection already on the parent stream's stack.
                    (void)Writer(document_, static_cast<std::size_t>(node.substream),
                                 target).Run();
                    const auto property = target->type.substr(0, target->type.size() - 6);
                    const auto nested = target->properties.find(property);
                    if (nested != target->properties.end() &&
                        nested->second.kind == Value::Kind::Object &&
                        nested->second.object) {
                        for (auto& [key, value] : nested->second.object->dictionary)
                            target->dictionary[key] = std::move(value);
                        for (auto& value : nested->second.object->items)
                            target->items.push_back(std::move(value));
                        target->properties.erase(nested);
                    }
                    // A Resources property is object-valued at the public ABI.
                    // Replacing the writer's synthetic collection marker lets
                    // the materializer activate and assign that object through
                    // IFrameworkElement::put_Resources.
                    target->type = "Windows.UI.Xaml.ResourceDictionary";
                }
                break;
            }

            // VisualStateGroupCollection custom data is still an ordinary
            // object graph in its referenced substream.  The optimized form
            // starts with PushScope/GetValue against the collection object
            // already on the parent stack, just like FrameworkElement.Resources.
            // Materialize that graph so named states, setters, and (critically)
            // their generated x:Bind connection ids reach IComponentConnector.
            const bool visual_state_collection =
                target->type.size() > 6 &&
                target->type.compare(target->type.size() - 6, 6, "#value") == 0 &&
                (node.custom_data_version == 1 ||
                 (node.custom_data_version >= 3 && node.custom_data_version <= 5));
            if (visual_state_collection && node.substream >= 0 &&
                static_cast<std::size_t>(node.substream) != stream_index_) {
                (void)Writer(document_, static_cast<std::size_t>(node.substream),
                             target).Run();
                const auto property = target->type.substr(0, target->type.size() - 6);
                const auto nested = target->properties.find(property);
                if (nested != target->properties.end() &&
                    nested->second.kind == Value::Kind::Object &&
                    nested->second.object) {
                    for (auto& value : nested->second.object->items)
                        target->items.push_back(std::move(value));
                    target->properties.erase(nested);
                }
                break;
            }

            const bool deferred_element =
                target->type == "Windows.UI.Xaml.Internal.DeferredElement" ||
                target->type == "stable-type:746";
            const bool deferred_version =
                node.custom_data_version == 745 ||
                node.custom_data_version == 6 ||
                node.custom_data_version == 9;
            if (deferred_element && deferred_version && node.substream >= 0 &&
                static_cast<std::size_t>(node.substream) != stream_index_) {
                target->deferred_content =
                    Writer(document_, static_cast<std::size_t>(node.substream)).Run();
                break;
            }
            target->properties["x:CustomRuntimeData"] =
                Value::Named(Value::Kind::Object,
                             "version:" + std::to_string(node.custom_data_version) +
                                 ",substream:" + std::to_string(node.substream));
            break;
        }
        }
    }

    const Document& document_;
    std::size_t stream_index_;
    std::size_t scopes_ = 0;
    std::size_t scopes_created_ = 0;
    std::vector<Value> objects_;
    std::optional<Value> last_constant_;
    std::string root_class_;
    std::map<std::string, ObjectPtr> names_;
};

std::size_t Count(const ObjectPtr& object, std::set<const Object*>& visited) {
    if (!object || !visited.insert(object.get()).second) return 0;
    std::size_t count = 1;
    auto add = [&](const Value& value) {
        if (value.kind == Value::Kind::Object) count += Count(value.object, visited);
    };
    for (const auto& [_, value] : object->properties) add(value);
    for (const auto& value : object->items) add(value);
    for (const auto& [_, value] : object->dictionary) add(value);
    count += Count(object->deferred_content, visited);
    return count;
}

}  // namespace

Value Value::FromConstant(Constant value) {
    Value result;
    result.kind = Kind::Constant;
    result.constant = std::move(value);
    return result;
}

Value Value::FromObject(std::shared_ptr<Object> value) {
    Value result;
    result.kind = Kind::Object;
    result.object = std::move(value);
    return result;
}

Value Value::Named(Kind kind, std::string value) {
    Value result;
    result.kind = kind;
    result.text = std::move(value);
    return result;
}

std::shared_ptr<Object> WriteObjectGraph(const Document& document,
                                         std::size_t stream_index) {
    return Writer(document, stream_index).Run();
}

std::size_t CountObjects(const std::shared_ptr<Object>& root) {
    std::set<const Object*> visited;
    return Count(root, visited);
}

}  // namespace openxaml::xbf
