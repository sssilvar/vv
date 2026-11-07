#pragma once

#include "ColorBarWidget.h"
#include "Colormaps.h"

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QWidget>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QScrollArea;
class QToolButton;

/// "Sequential", "Diverging" or "Cyclic".
QString colormapGroupName(ColormapGroup group);

/// The colormap's gradient, left to right, in a rounded rect of size.
QIcon colormapIcon(const Colormap& colormap, bool reversed, const QSize& size);

/// Histogram of a field's values over its full range, with a handle at each end of
/// the clip window to drag. Bars inside the window take their colormap color; the
/// rest are dimmed.
class RangeHistogram : public QWidget {
  Q_OBJECT
  friend class UiTests; // reads handle positions to drive them with simulated input

public:
  explicit RangeHistogram(QWidget* parent = nullptr);

  /// counts[i] covers the i-th of counts.size() equal bins from min to max.
  void setRange(double min, double max, std::vector<size_t> counts);

  /// Clamped to the range; does not emit clipRangeChanged.
  void setClipRange(double lower, double upper);

  void setColormap(const ColormapChoice& colormap);

  const std::array<double, 2>& range() const {
    return range_;
  }
  const std::array<double, 2>& clipRange() const {
    return clip_;
  }

  QSize sizeHint() const override;

signals:
  /// The user dragged a handle.
  void clipRangeChanged(double lower, double upper);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

private:
  enum class Handle { None, Lower, Upper };

  QRectF plotRect() const; // the bars, inset so a handle at either end stays whole
  double valueToX(double value) const;
  double xToValue(double x) const;
  Handle handleAt(const QPointF& pos) const;

  std::vector<size_t> counts_;
  std::array<double, 2> range_{0.0, 1.0};
  std::array<double, 2> clip_{0.0, 1.0};
  ColormapChoice colormap_;
  Handle drag_ = Handle::None;
  Handle hover_ = Handle::None;
};

/// Color settings of the field shown. A continuous field's: a button per colormap, grouped
/// Sequential, Diverging and Cyclic, each showing its gradient, a Reverse box, then its
/// range: a histogram with the clip window's handles, Min and Max fields, and how values
/// outside the window draw. A categorical field's: a tile per palette showing its colors,
/// then the categories, each opening a color menu like the legend's.
class ColorPanel : public QWidget {
  Q_OBJECT

public:
  explicit ColorPanel(QWidget* parent = nullptr);
  ~ColorPanel() override;

  /// Field name under the header, elided in the middle when it does not fit.
  void setField(const QString& name);

  /// Show a continuous field's sections with the colormap, Reverse and Outside range in
  /// use marked; emits nothing.
  void setColormap(const ColormapChoice& colormap);

  /// Show a categorical field's sections: a tile per palette with current checked, then a
  /// (label, color) row per category in ascending value order. Emits nothing.
  void setCategories(const std::vector<PaletteOption>& palettes,
                     const QString& current,
                     const std::vector<std::pair<QString, QColor>>& entries);

  /// The field's full range and the histogram of its values over it.
  void setRange(double min, double max, std::vector<size_t> histogram);

  /// The clip window, shown by the histogram's handles and the Min and Max fields;
  /// emits nothing.
  void setClipRange(double lower, double upper);

  /// All content, unscrolled.
  QSize sizeHint() const override;

  /// The size to show at most maxHeight tall: shorter content scrolls, and the panel
  /// widens by the scroll bar so the content keeps its width.
  QSize fittedSize(int maxHeight) const;

signals:
  /// The user clicked a colormap or toggled Reverse. id is a Colormap::id.
  void colormapChosen(const QString& id, bool reversed);

  /// The user picked Clamp or Gray for values outside the clip window.
  void outOfRangeChosen(OutOfRange outside);

  /// The user dragged a handle, typed Min or Max, or asked for the data range.
  void clipRangeChanged(double lower, double upper);

  /// Typed bounds keep meaningful digits; Use data range restores rounding.
  void clipValueFormatChanged(ColorBarWidget::Bound bound, ColorBarWidget::ValueFormat format);

  /// The user clicked a palette tile; id is a PaletteOption id.
  void paletteChosen(const QString& id);

  /// The user picked a color for the category in row entry, or std::nullopt to reset it.
  void categoryColorChanged(int entry, const std::optional<QColor>& color);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  // Whether showBounds overwrites a field holding text the user typed and not yet
  // committed: a refresh from the owner (a playback frame) keeps it.
  enum class TypedText { Keep, Replace };
  enum class Kind { Continuous, Categorical };

  void showKind(Kind kind);

  void updatePreviews();            // gradients follow Reverse
  void showBounds(TypedText typed); // Min and Max fields from the histogram's clip window
  void commitBounds();              // typed Min and Max, applied when valid
  void applyClip(double lower, double upper);

  QScrollArea* scroll_ = nullptr;
  QLabel* field_ = nullptr;
  QCheckBox* reverse_ = nullptr;
  QButtonGroup* buttons_ = nullptr; // ids index colormaps()
  RangeHistogram* histogram_ = nullptr;
  QLineEdit* min_ = nullptr;
  QLineEdit* max_ = nullptr;
  QButtonGroup* outside_ = nullptr; // ids are OutOfRange values

  QWidget* continuous_ = nullptr;  // colormaps and range
  QWidget* categorical_ = nullptr; // palettes and categories
  QGridLayout* paletteGrid_ = nullptr;
  QButtonGroup* paletteTiles_ = nullptr; // ids index paletteIds_
  std::vector<QString> paletteIds_;
  QScrollArea* categoryScroll_ = nullptr;
  QGridLayout* categoryGrid_ = nullptr;
  std::vector<QToolButton*> categoryRows_; // the first entries_.size() show
  std::vector<std::pair<QString, QColor>> entries_;
};
