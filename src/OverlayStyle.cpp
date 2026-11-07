#include "OverlayStyle.h"

#include "LucideIcon.h"

#include <QButtonGroup>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QWidget>

namespace overlay {

namespace {
class MenuStyle final : public QProxyStyle {
public:
  void drawPrimitive(PrimitiveElement element,
                     const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget) const override {
    if (element != PE_IndicatorMenuCheckMark && element != PE_IndicatorArrowRight &&
        element != PE_IndicatorArrowLeft) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }
    const Lucide icon = element == PE_IndicatorMenuCheckMark ? Lucide::Check
                        : element == PE_IndicatorArrowLeft   ? Lucide::ChevronLeft
                                                             : Lucide::ChevronRight;
    const QColor color = option->state.testFlag(State_Enabled) ? kText : kIconDisabled;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const int size = qMin(option->rect.width(), option->rect.height());
    painter->translate(option->rect.center().x() - size / 2.0,
                       option->rect.center().y() - size / 2.0);
    painter->scale(size / 24.0, size / 24.0);
    painter->strokePath(lucidePath(icon),
                        QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->restore();
  }
};

// top painted over the opaque bottom.
QColor over(const QColor& top, const QColor& bottom) {
  const int alpha = top.alpha();
  const auto mix = [alpha](int upper, int lower) {
    return (upper * alpha + lower * (255 - alpha) + 127) / 255;
  };
  return {mix(top.red(), bottom.red()),
          mix(top.green(), bottom.green()),
          mix(top.blue(), bottom.blue())};
}
} // namespace

QColor solidBorder(const QColor& border, const QColor& fill) {
  return over(border, over(fill, kPopupSurface));
}

QString panelStyleSheet(const QString& name) {
  return QStringLiteral(
             "QWidget#%1 { background: %2; border: 1px solid %3; border-radius: %4px; }"
             "QLabel { color: %5; font-size: 11px; }"
             "QLabel#panelTitle { color: %6; font-size: 13px; font-weight: 600; }"
             "QLabel#panelSection { font-weight: 600; }"
             "QCheckBox { color: %6; font-size: 12px; }"
             "QToolButton { color: %6; font-size: 11px; background: transparent;"
             "  border: 1px solid transparent; border-radius: 8px; }"
             "QToolButton:hover { background: %7; }"
             "QToolButton:checked { background: %8; border-color: %9; }"
             "QWidget#panelSegments { background: %7; border-radius: 8px; }"
             "QToolButton#panelSegment { font-size: 12px; padding: 2px 12px;"
             "  border-radius: 6px; }"
             "QToolButton#panelSegment:checked { border-color: transparent; }"
             "QScrollBar:vertical { background: transparent; width: 8px;"
             "  margin: 10px 2px 10px 0; }"
             "QScrollBar::handle:vertical { background: %10; border-radius: 3px;"
             "  min-height: 24px; }"
             "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
             "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
             "  background: none; }")
      .arg(name,
           css(kPopupSurface),
           css(solidBorder(kSurfaceBorder)),
           QString::number(kPanelRadius),
           css(kTextSecondary),
           css(kText),
           css(kHover),
           css(kSelected),
           css(kAccent),
           css(kSelectedBorder));
}

void styleMenu(QMenu* menu) {
  auto* style = new MenuStyle;
  style->setParent(menu);
  menu->setStyle(style);
  // Lets the rounded corners show the view behind instead of a square frame.
  menu->setAttribute(Qt::WA_TranslucentBackground);
  menu->setStyleSheet(QStringLiteral("QMenu { background: %1; border: 1px solid %2;"
                                     "  border-radius: 10px; padding: 6px; }"
                                     "QMenu::item { color: %3; padding: 7px 28px 7px 12px;"
                                     "  border-radius: 6px; }"
                                     "QMenu::item:exclusive:checked { background: %4; }"
                                     "QMenu::item:selected { background: %4; }"
                                     "QMenu::item:disabled { color: %5; background: transparent; }"
                                     "QMenu::indicator { width: 16px; height: 16px; left: 6px; }"
                                     "QMenu::right-arrow, QMenu::left-arrow {"
                                     "  width: 16px; height: 16px; right: 6px; }"
                                     "QMenu::separator { height: 1px; background: %2;"
                                     "  margin: 7px 8px; }")
                          .arg(css(kPopupSurface),
                               css(solidBorder(kSurfaceBorder)),
                               css(kText),
                               css(kHover),
                               css(kIconDisabled)));
}

QString identityBadgeStyle(const QColor& fill, const QColor& text) {
  return QStringLiteral("background: %1; color: %2; border-radius: 6px;"
                        "font-size: 11px; font-weight: 700; padding: 0 4px;")
      .arg(css(fill), css(text));
}

QLabel* identityBadge(QWidget* parent, const QString& letter, const QColor& color) {
  auto* label = new QLabel(letter, parent);
  label->setAlignment(Qt::AlignCenter);
  label->setMinimumWidth(22);
  label->setFixedHeight(22);
  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  label->setStyleSheet(identityBadgeStyle(color, kIdentityText));
  return label;
}

void stackOverlays(std::initializer_list<QWidget*> widgets) {
  for (QWidget* widget : widgets) {
    if (widget) {
      widget->raise();
    }
  }
}

QLabel* badge(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; font-size: 11px;"
                                      "  border-radius: 6px; padding: 3px 7px; }")
                           .arg(css(kTextSecondary), css(kBadgeFill)));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  // Tabular digits keep "1 / 12" and "8 / 12" the same width; older Qt has no font features.
  QFont font = label->font();
  font.setFeature(QFont::Tag("tnum"), 1);
  label->setFont(font);
#endif
  return label;
}

QWidget*
segmentedControl(QWidget* parent, QButtonGroup* group, std::initializer_list<Segment> segments) {
  auto* track = new QWidget(parent);
  track->setObjectName("panelSegments");
  track->setAttribute(Qt::WA_StyledBackground, true);
  auto* row = new QHBoxLayout(track);
  row->setContentsMargins(2, 2, 2, 2);
  row->setSpacing(2);
  for (const Segment& segment : segments) {
    auto* button = new QToolButton(track);
    button->setObjectName("panelSegment");
    button->setText(segment.text);
    button->setToolTip(segment.toolTip);
    button->setCheckable(true);
    // Keeps focus on the viewport, so Space keeps cycling fields after a click.
    button->setFocusPolicy(Qt::NoFocus);
    row->addWidget(button);
    group->addButton(button, segment.id);
  }
  return track;
}

} // namespace overlay
