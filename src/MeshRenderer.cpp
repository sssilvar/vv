#include "MeshRenderer.h"
#include "ScalarCyclerStyle.h"
#include "mesh_utils.h"
#include <set>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkLookupTable.h>
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>

const char *kVVWindowTitle = "VV mesh viewer";

MeshRenderer::~MeshRenderer() = default;
MeshRenderer::MeshRenderer() {}

void MeshRenderer::setup(const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
                         const std::vector<std::string> &names,
                         const std::vector<std::array<double, 3>> &colorsHex) {
  renderer = vtkSmartPointer<vtkRenderer>::New();

  context.window = vtkSmartPointer<vtkRenderWindow>::New();
  context.bar = vtkSmartPointer<vtkScalarBarActor>::New();
  context.bar->SetTitle("Scalars");
  context.bar->SetNumberOfLabels(5);
  renderer->AddActor2D(context.bar);
  context.bar->SetVisibility(false);

  mappers.clear();
  context.actors.clear();
  for (size_t i = 0; i < polys.size(); ++i) {
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(polys[i]);
    mapper->ScalarVisibilityOff();
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(colorsHex[i][0], colorsHex[i][1],
                                   colorsHex[i][2]);
    actor->GetProperty()->SetOpacity(1.0);
    renderer->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
  }
  context.colorsHex = colorsHex;

  context.window->AddRenderer(renderer);
  int screenWidth = 1200, screenHeight = 1024;
  context.window->SetSize(screenWidth, screenHeight);
  context.window->SetPosition(100, 100);
  context.window->SetWindowName(kVVWindowTitle);

  interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  interactor->SetRenderWindow(context.window);

  std::set<std::string> scalarNameSet;
  for (const auto &poly : polys) {
    vtkPointData *pd = poly->GetPointData();
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
      if (pd->GetArray(i) && pd->GetArray(i)->GetName())
        scalarNameSet.insert(pd->GetArray(i)->GetName());
  }
  std::vector<std::string> scalarNames(scalarNameSet.begin(),
                                       scalarNameSet.end());
  std::vector<vtkPolyDataMapper *> mapperPtrs;
  std::vector<vtkPolyData *> polyPtrs;
  for (size_t i = 0; i < mappers.size(); ++i) {
    mapperPtrs.push_back(mappers[i].GetPointer());
    polyPtrs.push_back(polys[i].GetPointer());
  }
  std::vector<vtkActor *> actorPtrs;
  for (size_t i = 0; i < context.actors.size(); ++i)
    actorPtrs.push_back(context.actors[i].GetPointer());
  vtkNew<ScalarCyclerStyle> style;
  if (!polys.empty() && !mappers.empty()) {
    style->initialize(context, scalarNames, mapperPtrs, polyPtrs);
    style->SetActors(actorPtrs, colorsHex);
  }
  interactor->SetInteractorStyle(style);

  if (!polys.empty() && !scalarNames.empty()) {
    const std::string &firstScalar = scalarNames[0];
    bool found = false;
    for (size_t i = 0; i < polys.size(); ++i) {
      auto *arr = polys[i]->GetPointData()->GetArray(firstScalar.c_str());
      if (arr) {
        polys[i]->GetPointData()->SetActiveScalars(firstScalar.c_str());
        mappers[i]->SelectColorArray(firstScalar.c_str());
        mappers[i]->SetScalarModeToUsePointData();
        mappers[i]->SetColorModeToMapScalars();
        mappers[i]->ScalarVisibilityOn();
        if (!found) {
          style->UpdateColorbar(firstScalar, true);
          context.bar->SetVisibility(true);
          found = true;
        }
      } else {
        mappers[i]->ScalarVisibilityOff();
      }
    }
  }
}

void MeshRenderer::start() {
  context.window->Render();
  interactor->Start();
}

#include <vtkCallbackCommand.h>
#include <vtkCommand.h>
#include <vtkCamera.h>
#include <vtkRendererCollection.h>
#include <vtkCollection.h>

