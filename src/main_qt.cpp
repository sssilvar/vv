#include "MeshLoading.h"
#include "ViewerWindow.h"
#include "version.h"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileOpenEvent>
#include <QString>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cxxopts.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkWindowToImageFilter.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

std::string QStringToUtf8(const QString& value) {
  const QByteArray bytes = value.toUtf8();
  return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

struct Args {
  std::vector<std::string> meshfiles;
  bool explode_view = false;
  bool common_cat_lut = false;
  bool annotate = false;
  std::string range; // "min,max": pin the color range for continuous fields
  bool version = false;
  bool help = false;
  std::string thumbnail_output; // non-empty → offscreen render to PNG and exit
};

// requireFiles=false used on macOS where the file may arrive via QFileOpenEvent instead of argv.
Args parseArgs(int argc, char* argv[], bool requireFiles = true) {
  Args args;
  cxxopts::Options options("vv", "A Qt-based mesh viewer");
  options.positional_help("<meshfile> [<meshfile2> ...]");
  options.add_options()(
      "e,explode", "Explode scalar view", cxxopts::value<bool>(args.explode_view))(
      "C,common-cat-lut",
      "Share one categorical colormap across all categorical scalars for cross-scalar comparison",
      cxxopts::value<bool>(args.common_cat_lut))(
      "a,annotate",
      "Annotation mode: paint a per-cell 'label' array with a surface brush",
      cxxopts::value<bool>(args.annotate))(
      "r,range",
      "Pin the color range for continuous scalars, e.g. -80,40",
      cxxopts::value<std::string>(args.range))(
      "v,version", "Show version and exit", cxxopts::value<bool>(args.version))(
      "h,help", "Show help and exit", cxxopts::value<bool>(args.help))(
      "T,thumbnail",
      "Render offscreen thumbnail to PNG (macOS Quick Look)",
      cxxopts::value<std::string>(args.thumbnail_output))(
      "meshfiles", "Mesh files or '-'", cxxopts::value<std::vector<std::string>>(args.meshfiles));
  options.parse_positional({"meshfiles"});

  try {
    options.parse(argc, argv);
    // cxxopts exceptions all derive from std::exception; catching the base type
    // works across cxxopts versions (older ones lack the cxxopts::exceptions
    // namespace) and keeps the Ubuntu/Docker system-cxxopts build green.
  } catch (const std::exception& e) {
    std::cerr << "vv: " << e.what() << "\n\n" << options.help() << '\n';
    std::exit(1);
  }

  if (args.help) {
    std::cout << options.help() << '\n';
    std::exit(0);
  }
  if (args.version) {
    std::cout << "vv version " << VV_VERSION << " (built " << VV_BUILD_DATE << ")\n";
    std::exit(0);
  }
  if (args.meshfiles.empty() && requireFiles && args.thumbnail_output.empty()) {
    std::cerr << "Usage: vv <meshfile> [<meshfile2> ...]\n" << options.help() << '\n';
    std::exit(1);
  }
  return args;
}

// Offscreen render of meshFile → PNG at outPath. Used by the macOS QLGenerator.
int renderThumbnail(const std::string& meshFile, const std::string& outPath) {
  MeshLoadResult result = loadMeshes({meshFile}, false);
  if (!result.ok || result.meshes.meshes.empty()) {
    std::cerr << "vv --thumbnail: failed to load " << meshFile << "\n";
    return 1;
  }

  vtkNew<vtkRenderer> renderer;
  renderer->SetBackground(0.15, 0.15, 0.15);

  for (auto& mesh : result.meshes.meshes) {
    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputDataObject(mesh);
    mapper->ScalarVisibilityOff();
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    renderer->AddActor(actor);
  }
  renderer->ResetCamera();

  vtkNew<vtkRenderWindow> window;
  window->SetOffScreenRendering(1);
  window->SetSize(1024, 768);
  window->AddRenderer(renderer);
  window->Render();

  vtkNew<vtkWindowToImageFilter> w2i;
  w2i->SetInput(window);
  w2i->Update();

  vtkNew<vtkPNGWriter> writer;
  writer->SetFileName(outPath.c_str());
  writer->SetInputConnection(w2i->GetOutputPort());
  writer->Write();
  return 0;
}

} // namespace

#ifdef __APPLE__
namespace {
void addMacOSQtPluginPath() {
  uint32_t pathSize = 0;
  _NSGetExecutablePath(nullptr, &pathSize);
  std::vector<char> executablePath(pathSize);
  if (_NSGetExecutablePath(executablePath.data(), &pathSize) != 0) {
    return;
  }

  char* resolvedPath = realpath(executablePath.data(), nullptr);
  if (resolvedPath == nullptr) {
    return;
  }
  const QString executable = QString::fromUtf8(resolvedPath);
  std::free(resolvedPath);

  const qsizetype separator = executable.lastIndexOf(QLatin1Char('/'));
  if (separator < 0) {
    return;
  }
  const QString plugins =
      QDir::cleanPath(executable.left(separator) + QStringLiteral("/../PlugIns"));
  if (QDir(plugins).exists()) {
    QCoreApplication::addLibraryPath(plugins);
  }
}
} // namespace

