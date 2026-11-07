#include "KeyHints.h"

#include "OverlayStyle.h"

#include <QHBoxLayout>
#include <QLabel>

KeyHints::KeyHints(const std::vector<KeyHint>& hints, QWidget* parent) : QWidget(parent) {
  setObjectName("keyHints");
  setAttribute(Qt::WA_StyledBackground, true);
  // Painting goes on under the strip.
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);
  setStyleSheet(QStringLiteral("QWidget#keyHints { background: %1; border: 1px solid %2;"
                               "  border-radius: 10px; }"
                               "QLabel { color: %3; font-size: 11px; }"
                               "QLabel#keyCap { color: %4; border: 1px solid %5;"
                               "  border-radius: 4px; padding: 0 4px; }")
                    .arg(overlay::css(overlay::kSurface),
                         overlay::css(overlay::solidBorder(overlay::kSurfaceBorder)),
                         overlay::css(overlay::kTextSecondary),
                         overlay::css(overlay::kText),
                         overlay::css(overlay::solidBorder(overlay::kSelectedBorder))));

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(12, 6, 12, 6);
  row->setSpacing(4);
  for (size_t i = 0; i < hints.size(); ++i) {
    if (i > 0) {
      row->addSpacing(12);
    }
    for (const QString& key : hints[i].keys) {
      auto* cap = new QLabel(key, this);
      cap->setObjectName("keyCap");
      row->addWidget(cap);
    }
    row->addSpacing(2);
    row->addWidget(new QLabel(hints[i].action, this));
  }
}
