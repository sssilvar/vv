#pragma once

#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <initializer_list>
#include <vector>

// Watches every press on a view and its children, application-wide once installed with
// QApplication::installEventFilter, and calls onOutside for each one that lands outside
// the given overlays and their children. It never takes the press, which still does its
// own job: dismissing a panel this way costs no extra click.
class OutsidePress : public QObject {
public:
  // Null overlays are skipped, so optional ones can be passed as they are. An overlay
  // must stop presses from propagating (WA_NoMousePropagation, as every overlay over the
  // view does so it never rotates the camera); one that reaches the view counts there.
  OutsidePress(QWidget* view,
               std::initializer_list<QWidget*> overlays,
               std::function<void()> onOutside,
               QObject* parent = nullptr);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  QPointer<QWidget> view_;
  std::vector<QPointer<QWidget>> overlays_;
  std::function<void()> onOutside_;
};
