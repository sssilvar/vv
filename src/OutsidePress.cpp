#include "OutsidePress.h"

#include <QEvent>
#include <algorithm>
#include <utility>

OutsidePress::OutsidePress(QWidget* view,
                           std::initializer_list<QWidget*> overlays,
                           std::function<void()> onOutside,
                           QObject* parent)
    : QObject(parent), view_(view), onOutside_(std::move(onOutside)) {
  for (QWidget* overlay : overlays) {
    if (overlay) {
      overlays_.emplace_back(overlay);
    }
  }
}

bool OutsidePress::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
    const auto* target = static_cast<QWidget*>(watched);
    // isAncestorOf stops at window boundaries: a press on a popup menu is not one on the view.
    const auto holds = [target](const QPointer<QWidget>& widget) {
      return widget && (widget == target || widget->isAncestorOf(target));
    };
    if (holds(view_) && std::none_of(overlays_.begin(), overlays_.end(), holds)) {
      onOutside_();
    }
  }
  return QObject::eventFilter(watched, event);
}
