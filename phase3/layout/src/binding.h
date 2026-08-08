// WinUI binding expressions over the native dependency-property store.
//
// The ABI-facing runtime can adapt INotifyPropertyChanged objects to
// ObservableObject; tests and the markup runtime use PropertyBag directly.
// Both {Binding} and {x:Bind} end here. Their only semantic difference at this
// layer is the default mode chosen by the caller (OneWay and OneTime).

#ifndef OPENXAML_BINDING_H
#define OPENXAML_BINDING_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "property.h"

namespace openxaml {

class BindingError : public std::runtime_error {
public:
    explicit BindingError(const std::string& what) : std::runtime_error(what) {}
};

class ObservableObject {
public:
    using Handler = std::function<void(const std::string&)>;
    using Token = size_t;

    virtual ~ObservableObject() = default;
    virtual bool TryGet(const std::string& path, PropertyValue& value) const = 0;
    virtual bool TrySet(const std::string& path, const PropertyValue& value) = 0;

    Token AddPropertyChanged(Handler handler);
    void RemovePropertyChanged(Token token);

protected:
    // An empty name has INotifyPropertyChanged's standard meaning: every
    // property may have changed, so every expression refreshes.
    void NotifyPropertyChanged(const std::string& path);

private:
    std::map<Token, Handler> handlers_;
    Token next_token_ = 1;
};

// A deterministic INotifyPropertyChanged-shaped source useful to controls,
// view models in the native harness, and binding tests.
class PropertyBag final : public ObservableObject {
public:
    bool TryGet(const std::string& path, PropertyValue& value) const override;
    bool TrySet(const std::string& path, const PropertyValue& value) override;
    void Set(const std::string& path, PropertyValue value);
    bool Remove(const std::string& path);

private:
    std::map<std::string, PropertyValue> values_;
};

enum class BindingMode { OneTime, OneWay, TwoWay, OneWayToSource };

struct Binding {
    std::string path;
    BindingMode mode = BindingMode::OneWay;
    std::optional<PropertyValue> fallback_value;
    std::function<PropertyValue(const PropertyValue&)> convert;
    std::function<PropertyValue(const PropertyValue&)> convert_back;
};

// Parses the argument after "Binding" or "x:Bind". Supports the positional
// path and WinUI's Path=, Mode= and FallbackValue= named arguments.
Binding ParseBindingMarkup(const std::string& extension_name, const std::string& argument);

class BindingExpression {
public:
    BindingExpression(DependencyObject& target, const DependencyProperty& target_property,
                      ObservableObject& source, Binding binding);
    ~BindingExpression();

    BindingExpression(const BindingExpression&) = delete;
    BindingExpression& operator=(const BindingExpression&) = delete;
    BindingExpression(BindingExpression&& other) noexcept;
    BindingExpression& operator=(BindingExpression&& other) noexcept;

    void UpdateTarget();
    void UpdateSource();
    void Detach();
    bool attached() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace openxaml

#endif  // OPENXAML_BINDING_H
