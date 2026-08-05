#ifndef OPENXAML_GRID_H
#define OPENXAML_GRID_H

#include <string>
#include <vector>

#include "element.h"

namespace openxaml {

enum class GridUnitType { Auto, Pixel, Star };

struct GridLength {
    GridUnitType type = GridUnitType::Star;
    double value = 1.0;

    bool IsAuto() const { return type == GridUnitType::Auto; }
    bool IsStar() const { return type == GridUnitType::Star; }
};

// A row or column, plus the scratch state the sizing algorithm keeps in it.
//
// The three sizes are distinct and the algorithm depends on all three:
// min_size is what content has proven it needs, measure_size is what to offer
// children during measure, and size_cache is the arranged result. Collapsing
// any two of them breaks spans or stars.
struct Definition {
    // What the markup declared. Star-ness is read from here at arrange time
    // even when measure treated the definition as Auto.
    GridLength user_size;
    double user_min_size = 0.0;
    double user_max_size = kInfinity;

    // How this definition behaves for the current measure pass. A Star
    // definition becomes Auto when the Grid is measured with an infinite
    // extent, since there is no space to take a share of.
    enum class LayoutTimeSizeType { Auto, Pixel, Star };
    LayoutTimeSizeType size_type = LayoutTimeSizeType::Auto;

    double min_size = 0.0;
    double measure_size = 0.0;
    double size_cache = 0.0;
    double final_offset = 0.0;

    double PreferredSize() const {
        double preferred = min_size;
        if (size_type != LayoutTimeSizeType::Auto && preferred < measure_size)
            preferred = measure_size;
        return preferred;
    }

    void UpdateMinSize(double candidate) {
        if (candidate > min_size) min_size = candidate;
    }
};

class Grid : public Panel {
public:
    std::string TypeName() const override { return "Windows.UI.Xaml.Controls.Grid"; }
    static const std::vector<std::string>& Owners();
    const std::vector<std::string>& PropertyOwners() const override { return Owners(); }

    // Attached properties, and genuinely attached: they are registered against
    // Grid and stored on whichever element carries them, so a Border knows
    // nothing about them and a Grid can still read them off it. This is why
    // they are static functions taking an Element rather than fields on one.
    static int GetColumn(const Element& element);
    static void SetColumn(Element& element, int value);
    static int GetRow(const Element& element);
    static void SetRow(Element& element, int value);
    static int GetColumnSpan(const Element& element);
    static void SetColumnSpan(Element& element, int value);
    static int GetRowSpan(const Element& element);
    static void SetRowSpan(Element& element, int value);

    static const DependencyProperty& ColumnProperty();
    static const DependencyProperty& RowProperty();
    static const DependencyProperty& ColumnSpanProperty();
    static const DependencyProperty& RowSpanProperty();

    // Collections rather than values: a definition is a mutable object the
    // sizing algorithm writes its scratch state into, which is not something
    // the property store carries.
    std::vector<Definition> column_definitions;
    std::vector<Definition> row_definitions;

protected:
    Size MeasureOverride(Size available) override;
    Size ArrangeOverride(Size final_size) override;

private:
    // Which definitions a child occupies, and what kind they are. Recomputed
    // each measure because a Star definition's layout-time type depends on the
    // available size.
    struct Cell {
        int column_index = 0;
        int row_index = 0;
        int column_span = 1;
        int row_span = 1;
        bool is_auto_u = false;
        bool is_star_u = false;
        bool is_auto_v = false;
        bool is_star_v = false;
    };

    void ValidateDefinitionsLayout(std::vector<Definition>& definitions, bool treat_star_as_auto);
    void ValidateCells();
    void MeasureCellsGroup(const std::vector<int>& group, Size reference_size);
    void MeasureCell(int cell_index);
    double GetMeasureSizeForRange(const std::vector<Definition>& definitions, int start,
                                  int count) const;
    double GetFinalSizeForRange(const std::vector<Definition>& definitions, int start,
                                int count) const;
    void EnsureMinSizeInDefinitionRange(std::vector<Definition>& definitions, int start, int count,
                                        double requested_size);
    void ResolveStar(std::vector<Definition>& definitions, double available_size);
    void SetFinalSize(std::vector<Definition>& definitions, double final_size, double dpi_scale);

    std::vector<Definition>& DefinitionsU() { return columns_; }
    std::vector<Definition>& DefinitionsV() { return rows_; }

    // Working copies. An empty ColumnDefinitions/RowDefinitions collection
    // still lays out as a single implicit Star definition, which is why these
    // are not the public vectors.
    std::vector<Definition> columns_;
    std::vector<Definition> rows_;
    std::vector<Cell> cells_;
    std::vector<int> group1_, group2_, group3_, group4_;
    bool has_star_cells_u_ = false;
    bool has_star_cells_v_ = false;
};

}  // namespace openxaml

#endif  // OPENXAML_GRID_H
