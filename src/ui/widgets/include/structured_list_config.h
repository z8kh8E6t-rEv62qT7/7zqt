// src/ui/widgets/include/structured_list_config.h
// Role: Declarative configuration for StructuredListView / Delegate.
//
// StructuredListView is a reusable QTableView-based list widget where one column
// (`primary_interactive_column`) owns all interactive semantics (hover, selection,
// double-click, drag). Non-primary columns are display-only metadata. Nothing in
// this file is file-manager specific.

#pragma once

#include <QColor>
#include <QString>
#include <Qt>

#include <vector>

namespace z7::ui::widgets {

// Description of one column rendered by StructuredListView.
struct StructuredListColumn {
  QString id;                // Stable identifier (used for persistence/debug).
  QString header_text;       // Display name shown in header.
  int default_width = -1;    // Initial width in px; <=0 uses view default.
  Qt::Alignment alignment =  // Text alignment inside the cell.
      Qt::AlignLeft | Qt::AlignVCenter;
  bool sortable = true;              // Whether the header section is clickable.
  bool hidden_by_default = false;    // Start hidden; caller can toggle later.
};

// Visual style parameters. An invalid QColor means the element is disabled.
struct StructuredListStyle {
  // Very faint fill painted behind the primary-column text rect when the mouse
  // hovers over that specific primary cell. Invalid => no hover chip.
  QColor primary_hover_bg;
  // Fill painted behind the primary-column text rect when the row is selected.
  QColor primary_selected_bg;
  // Text color to use when drawing the primary-column label of a selected row.
  // Invalid => use palette text color.
  QColor primary_selected_text;
  // Optional whole-row faint hover background. Only painted while the mouse
  // actually hovers over the primary column (row-wide hover does not activate
  // from non-primary columns). Invalid => off.
  QColor row_hover_bg;
  // Optional grid line color (drawn at the bottom of each cell). Invalid => no
  // grid lines.
  QColor grid_line;

  // Padding inside the primary-column text rect.
  int primary_text_padding_h = 6;
  int primary_text_padding_v = 2;
  // Minimum row height in px (a UX-sized hint).
  int row_height_hint = 22;
};

// Full configuration owned by the view. Passed in via `set_config`.
struct StructuredListConfig {
  std::vector<StructuredListColumn> columns;
  // Index (into `columns`) of the primary interactive column. Must be valid.
  int primary_interactive_column = 0;
  // Whether header clicks sort the proxy.
  bool sorting_enabled = true;
  // Show the horizontal header section.
  bool show_header = true;
  StructuredListStyle style;
};

}  // namespace z7::ui::widgets
