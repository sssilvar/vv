#pragma once

#include "AnnotationBar.h"
#include "MeshLoading.h"
#include "MeshRenderer.h"
#include "ScalarVizUtils.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointF>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class ColorBarWidget;
class ColorPanel;
class CommandPalette;
class FieldChip;
class FieldList;
class KeyHints;
class LayersPanel;
struct LayerVisibilityChange;
class Notice;
enum class NoticeKind;
class StylePanel;
class PlaybackBar;
class PanelLabel;
class RegionBorders;
class ProbeCard;
class ScalarPager;
class SliceView;
class VolumeScene;
class ToolRail;
struct PaletteItem;
struct SearchItem;
class QAction;
class QActionGroup;
class QCloseEvent;
class QCursor;
class QPoint;
class QTimer;
class QVTKOpenGLNativeWidget;
class TemporalSource;

struct ViewerOptions {
  PaneMode mode = PaneMode::Single; // how every file's pane opens
  // Pinned color range: overrides the data/sampled range for every continuous
  // field, so a wavefront keeps the same colors across frames and files.
  std::optional<std::array<double, 2>> fixedRange;
  bool commonCatLut = false;
  bool annotate = false;
};

class QWidget;
class vtkCellPicker;
class vtkLookupTable;
class vtkGenericOpenGLRenderWindow;
class QMouseEvent;

// Main application window: owns the VTK viewport, the overlay widgets (tool
// rail, layers, color and style panels, each pane's pager and legends, probe
// cards, playback and annotation bars) and all viewer state.
class ViewerWindow : public QMainWindow {
  Q_OBJECT
public:
  ViewerWindow(MeshLoadResult loadResult, const ViewerOptions& options, QWidget* parent = nullptr);
  ~ViewerWindow() override;

protected:
  // Asks to save, discard or keep unsaved labels.
  void closeEvent(QCloseEvent* event) override;

private:
  friend class ViewerTests;
  ViewerWindow(MeshLoadResult loadResult,
               const ViewerOptions& options,
               const vtkSmartPointer<vtkGenericOpenGLRenderWindow>& renderWindow,
               QWidget* parent);

  void setupWindowTitle();
  void buildViewport(vtkGenericOpenGLRenderWindow* renderWindow);
  QWidget* buildSliceViews(QWidget* parent); // the column beside the 3-D view
  void refreshSliceViews();                  // and the 3-D view's planes
  void setupPanes(); // the scene, then a pager and label per pane and a legend per tile
  ColorBarWidget* makeLegend(size_t tile);
  void fillLayersPanel();
  void changeVisibility(const std::vector<LayerVisibilityChange>& changes);
  void buildRail();
  void setupPlayback();

  void setAnnotating(bool on); // from the rail's Annotate action
  void setupAnnotation();      // on first entry: label array, colors, picker, bar, hints
  void bindLabelArray();
  AnnotationTool activeTool() const; // the held key's, else the one chosen on the bar
  void applyTool();                  // after the active tool changed
  void setPaintHold(bool active, bool erase);
  // Handle a pointer event over the viewport while annotating; returns true if
  // consumed (so the VTK camera does not also react).
  bool handleAnnotatePointer(QMouseEvent* event);
  vtkIdType annotationCellAt(const QPointF& pos);       // primary mesh cell under pos, or -1
  void paintAtWidgetPos(const QPointF& pos, int label); // label 0 writes what Erase does
  void endStroke();                                     // onto the undo stack
  void updateBrushPreview(const QPointF& pos);          // faded overlay of cells a click would hit
  void refreshBrushPreview(); // at the pointer, after the tool, label or brush changed
  void clearBrushPreview();
  void pickLabel(int label);
  void setBrushRings(int rings);
  void undoStroke();
  enum class SaveMode { InPlace, NewFile };
  // InPlace replaces the opened file, after a confirmation the user can turn off, or
  // asks for a file when vv cannot write it back; false when canceled or not written.
  bool saveAnnotations(SaveMode mode);
  QString inPlacePath() const; // empty when the opened file cannot be written back
  void syncAnnotateKeys();
  // Compare maps: other fields of the file beside the painted view while annotating.
  std::vector<size_t> compareCandidates() const; // the file's fields but the painted one
  void setCompareMaps(size_t count);             // keeping each slot's field, then applies them
  void applyCompare();        // to the renderer and the bar; the outline is set up on first use
  void updateRegionBorders(); // the outline, from the labels
  void labelsChanged();       // after a stroke or undo wrote them: recolors, outlines, renders
  void openCompareList(size_t slot);
  void layoutCompareOverlays();               // each map's chip, and the lines between the views
  bool inPaintTile(const QPointF& pos) const; // over the view painting writes from

