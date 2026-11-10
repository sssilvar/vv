#pragma once
#include "MeshLoading.h"
#include "ScalarVizUtils.h"
#include "SurfaceStyle.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vtkActor.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

class vtkCallbackCommand;
class vtkCellPicker;
class vtkStaticCellLocator;
class vtkFeatureEdges;
class vtkImageSlice;
class vtkPolyDataNormals;

struct RendererContext {
  vtkSmartPointer<vtkRenderWindow> window;
  std::vector<vtkSmartPointer<vtkActor>> actors;
  std::vector<std::array<double, 3>> colorsHex;
};

struct PartVisibilityChange {
  size_t part;
  bool visible;
};

struct FacetPanelInfo {
  std::string title; // scalar name; empty for a geometry-only column
  FieldAssociation association = FieldAssociation::Point;
  bool hasScalar = false; // false: the panel's file lacks this column's scalar
  bool shown = true;      // on the page shown
  size_t column = 0;      // panels in one column share a scalar and its color range
  double globalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  ScalarAnalysis analysis;
};

// A categorical field's legend row: its label and color.
struct ProbeCategory {
  std::string label;
  std::array<double, 3> rgb{};
};

// The mesh cell under a point of the window and the probed field's value there.
struct ProbeHit {
  size_t panel = 0;
  size_t mesh = 0; // index into the meshes given to setup or setupFacetGrid
  vtkIdType cell = -1;
  std::string field;                     // empty when no field is probed on this mesh
  double value = 0.0;                    // NaN where the field has no value
  std::optional<ProbeCategory> category; // a categorical field's row for value
  CellPoint at;                          // the hit inside the cell, to read it again later
  std::array<double, 3> position{};      // the hit in world coordinates
};

