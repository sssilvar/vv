#pragma once

#include <QString>
#include <QWidget>
#include <cstddef>

class QLabel;
class QToolButton;

// Space with this modifier steps back through the fields. Qt reads Cmd as Ctrl on macOS,
// where Cmd+Space and Ctrl+Space are system shortcuts, so Option there.
#ifdef __APPLE__
constexpr Qt::KeyboardModifier kStepBackModifier = Qt::AltModifier;
#else
constexpr Qt::KeyboardModifier kStepBackModifier = Qt::ControlModifier;
#endif

enum class SpaceAction { PlayPause, NextField, PreviousField };

// What Space does with these modifiers held: with kStepBackModifier it steps back through
// the fields. Otherwise, in a time series, it plays and pauses and Shift+Space steps to
// the next field; without one, it steps to the next field.
SpaceAction spaceAction(Qt::KeyboardModifiers modifiers, bool timeSeries);

// What the -e grid's pager names a page of count fields from field first (counted from 0)
// of total: "Fields 1–6 of 12", or "Field 13 of 13" for one.
QString facetPageLabel(std::size_t first, std::size_t count, std::size_t total);

// Where the pager sits: in a capsule of its own at the bottom of the view, or at the left
// of the playback bar, where Space plays and Shift+Space steps to the next field.
enum class PagerPlacement { Capsule, PlaybackBar };

// Pages through what Space cycles: previous/next buttons around the active scalar field's
// name, its kind under it and its position among the fields in a badge, at the capsule's
// right or after the kind in the playback bar. The name is a button that asks for the
// list of every field.
//
// Intent-only like PlaybackBar: it emits stepRequested and listRequested and shows
// what setPage pushes back.
class ScalarPager : public QWidget {
  Q_OBJECT
public:
  explicit ScalarPager(PagerPlacement placement, QWidget* parent = nullptr);

  // position counts fields from 1; 0 is the plain-geometry page. detail is the
  // page's kind, e.g. "Points · continuous".
  void setPage(const QString& title, const QString& detail, int position, int count);

  // What the step buttons say they step to, "Previous field" and "Next field" at first.
  void setStepNames(const QString& previous, const QString& next);

signals:
  void stepRequested(int step); // -1 previous, +1 next
  void listRequested();

private:
  void fitName(); // the name button to the labels laid out inside it

  PagerPlacement placement_;
  QToolButton* previous_ = nullptr;
  QToolButton* next_ = nullptr;
  QString previousKeys_; // the step keys, after the step's name in its tooltip
  QString nextKeys_;
  QToolButton* name_ = nullptr;
  QLabel* title_ = nullptr;
  QLabel* detail_ = nullptr;
  QLabel* position_ = nullptr;
};
