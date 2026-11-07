#pragma once

#include "Colormaps.h"

#include <QColor>
#include <QContextMenuEvent>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <QtGlobal>
#include <array>
#include <optional>
#include <utility>
#include <vector>

// Component type of QColor's *F() API: float in Qt 6, qreal (double) in Qt 5.
using QColorComponent = decltype(std::declval<QColor>().redF());

// QMouseEvent::localPos() is deprecated in Qt 6, position() absent in Qt 5.
inline QPointF mouseLocalPos(const QMouseEvent* ev) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return ev->position();
#else
  return ev->localPos();
#endif
}

/// A palette a categorical field can take, as the Color panel and the legend's menu offer it.
struct PaletteOption {
  QString id; // a CategoryPalette id, or kCustomPaletteId
  QString label;
  QString detail;             // for its tooltip
  std::vector<QColor> colors; // the field's first categories under it
};

/// colors side by side in a rounded rect of size.
QIcon paletteIcon(const std::vector<QColor>& colors, const QSize& size);

/// What was picked in a category's color menu.
struct CategoryMenuPick {
  enum class Kind { Color, Reset, Palette };
  Kind kind = Kind::Color;
  QColor color;    // Kind::Color
  QString palette; // Kind::Palette: a PaletteOption id
};

/// Runs the color menu of the category named label, now colored current, at globalPos:
/// swatches, More colors…, Reset to default and, unless palettes is empty, a Palette
/// submenu with currentPalette checked. std::nullopt when it closes without a pick.
std::optional<CategoryMenuPick> execCategoryMenu(QWidget* parent,
                                                 const QString& label,
                                                 const QColor& current,
                                                 const QPoint& globalPos,
                                                 const std::vector<PaletteOption>& palettes,
                                                 const QString& currentPalette);

/// How much a legend shows, from the whole card down to a strip for a small facet panel.
enum class LegendForm {
  Full,    // the name, the kind under it, then a row per category or the bar over its values
  Compact, // the name and kind on one line, then category chips or the bar between its values
  Minimal, // swatches, or the bar between its values; the name is in the tooltip
};

/// The form of the legend in a facet panel of this size, in logical pixels: Full from
/// 560 x 400, Compact from 260 x 160, Minimal below.
LegendForm legendFormFor(const QSize& panel);

/// Legend card for a colored field: the field name, a one-line summary, then
/// either one row per category or a gradient bar across the full range.
///
///   - Click the gradient or right-click the card to choose or reverse its colormap.
///   - Drag a bar handle to clip the color range; double-click a clip value to edit it.
///   - Click a category's swatch, or right-click its row, to pick its color or the
///     field's palette.
///
/// The compact and minimal forms show what fits: categories as chips or swatches, with a
/// +N chip whose tooltip lists the rest.
class ColorBarWidget : public QWidget {
  Q_OBJECT
  friend class ViewerTests;
  friend class UiTests; // reads handle positions to drive them with simulated input

public:
  enum class Bound { Lower, Upper };
  Q_ENUM(Bound)
  enum class ValueFormat { Rounded, Exact };
  Q_ENUM(ValueFormat)

  explicit ColorBarWidget(QWidget* parent = nullptr);
  ~ColorBarWidget() override;

  /// Set the full scalar range (global min/max).
  void setRange(double min, double max);

  /// Set the current clip window.  Values are clamped to [globalMin, globalMax].
  void setClipRange(double lower, double upper);

  /// Keep a typed bound's meaningful digits, or restore range-based rounding. Emits nothing.
  void setClipValueFormat(Bound bound, ValueFormat format);

  /// Field name, elided in the middle when it does not fit.
  void setTitle(const QString& title);

  /// Line under the title, e.g. "Cells · 3 categories".
  void setSubtitle(const QString& subtitle);

  /// Switch to categorical mode: one (label, color) row per category, in
  /// ascending value order. The clip handles are hidden.
  void setCategorical(const std::vector<std::pair<QString, QColor>>& entries);

  /// Restore continuous gradient mode.
  void clearCategorical();

  /// The palettes a category's menu offers, with the field's current one checked; none
  /// until set.
  void setPalettes(std::vector<PaletteOption> palettes, const QString& current);

  /// Colormap of the continuous gradient and its color outside the clip window, as
  /// the mesh is drawn with them.
  void setColormap(const ColormapChoice& colormap);

  void setForm(LegendForm form);
  LegendForm form() const {
    return form_;
  }

