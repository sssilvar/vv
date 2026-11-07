#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QWidget>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QLabel;
class QPoint;
class QScrollArea;
class QToolButton;

struct LayerPart {
  QString name;
  QColor color; // what the part is drawn in without a field
};

struct LayerFile {
  QString name; // shortened in the middle when it does not fit
  QString path; // the row's tooltip
  std::vector<LayerPart> parts;
};

struct LayerVisibilityChange {
  int file;
  int part;
  bool visible;
};

/// The letter that tells the file at index apart: A to Z, then AA, AB and so on. Its
/// Layers badge, mesh panel badge and probe card all show it.
QString fileLetter(size_t index);

/// Which files and parts show. A row per file: an eye, a letter badge in the file's
/// identity color and its name; under a file with several parts, a row per part
/// behind a chevron.
///
///   - Click an eye to hide or show the file or part.
///   - Option-click (Alt-click) an eye to show only that file or part, and again to
///     show everything.
///   - Right-click a row for Show only this and Show all.
class LayersPanel : public QWidget {
  Q_OBJECT

public:
  explicit LayersPanel(QWidget* parent = nullptr);

  /// Replace the rows, everything shown; emits nothing.
  void setFiles(const std::vector<LayerFile>& files);

  /// All rows, unscrolled.
  QSize sizeHint() const override;

  /// The size to show at most maxHeight tall: shorter, the rows scroll, and the panel
  /// widens by the scroll bar so they keep their width.
  QSize fittedSize(int maxHeight) const;

signals:
  /// The user showed or hid a part; file and part index the list given to setFiles.
  void visibilityChanged(int file, int part, bool visible);

  /// One complete visibility action, including solo and Show all.
  void visibilityBatchChanged(const std::vector<LayerVisibilityChange>& changes);

  /// The user expanded or collapsed a file's parts, which changes the panel's height.
  void expansionChanged();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  struct PartRow {
    bool visible = true;
    QWidget* row = nullptr; // null for the only part of a file, which has no row
    QToolButton* eye = nullptr;
    QLabel* dot = nullptr;
    QLabel* name = nullptr;
    QColor color;
  };
  struct FileRow {
    QWidget* row = nullptr;
    QToolButton* eye = nullptr;
    QLabel* badge = nullptr;
    QLabel* name = nullptr;
    QToolButton* chevron = nullptr; // with several parts only
    QColor identity;
    bool expanded = true;
    std::vector<PartRow> parts;
  };
  using Wanted = std::function<bool(size_t file, size_t part)>;

  void addFile(QWidget* content, size_t index, const LayerFile& file);
  QToolButton* addEye(QWidget* row, const QString& name, size_t file, std::optional<size_t> part);
  void eyeClicked(size_t file, std::optional<size_t> part, Qt::KeyboardModifiers modifiers);
  // The parts, as (file, part), whose visibility differs from wanted.
  std::vector<std::pair<size_t, size_t>> changes(const Wanted& wanted) const;
  void apply(const Wanted& wanted); // shows the parts wanted, hides the rest, emits each change
  void showMenu(size_t file, std::optional<size_t> part, const QPoint& globalPos);
  void setExpanded(FileRow& file, bool expanded);
  void toggleExpanded(size_t file); // the user's, so it emits expansionChanged
  void refresh();                   // eyes, dimming and Show all, from the parts' visibility

  QWidget* header_ = nullptr;
  QToolButton* showAll_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  std::vector<FileRow> files_;
  QIcon eye_;
  QIcon eyeOff_;
  QIcon eyePartial_; // a file with some parts hidden
  QIcon expanded_;
  QIcon collapsed_;
};
