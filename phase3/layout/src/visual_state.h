// Visual states and the deterministic endpoints of their storyboards.

#ifndef OPENXAML_VISUAL_STATE_H
#define OPENXAML_VISUAL_STATE_H

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "property.h"

namespace openxaml {

class VisualStateError : public std::runtime_error {
public:
    explicit VisualStateError(const std::string& what) : std::runtime_error(what) {}
};

class NameScope {
public:
    void Register(std::string name, DependencyObject& object);
    void Unregister(const std::string& name);
    DependencyObject* Find(const std::string& name) const;

private:
    std::map<std::string, DependencyObject*> names_;
};

struct Timeline {
    std::string target_name;
    const DependencyProperty* target_property = nullptr;
    std::optional<PropertyValue> from;
    PropertyValue to = 0.0;
    double duration_seconds = 0.0;
};

class Storyboard {
public:
    std::vector<Timeline> timelines;

    // Samples all timelines at a normalized progress. The Wave-3 oracle uses
    // exactly 0 and 1; interpolation is implemented too so those endpoints do
    // not need a separate, potentially divergent code path.
    void Sample(NameScope& names, DependencyObject& state_owner, double progress) const;
    void Stop(NameScope& names, DependencyObject& state_owner) const;

private:
    mutable std::map<std::pair<DependencyObject*, const DependencyProperty*>, PropertyValue>
        base_values_;
};

struct VisualStateSetter {
    std::string target_name;
    const DependencyProperty* property = nullptr;
    PropertyValue value = 0.0;
};

struct VisualState {
    std::string name;
    std::vector<VisualStateSetter> setters;
    Storyboard storyboard;
};

class VisualStateGroup {
public:
    explicit VisualStateGroup(std::string name = {}) : name_(std::move(name)) {}

    void Add(VisualState state);
    VisualState* Find(const std::string& name);
    const VisualState* Find(const std::string& name) const;
    const std::string& name() const { return name_; }
    const VisualState* current() const { return current_; }

private:
    friend class VisualStateManager;
    std::string name_;
    std::map<std::string, VisualState> states_;
    const VisualState* current_ = nullptr;
};

class VisualStateManager {
public:
    VisualStateManager(DependencyObject& owner, NameScope& names)
        : owner_(owner), names_(names) {}

    void AddGroup(VisualStateGroup group);
    bool GoToState(const std::string& state_name, bool use_transitions = true);
    void SampleCurrent(const std::string& group_name, double progress);
    const VisualState* CurrentState(const std::string& group_name) const;

private:
    DependencyObject* Resolve(const std::string& name) const;
    void Clear(VisualStateGroup& group);
    void ApplySetters(const VisualState& state);

    DependencyObject& owner_;
    NameScope& names_;
    std::map<std::string, VisualStateGroup> groups_;
};

}  // namespace openxaml

#endif  // OPENXAML_VISUAL_STATE_H
