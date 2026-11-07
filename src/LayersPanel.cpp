#include "LayersPanel.h"

#include "ColorBarWidget.h"
#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

namespace {

constexpr int kWidth = 300;
constexpr int kPadding = 12;
constexpr int kFileRowHeight = 40;
constexpr int kPartRowHeight = 30;
constexpr int kEyeSize = 28;
constexpr int kIconSize = 16;
constexpr int kBadgeSize = 22;
constexpr int kBadgeGap = 10; // badge to name, and where a part row's dot starts
constexpr int kDotSize = 10;
constexpr int kDimmedAlpha = 90; // a hidden file's badge, a hidden part's dot
constexpr size_t kMaxExpandedParts = 8;

#ifdef Q_OS_MACOS
const QString kSoloClick = QStringLiteral("Option-click");
#else
const QString kSoloClick = QStringLiteral("Alt-click");
#endif

// Keeps the modifiers of the click it reports, so an Option-click can solo.
class EyeButton : public QToolButton {
public:
  using QToolButton::QToolButton;

  Qt::KeyboardModifiers clickModifiers() const {
    return modifiers_;
  }

protected:
  void mouseReleaseEvent(QMouseEvent* event) override {
    modifiers_ = event->modifiers();
    QToolButton::mouseReleaseEvent(event);
  }

private:
  Qt::KeyboardModifiers modifiers_;
};

// Its text shortened in the middle to whatever width the layout leaves it.
class ElidedLabel : public QLabel {
public:
  ElidedLabel(const QString& text, QWidget* parent) : QLabel(text, parent), text_(text) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // The row under it takes the clicks, the right-click and the tooltip.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QLabel::resizeEvent(event);
    setText(fontMetrics().elidedText(text_, Qt::ElideMiddle, width()));
  }

private:
  QString text_;
};

QColor withAlpha(QColor color, int alpha) {
  color.setAlpha(alpha);
  return color;
}

// Repolishes on a change only, so the style sheet's [dimmed="true"] rule applies.
void setDimmed(QWidget* widget, bool dimmed) {
  if (widget->property("dimmed").toBool() == dimmed) {
    return;
  }
  widget->setProperty("dimmed", dimmed);
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
}

QWidget* newRow(QWidget* parent, int height, const QString& toolTip) {
  auto* row = new QWidget(parent);
  row->setObjectName("layersRow");
  row->setAttribute(Qt::WA_StyledBackground, true);
  row->setAttribute(Qt::WA_Hover, true);
  row->setFixedHeight(height);
  row->setToolTip(toolTip);
  row->setContextMenuPolicy(Qt::CustomContextMenu);
  return row;
}

QHBoxLayout* rowLayout(QWidget* row) {
  auto* line = new QHBoxLayout(row);
  line->setContentsMargins(0, 0, 6, 0);
  line->setSpacing(0);
  return line;
}

QString eyeToolTip(bool show) {
  return QStringLiteral("%1 (%2: show only this)")
      .arg(show ? QStringLiteral("Show") : QStringLiteral("Hide"), kSoloClick);
}

// The file's parts, or only the part when one is given.
std::function<bool(size_t, size_t)> only(size_t file, std::optional<size_t> part) {
  return [file, part](size_t f, size_t p) { return f == file && (!part || p == *part); };
}

bool everything(size_t, size_t) {
  return true;
}

} // namespace