  std::optional<size_t> paneAt(const QPointF& pos) const; // pos in vtkWidget_
  void setActivePane(size_t pane);      // the one Space, the Color panel and the palette act on
  void stepPane(size_t pane, int step); // +1 forward, -1 back, wrapping
  void showField(size_t pane, int position); // in the pane's fields; -1 is plain geometry
  void setPaneSplit(size_t pane, bool split);
  void showPaneMenu(size_t pane, const QPoint& globalPos);
  // After the tiles changed what they show: the frames' arrays, legends, pagers, the
  // Color panel, the overlays' layout, the probe and the trace.
  void panesChanged();
  void fieldChanged(size_t field); // after its colors changed, wherever it shows
  void syncPager(size_t pane);
  void syncLegend(size_t tile);
  void syncSplitAction();
  void syncLayoutMetrics(); // what the bars and pagers take from the panes
  void streamShownFields(); // a time series reads only the arrays its tiles show
  SearchItem fieldItem(size_t pane, int position) const; // as the pager and lists show it
  void openFieldList(size_t pane);
  // The field list over anchor, which stays its anchor; choose takes the row picked.
  void
  openList(QWidget* anchor, const std::vector<SearchItem>& items, std::function<void(int)> choose);
  void togglePalette();
  std::vector<PaletteItem> paletteItems(); // fields, colormaps or palettes, and commands
  // The field the Color panel and the palette's colors edit: the active pane's, while
  // it shows one field.
  std::optional<size_t> colorField() const;
  void chooseColormap(size_t field, const std::string& id, bool reversed);
  void clipField(size_t field, double lo, double hi);
  void chooseCategoryPalette(size_t field, const QString& id); // a PaletteOption id
  void changeCategoryColor(size_t field, int entry, const std::optional<QColor>& color);
  void showClipRange(size_t field); // on its legends and the Color panel
  void cycleVectorField();
  void syncColorPanel();   // enables the Color tool for a field, showing its colors
  void updateColorRange(); // the panel's range and histogram, from the loaded data
  void setSurfaceLook(const SurfaceLook& look); // on the meshes and the Style panel

  bool paintArmed() const;               // annotation's pencil is on, so the pointer paints
  QCursor sceneCursor() const;           // over the view while not painting
  void requestProbe(const QPointF& pos); // reads under pos on the next event-loop turn
  void refreshProbe(); // reads again where the card shows, after the view or data changed
  void hideProbe();
  void showProbe();
  void pinProbe(const QPointF& pos); // the mesh point there, or unpins over empty space
  void unpin();
  // The field the pinned point's trace follows: the one its tile shows, on the time series.
  std::optional<size_t> tracedField() const;
  void restartTrace(); // the pinned point's trace of the field shown, from scratch
  void readTrace();    // a few more frames of it, once per event-loop turn
  void showTrace();    // on the playback bar, with its label
  QString meshSource(size_t mesh) const; // file or part name for the card, if several

  void notify(const QString& text, NoticeKind kind); // a brief notice at the top of the view
  void layoutOverlays();
  void layoutPanes(); // each pane's label and pager, each tile's legend
  void stackOverlays();
  void onViewportResize();
  void saveSnapshot();
  void showFrame(int step);
  void playOrPause(bool playing); // the frame clock, and the bar's play button to match
  void applyPlayTimerInterval();
  void restartPlayClock();

  MeshLoadResult load_;
  ViewerOptions options_;
  std::vector<std::array<double, 3>> partColors_;

  MeshRenderer renderer_;
  QVTKOpenGLNativeWidget* vtkWidget_ = nullptr;
  LayersPanel* layersPanel_ = nullptr;
  ColorPanel* colorPanel_ = nullptr;
  StylePanel* stylePanel_ = nullptr;
  ToolRail* rail_ = nullptr;
  QAction* layersAction_ = nullptr;
  QAction* colorAction_ = nullptr;
  QAction* styleAction_ = nullptr;
  QAction* probeAction_ = nullptr;
  QAction* annotateAction_ = nullptr;
  QAction* splitAction_ = nullptr; // the active pane's mode
  QAction* linkAction_ = nullptr;  // among several files: panes follow each other's field
  QActionGroup* panels_ = nullptr; // Layers, Color and Style: one open at a time
  std::vector<QAction*> commands_; // what the palette lists: the rail's tools, X-ray and Glass
  CommandPalette* palette_ = nullptr;
  std::vector<ProbeCard*> probeCards_;
  Notice* notice_ = nullptr;
  QTimer* probeTimer_ = nullptr;
  QPointF probePos_;            // in vtkWidget_, where the last probe was asked for
  std::optional<ProbeHit> pin_; // where a click pinned the probe, while Probe is on
  SurfaceStyle styleBeforeSeeThrough_ = SurfaceStyle::Solid; // what X and G return to
  QAction* vectorsAction_ = nullptr;

