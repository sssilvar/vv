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
    const std::vector<std::string> &,
    const std::vector<std::array<double, 3>> &colorsHex) {
  if (polys.empty())
    return;

  std::set<std::string> sset;
  for (auto &p : polys) {
    auto *pd = p->GetPointData();
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
      if (auto *a = pd->GetArray(i))
        if (a->GetName())
          sset.insert(a->GetName());
  }
  std::vector<std::string> scalars(sset.begin(), sset.end());
  if (scalars.empty())
    return;

  context.window = vtkSmartPointer<vtkRenderWindow>::New();
  context.window->SetWindowName(
      (std::string(kVVWindowTitle) + " - Exploded (facet) view").c_str());
  auto sw = context.window->GetScreenSize();
  context.window->SetSize(std::min(sw[0], 1400), std::min(sw[1], 1200));
  context.window->SetPosition(80, 60);

  const int n = static_cast<int>(scalars.size());
  const int cols =
      static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
  const int rows = static_cast<int>(std::ceil(static_cast<double>(n) / cols));

  struct R {
    double lo, hi;
    bool ok;
  };
  std::vector<R> ranges(n, {0, 0, false});
  for (int i = 0; i < n; ++i) {
    double lo = 0, hi = 0;
    bool any = false;
    for (auto &p : polys)
      if (auto *a = p->GetPointData()->GetArray(scalars[i].c_str())) {
        double r[2];
        a->GetRange(r);
        if (!any) {
          lo = r[0];
          hi = r[1];
          any = true;
        } else {
          lo = std::min(lo, r[0]);
          hi = std::max(hi, r[1]);
        }
      }
    ranges[i] = {lo, hi, any && hi > lo};
  }

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

    vtkSmartPointer<vtkLookupTable> lut;
    if (ranges[i].ok) {
      lut = vtkSmartPointer<vtkLookupTable>::New();
      lut->SetNumberOfTableValues(256);
      lut->SetRange(ranges[i].lo, ranges[i].hi);
      lut->SetHueRange(0.0, 0.8);
      double meshColor[3];
      vtkNew<vtkNamedColors> colors;
      colors->GetColorRGB("Grey", meshColor);
      double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
      lut->SetNanColor(nanColor);
      lut->Build();
    }

    for (size_t j = 0; j < polys.size(); ++j) {
      auto& srcPoly = polys[j];
      vtkNew<vtkPolyData> poly;
      poly->ShallowCopy(srcPoly); // fix: each actor sees correct scalar
      poly->GetPointData()->SetActiveScalars(scalars[i].c_str());
      vtkNew<vtkPolyDataMapper> mapper;
      mapper->SetInputData(poly);
      mapper->SelectColorArray(scalars[i].c_str());
      mapper->SetScalarModeToUsePointData();
      mapper->SetColorModeToMapScalars();
      auto* arr = poly->GetPointData()->GetArray(scalars[i].c_str());
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
      } else {
          mapper->ScalarVisibilityOff();
      }
      auto color = (j < colorsHex.size()) ? colorsHex[j] : generateDistinctColor(static_cast<int>(j));
      vtkNew<vtkActor> actor;
      actor->SetMapper(mapper);
      actor->GetProperty()->SetColor(color[0], color[1], color[2]);
      ren->AddActor(actor);
      mappers.push_back(mapper);
      context.actors.push_back(actor);
    }

    if (lut) {
      auto bar = vtkSmartPointer<vtkScalarBarActor>::New();
      bar->SetLookupTable(lut);
      bar->SetTitle(scalars[i].c_str());
      bar->SetNumberOfLabels(4);
      ren->AddActor2D(bar);
    }
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