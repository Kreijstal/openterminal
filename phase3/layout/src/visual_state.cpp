#include "visual_state.h"

#include <algorithm>
#include <set>

namespace openxaml {
namespace {

DependencyObject* ResolveTarget(NameScope& names, DependencyObject& owner,
                                const std::string& name) {
    if (name.empty()) return &owner;
    DependencyObject* target = names.Find(name);
    if (!target) throw VisualStateError("the storyboard target '" + name + "' was not found");
    return target;
}

PropertyValue Interpolate(const PropertyValue& from, const PropertyValue& to, double progress) {
    if (from.index() != to.index())
        throw VisualStateError("a storyboard cannot interpolate values of different types");
    if (const double* start = std::get_if<double>(&from))
        return *start + (std::get<double>(to) - *start) * progress;
    if (const Thickness* start = std::get_if<Thickness>(&from)) {
        const Thickness end = std::get<Thickness>(to);
        return Thickness{start->left + (end.left - start->left) * progress,
                         start->top + (end.top - start->top) * progress,
                         start->right + (end.right - start->right) * progress,
                         start->bottom + (end.bottom - start->bottom) * progress};
    }
    // Object, enum, bool and string animations are discrete. WinUI key-frame
    // storyboards hold the base value until the final key.
    return progress < 1.0 ? from : to;
}

}  // namespace

void NameScope::Register(std::string name, DependencyObject& object) {
    if (name.empty()) throw VisualStateError("a namescope name cannot be empty");
    if (names_.count(name)) throw VisualStateError("the name '" + name + "' is already registered");
    names_.emplace(std::move(name), &object);
}

void NameScope::Unregister(const std::string& name) { names_.erase(name); }

DependencyObject* NameScope::Find(const std::string& name) const {
    const auto found = names_.find(name);
    return found == names_.end() ? nullptr : found->second;
}

void Storyboard::Sample(NameScope& names, DependencyObject& state_owner, double progress) const {
    if (progress < 0.0 || progress > 1.0)
        throw VisualStateError("storyboard progress must be between zero and one");
    for (const Timeline& timeline : timelines) {
        if (!timeline.target_property)
            throw VisualStateError("a timeline has no target property");
        DependencyObject* target = ResolveTarget(names, state_owner, timeline.target_name);
        const auto key = std::make_pair(target, timeline.target_property);
        auto base = base_values_.find(key);
        if (base == base_values_.end())
            base = base_values_.emplace(key, target->GetValue(*timeline.target_property)).first;
        const PropertyValue from = timeline.from ? *timeline.from : base->second;
        target->SetAnimatedValue(*timeline.target_property,
                                 Interpolate(from, timeline.to, progress));
    }
}

void Storyboard::Stop(NameScope& names, DependencyObject& state_owner) const {
    std::set<std::pair<DependencyObject*, const DependencyProperty*>> cleared;
    for (const Timeline& timeline : timelines) {
        if (!timeline.target_property) continue;
        DependencyObject* target = ResolveTarget(names, state_owner, timeline.target_name);
        if (cleared.emplace(target, timeline.target_property).second)
            target->ClearAnimatedValue(*timeline.target_property);
    }
    base_values_.clear();
}

void VisualStateGroup::Add(VisualState state) {
    if (state.name.empty()) throw VisualStateError("a visual state must have a name");
    if (states_.count(state.name))
        throw VisualStateError("the visual state '" + state.name + "' is declared twice");
    states_.emplace(state.name, std::move(state));
}

const VisualState* VisualStateGroup::Find(const std::string& name) const {
    const auto found = states_.find(name);
    return found == states_.end() ? nullptr : &found->second;
}

VisualState* VisualStateGroup::Find(const std::string& name) {
    const auto found = states_.find(name);
    return found == states_.end() ? nullptr : &found->second;
}

void VisualStateManager::AddGroup(VisualStateGroup group) {
    if (groups_.count(group.name()))
        throw VisualStateError("the visual-state group '" + group.name() + "' is declared twice");
    groups_.emplace(group.name(), std::move(group));
}

DependencyObject* VisualStateManager::Resolve(const std::string& name) const {
    return ResolveTarget(names_, owner_, name);
}

void VisualStateManager::Clear(VisualStateGroup& group) {
    if (!group.current_) return;
    for (const VisualStateSetter& setter : group.current_->setters) {
        if (setter.clear) {
            setter.clear();
        } else if (setter.property) {
            Resolve(setter.target_name)->ClearAnimatedValue(*setter.property);
        }
    }
    group.current_->storyboard.Stop(names_, owner_);
    group.current_ = nullptr;
}

void VisualStateManager::ApplySetters(const VisualState& state) {
    for (const VisualStateSetter& setter : state.setters) {
        if (setter.apply) {
            setter.apply();
            continue;
        }
        if (!setter.property)
            throw VisualStateError("a visual-state setter has no target property");
        Resolve(setter.target_name)->SetAnimatedValue(*setter.property, setter.value);
    }
}

bool VisualStateManager::GoToState(const std::string& state_name, bool use_transitions) {
    (void)use_transitions;  // Transition selection needs timing, a Wave-5 concern.
    for (auto& [name, group] : groups_) {
        (void)name;
        VisualState* state = group.Find(state_name);
        if (!state) continue;
        if (group.current_ == state) return true;
        Clear(group);
        group.current_ = state;
        ApplySetters(*state);
        state->storyboard.Sample(names_, owner_, 1.0);
        return true;
    }
    return false;
}

void VisualStateManager::SampleCurrent(const std::string& group_name, double progress) {
    const auto found = groups_.find(group_name);
    if (found == groups_.end())
        throw VisualStateError("the visual-state group '" + group_name + "' was not found");
    VisualStateGroup& group = found->second;
    if (!group.current_)
        throw VisualStateError("the visual-state group '" + group_name + "' has no current state");
    group.current_->storyboard.Sample(names_, owner_, progress);
}

const VisualState* VisualStateManager::CurrentState(const std::string& group_name) const {
    const auto found = groups_.find(group_name);
    return found == groups_.end() ? nullptr : found->second.current_;
}

}  // namespace openxaml