QString fileLetter(size_t index) {
  QString letters;
  // Bijective base 26: after Z comes AA, as spreadsheet columns count.
  for (size_t n = index + 1; n > 0; n = (n - 1) / 26) {
    letters.prepend(QLatin1Char(static_cast<char>('A' + (n - 1) % 26)));
  }
  return letters;
}

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent), eye_(lucideIcon(Lucide::Eye, kIconSize)),
      eyeOff_(lucideIcon(Lucide::EyeOff, kIconSize)),
      eyePartial_(lucideIcon(Lucide::Eye, kIconSize, withAlpha(overlay::kIcon, 128))),
      expanded_(lucideIcon(Lucide::ChevronDown, kIconSize)),
      collapsed_(lucideIcon(Lucide::ChevronRight, kIconSize)) {
  setObjectName("layersPanel");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the panel's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setStyleSheet(
      overlay::panelStyleSheet(objectName()) +
      QStringLiteral(
          "QScrollArea#layersPanelScroll, QWidget#layersPanelContent { background: transparent; }"
          "QWidget#layersRow { border-radius: 8px; }"
          "QWidget#layersRow:hover { background: %1; }"
          "QLabel#layersName { color: %2; font-size: 12px; }"
          "QLabel#layersName[dimmed=\"true\"] { color: %3; }"
          "QToolButton#layersShowAll { color: %4; padding: 1px 4px; border-radius: 6px; }"
          "QToolButton#layersShowAll:disabled { color: %5; }")
          .arg(overlay::css(overlay::kHover),
               overlay::css(overlay::kText),
               overlay::css(overlay::kTextSecondary),
               overlay::css(overlay::kAccent),
               overlay::css(overlay::kIconDisabled)));

  auto* frame = new QVBoxLayout(this);
  frame->setContentsMargins(1, 1, 1, 1);
  frame->setSpacing(0);

  header_ = new QWidget(this);
  auto* header = new QHBoxLayout(header_);
  header->setContentsMargins(kPadding - 1, kPadding - 1, kPadding - 5, 4);
  auto* title = new QLabel(QStringLiteral("Layers"), header_);
  title->setObjectName("panelTitle");
  header->addWidget(title);
  header->addStretch(1);
  showAll_ = new QToolButton(header_);
  showAll_->setObjectName("layersShowAll");
  showAll_->setText(QStringLiteral("Show all"));
  showAll_->setToolTip(QStringLiteral("Show every file and part"));
  // Keeps focus on the viewport, so Space keeps cycling fields after a click.
  showAll_->setFocusPolicy(Qt::NoFocus);
  header->addWidget(showAll_);
  frame->addWidget(header_);

  // Scrolls only when the viewport is too short for every row.
  scroll_ = new QScrollArea(this);
  scroll_->setObjectName("layersPanelScroll");
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setWidgetResizable(true);
  scroll_->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->setAutoFillBackground(false);
  frame->addWidget(scroll_, 1);

  connect(showAll_, &QToolButton::clicked, this, [this]() { apply(everything); });
  setFiles({});
}

void LayersPanel::setFiles(const std::vector<LayerFile>& files) {
  files_.clear();
  files_.reserve(files.size());
  auto* content = new QWidget;
  content->setObjectName("layersPanelContent");
  auto* column = new QVBoxLayout(content);
  column->setContentsMargins(5, 0, 5, 5);
  column->setSpacing(0);
  for (size_t index = 0; index < files.size(); ++index) {
    addFile(content, index, files[index]);
  }
  column->addStretch(1);
  // Deletes the previous rows.
  scroll_->setWidget(content);
  refresh();
  updateGeometry();
}

