#include "grid.h"

#include <algorithm>
#include <map>
#include <numeric>

namespace openxaml {
namespace {

using LayoutTimeSizeType = Definition::LayoutTimeSizeType;

// Grid compares with a far looser epsilon than the rest of layout does, and
// the choice is load-bearing: star weights are normalised by division, so
// exact comparisons would treat arithmetic noise as a real difference.
constexpr double kGridEpsilon = 1e-5;
// Ceiling for star weights and for the max/weight ratio, so that an unbounded
// star column cannot turn the distribution arithmetic into infinity or NaN.
constexpr double kStarClip = 1e298;
constexpr int kLayoutLoopMaxCount = 5;

bool GridIsZero(double v) { return std::abs(v) < kGridEpsilon; }
bool GridAreClose(double a, double b) { return std::abs(a - b) < kGridEpsilon; }

// Attached properties are registered under their dotted name, which is also
// how the markup writes them: the name carries its owner, so it resolves on
// any element rather than against that element's own type chain.
const DependencyProperty* const kColumn = RegisterProperty("Grid", "Grid.Column", {0, false, true});
const DependencyProperty* const kRow = RegisterProperty("Grid", "Grid.Row", {0, false, true});
const DependencyProperty* const kColumnSpan =
    RegisterProperty("Grid", "Grid.ColumnSpan", {1, false, true});
const DependencyProperty* const kRowSpan = RegisterProperty("Grid", "Grid.RowSpan", {1, false, true});

// kChromePropertyOwner is where BorderThickness and Padding come from -- see
// chrome.h. Grid grew them in WinUI 2.6, so that a page does not have to wrap
// a Grid in a Border just to give it one.
const std::vector<std::string> kOwners = {"Grid", kChromePropertyOwner, "Panel",
                                          "FrameworkElement", "UIElement"};

}  // namespace

const DependencyProperty& Grid::ColumnProperty() { return *kColumn; }
const DependencyProperty& Grid::RowProperty() { return *kRow; }
const DependencyProperty& Grid::ColumnSpanProperty() { return *kColumnSpan; }
const DependencyProperty& Grid::RowSpanProperty() { return *kRowSpan; }

const std::vector<std::string>& Grid::Owners() { return kOwners; }

int Grid::GetColumn(const Element& element) { return element.GetInt(*kColumn); }
void Grid::SetColumn(Element& element, int value) { element.SetValue(*kColumn, value); }
int Grid::GetRow(const Element& element) { return element.GetInt(*kRow); }
void Grid::SetRow(Element& element, int value) { element.SetValue(*kRow, value); }
int Grid::GetColumnSpan(const Element& element) { return element.GetInt(*kColumnSpan); }
void Grid::SetColumnSpan(Element& element, int value) { element.SetValue(*kColumnSpan, value); }
int Grid::GetRowSpan(const Element& element) { return element.GetInt(*kRowSpan); }
void Grid::SetRowSpan(Element& element, int value) { element.SetValue(*kRowSpan, value); }

void Grid::ValidateDefinitionsLayout(std::vector<Definition>& definitions,
                                     bool treat_star_as_auto) {
    for (Definition& definition : definitions) {
        double user_min_size = definition.user_min_size;
        const double user_max_size = definition.user_max_size;
        double user_size = 0.0;

        switch (definition.user_size.type) {
            case GridUnitType::Pixel:
                definition.size_type = LayoutTimeSizeType::Pixel;
                user_size = definition.user_size.value;
                user_min_size = std::max(user_min_size, std::min(user_size, user_max_size));
                break;
            case GridUnitType::Auto:
                definition.size_type = LayoutTimeSizeType::Auto;
                user_size = kInfinity;
                break;
            case GridUnitType::Star:
                // With nothing to take a share of, a star behaves as Auto.
                definition.size_type =
                    treat_star_as_auto ? LayoutTimeSizeType::Auto : LayoutTimeSizeType::Star;
                user_size = kInfinity;
                break;
        }

        definition.min_size = 0.0;
        definition.UpdateMinSize(user_min_size);
        definition.measure_size = std::max(user_min_size, std::min(user_size, user_max_size));
    }
}

void Grid::ValidateCells() {
    cells_.assign(Children().size(), Cell{});
    group1_.clear();
    group2_.clear();
    group3_.clear();
    group4_.clear();
    has_star_cells_u_ = false;
    has_star_cells_v_ = false;

    const std::vector<Element*> children = Children();
    const int column_count = static_cast<int>(columns_.size());
    const int row_count = static_cast<int>(rows_.size());

    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
        const Element* child = children[i];
        Cell& cell = cells_[i];

        // Out-of-range indices and spans are clamped rather than rejected;
        // that is what the runtime does with Grid.Column="9" on a two-column
        // grid, and rejecting it here would fail cases the oracle accepts.
        cell.column_index = std::min(GetColumn(*child), column_count - 1);
        cell.row_index = std::min(GetRow(*child), row_count - 1);
        cell.column_span = std::min(GetColumnSpan(*child), column_count - cell.column_index);
        cell.row_span = std::min(GetRowSpan(*child), row_count - cell.row_index);

        // A span covering several definitions takes on the character of all of
        // them at once: spanning one Auto and one Star column is both.
        auto classify = [](const std::vector<Definition>& definitions, int start, int count,
                           bool& is_auto, bool& is_star) {
            is_auto = false;
            is_star = false;
            for (int j = start; j < start + count; ++j) {
                if (definitions[j].size_type == LayoutTimeSizeType::Auto) is_auto = true;
                if (definitions[j].size_type == LayoutTimeSizeType::Star) is_star = true;
            }
        };
        classify(columns_, cell.column_index, cell.column_span, cell.is_auto_u, cell.is_star_u);
        classify(rows_, cell.row_index, cell.row_span, cell.is_auto_v, cell.is_star_v);

        has_star_cells_u_ = has_star_cells_u_ || cell.is_star_u;
        has_star_cells_v_ = has_star_cells_v_ || cell.is_star_v;

        // Cells are measured in an order that lets star sizes be resolved from
        // already-known content sizes:
        //
        //              Px     Auto   Star     (column)
        //      Px  |    1   |   1   |   3   |
        //    Auto  |    1   |   1   |   3   |
        //    Star  |    4   |   2   |   4   |     (row)
        if (!cell.is_star_v) {
            if (!cell.is_star_u) {
                group1_.push_back(i);
            } else {
                group3_.push_back(i);
            }
        } else if (cell.is_auto_u && !cell.is_star_u) {
            group2_.push_back(i);
        } else {
            group4_.push_back(i);
        }
    }
}

