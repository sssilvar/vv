#pragma once

#include <QColor>
#include <QString>
#include <array>
#include <cstddef>
#include <initializer_list>

class QButtonGroup;
class QLabel;
class QMenu;
class QWidget;

// One look for everything floating over the 3-D view: the rail, the panels beside
// it, the legend, the field pager and the playback bar.
namespace overlay {

inline const QColor kSurface(22, 22, 25, 235);
// A popup over other overlays, which would show through kSurface.
inline const QColor kPopupSurface(22, 22, 25);
inline const QColor kSurfaceBorder(255, 255, 255, 22);
inline const QColor kHover(255, 255, 255, 18);
inline const QColor kSelected(255, 255, 255, 34);
inline const QColor kSelectedBorder(255, 255, 255, 42);
inline const QColor kText(237, 237, 240);
inline const QColor kTextSecondary(161, 161, 170);
inline const QColor kIcon(201, 201, 207);
inline const QColor kIconChecked(255, 255, 255);
inline const QColor kIconDisabled(92, 92, 102);
inline const QColor kAccent(80, 150, 250);
// The accent as a thin line or a glyph on a dark fill, where kAccent reads too dim.
inline const QColor kAccentBright(0x8F, 0xBC, 0xFF);
inline const QColor kBadgeFill(255, 255, 255, 15);
// Marks a message about something that failed.
inline const QColor kError(0xF2, 0x77, 0x7A);

// Tell files apart on the Layers badges, cycling past the last; never color data with them.
inline const std::array<QColor, 6> kIdentityColors{QColor(0x50, 0x96, 0xFA),
                                                   QColor(0xE0, 0xA4, 0x58),
                                                   QColor(0xB3, 0x92, 0xF0),
                                                   QColor(0x4F, 0xC1, 0xA6),
                                                   QColor(0xF2, 0x77, 0x7A),
                                                   QColor(0xB5, 0xCC, 0x6A)};
// Text on an identity color: dark, which reads on every one of them.
inline const QColor kIdentityText(0x0C, 0x0C, 0x0E);

inline QColor identityColor(size_t index) {
  return kIdentityColors[index % kIdentityColors.size()];
}

constexpr int kPanelRadius = 14;

// Style sheets take #AARRGGBB, which keeps the alpha that rgba() would need spelled out.
inline QString css(const QColor& color) {
  return color.name(QColor::HexArgb);
}

// The opaque color of a translucent border painted over fill on the popup surface.
// Style sheets take it for every border: they draw a rounded border as arcs and edges
// that overlap where they meet, so a translucent one shows a brighter square at each join.
QColor solidBorder(const QColor& border, const QColor& fill = Qt::transparent);

// A field value for probe readings and other overlays without a display range.
inline QString formatValue(double value) {
  return QString::number(value, 'g', 4);
}

// Style sheet of a panel beside the rail whose object name is name: the card, its
// title (object name panelTitle) and section labels (panelSection), buttons that
// show an accent outline when checked, segmented controls and a thin vertical
// scroll bar.
QString panelStyleSheet(const QString& name);

// A popup menu over the view: the overlay surface with rounded corners.
void styleMenu(QMenu* menu);

// A pill for a short count or position such as "1 / 12": 11 px secondary text with
// tabular digits on a subtle fill.
QLabel* badge(QWidget* parent);

QString identityBadgeStyle(const QColor& fill, const QColor& text);
QLabel* identityBadge(QWidget* parent, const QString& letter, const QColor& color);

// Raise in back-to-front order, independently of widget creation order.
void stackOverlays(std::initializer_list<QWidget*> widgets);

struct Segment {
  int id;
  QString text;
  QString toolTip;
};

// A track of exclusive buttons, each added to group under its id.
QWidget*
segmentedControl(QWidget* parent, QButtonGroup* group, std::initializer_list<Segment> segments);

} // namespace overlay