void LayersPanel::addFile(QWidget* content, size_t index, const LayerFile& file) {
  FileRow& added = files_.emplace_back();
  added.identity = overlay::identityColor(index);
  const bool nested = file.parts.size() > 1;

  added.row = newRow(content, kFileRowHeight, file.path);
  QHBoxLayout* line = rowLayout(added.row);
  added.eye = addEye(added.row, file.name, index, std::nullopt);
  line->addWidget(added.eye);
  line->addSpacing(6);
  added.badge = overlay::identityBadge(added.row, fileLetter(index), added.identity);
  line->addWidget(added.badge);
  line->addSpacing(kBadgeGap);

  auto* text = new QVBoxLayout;
  text->setSpacing(1);
  text->addStretch(1);
  added.name = new ElidedLabel(file.name, added.row);
  added.name->setObjectName("layersName");
  text->addWidget(added.name);
  QString detail;
  if (nested) {
    detail = QStringLiteral("%1 parts").arg(file.parts.size());
  } else if (!file.parts.empty() && file.parts.front().name != file.name) {
    detail = file.parts.front().name;
  }
  if (!detail.isEmpty()) {
    text->addWidget(new ElidedLabel(detail, added.row));
  }
  text->addStretch(1);
  line->addLayout(text, 1);
  content->layout()->addWidget(added.row);
  connect(added.row, &QWidget::customContextMenuRequested, this, [this, index](const QPoint& pos) {
    showMenu(index, std::nullopt, files_[index].row->mapToGlobal(pos));
  });

  for (size_t partIndex = 0; partIndex < file.parts.size(); ++partIndex) {
    const LayerPart& part = file.parts[partIndex];
    PartRow& partRow = added.parts.emplace_back();
    partRow.color = part.color;
    if (!nested) {
      continue;
    }
    partRow.row = newRow(content, kPartRowHeight, part.name);
    QHBoxLayout* partLine = rowLayout(partRow.row);
    partRow.eye = addEye(partRow.row, part.name, index, partIndex);
    partLine->addWidget(partRow.eye);
    // The dot and name line up with the file's name.
    partLine->addSpacing(6 + kBadgeSize + kBadgeGap);
    partRow.dot = new QLabel(partRow.row);
    partRow.dot->setFixedSize(kDotSize, kDotSize);
    partRow.dot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    partLine->addWidget(partRow.dot);
    partLine->addSpacing(8);
    partRow.name = new ElidedLabel(part.name, partRow.row);
    partRow.name->setObjectName("layersName");
    partLine->addWidget(partRow.name, 1);
    content->layout()->addWidget(partRow.row);
    connect(partRow.row,
            &QWidget::customContextMenuRequested,
            this,
            [this, index, partIndex](const QPoint& pos) {
              showMenu(index, partIndex, files_[index].parts[partIndex].row->mapToGlobal(pos));
            });
  }

  if (nested) {
    added.chevron = new QToolButton(added.row);
    added.chevron->setFixedSize(kBadgeSize, kBadgeSize);
    added.chevron->setIconSize(QSize(kIconSize, kIconSize));
    added.chevron->setFocusPolicy(Qt::NoFocus);
    added.chevron->setAccessibleName(QStringLiteral("Parts of %1").arg(file.name));
    line->addSpacing(6);
    line->addWidget(added.chevron);
    connect(added.chevron, &QToolButton::clicked, this, [this, index]() { toggleExpanded(index); });
    // A click anywhere else on the row expands or collapses it too.
    added.row->installEventFilter(this);
    setExpanded(added, file.parts.size() <= kMaxExpandedParts);
  }
}

QToolButton*
LayersPanel::addEye(QWidget* row, const QString& name, size_t file, std::optional<size_t> part) {
  auto* eye = new EyeButton(row);
  eye->setFixedSize(kEyeSize, kEyeSize);
  eye->setIconSize(QSize(kIconSize, kIconSize));
  eye->setAccessibleName(QStringLiteral("Show or hide %1").arg(name));
  // Keeps focus on the viewport, so Space keeps cycling fields after a click.
  eye->setFocusPolicy(Qt::NoFocus);
  connect(eye, &QToolButton::clicked, this, [this, eye, file, part]() {
    eyeClicked(file, part, eye->clickModifiers());
  });
  return eye;
}

void LayersPanel::eyeClicked(size_t file,
                             std::optional<size_t> part,
                             Qt::KeyboardModifiers modifiers) {
  const Wanted target = only(file, part);
  if (modifiers.testFlag(Qt::AltModifier)) {
    // Again on what already shows alone, everything shows.
    apply(changes(target).empty() ? Wanted(everything) : target);
    return;
  }
  const std::vector<PartRow>& parts = files_[file].parts;
  // A partly shown file shows whole, as a partly checked box checks.
  const bool show = part ? !parts[*part].visible
                         : std::any_of(parts.begin(), parts.end(), [](const PartRow& row) {
                             return !row.visible;
                           });
  apply([this, &target, show](size_t f, size_t p) {
    return target(f, p) ? show : files_[f].parts[p].visible;
  });
}

std::vector<std::pair<size_t, size_t>> LayersPanel::changes(const Wanted& wanted) const {
  std::vector<std::pair<size_t, size_t>> changed;
  for (size_t file = 0; file < files_.size(); ++file) {
    for (size_t part = 0; part < files_[file].parts.size(); ++part) {
      if (files_[file].parts[part].visible != wanted(file, part)) {
        changed.emplace_back(file, part);
      }
    }
  }
  return changed;
}

