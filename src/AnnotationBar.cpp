#include "AnnotationBar.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLayoutItem>
#include <QMargins>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr int kPadding = 8;
constexpr int kGroupSpacing = 12; // on each side of a separator
constexpr int kRowSpacing = 6;
constexpr int kIconSize = 18;
constexpr int kButtonSize = 32;
constexpr int kSwatchSize = 26;
constexpr int kSliderWidth = 96;

struct ToolSegment {
  AnnotationTool tool;
  Lucide icon;
  const char* name;
  const char* toolTip;
};

constexpr ToolSegment kToolSegments[] = {
    {AnnotationTool::Rotate, Lucide::Rotate3d, "Rotate", "Rotate: drags turn the view"},
    {AnnotationTool::Paint, Lucide::Brush, "Paint", "Paint (hold A)"},
    {AnnotationTool::Erase, Lucide::Eraser, "Erase", "Erase (hold E); right-drag also erases"},
};

// A row of controls that wraps as one unit.
QWidget* makeGroup(QWidget* parent, int spacing) {
  auto* group = new QWidget(parent);
  auto* row = new QHBoxLayout(group);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(spacing);
  return group;
}

QToolButton* iconButton(QWidget* parent, Lucide icon, const QString& name, const QString& key) {
  auto* button = new QToolButton(parent);
  button->setIcon(lucideIcon(icon, kIconSize));
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setFixedSize(kButtonSize, kButtonSize);
  button->setAccessibleName(name);
  button->setToolTip(QStringLiteral("%1 (%2)").arg(name, key));
  // Keeps focus on the viewport, so the annotation keys keep working after a click.
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

// A round swatch in its label's color showing the label's number; a white ring marks
// the checked one. Painted, as style sheets do not round a fill inset from its ring.
class LabelSwatch : public QAbstractButton {
public:
  LabelSwatch(int label, QWidget* parent) : QAbstractButton(parent) {
    setText(QString::number(label));
    setCheckable(true);
    setFixedSize(kSwatchSize, kSwatchSize);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true); // repaints for the hover ring
  }

  void setColor(const QColor& color) {
    color_ = color;
    update();
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF ring = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    if (isChecked() || underMouse()) {
      painter.setPen(QPen(isChecked() ? QColor(Qt::white) : overlay::kSelectedBorder, 2.0));
      painter.drawEllipse(ring);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color_);
    painter.drawEllipse(ring.adjusted(3.0, 3.0, -3.0, -3.0));

    QFont digits = font();
    digits.setPixelSize(11);
    digits.setWeight(QFont::DemiBold);
    painter.setFont(digits);
    // Dark digits on light fills, white on the rest.
    painter.setPen(qGray(color_.rgb()) > 150 ? QColor(24, 24, 27) : QColor(Qt::white));
    painter.drawText(rect(), Qt::AlignCenter, text());
  }

private:
  QColor color_ = overlay::kIcon;
};

// A dot size wide at one end of the brush slider.
QLabel* brushDot(QWidget* parent, int size) {
  auto* dot = new QLabel(parent);
  dot->setFixedSize(size, size);
  dot->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;")
                         .arg(overlay::css(overlay::kIcon))
                         .arg(size / 2));
  return dot;
}

} // namespace

std::optional<int> strokeLabel(AnnotationTool tool, Qt::MouseButton button, int label) {
  if (tool == AnnotationTool::Rotate) {
    return std::nullopt;
  }
  if (button == Qt::RightButton) {
    return 0;
  }
  if (button != Qt::LeftButton) {
    return std::nullopt;
  }
  return tool == AnnotationTool::Erase ? 0 : label;
}

bool strokeReleaseMissed(QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons) {
  // Qt counts the button a press presses among those held.
  return (type == QEvent::MouseMove || type == QEvent::MouseButtonPress) &&
         (buttons & ~Qt::MouseButtons(button)) == Qt::NoButton;
}

