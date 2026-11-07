#pragma once

#include <QColor>
#include <QEvent>
#include <QSize>
#include <QString>
#include <QWidget>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

class QButtonGroup;
class QFrame;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

/// What a drag over the mesh does while annotating.
enum class AnnotationTool { Rotate, Paint, Erase };

/// The label a stroke started with button writes: label with Paint, 0 (unlabeled)
/// with Erase or the right button. std::nullopt when the press is not a stroke: with
/// Rotate, where drags turn the view, or with another button.
std::optional<int> strokeLabel(AnnotationTool tool, Qt::MouseButton button, int label);

/// Whether a pointer event during a stroke shows that the stroke's button came up
/// unseen, as when a modal dialog opened mid-stroke (the save prompt of Q) took the
/// release: a move with no button held, or a press with no other button held. A
/// release is not one; it ends the stroke itself once no button is left.
bool strokeReleaseMissed(QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons);

/// Bar of the annotate mode: the painted array's name, the tool (Rotate, Paint,
/// Erase), a swatch per label, the brush size in rings of cells, Undo, Save as… and
/// a button that leaves the mode.
///
/// Intent-only: it emits what the user asks for and shows what the set methods push.
class AnnotationBar : public QWidget {
  Q_OBJECT

public:
  /// Labels 1 to kLabels have a swatch; 0 is unlabeled.
  static constexpr int kLabels = 9;
  static constexpr int kMaxBrushRings = 15;

  explicit AnnotationBar(QWidget* parent = nullptr);

  void setArrayName(const QString& name);
  /// colors[i] is label i + 1's.
  void setLabelColors(const std::array<QColor, kLabels>& colors);

  // Mark the state; they emit nothing.
  void setTool(AnnotationTool tool);
  void setLabel(int label);
  void setBrushRings(int rings);
  void setUndoAvailable(bool available);

  /// Wraps the groups of controls into as many rows as maxWidth needs and returns
  /// the bar's size.
  QSize fitWidth(int maxWidth);

signals:
  void toolChosen(AnnotationTool tool);
  void labelChosen(int label);
  void brushRingsChosen(int rings);
  void undoRequested();
  void saveRequested();
  void leaveRequested();

private:
  void showRings(int rings);

  QLabel* arrayName_ = nullptr;
  QButtonGroup* tools_ = nullptr;    // ids are AnnotationTool values
  QButtonGroup* swatches_ = nullptr; // ids are labels
  QSlider* brush_ = nullptr;
  QLabel* rings_ = nullptr;
  QToolButton* undo_ = nullptr;
  QVBoxLayout* rows_ = nullptr;
  std::vector<QWidget*> groups_;    // wrapped as whole units, in order
  std::vector<QFrame*> separators_; // separators_[i] precedes groups_[i + 1] in a row
  std::vector<size_t> rowStarts_;   // the first group of each row
};