class MeshRenderer {
public:
  MeshRenderer();
  ~MeshRenderer();
  void setRenderContext(vtkRenderWindow* externalWindow,
                        vtkRenderWindowInteractor* externalInteractor);
  // One viewport per group (file), all sharing a single camera; a lone group
  // fills the window.
  void setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
             const std::vector<MeshGroup>& groups,
             const std::vector<std::array<double, 3>>& colorsHex);
  void start();
  // A volume's slice planes, drawn with the meshes in the single view; bounds widen
  // what the camera frames. Call after setup; does not render.
  void addImagePlanes(const std::vector<vtkSmartPointer<vtkImageSlice>>& planes,
                      const std::array<double, 6>& bounds);

  // When set, all categorical scalars use this shared LUT instead of per-scalar detection.
  // Pass an empty ScalarAnalysis to clear.
  void setSharedCatAnalysis(const ScalarAnalysis& shared);

  bool setActiveScalar(const std::string& scalarName, FieldAssociation association);
  // The analysis setActiveScalar would give the field: its categories, or the shared
  // ones, with the category colors and colormap picked for it.
  ScalarAnalysis analyzeField(const std::string& scalarName, FieldAssociation association) const;
  // Color the primary mesh by a categorical array using an externally-owned LUT,
  // bypassing per-scalar analysis so the value→color mapping stays fixed while
  // the array is edited (annotation mode).
  void colorByFixedCategorical(const std::string& scalarName,
                               FieldAssociation association,
                               vtkLookupTable* lut,
                               const double range[2]);
  void clearActiveScalar();
  // Re-apply the current scalar mapping after the specified mesh data changed
  // (e.g. a new playback frame was shallow-copied in), keeping the color range
  // fixed, then re-render.
  void refreshAfterDataChange(size_t mesh);
  // Override the active scalar's color range (used to fix a stable range across a
  // whole time series instead of the current frame's range).
  void setActiveScalarRange(double minValue, double maxValue);
  bool getActiveScalarGlobalRange(double outRange[2]) const;
  const ScalarAnalysis& getActiveScalarAnalysis() const;
  // The table of the first mesh that has the active field; nullptr without one.
  vtkLookupTable* getActiveLUT() const;
  void getClipRange(double outRange[2]) const;
  bool setClipRange(double minValue, double maxValue);
  // Draw the active categorical field with the palette of this id, or its custom colors
  // again for kCustomPaletteId; the analysis reports it. Kept for that field, so it
  // survives cycling away and back.
  bool setCategoryPalette(std::string_view id);
  // Recolor one category of the active categorical field, which makes its colors custom;
  // std::nullopt gives it its palette color back (see recolorCategory). Kept like the palette.
  // NaN changes the separate missing-value color, leaving finite categories alone.
  bool setCategoryColor(double value, const std::optional<std::array<double, 3>>& rgb);
  // Draw the active continuous field with the colormap of this id, reversed or
  // not; the analysis reports it. Like category colors, it is kept for that field.
  bool setColormap(std::string_view id, bool reversed);
  // How the active continuous field draws values outside the clip window; kept
  // for that field with its colormap.
  bool setOutOfRange(OutOfRange outside);
  // The active field's histogram over its full range (see computeScalarHistogram).
  std::vector<size_t> getActiveScalarHistogram(size_t bins) const;
  // How every mesh is drawn, in the single view and in every facet panel; kept for
  // meshes set up later.
  void setSurfaceLook(const SurfaceLook& look);
  const SurfaceLook& getSurfaceLook() const {
    return surfaceLook_;
  }
  bool setPartVisible(size_t partIndex, bool visible);
  // Validate the whole batch before applying it; render once after all actors agree.
  bool setPartsVisible(const std::vector<PartVisibilityChange>& changes);
  // False for a hidden panel or invalid index. Leaves viewport unchanged in that case.
  bool getPanelViewport(size_t index, double viewport[4]) const;
  size_t getFacetPanelCount() const;
  bool getFacetPanelInfo(size_t panelIndex, FacetPanelInfo& outInfo) const;
  vtkLookupTable* getFacetPanelLUT(size_t panelIndex) const;
  bool setFacetPanelClipRange(size_t panelIndex, double minValue, double maxValue);
  bool setFacetClipRange(size_t panelIndex, double minValue, double maxValue);
  // Apply to every scalar panel in this field column, retaining clip ranges.
  bool setFacetColormap(size_t panelIndex, std::string_view id, bool reversed);
  // Apply palettes and category colors to the selected panel's entire field column.
  bool setFacetCategoryPalette(size_t panelIndex, std::string_view id);
  bool setFacetCategoryColor(size_t panelIndex,
                             double value,
                             const std::optional<std::array<double, 3>>& rgb);
  // Widen or narrow a panel's full range (what the colorbar ends show), not just
  // the clip window inside it — used to pin one range across every panel.
  bool setFacetPanelGlobalRange(size_t panelIndex, double minValue, double maxValue);

  // Draw a short line glyph per cell (or per point) tangent to the surface, one
  // for each tuple of a 3-component array, on every mesh that has it. Empty name
  // removes the glyphs.
  bool setVectorGlyphs(const std::string& name, FieldAssociation association);

  // One panel per (group, scalar): an adaptive grid of scalars for a single group,
  // otherwise a matrix with a row per group and a column per scalar.
  void setupFacetGrid(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                      const std::vector<MeshGroup>& groups,
                      const std::vector<std::array<double, 3>>& colorsHex);
  void startFacetGrid();
  // Shows the facet grid's scalars pageSize at a time (0 shows them all), page being the
  // one shown, in the window above bottomInset pixels while there are several pages. The
  // panels of other pages keep their state and are not drawn. Lays out without rendering.
  void setFacetPages(size_t pageSize, size_t page, int bottomInset);
  // Another page of the same size, under the same camera; renders.
  void showFacetPage(size_t page);
  size_t getFacetPage() const {
    return page_;
  }
  size_t getFacetPageSize() const;
  size_t getFacetPageCount() const;
  size_t getFacetColumnCount() const {
    return facetColumns_;
  }
  // Reflow adaptive panels, then re-frame unless the camera moved
  // since vv last framed it; the first fit can run before the widget is laid out.
  void refitCameraIfUntouched();
  // Back to the opening view: looking down -Z with +Y up, every mesh framed.
  void resetCamera();

  // The mesh cell at display point (x, y), in pixels from the window's bottom-left,
  // with the active field's value there, or the facet panel's own field with -e.
  // std::nullopt off every mesh. Picks without rendering.
  std::optional<ProbeHit> probe(double x, double y);
  // What probe reads at a hit it gave before, from the meshes as they are now (a later
  // frame or another field). std::nullopt for a mesh or panel it does not show.
  std::optional<ProbeHit> probeAgain(const ProbeHit& hit) const;
  // Read each visible panel at the same world position, preserving its own field and cell.
  // Equal cell IDs in different files do not establish correspondence.
  std::vector<ProbeHit> matchingProbes(const ProbeHit& hit);
  // Markers at matching hits; std::nullopt removes them. Renders.
  void setPinMarker(const std::optional<ProbeHit>& hit);
  // Where a hit probe gave shows now, in display pixels from the window's bottom-left;
  // std::nullopt when it lies behind the camera.
  std::optional<std::array<double, 2>> displayPosition(const ProbeHit& hit) const;

  RendererContext context;

  // Annotation mode needs the renderer (for picking) and the primary dataset; it
  // is only enabled for a single file, so the first panel holds that dataset.
  vtkRenderer* getRenderer() const {
    return renderer;
  }
  vtkDataSet* getPrimaryMesh() const {
    return sceneMeshes.empty() ? nullptr : sceneMeshes.front();
  }