double Grid::GetMeasureSizeForRange(const std::vector<Definition>& definitions, int start,
                                    int count) const {
    double measure_size = 0.0;
    for (int i = start; i < start + count; ++i) {
        measure_size += definitions[i].size_type == LayoutTimeSizeType::Auto
                            ? definitions[i].min_size
                            : definitions[i].measure_size;
    }
    return measure_size;
}

double Grid::GetFinalSizeForRange(const std::vector<Definition>& definitions, int start,
                                  int count) const {
    double size = 0.0;
    for (int i = start; i < start + count; ++i) size += definitions[i].size_cache;
    return size;
}

void Grid::MeasureCell(int cell_index) {
    const Cell& cell = cells_[cell_index];

    // An Auto definition offers infinite room, because its size is whatever
    // the content turns out to need. Anything else offers its resolved size.
    const double measure_width =
        cell.is_auto_u && !cell.is_star_u
            ? kInfinity
            : GetMeasureSizeForRange(columns_, cell.column_index, cell.column_span);
    const double measure_height =
        cell.is_auto_v && !cell.is_star_v
            ? kInfinity
            : GetMeasureSizeForRange(rows_, cell.row_index, cell.row_span);

    Children()[cell_index]->Measure({measure_width, measure_height});
}

void Grid::MeasureCellsGroup(const std::vector<int>& group, Size reference_size) {
    if (group.empty()) return;

    // A child spanning several definitions cannot claim its whole desired size
    // from any one of them, so those claims are collected and shared out after
    // the single-definition claims are known.
    std::map<std::pair<int, int>, double> column_spans;
    std::map<std::pair<int, int>, double> row_spans;

    for (int index : group) {
        MeasureCell(index);
        const Size child_desired = Children()[index]->desired_size();
        const Cell& cell = cells_[index];

        if (cell.column_span == 1) {
            columns_[cell.column_index].UpdateMinSize(
                std::min(child_desired.width, columns_[cell.column_index].user_max_size));
        } else {
            double& claim = column_spans[{cell.column_index, cell.column_span}];
            claim = std::max(claim, child_desired.width);
        }

        if (cell.row_span == 1) {
            rows_[cell.row_index].UpdateMinSize(
                std::min(child_desired.height, rows_[cell.row_index].user_max_size));
        } else {
            double& claim = row_spans[{cell.row_index, cell.row_span}];
            claim = std::max(claim, child_desired.height);
        }
    }

    for (const auto& [key, requested] : column_spans)
        EnsureMinSizeInDefinitionRange(columns_, key.first, key.second, requested);
    for (const auto& [key, requested] : row_spans)
        EnsureMinSizeInDefinitionRange(rows_, key.first, key.second, requested);

    (void)reference_size;
}

