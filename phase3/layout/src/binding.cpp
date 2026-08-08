#include "binding.h"

#include <utility>
#include <vector>
#include <cctype>

namespace openxaml {
namespace {

std::string TrimBinding(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

Binding ParseBindingMarkup(const std::string& extension_name, const std::string& argument) {
    if (extension_name != "Binding" && extension_name != "x:Bind")
        throw BindingError("the markup extension '{" + extension_name + "}' is not a binding");
    Binding result;
    result.mode = extension_name == "x:Bind" ? BindingMode::OneTime : BindingMode::OneWay;
    size_t start = 0;
    bool positional_seen = false;
    while (start <= argument.size()) {
        const size_t comma = argument.find(',', start);
        const std::string part = TrimBinding(argument.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty()) {
            const size_t equals = part.find('=');
            if (equals == std::string::npos) {
                if (positional_seen || !result.path.empty())
                    throw BindingError("a binding has more than one positional path");
                result.path = part;
                positional_seen = true;
            } else {
                const std::string name = TrimBinding(part.substr(0, equals));
                const std::string value = TrimBinding(part.substr(equals + 1));
                if (value.empty()) throw BindingError("the binding argument '" + name + "' is empty");
                if (name == "Path") {
                    result.path = value;
                } else if (name == "Mode") {
                    if (value == "OneTime") result.mode = BindingMode::OneTime;
                    else if (value == "OneWay") result.mode = BindingMode::OneWay;
                    else if (value == "TwoWay") result.mode = BindingMode::TwoWay;
                    else if (value == "OneWayToSource") result.mode = BindingMode::OneWayToSource;
                    else throw BindingError("'" + value + "' is not a binding mode");
                } else if (name == "FallbackValue") {
                    result.fallback_value = value;
                } else {
                    throw BindingError("the binding argument '" + name + "' is not implemented");
                }
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (result.path.empty()) result.path = ".";
    return result;
}

ObservableObject::Token ObservableObject::AddPropertyChanged(Handler handler) {
    if (!handler) throw BindingError("a source property-changed handler cannot be empty");
    const Token token = next_token_++;
    handlers_.emplace(token, std::move(handler));
    return token;
}

void ObservableObject::RemovePropertyChanged(Token token) { handlers_.erase(token); }

void ObservableObject::NotifyPropertyChanged(const std::string& path) {
    std::vector<Token> tokens;
    tokens.reserve(handlers_.size());
    for (const auto& [token, handler] : handlers_) {
        (void)handler;
        tokens.push_back(token);
    }
    for (Token token : tokens) {
        const auto found = handlers_.find(token);
        if (found != handlers_.end()) found->second(path);
    }
}

bool PropertyBag::TryGet(const std::string& path, PropertyValue& value) const {
    const auto found = values_.find(path);
    if (found == values_.end()) return false;
    value = found->second;
    return true;
}

bool PropertyBag::TrySet(const std::string& path, const PropertyValue& value) {
    Set(path, value);
    return true;
}

void PropertyBag::Set(const std::string& path, PropertyValue value) {
    const auto found = values_.find(path);
    if (found != values_.end() && SameValue(found->second, value)) return;
    values_[path] = std::move(value);
    NotifyPropertyChanged(path);
}

bool PropertyBag::Remove(const std::string& path) {
    if (!values_.erase(path)) return false;
    NotifyPropertyChanged(path);
    return true;
}

struct BindingExpression::State {
    DependencyObject* target;
    const DependencyProperty* target_property;
    ObservableObject* source;
    Binding binding;
    ObservableObject::Token source_token = 0;
    DependencyObject::PropertyChangedToken target_token = 0;
    bool updating = false;
    bool is_attached = true;

    void TargetFromSource() {
        if (!is_attached || updating) return;
        PropertyValue value;
        if (!source->TryGet(binding.path, value)) {
            if (!binding.fallback_value) {
                throw BindingError("the binding path '" + binding.path + "' was not found");
            }
            value = *binding.fallback_value;
        }
        if (binding.convert) value = binding.convert(value);
        updating = true;
        try {
            target->SetValue(*target_property, std::move(value));
            updating = false;
        } catch (...) {
            updating = false;
            throw;
        }
    }

    void SourceFromTarget() {
        if (!is_attached || updating) return;
        PropertyValue value = target->GetValue(*target_property);
        if (binding.convert_back) value = binding.convert_back(value);
        updating = true;
        try {
            if (!source->TrySet(binding.path, value)) {
                throw BindingError("the binding path '" + binding.path + "' is not writable");
            }
            updating = false;
        } catch (...) {
            updating = false;
            throw;
        }
    }

    void Detach() {
        if (!is_attached) return;
        is_attached = false;
        if (source_token) source->RemovePropertyChanged(source_token);
        if (target_token) target->RemovePropertyChangedHandler(target_token);
        source_token = 0;
        target_token = 0;
    }
};

BindingExpression::BindingExpression(DependencyObject& target,
                                     const DependencyProperty& target_property,
                                     ObservableObject& source, Binding binding)
    : state_(std::make_shared<State>(
          State{&target, &target_property, &source, std::move(binding)})) {
    std::weak_ptr<State> weak = state_;
    const BindingMode mode = state_->binding.mode;
    if (mode == BindingMode::OneWay || mode == BindingMode::TwoWay) {
        state_->source_token = source.AddPropertyChanged(
            [weak](const std::string& property) {
                if (auto state = weak.lock(); state &&
                    (property.empty() || property == state->binding.path)) {
                    state->TargetFromSource();
                }
            });
    }
    if (mode == BindingMode::TwoWay || mode == BindingMode::OneWayToSource) {
        state_->target_token = target.AddPropertyChangedHandler(
            [weak](DependencyObject&, const DependencyProperty& property, const PropertyValue&) {
                if (auto state = weak.lock(); state && &property == state->target_property)
                    state->SourceFromTarget();
            });
    }

    if (mode == BindingMode::OneWayToSource)
        state_->SourceFromTarget();
    else
        state_->TargetFromSource();
}

BindingExpression::~BindingExpression() { Detach(); }

BindingExpression::BindingExpression(BindingExpression&& other) noexcept
    : state_(std::move(other.state_)) {}

BindingExpression& BindingExpression::operator=(BindingExpression&& other) noexcept {
    if (this == &other) return *this;
    Detach();
    state_ = std::move(other.state_);
    return *this;
}

void BindingExpression::UpdateTarget() {
    if (state_) state_->TargetFromSource();
}

void BindingExpression::UpdateSource() {
    if (state_) state_->SourceFromTarget();
}

void BindingExpression::Detach() {
    if (state_) state_->Detach();
}

bool BindingExpression::attached() const { return state_ && state_->is_attached; }

}  // namespace openxaml
