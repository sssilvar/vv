#pragma once
#include "MeshLoading.h"
#include "ScalarVizUtils.h"
#include "SurfaceStyle.h"

#include <algorithm>
#include <array>
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
class vtkCamera;
class vtkCellPicker;
class vtkStaticCellLocator;
class vtkFeatureEdges;
class vtkImageSlice;
class vtkPolyDataNormals;

struct RendererContext {
  vtkSmartPointer<vtkRenderWindow> window;
  std::vector<vtkSmartPointer<vtkActor>> actors; // a tile's, one per mesh of its pane
};

struct PartVisibilityChange {
  size_t part; // index into the meshes given to setup
  bool visible;
};

// A scalar field of the scene and how every tile showing it draws it. Files that carry
// a field of the same name and association share it, with its range and colors.
struct SceneField {
  ScalarField field;
  ScalarAnalysis analysis;
  double globalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  vtkSmartPointer<vtkLookupTable> lut;
};

// How a pane shows its file's fields: one at a time, or a page of them side by side.
enum class PaneMode { Single, Split };

// One viewport of a pane: the pane's meshes colored by one field, or by their part colors.
struct Tile {
  vtkSmartPointer<vtkRenderer> renderer;
  size_t pane = 0;
  size_t firstActor = 0;       // its actors follow in context.actors, one per pane mesh
  std::optional<size_t> field; // index into the scene's fields while shown
  bool shown = false;
  double viewport[4] = {0.0, 0.0, 1.0, 1.0};
};

// A file's share of the window. Its tiles are a fixed pool: Single draws the first, Split
// as many as its page holds, and paging hands them other fields.
struct Pane {
  std::vector<size_t> meshes; // indices into the meshes given to setup
  std::vector<size_t> fields; // the scene fields its meshes carry, in the scene's order
  PaneMode mode = PaneMode::Single;
  int first = -1;       // position in fields of the first one shown; -1 is plain geometry
  size_t pageSize = 1;  // fields a page shows
  size_t firstTile = 0; // its tiles follow in the renderer's tiles
  size_t tileCount = 0;
  double viewport[4] = {0.0, 0.0, 1.0, 1.0};

  // Single pages through plain geometry, then each field; Split through pages of fields.
  size_t pageCount() const {
    return mode == PaneMode::Single ? fields.size() + 1 : (fields.size() + pageSize - 1) / pageSize;
  }
  size_t page() const {
    return mode == PaneMode::Single ? static_cast<size_t>(first + 1)
                                    : static_cast<size_t>(first) / pageSize;
  }
  // Fields the page shown holds.
  size_t shownCount() const {
    return first < 0 ? 0 : std::min(pageSize, fields.size() - static_cast<size_t>(first));
  }
};

// What the window's overlays take from the panes, in logical pixels.
struct LayoutMetrics {
  double pixelRatio = 1.0; // window pixels per logical pixel
  int bottomInset = 0;     // under every pane, for a bar along the window's bottom
  int pagerStrip = 0;      // under a Split pane's tiles, for its pager
};

// A categorical field's legend row: its label and color.
struct ProbeCategory {
  std::string label;
  std::array<double, 3> rgb{};
};

// The mesh cell under a point of the window and the probed field's value there.
struct ProbeHit {
  size_t tile = 0;
  size_t mesh = 0; // index into the meshes given to setup
  vtkIdType cell = -1;
  std::string field;                     // empty when no field is probed on this mesh
  double value = 0.0;                    // NaN where the field has no value
  std::optional<ProbeCategory> category; // a categorical field's row for value
  CellPoint at;                          // the hit inside the cell, to read it again later
  std::array<double, 3> position{};      // the hit in world coordinates
};