void Grid::EnsureMinSizeInDefinitionRange(std::vector<Definition>& definitions, int start,
                                          int count, double requested_size) {
    if (GridIsZero(requested_size)) return;

    const int end = start + count;
    int auto_definitions_count = 0;
    double range_min_size = 0.0;
    double range_preferred_size = 0.0;
    double range_max_size = 0.0;
    double max_max_size = 0.0;

    std::vector<Definition*> temp;
    temp.reserve(count);
    for (int i = start; i < end; ++i) {
        const double min_size = definitions[i].min_size;
        const double max_size = std::max(definitions[i].user_max_size, min_size);
        range_min_size += min_size;
        range_preferred_size += definitions[i].PreferredSize();
        range_max_size += max_size;
        definitions[i].size_cache = max_size;
        max_max_size = std::max(max_max_size, max_size);
        if (definitions[i].user_size.IsAuto()) ++auto_definitions_count;
        temp.push_back(&definitions[i]);
    }

    if (requested_size <= range_min_size) return;

    // std::stable_sort where the original sorts unstably: ties then resolve by
    // definition order, which keeps a run reproducible.
    if (requested_size <= range_preferred_size) {
        // Auto definitions first, since they are already at exactly what their
        // content needs; the rest absorb the difference.
        std::stable_sort(temp.begin(), temp.end(), [](const Definition* x, const Definition* y) {
            if (x->user_size.IsAuto() != y->user_size.IsAuto()) return x->user_size.IsAuto();
            if (x->user_size.IsAuto()) return x->min_size < y->min_size;
            return x->PreferredSize() < y->PreferredSize();
        });

        double size_to_distribute = requested_size;
        int i = 0;
        for (; i < auto_definitions_count; ++i) size_to_distribute -= temp[i]->min_size;
        for (; i < count; ++i) {
            const double new_min_size =
                std::min(size_to_distribute / (count - i), temp[i]->PreferredSize());
            if (new_min_size > temp[i]->min_size) temp[i]->UpdateMinSize(new_min_size);
            size_to_distribute -= new_min_size;
        }
    } else if (requested_size <= range_max_size) {
        // Now the non-Auto definitions go first: they still have headroom
        // between their preferred and maximum sizes.
        std::stable_sort(temp.begin(), temp.end(), [](const Definition* x, const Definition* y) {
            if (x->user_size.IsAuto() != y->user_size.IsAuto()) return y->user_size.IsAuto();
            return x->size_cache < y->size_cache;
        });

        double size_to_distribute = requested_size - range_preferred_size;
        int i = 0;
        for (; i < count - auto_definitions_count; ++i) {
            const double preferred = temp[i]->PreferredSize();
            const double new_min_size =
                preferred + size_to_distribute / (count - auto_definitions_count - i);
            temp[i]->UpdateMinSize(std::min(new_min_size, temp[i]->size_cache));
            size_to_distribute -= temp[i]->min_size - preferred;
        }
        for (; i < count; ++i) {
            const double preferred = temp[i]->min_size;
            const double new_min_size = preferred + size_to_distribute / (count - i);
            temp[i]->UpdateMinSize(std::min(new_min_size, temp[i]->size_cache));
            size_to_distribute -= temp[i]->min_size - preferred;
        }
    } else {
        // More was asked for than every definition's maximum combined, so the
        // maxima stop meaning anything and the surplus is spread by headroom.
        const double equal_size = requested_size / count;
        if (equal_size < max_max_size && !GridAreClose(equal_size, max_max_size)) {
            const double total_remaining_size = max_max_size * count - range_max_size;
            const double size_to_distribute = requested_size - range_max_size;
            for (int i = 0; i < count; ++i) {
                const double delta =
                    (max_max_size - temp[i]->size_cache) * size_to_distribute / total_remaining_size;
                temp[i]->UpdateMinSize(temp[i]->size_cache + delta);
            }
        } else {
            for (int i = 0; i < count; ++i) temp[i]->UpdateMinSize(equal_size);
        }
    }
}