// Subclass to capture QFileOpenEvent (sent by macOS when user opens a file in Finder).
class VVApplication : public QApplication {
public:
  using QApplication::QApplication;
  QString fileToOpen;

protected:
  bool event(QEvent* e) override {
    if (e->type() == QEvent::FileOpen) {
      fileToOpen = static_cast<QFileOpenEvent*>(e)->file();
      return true;
    }
    return QApplication::event(e);
  }
};
#endif

#ifdef _WIN32
namespace {
void addWindowsQtPluginPath() {
  wchar_t buf[MAX_PATH];
  if (GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0U) {
    return;
  }
  QString exe = QString::fromWCharArray(buf);
  const qsizetype sep =
      std::max(exe.lastIndexOf(QLatin1Char('/')), exe.lastIndexOf(QLatin1Char('\\')));
  if (sep < 0) {
    return;
  }
  QCoreApplication::addLibraryPath(exe.left(sep) + QStringLiteral("/plugins"));
}
} // namespace
#endif

// ═════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) try {
#ifdef __APPLE__
  // Strip the macOS -psn_XXXX process serial number argument before option parsing.
  std::vector<char*> filteredArgv;
  for (int i = 0; i < argc; ++i) {
    if (std::string(argv[i]).substr(0, 5) != "-psn_") {
      filteredArgv.push_back(argv[i]);
    }
  }
  int filteredArgc = static_cast<int>(filteredArgv.size());
  Args args = parseArgs(filteredArgc, filteredArgv.data(), /*requireFiles=*/false);
#else
  Args args = parseArgs(argc, argv);
#endif

  // --thumbnail mode: offscreen render, no GUI needed.
  if (!args.thumbnail_output.empty()) {
    if (args.meshfiles.empty()) {
      std::cerr << "Usage: vv --thumbnail <meshfile> <output.png>\n";
      return 1;
    }
    return renderThumbnail(args.meshfiles.front(), args.thumbnail_output);
  }

  if (args.meshfiles.size() > 1 && !args.explode_view) {
    std::cerr << "Warning: Multiple mesh files provided without -e flag. "
                 "Using only the first file.\n";
  }

  // ── Qt + VTK setup ────────────────────────────────────────────
  QSurfaceFormat format = QVTKOpenGLNativeWidget::defaultFormat();
  format.setSwapInterval(0);
  format.setSamples(0);
  QSurfaceFormat::setDefaultFormat(format);
#ifdef _WIN32
  addWindowsQtPluginPath();
#elif defined(__APPLE__)
  addMacOSQtPluginPath();
#endif

#ifdef __APPLE__
  VVApplication app(filteredArgc, filteredArgv.data());
  // Drain the Apple Event queue so QFileOpenEvent (Finder double-click) is delivered
  // before we check for files.
  QCoreApplication::processEvents();
  if (!app.fileToOpen.isEmpty() && args.meshfiles.empty()) {
    args.meshfiles.push_back(QStringToUtf8(app.fileToOpen));
  }
#else
  QApplication app(argc, argv);
#endif

  // No file from argv or Apple Event → file picker + explode option dialog.
  if (args.meshfiles.empty()) {
    QString path = QFileDialog::getOpenFileName(
        nullptr,
        "Open mesh file",
        QString(),
        "Mesh files (*.vtk *.vtp *.vtu *.vtkhdf *.ply *.k *.key *.json);;All files (*)");
    if (path.isEmpty())
      return 0;

    // Small options dialog after file selection.
    QDialog opts;
    opts.setWindowTitle("Open options");
    auto* vl = new QVBoxLayout(&opts);
    auto* explodeCheck = new QCheckBox("Explode scalar view (-e)", &opts);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &opts);
    vl->addWidget(explodeCheck);
    vl->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &opts, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &opts, &QDialog::reject);
    if (opts.exec() != QDialog::Accepted)
      return 0;

    args.meshfiles.push_back(QStringToUtf8(path));
    args.explode_view = explodeCheck->isChecked();
  }

  MeshLoadResult loadResult = loadMeshes(args.meshfiles, args.explode_view);
  if (!loadResult.ok) {
    std::cerr << loadResult.error << '\n';
    return loadResult.exitCode;
  }

  ViewerOptions viewerOptions;
  if (!args.range.empty()) {
    double lo = 0.0, hi = 0.0;
    if (std::sscanf(args.range.c_str(), "%lf,%lf", &lo, &hi) != 2 || lo >= hi) {
      std::cerr << "vv: --range expects min,max with min < max (e.g. -80,40)\n";
      return 1;
    }
    viewerOptions.hasFixedRange = true;
    viewerOptions.fixedRange[0] = lo;
    viewerOptions.fixedRange[1] = hi;
  }
  viewerOptions.explodeView = args.explode_view;
  viewerOptions.commonCatLut = args.common_cat_lut;
  viewerOptions.annotate = args.annotate && !args.explode_view;
  if (args.annotate && args.explode_view) {
    std::cerr << "Warning: --annotate is not supported with --explode; ignoring --annotate.\n";
  }

  ViewerWindow window(std::move(loadResult), viewerOptions);
  window.show();
  return QApplication::exec();
} catch (const std::exception& e) {
  std::cerr << "vv: fatal: " << e.what() << '\n';
  return 1;
}
