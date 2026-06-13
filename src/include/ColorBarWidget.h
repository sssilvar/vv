#pragma once

#include <QColor>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QWidget>
#include <QtGlobal>
#include <utility>
#include <vector>

/// A vertical colorbar widget with draggable clip handles, inspired by
/// the mapping-system UIs of CARTO and RHYTHMIA.
///
/// Features:
///   - Full-range gradient with clip-range compression (hue 0→0.8)
///   - Upper/lower clip handles (draggable)
///   - Double-click handle for in-place numeric editing
///   - Tick marks and global min/max labels
///   - Title label
class ColorBarWidget : public QWidget {
  Q_OBJECT
public:
  explicit ColorBarWidget(QWidget* parent = nullptr);

  /// Set the full scalar range (global min/max).
  void setRange(double min, double max);

  /// Set the current clip window.  Values are clamped to [globalMin, globalMax].
  void setClipRange(double lower, double upper);

  /// Title displayed above the bar (e.g. scalar name).
  void setTitle(const QString& title);

  /// Switch to categorical (indexed) display mode.
  /// Entries are (value, color, label) sorted by value ascending.
  /// Clip handles are hidden; min/max labels still shown.
  void setCategorical(const std::vector<std::pair<QString, QColor>>& entries);

  /// Restore continuous gradient mode.
  void clearCategorical();

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

signals:
  /// Emitted whenever the user moves a handle or edits a value.
  void clipRangeChanged(double lower, double upper);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEnterEvent* event) override;
#else
  void enterEvent(QEvent* event) override;
#endif
  void leaveEvent(QEvent* event) override;

private:
  // ── data ──────────────────────────────────────────────────────────
  double globalMin_ = 0.0;
  double globalMax_ = 1.0;
  double clipMin_ = 0.0;
  double clipMax_ = 1.0;
  QString title_;

  // categorical mode
  bool categorical_ = false;
  std::vector<std::pair<QString, QColor>> catEntries_; // (label, color), bottom→top order

  // ── interaction state ─────────────────────────────────────────────
  enum Handle { None, Lower, Upper };
  Handle dragHandle_ = None;
  Handle hoverHandle_ = None;
  Handle editHandle_ = None;
  QLineEdit* inlineEditor_ = nullptr;

  // ── helpers ───────────────────────────────────────────────────────
  QRectF barRect() const;
  double valueToY(double value) const;
  double yToValue(double y) const;
  Handle hitTestHandle(const QPointF& pos, double tolerance = 10.0) const;
  void emitClipChanged();
  void showInlineEditor(Handle handle);
  void commitInlineEditor();
};