  /// The card's size in its form inside bounds: Full and Compact give up category rows,
  /// and every form narrows, to stay inside.
  QSize fittedSize(const QSize& bounds) const;

  /// Sets the form for a facet panel of this size and returns the card's size there: at
  /// most a quarter of the panel, below its middle and margin inside its other edges.
  /// Category cards use at most a fifth of its height when a row fits.
  QSize fitPanel(const QSize& panel, int margin);

  /// Card size that shows every category row, or chip in the compact form.
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  void sizeHintChanged();

  /// Emitted whenever the user moves a handle or edits a value.
  void clipRangeChanged(double lower, double upper);

  void clipValueFormatChanged(ColorBarWidget::Bound bound, ColorBarWidget::ValueFormat format);

  void colormapChosen(const QString& id, bool reversed);

  /// Emitted when the user picks a color for a categorical swatch, or
  /// std::nullopt to reset it to its default. entry indexes the list passed to
  /// setCategorical, which the owner calls again to show the new color.
  void categoryColorChanged(int entry, const std::optional<QColor>& color);

  /// Emitted when the user picks a palette in a category's menu; id is a PaletteOption id.
  void paletteChosen(const QString& id);

protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  double globalMin_ = 0.0;
  double globalMax_ = 1.0;
  double clipMin_ = 0.0;
  double clipMax_ = 1.0;
  std::array<ValueFormat, 2> clipValueFormats_{};
  QString title_;
  QString subtitle_;

  bool categorical_ = false;
  LegendForm form_ = LegendForm::Full;
  ColormapChoice colormap_;
  std::vector<std::pair<QString, QColor>> catEntries_; // (label, color), ascending value
  std::vector<PaletteOption> palettes_;
  QString currentPalette_;

  enum Handle { None, Lower, Upper };
  Handle dragHandle_ = None;
  Handle hoverHandle_ = None;
  Handle editHandle_ = None;
  int hoverEntry_ = -1;
  QLineEdit* inlineEditor_ = nullptr;
  std::array<double, 2> valueColumns_{}; // lower and upper values beside the bar

  // Where each category shows, an empty rectangle for one left out, and the +N chip.
  struct CategoryLayout {
    std::vector<QRectF> entries;
    QRectF more; // empty when every category shows
    int hidden = 0;
    int rows = 0;
  };

  QFont titleFont() const;
  QFont labelFont() const;
  QFont smallFont() const;
  double pad() const;
  double knobHeight() const;
  double headerHeight() const; // the compact form's line holding the name and kind
  double bodyTop() const;      // below the title and subtitle
  double fullHeight() const;   // of the Full form with every category row
  QRectF bodyRect() const;
  QRectF barRect() const;
  bool valuesBeside() const; // the clip values fit beside the bar
  QString valueText(double value) const;
  QString roundedValueText(double value) const;
  void updateValueColumns();
  double valueToX(double value) const;
  double xToValue(double x) const;
  std::array<QRectF, 2> clipValueRects() const; // lower, upper; empty when not shown
  Handle hitTestValue(const QPointF& pos) const;
  Handle hitTestHandle(const QPointF& pos) const;
  int categoryRows() const; // Full form: rows per column; columns appear once rows get too short
  QRectF categoryRect(int entry) const; // Full form
  CategoryLayout categoryLayout() const;
  // Compact chips or minimal swatches flowed into body, at most maxRows rows of them.
  CategoryLayout flowCategories(const QRectF& body, int maxRows) const;
  double chipWidth(int entry) const;
  QRectF swatchRect(const CategoryLayout& layout, int entry) const;
  int categoryAt(const CategoryLayout& layout, const QPointF& pos) const; // -1 outside every one
  QString hiddenCategories(const CategoryLayout& layout) const;

  void showCategoryMenu(int entry, const QPoint& globalPos);
  void showColormapMenu(const QPoint& globalPos);
  void showOverflowMenu(const CategoryLayout& layout, const QPoint& globalPos);
  void setClipValue(Handle handle, double value);
  void showInlineEditor(Handle handle);
  void commitInlineEditor();
};

// What Save snapshot writes: frame, the view's framebuffer at devicePixelRatio, with the
// mesh identities and legends painted where they sit on the view, as they look on screen.
QImage snapshotWithLegends(QImage frame,
                           qreal devicePixelRatio,
                           const std::vector<ColorBarWidget*>& legends,
                           const std::vector<QWidget*>& panels = {});
