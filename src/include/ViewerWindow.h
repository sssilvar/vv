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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ColorBarWidget;
class ColorPanel;
class CommandPalette;
class FieldList;
class KeyHints;
class LayersPanel;
struct LayerVisibilityChange;
class Notice;
enum class NoticeKind;
class StylePanel;
class PlaybackBar;
class PanelLabel;
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
class QTimer;
class QVTKOpenGLNativeWidget;
class TemporalSource;

struct ViewerOptions {
  bool explodeView = false;
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
// rail, layers, color and style panels, legend, probe card, playback and
// annotation bars) and all viewer state.
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
  void setupFacetMode();
  void setupNormalMode();
  void fillLayersPanel();
  void changeVisibility(const std::vector<LayerVisibilityChange>& changes);
  void buildRail();
  void setupPlayback();

  void setAnnotating(bool on); // from the rail's Annotate action
  void setupAnnotation();      // on first entry: label array, colors, picker, bar, hints
  void ensureLabelArray();
  AnnotationTool activeTool() const; // the held key's, else the one chosen on the bar
  void applyTool();                  // after the active tool changed
  void setPaintHold(bool active, bool erase);
  // Handle a pointer event over the viewport while annotating; returns true if
  // consumed (so the VTK camera does not also react).
  bool handleAnnotatePointer(QMouseEvent* event);
  vtkIdType annotationCellAt(const QPointF& pos); // primary mesh cell under pos, or -1
  void paintAtWidgetPos(const QPointF& pos, int value);
  void endStroke();                            // onto the undo stack
  void updateBrushPreview(const QPointF& pos); // faded overlay of cells a click would hit
  void refreshBrushPreview(); // at the pointer, after the tool, label or brush changed
  void clearBrushPreview();
  void pickLabel(int label);
  void setBrushRings(int rings);
  void applyLabelColoring();
  void undoStroke();
  bool saveAnnotations(); // false when canceled or not written
  void syncDigitKeys();   // labels while annotating, else styles while the Style panel is open

  void applyScalarAtIndex(int index);
  void applyNoScalar();
  void cycleScalar(int step);          // +1 forward, -1 back
  void showPage(int page);             // 0 is plain geometry, then a page per field
  SearchItem pageItem(int page) const; // its name and kind, as the pager and lists show it
  void syncPager();
  void openFieldList();
  void togglePalette();
  std::vector<PaletteItem> paletteItems(); // fields, colormaps or palettes, and commands
  void chooseColormap(const std::string& id, bool reversed); // for the active field
  void cycleVectorField();
  void syncColorPanel();     // enables the Color tool for a field, showing its colors
  void showCategoryLegend(); // the active categorical field's rows and palettes
  void showCategoryColors(); // on the legend, the Color panel and the probe card
  void refreshFacetColumn(size_t panel);
  void showFacetCategories(size_t panel);        // a facet panel legend's rows and palettes
  void chooseCategoryPalette(const QString& id); // for the active field, a PaletteOption id
  void changeCategoryColor(int entry, const std::optional<QColor>& color); // legend row entry
  void updateColorRange(); // the panel's range and histogram, from the loaded data
  void showClipRange();    // the renderer's clip window on the legend and the panel
  void setSurfaceLook(const SurfaceLook& look); // on the meshes and the Style panel

  void updateFacetPages();         // as many fields as the view holds, from the first shown
  void showFacetPage(size_t page); // keeps clip ranges, colors and the camera
  void syncFacetPager();
  std::vector<SearchItem> facetFieldItems() const; // every column, those shown checked

  bool paintArmed() const;               // annotation's pencil is on, so the pointer paints
  QCursor sceneCursor() const;           // over the view while not painting
  void requestProbe(const QPointF& pos); // reads under pos on the next event-loop turn
  void refreshProbe(); // reads again where the card shows, after the view or data changed
  void hideProbe();
  void showProbe();
  void pinProbe(const QPointF& pos); // the mesh point there, or unpins over empty space
  void unpin();
  void restartTrace(); // the pinned point's trace of the field shown, from scratch
  void readTrace();    // a few more frames of it, once per event-loop turn
  void showTrace();    // on the playback bar, with its label
  QString meshSource(size_t mesh) const; // file or part name for the card, if several