AnnotationBar::AnnotationBar(QWidget* parent) : QWidget(parent) {
  setObjectName("annotationBar");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the bar's padding must not reach the viewport and paint or rotate.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setStyleSheet(
      overlay::panelStyleSheet(objectName()) +
      QStringLiteral(
          "QLabel#annotationArray { font-size: 12px; }"
          "QLabel#annotationRings { color: %1; font-size: 12px; }"
          "QToolButton#panelSegment { padding: 3px 8px; }"
          "QToolButton#annotationSave { font-size: 12px; padding: 0 10px 0 6px; }"
          "QFrame#annotationSeparator { background: %4; }"
          "QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }"
          "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
          "  background: %1; }"
          "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }")
          .arg(overlay::css(overlay::kText),
               overlay::css(overlay::kSelectedBorder),
               overlay::css(overlay::kAccent),
               overlay::css(overlay::kSurfaceBorder)));

  rows_ = new QVBoxLayout(this);
  rows_->setContentsMargins(kPadding + 4, kPadding, kPadding, kPadding);
  rows_->setSpacing(kRowSpacing);

  QWidget* title = makeGroup(this, 6);
  auto* name = new QLabel(QStringLiteral("Annotate"), title);
  name->setObjectName("panelTitle");
  title->layout()->addWidget(name);
  arrayName_ = new QLabel(title);
  arrayName_->setObjectName("annotationArray");
  title->layout()->addWidget(arrayName_);

  tools_ = new QButtonGroup(this);
  QWidget* tools = overlay::segmentedControl(this,
                                             tools_,
                                             {{static_cast<int>(AnnotationTool::Rotate), {}, {}},
                                              {static_cast<int>(AnnotationTool::Paint), {}, {}},
                                              {static_cast<int>(AnnotationTool::Erase), {}, {}}});
  for (const ToolSegment& segment : kToolSegments) {
    QAbstractButton* button = tools_->button(static_cast<int>(segment.tool));
    button->setIcon(lucideIcon(segment.icon, kIconSize));
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setAccessibleName(QString::fromUtf8(segment.name));
    button->setToolTip(QString::fromUtf8(segment.toolTip));
  }
  connect(tools_, &QButtonGroup::idClicked, this, [this](int id) {
    emit toolChosen(static_cast<AnnotationTool>(id));
  });

  QWidget* swatches = makeGroup(this, 4);
  swatches_ = new QButtonGroup(this);
  for (int label = 1; label <= kLabels; ++label) {
    auto* swatch = new LabelSwatch(label, swatches);
    swatch->setAccessibleName(QStringLiteral("Label %1").arg(label));
    swatch->setToolTip(QStringLiteral("Label %1 (%1)").arg(label));
    swatches->layout()->addWidget(swatch);
    swatches_->addButton(swatch, label);
  }
  connect(
      swatches_, &QButtonGroup::idClicked, this, [this](int label) { emit labelChosen(label); });

  QWidget* brush = makeGroup(this, 8);
  const QString brushTip =
      QStringLiteral("Brush size: rings of cells around the one under the pointer (+ −)");
  brush->setToolTip(brushTip);
  brush->layout()->addWidget(brushDot(brush, 6));
  brush_ = new QSlider(Qt::Horizontal, brush);
  brush_->setRange(0, kMaxBrushRings);
  brush_->setFixedWidth(kSliderWidth);
  brush_->setFocusPolicy(Qt::NoFocus);
  brush_->setAccessibleName(QStringLiteral("Brush size"));
  brush_->setToolTip(brushTip);
  brush->layout()->addWidget(brush_);
  brush->layout()->addWidget(brushDot(brush, 14));
  rings_ = new QLabel(brush);
  rings_->setObjectName("annotationRings");
  rings_->ensurePolished();
  // Wide enough for the longest reading, so the slider stays put as the value changes.
  rings_->setMinimumWidth(
      rings_->fontMetrics().horizontalAdvance(QStringLiteral("%1 rings").arg(kMaxBrushRings)) + 4);
  brush->layout()->addWidget(rings_);
  connect(brush_, &QSlider::valueChanged, this, [this](int rings) {
    showRings(rings);
    emit brushRingsChosen(rings);
  });

  QWidget* actions = makeGroup(this, 4);
  undo_ = iconButton(actions,
                     Lucide::Undo2,
                     QStringLiteral("Undo"),
                     QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText));
  actions->layout()->addWidget(undo_);
  connect(undo_, &QToolButton::clicked, this, &AnnotationBar::undoRequested);
  auto* save = new QToolButton(actions);
  save->setObjectName("annotationSave");
  save->setText(QStringLiteral("Save as…"));
  save->setIcon(lucideIcon(Lucide::Save, kIconSize));
  save->setIconSize(QSize(kIconSize, kIconSize));
  save->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  save->setFixedHeight(kButtonSize);
  save->setToolTip(QStringLiteral("Save the mesh with its labels to a .vtp or .vtu file"));
  save->setFocusPolicy(Qt::NoFocus);
  actions->layout()->addWidget(save);
  connect(save, &QToolButton::clicked, this, &AnnotationBar::saveRequested);
  auto* leave =
      iconButton(actions, Lucide::X, QStringLiteral("Leave annotate"), QStringLiteral("Esc"));
  actions->layout()->addWidget(leave);
  connect(leave, &QToolButton::clicked, this, &AnnotationBar::leaveRequested);

  groups_ = {title, tools, swatches, brush, actions};
  for (size_t i = 1; i < groups_.size(); ++i) {
    auto* separator = new QFrame(this);
    separator->setObjectName("annotationSeparator");
    separator->setFixedSize(1, 22);
    separators_.push_back(separator);
  }

  setTool(AnnotationTool::Rotate);
  setLabel(1);
  setBrushRings(3);
  setUndoAvailable(false);
  fitWidth(QWIDGETSIZE_MAX);
}

