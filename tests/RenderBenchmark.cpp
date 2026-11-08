#include "MeshRenderer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vtkAppendPolyData.h>
#include <vtkCamera.h>
#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkInteractorStyle.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>
#include <vtkVersion.h>
#include <vtkWindowToImageFilter.h>

namespace {
bool save(const QJsonObject& result, const QString& path) {
  const QByteArray json = QJsonDocument(result).toJson();
  if (path.isEmpty()) {
    std::cout << json.constData();
    return true;
  }
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(json) == json.size();
}
} // namespace

int main(int argc, char** argv) {
  QSurfaceFormat format;
  format.setVersion(3, 2);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({"output", "Write benchmark JSON to this file.", "path"});
  parser.addOption({"require-rendering", "Fail if OpenGL 3.2 is unavailable."});
  parser.addOption({"smoke", "Use a smaller scene for software rendering checks."});
  parser.process(app);

  const bool smoke = parser.isSet("smoke");
  const int width = smoke ? 300 : 900;
  const int height = smoke ? 200 : 600;
  const int sphereResolution = smoke ? 24 : 64;
  QJsonObject result{{"os", QSysInfo::prettyProductName()},
                     {"architecture", QSysInfo::currentCpuArchitecture()},
                     {"qt", qVersion()},
                     {"vtk", vtkVersion::GetVTKVersion()},
                     {"workload", smoke ? "smoke" : "full"},
                     {"width", width},
                     {"height", height}};
  QOffscreenSurface surface;
  surface.setFormat(format);
  surface.create();
  QOpenGLContext context;
  context.setFormat(format);
  if (!context.create() || !context.makeCurrent(&surface) || context.format().majorVersion() < 3 ||
      (context.format().majorVersion() == 3 && context.format().minorVersion() < 2)) {
    result.insert("status", "unavailable");
    result.insert("reason", "The runner could not create an OpenGL 3.2 context.");
    const bool written = save(result, parser.value("output"));
    return written && !parser.isSet("require-rendering") ? 0 : 1;
  }
  auto* functions = context.functions();
  result.insert(
      "renderer",
      QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_RENDERER))));
  result.insert(
      "driver",
      QString::fromLatin1(reinterpret_cast<const char*>(functions->glGetString(GL_VERSION))));
  std::cerr << "OpenGL renderer: " << result.value("renderer").toString().toStdString() << " ("
            << result.value("driver").toString().toStdString() << ")" << std::endl;
  context.doneCurrent();

  QJsonArray measurements;
  bool validFrames = true;
  for (const int files : {1, 4}) {
    vtkNew<vtkRenderWindow> window;
    window->SetOffScreenRendering(1);
    window->SetSize(width, height);
    window->SetMultiSamples(0);
    vtkNew<vtkRenderWindowInteractor> interactor;
    MeshRenderer renderer;
    renderer.setRenderContext(window, interactor);
    vtkNew<vtkAppendPolyData> shells;
    for (int layer = 0; layer < 4; ++layer) {
      vtkNew<vtkSphereSource> sphere;
      sphere->SetThetaResolution(sphereResolution);
      sphere->SetPhiResolution(sphereResolution);
      sphere->SetRadius(1.0 - layer * 0.15);
      sphere->SetCenter(layer * 0.06, 0.0, 0.0);
      shells->AddInputConnection(sphere->GetOutputPort());
    }
    shells->Update();
    vtkPolyData* mesh = shells->GetOutput();
    result.insert("points_per_mesh", static_cast<int>(mesh->GetNumberOfPoints()));
    result.insert("triangles_per_mesh", static_cast<int>(mesh->GetNumberOfCells()));
    vtkNew<vtkFloatArray> voltage;
    voltage->SetName("voltage");
    for (vtkIdType i = 0; i < mesh->GetNumberOfPoints(); ++i) {
      voltage->InsertNextValue(static_cast<float>(mesh->GetPoint(i)[0]));
    }
    mesh->GetPointData()->AddArray(voltage);
    std::vector<vtkSmartPointer<vtkDataSet>> meshes(static_cast<size_t>(files), mesh);
    std::vector<MeshGroup> groups;
    for (int i = 0; i < files; ++i) {
      groups.push_back({"shells", {static_cast<size_t>(i)}});
    }
    renderer.setup(meshes,
                   groups,
                   std::vector<std::array<double, 3>>(static_cast<size_t>(files), {0.0, 0.5, 1.0}));
    renderer.setActiveScalar("voltage", FieldAssociation::Point);
    auto* style = vtkInteractorStyle::SafeDownCast(interactor->GetInteractorStyle());
    for (const SurfaceStyle look : {SurfaceStyle::Solid, SurfaceStyle::XRay, SurfaceStyle::Glass}) {
      const char* name = look == SurfaceStyle::Glass  ? "glass"
                         : look == SurfaceStyle::XRay ? "xray"
                                                      : "solid";
      std::cerr << "Rendering " << files << "-file " << name << " scene at " << width << 'x'
                << height << std::endl;
      const auto styleStart = std::chrono::steady_clock::now();
      renderer.setSurfaceLook({look, 0.4, 0.5});
      window->WaitForCompletion();
      const double styleMs =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - styleStart)
              .count();
      for (const bool moving : {false, true}) {
        if (moving) {
          style->StartRotate();
        }
        std::vector<double> samples;
        for (int frame = 0; frame < 23; ++frame) {
          if (moving) {
            renderer.getRenderer()->GetActiveCamera()->Azimuth(0.5);
          }
          const auto start = std::chrono::steady_clock::now();
          window->Render();
          window->WaitForCompletion();
          const double ms =
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                  .count();
          if (frame >= 3) {
            samples.push_back(ms);
          }
        }
        const int peelingBudget = renderer.getRenderer()->GetMaximumNumberOfPeels();
        vtkNew<vtkWindowToImageFilter> capture;
        capture->SetInput(window);
        capture->ReadFrontBufferOff();
        capture->ShouldRerenderOff();
        capture->Update();
        auto* pixels = capture->GetOutput()->GetPointData()->GetScalars();
        bool nonempty = false;
        for (vtkIdType pixel = 0; pixels && pixel < pixels->GetNumberOfTuples(); pixel += 17) {
          if (pixels->GetComponent(pixel, 0) + pixels->GetComponent(pixel, 1) +
                  pixels->GetComponent(pixel, 2) >
              0.0) {
            nonempty = true;
            break;
          }
        }
        validFrames = validFrames && nonempty;
        std::sort(samples.begin(), samples.end());
        measurements.append(QJsonObject{
            {"files", files},
            {"style", name},
            {"moving", moving},
            {"samples", static_cast<int>(samples.size())},
            {"style_change_ms", styleMs},
            {"nonempty_frame", nonempty},
            {"peel_budget", peelingBudget},
            {"p50_ms", (samples[9] + samples[10]) / 2.0},
            {"p95_ms", samples[18]},
            {"depth_peeling", renderer.getRenderer()->GetLastRenderingUsedDepthPeeling() != 0}});
        if (moving) {
          style->StopState();
        }
      }
    }
  }
  result.insert("status", validFrames ? "measured" : "empty-frame");
  result.insert("measurements", measurements);
  return save(result, parser.value("output")) && validFrames ? 0 : 1;
}