  void notify(const QString& text, NoticeKind kind); // a brief notice at the top of the view
  void layoutOverlays();
  void layoutFacetColorBars();
  void layoutPanelLabels();
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
  ColorBarWidget* colorBar_ = nullptr;
  LayersPanel* layersPanel_ = nullptr;
  ColorPanel* colorPanel_ = nullptr;
  StylePanel* stylePanel_ = nullptr;
  ToolRail* rail_ = nullptr;
  QAction* layersAction_ = nullptr;
  QAction* colorAction_ = nullptr;
  QAction* styleAction_ = nullptr;
  QAction* probeAction_ = nullptr;
  QAction* annotateAction_ = nullptr;
  QActionGroup* panels_ = nullptr; // Layers, Color and Style: one open at a time
  std::vector<QAction*> commands_; // what the palette lists: the rail's tools, X-ray and Glass
  CommandPalette* palette_ = nullptr;
  std::vector<ProbeCard*> probeCards_;
  Notice* notice_ = nullptr;
  QTimer* probeTimer_ = nullptr;
  QPointF probePos_;                // in vtkWidget_, where the last probe was asked for
  std::optional<ProbeHit> pin_;     // where a click pinned the probe, while Probe is on
  std::vector<QAction*> styleKeys_; // 1, 2 and 3, live while the Style panel is open
  SurfaceStyle styleBeforeSeeThrough_ = SurfaceStyle::Solid; // what X and G return to
  QAction* vectorsAction_ = nullptr;
  std::vector<ColorBarWidget*> facetColorBars_;
  std::vector<PanelLabel*> panelLabels_;

  // Volumes, outside exploded view: the slice views beside the 3-D view.
  std::unique_ptr<VolumeScene> volumeScene_;
  std::vector<SliceView*> sliceViews_;

  std::vector<ScalarField> scalarFields_;
  std::vector<QString> fieldDetails_; // each field's kind, "Points · continuous"
  int activeScalarIdx_ = -1;
  ScalarPager* scalarPager_ = nullptr;    // normal mode with a field; a time series' is in its bar
  ScalarPager* facetPager_ = nullptr;     // -e, showing while its fields need several pages
  FieldList* fieldList_ = nullptr;        // opened from either pager
  std::vector<ScalarField> vectorFields_; // 3-component arrays, drawn as glyphs
  int activeVectorIdx_ = -1;

  // Temporal (playable) support: when a time-series file is loaded, the color
  // range is fixed across the whole animation (sampled once per scalar) so the
  // colormap stays stable while frames advance.
  std::shared_ptr<TemporalSource> temporal_;
  std::map<std::string, std::array<double, 2>> temporalRangeCache_;
  PlaybackBar* playbackBar_ = nullptr; // a playable time series
  QTimer* playTimer_ = nullptr;
  ProbeTrace trace_;                       // the pinned point's value per frame
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
  std::optional<int> strokeValue_;                       // what the stroke in progress writes
  std::array<double, 2> clipBeforeAnnotating_{0.0, 1.0}; // restored on leaving
  vtkSmartPointer<vtkCellPicker> picker_;
  vtkSmartPointer<vtkLookupTable> labelLut_;     // fixed value→color, stable while painting
  class vtkIntArray* labelArray_ = nullptr;      // owned by the mesh's cell data
  vtkSmartPointer<class vtkActor> previewActor_; // faded hover overlay of the brush footprint
  // Undo: each stroke records the (cell, previous value) pairs it changed.
  std::vector<std::pair<long long, int>> currentStroke_;
  std::vector<std::vector<std::pair<long long, int>>> undoStack_;
};