private:
  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkRenderWindowInteractor> interactor;
  std::vector<vtkSmartPointer<vtkDataSet>> sceneMeshes;
  std::vector<vtkSmartPointer<vtkDataSetMapper>> mappers;
  std::string activeScalarName;
  FieldAssociation activeScalarAssociation = FieldAssociation::Point;
  ScalarAnalysis activeScalarAnalysis;
  ScalarAnalysis sharedCatAnalysis; // non-empty = override per-scalar detection
  std::map<std::pair<std::string, FieldAssociation>, CategoryColoring> categoryColorings_;
  std::map<std::pair<std::string, FieldAssociation>, ColormapChoice> colormaps_;
  double activeScalarGlobalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  bool applyFieldColors(); // keeps the field's colormap and coloring for it, and redraws
  bool redrawFacetColumn(size_t panelIndex);
  bool clipFacetPanel(size_t panelIndex, double minValue, double maxValue);
  SurfaceLook surfaceLook_;
  void applySurfaceLook(); // to context.actors and the panel renderers, without rendering
  // Glass's stages for one mesh, made with the scene; they run only while Glass shows.
  struct GlassStages {
    vtkSmartPointer<vtkDataSet> mesh;
    vtkSmartPointer<vtkPolyDataNormals> normals; // smooth shading; polydata without strips
    vtkSmartPointer<vtkFeatureEdges> borders;    // open borders; polydata only
  };
  std::vector<GlassStages> glassStages_; // by mesh index
  // Parallel to context.actors; null where the mesh has no borders stage.
  std::vector<vtkSmartPointer<vtkActor>> outlines_;
  // After the scene's actors exist and before applySurfaceLook.
  void buildGlassStages(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes);
  void applyGlass(size_t actorIndex, bool on);
  // Viewports sharing one camera; clipping follows the bounds of every mesh so
  // a panel never clips geometry another panel frames.
  void buildPanels(size_t count, size_t cols);
  void layoutPanels();
  void finishPanels(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes);

  std::vector<vtkSmartPointer<vtkRenderer>> panelRenderers_;
  struct ActorSlot {
    size_t mesh = 0;  // index into the meshes given to setup or setupFacetGrid
    size_t panel = 0; // index into panelRenderers_
  };
  std::vector<ActorSlot> actorSlots_; // parallel to context.actors
  vtkSmartPointer<vtkCellPicker> probePicker_;
  // Fills hit's field, value and category from data, the mesh the actor draws.
  void readProbedField(ProbeHit& hit, size_t actorIndex, vtkDataSet* data) const;
  std::vector<vtkSmartPointer<vtkStaticCellLocator>> probeLocators_;
  struct PinMarker {
    size_t actor;
    size_t panel;
    vtkSmartPointer<vtkActor> marker;
  };
  void updatePinMarkers();
  std::vector<PinMarker> pinMarkers_;
  std::optional<ProbeHit> markerHit_;
  bool adaptiveGrid_ = true;
  std::vector<size_t> meshPanel_; // mesh index -> panel (normal mode)
  size_t facetColumns_ = 0;       // scalars of the facet grid; 0 in the single view
  size_t pageSize_ = 0;           // facet columns per page; 0 for all
  size_t page_ = 0;
  int bottomInset_ = 0; // window pixels below the panels while there are several pages
  // Empty cells of the page shown, which would keep what an earlier page drew there.
  std::vector<vtkSmartPointer<vtkRenderer>> blanks_;
  double sceneBounds_[6] = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
  vtkSmartPointer<vtkCallbackCommand> clipCb_;
  vtkMTimeType fittedCameraMTime_ = 0;
  struct FacetPanelState {
    std::vector<vtkSmartPointer<vtkDataSetMapper>> scalarMappers; // share lut
    vtkSmartPointer<vtkLookupTable> lut;
    std::string title; // scalar name, which buildLookupTable keys on
    FieldAssociation association = FieldAssociation::Point;
    bool hasScalar = false;
    size_t column = 0;
    ScalarAnalysis analysis;
    double globalRange[2] = {0.0, 1.0};
    double clipRange[2] = {0.0, 1.0};
    double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  };
  std::vector<FacetPanelState> facetPanels;
  std::vector<std::pair<vtkRenderer*, vtkSmartPointer<vtkActor>>> glyphActors_;
  bool embeddedMode = false;
};