void Grid::ResolveStar(std::vector<Definition>& definitions, double available_size) {
    std::vector<Definition*> stars;
    double taken_size = 0.0;

    for (Definition& definition : definitions) {
        switch (definition.size_type) {
            case LayoutTimeSizeType::Auto:
                taken_size += definition.min_size;
                break;
            case LayoutTimeSizeType::Pixel:
                taken_size += definition.measure_size;
                break;
            case LayoutTimeSizeType::Star: {
                stars.push_back(&definition);
                double star_value = definition.user_size.value;
                if (GridIsZero(star_value)) {
                    definition.measure_size = 0.0;
                    definition.size_cache = 0.0;
                } else {
                    star_value = std::min(star_value, kStarClip);
                    definition.measure_size = star_value;
                    double max_size = std::max(definition.min_size, definition.user_max_size);
                    max_size = std::min(max_size, kStarClip);
                    // Sorted on below: how much space this column can take per
                    // unit of star weight. Distributing the most constrained
                    // first is what keeps a column at its minimum from
                    // stealing the whole remainder.
                    definition.size_cache = max_size / star_value;
                }
                break;
            }
        }
    }

    if (stars.empty()) return;

    std::stable_sort(stars.begin(), stars.end(),
                     [](const Definition* x, const Definition* y) { return x->size_cache < y->size_cache; });

    // Running total of the star weights still to be satisfied, from the back
    // forward, so each definition's share is measured against what remains.
    double all_star_weights = 0.0;
    for (int i = static_cast<int>(stars.size()) - 1; i >= 0; --i) {
        all_star_weights += stars[i]->measure_size;
        stars[i]->size_cache = all_star_weights;
    }

    for (Definition* star : stars) {
        const double star_value = star->measure_size;
        double resolved_size;
        if (GridIsZero(star_value)) {
            resolved_size = star->min_size;
        } else {
            const double user_size =
                std::max(available_size - taken_size, 0.0) * (star_value / star->size_cache);
            resolved_size = std::max(star->min_size, std::min(user_size, star->user_max_size));
        }
        star->measure_size = resolved_size;
        taken_size += resolved_size;
    }
}

Size Grid::MeasureOverride(Size available) {
    // Chrome comes off the top, before any of the definition arithmetic sees
    // the constraint, and goes back on at the end. Everything in between works
    // in the Grid's inner coordinates and does not know the chrome exists.
    const Size chrome = CombinedThickness();
    available.width = std::max(0.0, available.width - chrome.width);
    available.height = std::max(0.0, available.height - chrome.height);

    // No definitions at all: the Grid is a plain overlay, every child gets the
    // full constraint and the Grid is as big as its largest child.
    if (column_definitions.empty() && row_definitions.empty()) {
        columns_.clear();
        rows_.clear();
        cells_.clear();
        Size desired;
        for (Element* child : Children()) {
            child->Measure(available);
            desired.width = std::max(desired.width, child->desired_size().width);
            desired.height = std::max(desired.height, child->desired_size().height);
        }
        return {desired.width + chrome.width, desired.height + chrome.height};
    }

    // An empty collection still lays out as one implicit Star definition --
    // which is why a Grid with only ColumnDefinitions still stretches its
    // children to the full height.
    columns_ = column_definitions.empty() ? std::vector<Definition>{Definition{}} : column_definitions;
    rows_ = row_definitions.empty() ? std::vector<Definition>{Definition{}} : row_definitions;

    const bool size_to_content_u = std::isinf(available.width);
    const bool size_to_content_v = std::isinf(available.height);

    ValidateDefinitionsLayout(columns_, size_to_content_u);
    ValidateDefinitionsLayout(rows_, size_to_content_v);
    ValidateCells();

    MeasureCellsGroup(group1_, available);

    // Group 3 in an Auto row is the cyclic case: the row needs the cell's
    // height to size itself, and the cell needs the row's star column width to
    // know its height. It is broken by measuring group 2 first and then
    // iterating until the widths stop moving.
    const bool has_group3_cells_in_auto_rows = std::any_of(
        group3_.begin(), group3_.end(), [this](int i) { return cells_[i].is_auto_v; });

    if (!has_group3_cells_in_auto_rows) {
        if (has_star_cells_v_) ResolveStar(rows_, available.height);
        MeasureCellsGroup(group2_, available);
        if (has_star_cells_u_) ResolveStar(columns_, available.width);
        MeasureCellsGroup(group3_, available);
    } else if (group2_.empty()) {
        if (has_star_cells_u_) ResolveStar(columns_, available.width);
        MeasureCellsGroup(group3_, available);
        if (has_star_cells_v_) ResolveStar(rows_, available.height);
    } else {
        MeasureCellsGroup(group2_, available);
        for (int pass = 0; pass <= kLayoutLoopMaxCount; ++pass) {
            std::vector<double> widths_before;
            widths_before.reserve(group2_.size());
            for (int i : group2_) widths_before.push_back(Children()[i]->desired_size().width);

            if (has_star_cells_u_) ResolveStar(columns_, available.width);
            MeasureCellsGroup(group3_, available);
            if (has_star_cells_v_) ResolveStar(rows_, available.height);
            MeasureCellsGroup(group2_, available);

            bool changed = false;
            for (size_t j = 0; j < group2_.size(); ++j) {
                if (!AreClose(widths_before[j], Children()[group2_[j]]->desired_size().width))
                    changed = true;
            }
            if (!changed) break;
        }
    }

    MeasureCellsGroup(group4_, available);

    // The Grid wants exactly the sum of what its definitions turned out to
    // need -- not the available size, even when stars filled it.
    const auto sum_min_sizes = [](const std::vector<Definition>& definitions) {
        double total = 0.0;
        for (const Definition& definition : definitions) total += definition.min_size;
        return total;
    };
    return {sum_min_sizes(columns_) + chrome.width, sum_min_sizes(rows_) + chrome.height};
}

