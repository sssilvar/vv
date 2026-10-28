#pragma once

#include <QString>
#include <QWidget>

class QLabel;

// Space steps forward; Space with this modifier steps back. Qt reads Cmd as Ctrl
// on macOS, where Cmd+Space and Ctrl+Space are system shortcuts, so Option there.
#ifdef __APPLE__
constexpr Qt::KeyboardModifier kStepBackModifier = Qt::AltModifier;
#else
constexpr Qt::KeyboardModifier kStepBackModifier = Qt::ControlModifier;
#endif

// Bottom overlay paging through what Space cycles: previous/next buttons around
// the active scalar field's name and its position among the fields.
//
// Intent-only like PlaybackBar: it emits stepRequested and shows what setPage
// pushes back.
class ScalarPager : public QWidget {
  Q_OBJECT
public:
  explicit ScalarPager(QWidget* parent = nullptr);

  // position counts fields from 1; 0 is the plain-geometry page.
  void setPage(const QString& title, int position, int count);

signals:
  void stepRequested(int step); // -1 previous, +1 next

private:
  QLabel* title_ = nullptr;
  QLabel* counter_ = nullptr;
};