void MeshRenderer::setupFacetGrid(
    const std::vector<vtkSmartPointer<vtkPolyData>> &polys,
    const std::vector<std::string> &names,
    const std::vector<std::array<double, 3>> &colorsHex) {
  if (polys.empty())
    return;

  // Collect all (mesh_index, scalar_name) pairs
  struct MeshScalarPair {
    size_t meshIndex;
    std::string scalarName;
    std::string meshName;
  };
  std::vector<MeshScalarPair> pairs;
  for (size_t j = 0; j < polys.size(); ++j) {
    auto *pd = polys[j]->GetPointData();
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
      if (auto *a = pd->GetArray(i)) {
        if (a->GetName()) {
          pairs.push_back({j, a->GetName(), names[j]});
        }
      }
    }
  }
  if (pairs.empty())
    return;

  context.window = vtkSmartPointer<vtkRenderWindow>::New();
  context.window->SetWindowName(
      (std::string(kVVWindowTitle) + " - Exploded (facet) view").c_str());
  auto sw = context.window->GetScreenSize();
  context.window->SetSize(std::min(sw[0], 1400), std::min(sw[1], 1200));
  context.window->SetPosition(80, 60);

  const int n = static_cast<int>(pairs.size());
  const int cols =
      static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
  const int rows = static_cast<int>(std::ceil(static_cast<double>(n) / cols));

  mappers.clear();
  context.actors.clear();
  context.colorsHex = colorsHex;

  for (int i = 0; i < n; ++i) {
    const int r = i / cols, c = i % cols;
    const double xmin = double(c) / cols, xmax = double(c + 1) / cols;
    const double ymin = 1.0 - double(r + 1) / rows,
                 ymax = 1.0 - double(r) / rows;

    auto ren = vtkSmartPointer<vtkRenderer>::New();
    ren->SetViewport(xmin, ymin, xmax, ymax);
    context.window->AddRenderer(ren);

    const auto& pair = pairs[i];
    auto& srcPoly = polys[pair.meshIndex];
    
    vtkNew<vtkPolyData> poly;
    poly->ShallowCopy(srcPoly);
    poly->GetPointData()->SetActiveScalars(pair.scalarName.c_str());
    
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(poly);
    mapper->SelectColorArray(pair.scalarName.c_str());
    mapper->SetScalarModeToUsePointData();
    mapper->SetColorModeToMapScalars();
    
    auto* arr = poly->GetPointData()->GetArray(pair.scalarName.c_str());
    if (arr) {
      double range[2];
      arr->GetRange(range);
      vtkNew<vtkLookupTable> lut;
      lut->SetNumberOfTableValues(256);
      lut->SetRange(range);
      lut->SetHueRange(0.0, 0.8);
      double meshColor[3];
      vtkNew<vtkNamedColors> colors;
      colors->GetColorRGB("Grey", meshColor);
      double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
      lut->SetNanColor(nanColor);
      lut->Build();
      mapper->SetLookupTable(lut);
      mapper->SetScalarRange(range);
      mapper->ScalarVisibilityOn();
      
      auto bar = vtkSmartPointer<vtkScalarBarActor>::New();
      bar->SetLookupTable(lut);
      std::string basename = pair.meshName;
      size_t lastSlash = basename.find_last_of("/\\");
      if (lastSlash != std::string::npos) {
        basename = basename.substr(lastSlash + 1);
      }
      bar->SetTitle((pair.scalarName + "\n(" + basename + ")").c_str());
      bar->SetNumberOfLabels(4);
      bar->SetWidth(0.05);
      bar->SetHeight(0.6);
      bar->SetUnconstrainedFontSize(true);
      bar->GetTitleTextProperty()->SetFontSize(24);
      bar->GetLabelTextProperty()->SetFontSize(24);
      ren->AddActor2D(bar);
      
    } else {
      mapper->ScalarVisibilityOff();
    }
    
    auto color = (pair.meshIndex < colorsHex.size()) ? colorsHex[pair.meshIndex] : generateDistinctColor(static_cast<int>(pair.meshIndex));
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color[0], color[1], color[2]);
    ren->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
  }

  vtkBoundingBox bbox;
  for (auto &p : polys) {
    double bb[6];
    p->GetBounds(bb);
    bbox.AddBounds(bb);
  }
  double ub[6];
  bbox.GetBounds(ub);

  auto tmplRen = vtkSmartPointer<vtkRenderer>::New();
  auto tmplCam = vtkSmartPointer<vtkCamera>::New();
  tmplRen->SetActiveCamera(tmplCam);
  tmplRen->ResetCamera(ub);

  auto *rens = context.window->GetRenderers();
  vtkCollectionSimpleIterator cookie;
  rens->InitTraversal(cookie);
  for (vtkRenderer *ren = rens->GetNextRenderer(cookie); ren;
       ren = rens->GetNextRenderer(cookie)) {
    auto cam = vtkSmartPointer<vtkCamera>::New();
    cam->DeepCopy(tmplCam);
    ren->SetActiveCamera(cam);
    ren->ResetCameraClippingRange(ub);
  }

  static vtkSmartPointer<vtkCallbackCommand> camLinkCb;
  if (!camLinkCb)
    camLinkCb = vtkSmartPointer<vtkCallbackCommand>::New();
  camLinkCb->SetClientData(context.window);
  camLinkCb->SetCallback(
      [](vtkObject *caller, unsigned long, void *cd, void *) {
        auto *src = vtkCamera::SafeDownCast(caller);
        auto *win = static_cast<vtkRenderWindow *>(cd);
        if (!src || !win)
          return;
        auto *r = win->GetRenderers();
        vtkCollectionSimpleIterator it;
        r->InitTraversal(it);
        for (vtkRenderer *ren = r->GetNextRenderer(it); ren;
             ren = r->GetNextRenderer(it)) {
          auto *cam = ren->GetActiveCamera();
          if (cam && cam != src)
            cam->DeepCopy(src);
        }
      });

  rens->InitTraversal(cookie);
  for (vtkRenderer *ren = rens->GetNextRenderer(cookie); ren;
       ren = rens->GetNextRenderer(cookie))
    ren->GetActiveCamera()->AddObserver(vtkCommand::ModifiedEvent, camLinkCb);

  interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  interactor->SetRenderWindow(context.window);
  auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  style->SetMotionFactor(10.0);
  interactor->SetInteractorStyle(style);
}

void MeshRenderer::startFacetGrid() {
  if (!context.window || !interactor)
    return;
  context.window->Render();
  interactor->Start();
}