  // A file's overlays; the renderer holds what its pane shows.
  struct PaneUi {
    ScalarPager* pager = nullptr; // null without a field to page through
    PanelLabel* label = nullptr;  // among several files
  };
  std::vector<PaneUi> paneUi_;           // parallel to the renderer's panes
  std::vector<ColorBarWidget*> legends_; // parallel to the renderer's tiles
  size_t activePane_ = 0;
  FieldList* fieldList_ = nullptr; // opened from a pager or a compare chip
  // The pager or chip the list opened from, which live as long as the window. Not a QPointer:
  // clang-analyzer 18 reports a false use-after-free in ~QWeakPointer.
  const QWidget* listAnchor_ = nullptr;
  std::function<void(int)> listChoose_;   // takes the row picked in it
  std::vector<ScalarField> vectorFields_; // 3-component arrays, drawn as glyphs
  int activeVectorIdx_ = -1;

  // Volumes: the slice views beside the 3-D view.
  std::unique_ptr<VolumeScene> volumeScene_;
  std::vector<SliceView*> sliceViews_;

  // Temporal (playable) support: when a time-series file is loaded, the color
  // range is fixed across the whole animation (sampled once per scalar) so the
  // colormap stays stable while frames advance.
  std::shared_ptr<TemporalSource> temporal_;
  std::set<size_t> temporalFields_;
  std::set<size_t> sampledFields_;     // whose range the series' frames have fixed
  std::vector<std::string> streamed_;  // the arrays frames bring now
  PlaybackBar* playbackBar_ = nullptr; // a playable time series
  QTimer* playTimer_ = nullptr;
  ProbeTrace trace_;                       // the pinned point's value per frame
  std::optional<size_t> traced_;           // the field trace_ holds
  vtkSmartPointer<vtkDataSet> traceFrame_; // frames the trace reads, apart from the one shown
  QTimer* traceTimer_ = nullptr;
  QElapsedTimer playClock_;
  int playAnchorStep_ = 0;
  int currentPlaybackStep_ = 0;

  // Annotation state, set up on first entry and kept after leaving.
  bool annotating_ = false;
  AnnotationBar* annotationBar_ = nullptr;
  KeyHints* keyHints_ = nullptr;
  std::vector<QAction*> annotateKeys_;           // 1 to 9, + and −, undo: live while annotating
  AnnotationTool tool_ = AnnotationTool::Rotate; // chosen on the bar
  std::optional<AnnotationTool> heldTool_;       // while A or E is held
  int currentLabel_ = 1;
  int brushRings_ = 3;
  std::optional<int> strokeValue_; // what the stroke in progress writes
  vtkSmartPointer<vtkCellPicker> picker_;
  vtkSmartPointer<vtkLookupTable> labelLut_; // fixed value→color, stable while painting
  ScalarField labelField_{"label", FieldAssociation::Cell}; // what strokes write
  class vtkDataArray* labelArray_ = nullptr;                // owned by the mesh
  std::vector<double> labelValues_; // [0] is what Erase writes, [i] what label i writes
  double labelRange_[2] = {0.0, 1.0};
  vtkSmartPointer<class vtkActor> previewActor_; // faded hover overlay of the brush footprint
  // Compare maps: each slot's field, kept for the next entry, and how many show.
  std::vector<size_t> compareFields_;
  size_t compareCount_ = 0;
  std::vector<FieldChip*> compareChips_;  // per slot, over its tile
  std::vector<QWidget*> compareDividers_; // left of the maps, then between them
  std::unique_ptr<RegionBorders> regionBorders_;
  // Undo: each stroke records the (tuple, previous value) pairs it changed.
  std::vector<std::pair<long long, double>> currentStroke_;
  std::vector<std::vector<std::pair<long long, double>>> undoStack_;
};