// Draws the meshes in panes, one per group (file), laid out in an adaptive grid under a
// single camera. Each pane shows its own fields in its own mode.
class MeshRenderer {
public:
  MeshRenderer();
  ~MeshRenderer();
  void setRenderContext(vtkRenderWindow* externalWindow,
                        vtkRenderWindowInteractor* externalInteractor);
  // A pane per group, or one for every mesh without groups, each in mode showing its
  // first field. A pane without fields stays Single.
  void setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
             const std::vector<MeshGroup>& groups,
             const std::vector<std::array<double, 3>>& colorsHex,
             PaneMode mode);
  // A volume's slice planes, drawn with the meshes in every tile; bounds widen what the
  // camera frames. Call after setup; does not render.
  void addImagePlanes(const std::vector<vtkSmartPointer<vtkImageSlice>>& planes,
                      const std::array<double, 6>& bounds);

  // When set, all categorical scalars use this shared LUT instead of per-scalar detection.
  // Pass an empty ScalarAnalysis to clear. Takes effect at the next setup.
  void setSharedCatAnalysis(const ScalarAnalysis& shared);

  const std::vector<SceneField>& fields() const {
    return fields_;
  }
  const std::vector<Pane>& panes() const {
    return panes_;
  }
  const std::vector<Tile>& tiles() const {
    return tiles_;
  }
  std::optional<size_t> findField(const ScalarField& field) const;

  // Every setter below lays out or recolors what changed, then renders.
  void setPaneMode(size_t pane, PaneMode mode);
  // The pane's field at position, or plain geometry for -1 in a Single pane; a Split pane
  // shows the page holding it.
  void showField(size_t pane, int position);
  // The pane's page that many pages on, wrapping both ways.
  void stepPane(size_t pane, int step);
  // While linked, showing a field in one pane shows it in every pane whose file has it,
  // and plain geometry in every Single pane; modes stay each pane's own.
  void setFieldsLinked(bool linked) {
    linked_ = linked;
  }
  bool fieldsLinked() const {
    return linked_;
  }
  // Lays the panes out again without rendering.
  void setLayoutMetrics(const LayoutMetrics& metrics);
  // The pane at display point (x, y), in pixels from the window's bottom-left.
  std::optional<size_t> paneAt(double x, double y) const;

  // The clip window of a continuous field, inside its full range.
  bool setClipRange(size_t field, double minValue, double maxValue);
  // A field's full range, which its clip window then spans: a range fixed across a time
  // series or pinned by the user rather than the loaded values'.
  bool setFieldRange(size_t field, double minValue, double maxValue);
  bool setColormap(size_t field, std::string_view id, bool reversed);
  // How a continuous field draws values outside its clip window.
  bool setOutOfRange(size_t field, OutOfRange outside);
  // The palette of this id for a categorical field, or its custom colors again for
  // kCustomPaletteId.
  bool setCategoryPalette(size_t field, std::string_view id);
  // Recolor one category, which makes the field's colors custom; std::nullopt gives it its
  // palette color back (see recolorCategory). NaN changes the separate missing-value color.
  bool
  setCategoryColor(size_t field, double value, const std::optional<std::array<double, 3>>& rgb);
  // The field's histogram over its full range (see computeScalarHistogram).
  std::vector<size_t> fieldHistogram(size_t field, size_t bins) const;
  // Analyze a field again after its values changed, keeping its colormap and palette.
  void reanalyzeField(size_t field);

  // Color the primary mesh in the first tile by a categorical cell array through an
  // externally owned LUT, so the value-to-color mapping stays fixed while the array is
  // edited (annotation mode). A null lut gives the tile its field back.
  void setLabelColoring(const std::string& arrayName, vtkLookupTable* lut, const double range[2]);
  // Draw again after the specified mesh's data changed (e.g. a new playback frame),
  // keeping every color range.
  void refreshAfterDataChange(size_t mesh);

  // How every mesh is drawn in every tile; kept for meshes set up later.
  void setSurfaceLook(const SurfaceLook& look);
  const SurfaceLook& getSurfaceLook() const {
    return surfaceLook_;
  }
  bool setPartVisible(size_t partIndex, bool visible);
  // Validate the whole batch before applying it; render once after all actors agree.
  bool setPartsVisible(const std::vector<PartVisibilityChange>& changes);

  // Draw a short line glyph per cell (or per point) tangent to the surface, one
  // for each tuple of a 3-component array, on every mesh that has it. Empty name
  // removes the glyphs.
  bool setVectorGlyphs(const std::string& name, FieldAssociation association);

  // Lay out again, then re-frame unless the camera moved since vv last framed it; the
  // first fit can run before the widget is laid out.
  void refitCameraIfUntouched();
  // Back to the opening view: looking down -Z with +Y up, every mesh framed.
  void resetCamera();
  // Move the camera toward (factor > 1) or away from its focal point; renders.
  void dolly(double factor);

  // The mesh cell at display point (x, y), in pixels from the window's bottom-left,
  // with the value there of the field its tile shows. std::nullopt off every mesh.
  // Picks without rendering.
  std::optional<ProbeHit> probe(double x, double y);
  // What probe reads at a hit it gave before, from the meshes as they are now (a later
  // frame or another field). std::nullopt for a mesh or tile it does not show.
  std::optional<ProbeHit> probeAgain(const ProbeHit& hit) const;
  // Read each shown tile at the same world position, preserving its own field and cell.
  // Equal cell IDs in different files do not establish correspondence.
  std::vector<ProbeHit> matchingProbes(const ProbeHit& hit);
  // Markers at matching hits; std::nullopt removes them. Renders.
  void setPinMarker(const std::optional<ProbeHit>& hit);
  // Where a hit probe gave shows now, in display pixels from the window's bottom-left;
  // std::nullopt when it lies behind the camera or its tile is not shown.
  std::optional<std::array<double, 2>> displayPosition(const ProbeHit& hit) const;

  RendererContext context;

  // Annotation mode needs the first tile's renderer (for picking) and the primary
  // dataset; it is only enabled for a single file in a Single pane.
  vtkRenderer* getRenderer() const {
    return tiles_.empty() ? nullptr : tiles_.front().renderer.Get();
  }
  vtkDataSet* getPrimaryMesh() const {
    return sceneMeshes.empty() ? nullptr : sceneMeshes.front();
  }