void AnnotationBar::setArrayName(const QString& name) {
  arrayName_->setText(name);
}

void AnnotationBar::setLabelColors(const std::array<QColor, kLabels>& colors) {
  for (int label = 1; label <= kLabels; ++label) {
    static_cast<LabelSwatch*>(swatches_->button(label))
        ->setColor(colors[static_cast<size_t>(label - 1)]);
  }
}

void AnnotationBar::setTool(AnnotationTool tool) {
  tools_->button(static_cast<int>(tool))->setChecked(true);
}

void AnnotationBar::setLabel(int label) {
  if (QAbstractButton* swatch = swatches_->button(label)) {
    swatch->setChecked(true);
  }
}

void AnnotationBar::setBrushRings(int rings) {
  {
    const QSignalBlocker block(brush_);
    brush_->setValue(rings);
  }
  showRings(brush_->value());
}

void AnnotationBar::setUndoAvailable(bool available) {
  undo_->setEnabled(available);
}

void AnnotationBar::showRings(int rings) {
  rings_->setText(rings == 1 ? QStringLiteral("1 ring") : QStringLiteral("%1 rings").arg(rings));
}

QSize AnnotationBar::fitWidth(int maxWidth) {
  const QMargins margins = rows_->contentsMargins();
  const int available = maxWidth - margins.left() - margins.right();
  const int separatorWidth = 2 * kGroupSpacing + 1;
  std::vector<size_t> starts{0};
  int x = groups_.front()->sizeHint().width();
  for (size_t i = 1; i < groups_.size(); ++i) {
    const int width = groups_[i]->sizeHint().width();
    if (x + separatorWidth + width > available) {
      starts.push_back(i);
      x = width;
    } else {
      x += separatorWidth + width;
    }
  }

  if (starts != rowStarts_) {
    // Deleting a row layout leaves its widgets to the bar, which owns them.
    while (QLayoutItem* row = rows_->takeAt(0)) {
      delete row;
    }
    for (size_t row = 0; row < starts.size(); ++row) {
      const size_t end = row + 1 < starts.size() ? starts[row + 1] : groups_.size();
      auto* line = new QHBoxLayout;
      line->setSpacing(kGroupSpacing);
      for (size_t i = starts[row]; i < end; ++i) {
        if (i > starts[row]) {
          line->addWidget(separators_[i - 1]);
        }
        line->addWidget(groups_[i]);
      }
      line->addStretch(1);
      rows_->addLayout(line);
    }
    for (size_t i = 1; i < groups_.size(); ++i) {
      separators_[i - 1]->setVisible(std::find(starts.begin(), starts.end(), i) == starts.end());
    }
    rowStarts_ = std::move(starts);
  }
  return sizeHint();
}
