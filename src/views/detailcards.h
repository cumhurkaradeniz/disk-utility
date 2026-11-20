// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "views/detailcontent.h"

#include "ui/view.h"

#include <vector>

namespace cutefish::ui {
class Label;
class ProgressBar;
class ThemeIcon;
}

namespace views {

// The space as one rounded bar of segments, a hairline of the card between
// neighbours. Named "detail:usage".
class UsageBar : public cutefish::ui::View
{
public:
    explicit UsageBar(std::vector<UsageSegment> segments);

    cutefish::ui::SizeF sizeHint() const override;

protected:
    void paint(cutefish::ui::Painter& painter) override;

private:
    std::vector<UsageSegment> m_segments;
};

// The colour of a segment, on the bar and in the legend.
cutefish::ui::Color segmentColor(const UsageSegment& segment);

// The card at the top of the detail pane: the large icon, the name and the
// subtitle, the usage bar with its legend, and the job working on the
// selection with its progress.
class SummaryCard : public cutefish::ui::View
{
public:
    explicit SummaryCard(const DetailContent& content);

    double heightForWidth(double width) const override;
    cutefish::ui::SizeF sizeHint() const override { return {0, heightForWidth(0)}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    class LegendItem;

    // The legend's rows, laid out for a width: where each item goes.
    std::vector<cutefish::ui::RectF> legendPlaces(double width) const;
    double headTop() const;

    cutefish::ui::ThemeIcon* m_icon;
    cutefish::ui::Label* m_name;
    cutefish::ui::Label* m_subtitle;
    UsageBar* m_bar = nullptr;
    std::vector<LegendItem*> m_legend;
    cutefish::ui::Label* m_job = nullptr;
    cutefish::ui::Label* m_percent = nullptr;
    cutefish::ui::ProgressBar* m_progress = nullptr;
};

// The information grid in Settings' card rows, two columns side by side
// when there is room, a hairline between rows and between the columns.
class InfoGrid : public cutefish::ui::View
{
public:
    InfoGrid(const std::vector<InfoItem>& left, const std::vector<InfoItem>& right);

    double heightForWidth(double width) const override;
    cutefish::ui::SizeF sizeHint() const override { return {0, heightForWidth(0)}; }

protected:
    void layout() override;
    void paint(cutefish::ui::Painter& painter) override;

private:
    struct Cell
    {
        cutefish::ui::Label* title;
        cutefish::ui::Label* value;
    };

    bool twoColumns(double width) const;
    size_t rowCount(double width) const;

    std::vector<Cell> m_left;
    std::vector<Cell> m_right;
};

} // namespace views
