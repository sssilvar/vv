#include "MeshLoading.h"
#include "TemporalSource.h"
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
#include <array>
#include <cmath>
#include <cstdlib>
#include <cxxopts.hpp>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

struct Args {
  std::vector<std::string> meshfiles;
  bool explode_view = false;
  bool overlay = false;
  bool common_cat_lut = false;
  bool annotate = false;
  std::optional<std::array<double, 2>> range;
  bool version = false;
  bool help = false;
};

// Accepts exactly "min,max" with finite min < max; the classic locale keeps the
// decimal separator independent of the user's environment.
std::optional<std::array<double, 2>> parseRange(const std::string& text) {
  std::istringstream input(text);
  input.imbue(std::locale::classic());
  std::array<double, 2> range{};
  char delimiter = '\0';
  if (!(input >> range[0] >> delimiter >> range[1]) || delimiter != ',' ||
      !std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] >= range[1]) {
    return std::nullopt;
  }
  // Skipping trailing whitespace may set failbit once eof is reached; only eof matters.
  input >> std::ws;
  if (!input.eof()) {
    return std::nullopt;
  }
  return range;
}

Args parseArgs(int argc, char* argv[]) {
  Args args;
  std::string range;
  cxxopts::Options options("vv", "A Qt-based mesh viewer");
  options.positional_help("<meshfile> [<meshfile2> ...]");
  options.add_options()(
      "e,explode",
      "One panel per scalar; with several files, a row per file and a column per scalar",
      cxxopts::value<bool>(args.explode_view))(
      "o,overlay",
      "Show all files together in one view instead of side by side",
      cxxopts::value<bool>(args.overlay))(
      "C,common-cat-lut",
      "Share one categorical colormap across all categorical scalars for cross-scalar comparison",
      cxxopts::value<bool>(args.common_cat_lut))(
      "a,annotate",
      "Start in annotation mode, painting a per-cell 'label' array (B toggles it)",
      cxxopts::value<bool>(args.annotate))(
      "r,range",
      "Pin the color range for continuous scalars, e.g. -80,40",
      cxxopts::value<std::string>(range))(
      "v,version", "Show version and exit", cxxopts::value<bool>(args.version))(
      "h,help", "Show help and exit", cxxopts::value<bool>(args.help))(
      "meshfiles",
      "Mesh files (several open side by side) or '-'",
      cxxopts::value<std::vector<std::string>>(args.meshfiles));
  options.parse_positional({"meshfiles"});

  bool hasRange = false;
  try {
    hasRange = options.parse(argc, argv).count("range") != 0;
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
  if (hasRange) {
    args.range = parseRange(range);
    if (!args.range) {
      std::cerr << "vv: --range expects finite min,max with min < max (e.g. -80,40)\n";
      std::exit(1);
    }
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

int main(int argc, char* argv[]) try {
  Args args = parseArgs(argc, argv);

  MeshLoadResult loadResult = loadMeshes(args.meshfiles);
  if (!loadResult.ok) {
    std::cerr << loadResult.error << '\n';
    return loadResult.exitCode;
  }
  const bool volumes = !loadResult.volumes.empty();
  if (volumes && args.explode_view) {
    std::cerr << "Warning: slice views are not shown with --explode.\n";
  }
  // The slice views share one 3-D view, which shows every file.
  if (args.overlay || (volumes && !args.explode_view)) {
    overlayGroups(loadResult.meshes);
  }

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

  ViewerOptions viewerOptions;
  viewerOptions.fixedRange = args.range;
  viewerOptions.explodeView = args.explode_view;
  viewerOptions.commonCatLut = args.common_cat_lut;
  const bool singleFile = args.meshfiles.size() == 1;
  const bool timeSeries = loadResult.temporal && loadResult.temporal->playable();
  viewerOptions.annotate = args.annotate && !args.explode_view && singleFile && !timeSeries;
  if (args.annotate && args.explode_view) {
    std::cerr << "Warning: --annotate is not supported with --explode; ignoring --annotate.\n";
  } else if (args.annotate && !singleFile) {
    std::cerr << "Warning: --annotate needs a single mesh file; ignoring --annotate.\n";
  } else if (args.annotate && timeSeries) {
    std::cerr << "Warning: --annotate is not supported for a time series; ignoring --annotate.\n";
  }

  ViewerWindow window(std::move(loadResult), viewerOptions);
  window.show();
  return QApplication::exec();
} catch (const std::exception& e) {
  std::cerr << "vv: fatal: " << e.what() << '\n';
  return 1;
}
