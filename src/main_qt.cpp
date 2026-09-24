#include "MeshLoading.h"
#include "ViewerWindow.h"
#include "version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QSurfaceFormat>
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
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

struct Args {
  std::vector<std::string> meshfiles;
  bool explode_view = false;
  bool common_cat_lut = false;
  bool annotate = false;
  std::string range; // "min,max": pin the color range for continuous fields
  bool version = false;
  bool help = false;
};

Args parseArgs(int argc, char* argv[]) {
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
  if (args.meshfiles.empty()) {
    std::cerr << "Usage: vv <meshfile> [<meshfile2> ...]\n" << options.help() << '\n';
    std::exit(1);
  }
  return args;
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
  Args args = parseArgs(argc, argv);

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

  QApplication app(argc, argv);

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