void Grid::SetFinalSize(std::vector<Definition>& definitions, double final_size, double dpi_scale) {
    std::vector<Definition*> stars;
    double all_preferred_arrange_size = 0.0;
    const bool rounding = use_layout_rounding();

    // Each definition's size before rounding. Rounding every column
    // independently can leave the row a pixel short or long of the Grid, and
    // the leftover is handed to whichever columns lost the most to rounding.
    std::vector<double> rounding_errors(definitions.size(), 0.0);
    const auto round = [&](double value, size_t index) {
        if (!rounding) return value;
        rounding_errors[index] = value;
        return RoundLayoutValue(value, dpi_scale);
    };
    const auto index_of = [&](const Definition* definition) -> size_t {
        return static_cast<size_t>(definition - definitions.data());
    };

    for (Definition& definition : definitions) {
        // Star-ness is read from the markup here, not from the layout-time
        // type: a Star column measured as Auto (infinite width) is still a
        // star when it comes to dividing up a now-finite arrange size.
        if (definition.user_size.IsStar()) {
            double star_value = definition.user_size.value;
            if (GridIsZero(star_value)) {
                definition.measure_size = 0.0;
                definition.size_cache = 0.0;
            } else {
                star_value = std::min(star_value, kStarClip);
                definition.measure_size = star_value;
                double max_size = std::max(definition.min_size, definition.user_max_size);
                max_size = std::min(max_size, kStarClip);
                definition.size_cache = round(max_size / star_value, index_of(&definition));
            }
            stars.push_back(&definition);
        } else {
            const double user_size = definition.user_size.type == GridUnitType::Pixel
                                         ? definition.user_size.value
                                         : definition.min_size;
            definition.size_cache = round(
                std::max(definition.min_size, std::min(user_size, definition.user_max_size)),
                index_of(&definition));
            all_preferred_arrange_size += definition.size_cache;
        }
    }

    if (!stars.empty()) {
        std::stable_sort(stars.begin(), stars.end(), [](const Definition* x, const Definition* y) {
            return x->size_cache < y->size_cache;
        });

        double all_star_weights = 0.0;
        for (int i = static_cast<int>(stars.size()) - 1; i >= 0; --i) {
            all_star_weights += stars[i]->measure_size;
            stars[i]->size_cache = all_star_weights;
        }

        for (Definition* star : stars) {
            const double star_value = star->measure_size;
            double resolved_size;
            if (GridIsZero(star_value)) {
                resolved_size = star->min_size;
            } else {
                const double user_size = std::max(final_size - all_preferred_arrange_size, 0.0) *
                                         (star_value / star->size_cache);
                resolved_size = std::max(star->min_size, std::min(user_size, star->user_max_size));
            }
            star->size_cache = round(resolved_size, index_of(star));
            // Rounded before it is banked, so the next star divides up what is
            // actually left rather than a fractional remainder that no column
            // will ever occupy.
            all_preferred_arrange_size += star->size_cache;
        }
    }

    // Overflow: the definitions together want more than the Grid received.
    // Shrink them, starting with the ones that have the most slack above their
    // minimum, so a column already at its content size is touched last.
    if (all_preferred_arrange_size > final_size &&
        !GridAreClose(all_preferred_arrange_size, final_size)) {
        std::vector<Definition*> order;
        order.reserve(definitions.size());
        for (Definition& definition : definitions) order.push_back(&definition);
        std::stable_sort(order.begin(), order.end(), [](const Definition* x, const Definition* y) {
            return x->size_cache - x->min_size < y->size_cache - y->min_size;
        });

        double size_to_distribute = final_size - all_preferred_arrange_size;
        for (size_t i = 0; i < order.size(); ++i) {
            const double unrounded = order[i]->size_cache + size_to_distribute / (order.size() - i);
            double final_definition_size = unrounded;
            if (rounding) {
                rounding_errors[index_of(order[i])] =
                    std::min(std::max(unrounded, order[i]->min_size), order[i]->size_cache);
                final_definition_size = RoundLayoutValue(unrounded, dpi_scale);
            }
            final_definition_size = std::max(final_definition_size, order[i]->min_size);
            final_definition_size = std::min(final_definition_size, order[i]->size_cache);
            size_to_distribute -= final_definition_size - order[i]->size_cache;
            order[i]->size_cache = final_definition_size;
        }
        all_preferred_arrange_size = final_size - size_to_distribute;
    }

    // Rounding each definition independently can leave the total off by a
    // pixel or two. Give the difference to the definitions that lost the most
    // to rounding, so the columns still add up to the Grid.
    //
    // Only star definitions are owed this. They are the ones contracted to
    // consume the whole extent, so a shortfall against it is rounding error.
    // Auto and Pixel definitions are entitled to exactly their content or
    // their declared size and legitimately leave the rest of the Grid empty --
    // handing them the remainder stretches columns that asked for nothing,
    // which the runtime does not do.
    if (rounding && !stars.empty() &&
        !GridAreClose(all_preferred_arrange_size, final_size)) {
        std::vector<Definition*> order;
        order.reserve(definitions.size());
        for (Definition& definition : definitions) {
            rounding_errors[index_of(&definition)] -= definition.size_cache;
            order.push_back(&definition);
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](const Definition* x, const Definition* y) {
                             return rounding_errors[index_of(x)] < rounding_errors[index_of(y)];
                         });

        double adjusted_size = all_preferred_arrange_size;
        const double increment = RoundLayoutValue(1.0, dpi_scale);

        if (all_preferred_arrange_size > final_size) {
            // Take from the definitions that rounded up the most, largest
            // error last, so the most over-sized shrinks first.
            for (int i = static_cast<int>(order.size()) - 1;
                 i >= 0 && adjusted_size > final_size && !GridAreClose(adjusted_size, final_size);
                 --i) {
                const double shrunk = std::max(order[i]->size_cache - increment, order[i]->min_size);
                if (shrunk < order[i]->size_cache) adjusted_size -= increment;
                order[i]->size_cache = shrunk;
            }
        } else {
            for (size_t i = 0;
                 i < order.size() && adjusted_size < final_size && !GridAreClose(adjusted_size, final_size);
                 ++i) {
                const double grown = std::max(order[i]->size_cache + increment, order[i]->min_size);
                if (grown > order[i]->size_cache) adjusted_size += increment;
                order[i]->size_cache = grown;
            }
        }
    }

    double offset = 0.0;
    for (Definition& definition : definitions) {
        definition.final_offset = offset;
        offset += definition.size_cache;
    }
}

Size Grid::ArrangeOverride(Size final_size) {
    // Definitions are laid out inside the chrome and the whole grid of them is
    // then shifted by it, so a cell offset is relative to the inner rect
    // rather than to the Grid.
    const Rect inner = InnerRect(final_size);

    if (columns_.empty() || rows_.empty()) {
        for (Element* child : Children())
            child->Arrange({inner.x, inner.y, inner.width, inner.height});
        return final_size;
    }

    SetFinalSize(columns_, inner.width, dpi_scale_x);
    SetFinalSize(rows_, inner.height, dpi_scale_y);

    const std::vector<Element*> children = Children();
    for (size_t i = 0; i < children.size(); ++i) {
        const Cell& cell = cells_[i];
        children[i]->Arrange({inner.x + columns_[cell.column_index].final_offset,
                              inner.y + rows_[cell.row_index].final_offset,
                              GetFinalSizeForRange(columns_, cell.column_index, cell.column_span),
                              GetFinalSizeForRange(rows_, cell.row_index, cell.row_span)});
    }
    return final_size;
}

}  // namespace openxaml