void LayersPanel::apply(const Wanted& wanted) {
  const std::vector<std::pair<size_t, size_t>> changed = changes(wanted);
  for (const auto& change : changed) {
    PartRow& row = files_[change.first].parts[change.second];
    row.visible = !row.visible;
  }
  refresh();
  std::vector<LayerVisibilityChange> batch;
  batch.reserve(changed.size());
  for (const auto& [file, part] : changed) {
    batch.push_back(
        {static_cast<int>(file), static_cast<int>(part), files_[file].parts[part].visible});
    emit visibilityChanged(batch.back().file, batch.back().part, batch.back().visible);
  }
  if (!batch.empty()) {
    emit visibilityBatchChanged(batch);
  }
}

void LayersPanel::showMenu(size_t file, std::optional<size_t> part, const QPoint& globalPos) {
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  overlay::styleMenu(menu);
  QAction* showOnly = menu->addAction(QStringLiteral("Show only this"));
  showOnly->setEnabled(!changes(only(file, part)).empty());
  connect(showOnly, &QAction::triggered, this, [this, file, part]() { apply(only(file, part)); });
  QAction* showAll = menu->addAction(QStringLiteral("Show all"));
  showAll->setEnabled(showAll_->isEnabled());
  connect(showAll, &QAction::triggered, this, [this]() { apply(everything); });
  menu->popup(globalPos);
}

void LayersPanel::setExpanded(FileRow& file, bool expanded) {
  file.expanded = expanded;
  file.chevron->setIcon(expanded ? expanded_ : collapsed_);
  file.chevron->setToolTip(expanded ? QStringLiteral("Collapse") : QStringLiteral("Expand"));
  for (PartRow& part : file.parts) {
    part.row->setVisible(expanded);
  }
}

void LayersPanel::toggleExpanded(size_t file) {
  setExpanded(files_[file], !files_[file].expanded);
  updateGeometry();
  emit expansionChanged();
}

void LayersPanel::refresh() {
  for (FileRow& file : files_) {
    const auto shown = std::count_if(
        file.parts.begin(), file.parts.end(), [](const PartRow& part) { return part.visible; });
    const bool none = shown == 0;
    const bool all = static_cast<size_t>(shown) == file.parts.size();
    file.eye->setIcon(all ? eye_ : none ? eyeOff_ : eyePartial_);
    file.eye->setToolTip(eyeToolTip(!all));
    setDimmed(file.name, none);
    const int alpha = none ? kDimmedAlpha : 255;
    file.badge->setStyleSheet(overlay::identityBadgeStyle(
        withAlpha(file.identity, alpha), withAlpha(overlay::kIdentityText, alpha)));
    for (PartRow& part : file.parts) {
      if (!part.row) {
        continue;
      }
      part.eye->setIcon(part.visible ? eye_ : eyeOff_);
      part.eye->setToolTip(eyeToolTip(!part.visible));
      setDimmed(part.name, !part.visible);
      part.dot->setStyleSheet(
          QStringLiteral("background: %1; border-radius: %2px;")
              .arg(overlay::css(withAlpha(part.color, part.visible ? 255 : kDimmedAlpha)),
                   QString::number(kDotSize / 2)));
    }
  }
  showAll_->setEnabled(!changes(everything).empty());
}

QSize LayersPanel::sizeHint() const {
  // The style sheet sets the header's fonts.
  ensurePolished();
  return {kWidth, header_->sizeHint().height() + scroll_->widget()->sizeHint().height() + 2};
}

QSize LayersPanel::fittedSize(int maxHeight) const {
  const QSize size = sizeHint();
  if (size.height() <= maxHeight) {
    return size;
  }
  return {size.width() + scroll_->verticalScrollBar()->sizeHint().width(), std::max(1, maxHeight)};
}

bool LayersPanel::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    const auto* release = static_cast<QMouseEvent*>(event);
    for (size_t index = 0; index < files_.size(); ++index) {
      QWidget* row = files_[index].row;
      if (row == watched && release->button() == Qt::LeftButton &&
          row->rect().contains(mouseLocalPos(release).toPoint())) {
        toggleExpanded(index);
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}