private:
  struct ActorSlot {
    size_t mesh = 0; // index into the meshes given to setup
    size_t tile = 0;
  };
  // Glass's stages for one mesh, made with the scene; they run only while Glass shows.
  struct GlassStages {
    vtkSmartPointer<vtkDataSet> mesh;
    vtkSmartPointer<vtkPolyDataNormals> normals; // smooth shading; polydata without strips
    vtkSmartPointer<vtkFeatureEdges> borders;    // open borders; polydata only
  };
  struct PinMarker {
    size_t actor;
    vtkSmartPointer<vtkActor> marker;
  };
  struct LabelColoring {
    ScalarField field;
    vtkSmartPointer<vtkLookupTable> lut;
    double range[2] = {0.0, 1.0};
  };

  void analyze(SceneField& field) const; // its categories, range and table
  void buildTiles(const std::vector<std::array<double, 3>>& colorsHex);
  void frameScene();
  // The panes' grid, each pane's page and tiles, and the field every shown tile draws.
  void layout();
  void colorTile(size_t tile);
  void colorActor(size_t actor);
  bool recolor(size_t field); // after its table or range changed; renders
  bool retable(size_t field); // after its colormap or category colors changed; renders
  void render();
  void applySurfaceLook(); // to every actor and tile, without rendering
  // After the scene's actors exist and before applySurfaceLook.
  void buildGlassStages();
  void applyGlass(size_t actorIndex, bool on);
  // Fills hit's field, value and category from data, the mesh the actor draws.
  void readProbedField(ProbeHit& hit, size_t actorIndex, vtkDataSet* data) const;
  void updatePinMarkers();

  std::vector<vtkSmartPointer<vtkDataSet>> sceneMeshes;
  std::vector<SceneField> fields_;
  std::vector<Pane> panes_;
  std::vector<Tile> tiles_;
  std::vector<vtkSmartPointer<vtkDataSetMapper>> mappers; // parallel to context.actors
  std::vector<ActorSlot> actorSlots_;                     // parallel to context.actors
  // Clears the whole window before the tiles draw, so a cell no tile covers keeps
  // nothing an earlier layout drew there.
  vtkSmartPointer<vtkRenderer> background_;
  vtkSmartPointer<vtkCamera> camera_; // shared by every tile
  vtkSmartPointer<vtkRenderWindowInteractor> interactor;
  LayoutMetrics metrics_;
  bool linked_ = false;
  ScalarAnalysis sharedCatAnalysis; // non-empty = override per-scalar detection
  std::optional<LabelColoring> labels_;
  SurfaceLook surfaceLook_;
  std::vector<GlassStages> glassStages_; // by mesh index
  // Parallel to context.actors; null where the mesh has no borders stage.
  std::vector<vtkSmartPointer<vtkActor>> outlines_;
  vtkSmartPointer<vtkCellPicker> probePicker_;
  std::vector<vtkSmartPointer<vtkStaticCellLocator>> probeLocators_;
  std::vector<PinMarker> pinMarkers_;
  std::optional<ProbeHit> markerHit_;
  double sceneBounds_[6] = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
  vtkSmartPointer<vtkCallbackCommand> clipCb_;
  vtkMTimeType fittedCameraMTime_ = 0;
  std::vector<std::pair<vtkRenderer*, vtkSmartPointer<vtkActor>>> glyphActors_;
};
