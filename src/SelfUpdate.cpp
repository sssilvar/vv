#include "SelfUpdate.h"

#include "version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <array>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

constexpr const char* kRepo = "sssilvar/vv";

std::optional<std::array<long, 3>> parseVersion(std::string text) {
  if (!text.empty() && text.front() == 'v') {
    text.erase(0, 1);
  }
  std::istringstream input(text);
  std::array<long, 3> parts{};
  char dot1 = '\0';
  char dot2 = '\0';
  if (!(input >> parts[0] >> dot1 >> parts[1] >> dot2 >> parts[2]) || dot1 != '.' || dot2 != '.' ||
      parts[0] < 0 || parts[1] < 0 || parts[2] < 0) {
    return std::nullopt;
  }
  input.peek();
  if (!input.eof()) {
    return std::nullopt;
  }
  return parts;
}

// curl ships with macOS, Linux distributions and Windows 10+, which avoids
// linking Qt Network and an OpenSSL runtime into every package.
bool runTool(const QString& program, const QStringList& arguments, QByteArray* output = nullptr) {
  QProcess process;
  process.setProcessChannelMode(output ? QProcess::SeparateChannels : QProcess::ForwardedChannels);
  process.start(program, arguments);
  if (!process.waitForStarted(10000)) {
    std::cerr << "vv: cannot run " << program.toStdString() << '\n';
    return false;
  }
  // Downloads are bounded by curl's own --max-time.
  process.waitForFinished(-1);
  if (output) {
    *output = process.readAllStandardOutput();
    std::cerr << process.readAllStandardError().toStdString();
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::cerr << "vv: " << program.toStdString() << " failed\n";
    return false;
  }
  return true;
}

bool download(const QString& url, const QString& destination) {
  return runTool(QStringLiteral("curl"),
                 {QStringLiteral("--fail"),
                  QStringLiteral("--location"),
                  QStringLiteral("--progress-bar"),
                  QStringLiteral("--connect-timeout"),
                  QStringLiteral("15"),
                  QStringLiteral("--max-time"),
                  QStringLiteral("900"),
                  QStringLiteral("--output"),
                  destination,
                  url});
}

std::optional<std::string> latestTag() {
  QByteArray body;
  if (!runTool(QStringLiteral("curl"),
               {QStringLiteral("--fail"),
                QStringLiteral("--silent"),
                QStringLiteral("--show-error"),
                QStringLiteral("--location"),
                QStringLiteral("--max-time"),
                QStringLiteral("30"),
                QStringLiteral("--header"),
                QStringLiteral("Accept: application/vnd.github+json"),
                QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                    .arg(QLatin1String(kRepo))},
               &body)) {
    return std::nullopt;
  }
  const auto json = nlohmann::json::parse(body.toStdString(), nullptr, false);
  if (!json.is_object() || !json.contains("tag_name") || !json["tag_name"].is_string()) {
    std::cerr << "vv: unexpected response from GitHub\n";
    return std::nullopt;
  }
  return json["tag_name"].get<std::string>();
}

QString assetUrl(const std::string& tag, const QString& asset) {
  return QStringLiteral("https://github.com/%1/releases/download/%2/%3")
      .arg(QLatin1String(kRepo), QString::fromStdString(tag), asset);
}

// Rename-based swaps keep a running copy valid on Unix: the process keeps the
// old inode open while the path points at the new file.
bool replaceFile(const QString& replacement, const QString& target) {
  QFile::setPermissions(replacement, QFile::permissions(target));
  const QString backup = target + QStringLiteral(".old");
  QFile::remove(backup);
  if (!QFile::rename(target, backup)) {
    std::cerr << "vv: cannot replace " << target.toStdString() << '\n';
    return false;
  }
  if (!QFile::rename(replacement, target)) {
    QFile::rename(backup, target);
    std::cerr << "vv: cannot replace " << target.toStdString() << '\n';
    return false;
  }
  QFile::remove(backup);
  return true;
}

bool extract(const QString& archive, const QString& directory) {
  return runTool(QStringLiteral("tar"),
                 {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), directory});
}

// Release tarballs extract into a prefix as bin/vv -> ../lib/vv/bin/vv.
bool installMacOSBundle(const std::string& tag, const QString& executable) {
  QDir bundle = QFileInfo(executable).dir();
  if (bundle.dirName() != QLatin1String("bin") || !bundle.cdUp() ||
      bundle.dirName() != QLatin1String("vv") || !bundle.cdUp() ||
      bundle.dirName() != QLatin1String("lib") || !bundle.cdUp()) {
    std::cerr << "vv: " << executable.toStdString()
              << " is not a release install; update it the way it was installed\n";
    return false;
  }
  const QString prefix = bundle.absolutePath();
  QTemporaryDir work(prefix + QStringLiteral("/.vv-update-XXXXXX"));
  if (!work.isValid()) {
    std::cerr << "vv: cannot write to " << prefix.toStdString() << '\n';
    return false;
  }
  const QString archive = work.filePath(QStringLiteral("vv.tar.gz"));
  const QString asset =
      QStringLiteral("vv-%1-aarch64-apple-darwin.tar.gz").arg(QString::fromStdString(tag));
  if (!download(assetUrl(tag, asset), archive) || !extract(archive, work.path())) {
    return false;
  }
  const QString current = prefix + QStringLiteral("/lib/vv");
  const QString fresh = work.filePath(QStringLiteral("lib/vv"));
  const QString old = work.filePath(QStringLiteral("previous"));
  if (!QDir().rename(current, old)) {
    std::cerr << "vv: cannot replace " << current.toStdString() << '\n';
    return false;
  }
  if (!QDir().rename(fresh, current)) {
    QDir().rename(old, current);
    std::cerr << "vv: cannot replace " << current.toStdString() << '\n';
    return false;
  }
  return true;
}

// The portable .exe launches a copy unpacked under %LOCALAPPDATA%, so the
// file the user starts is unknown here; hand them the new one instead.
bool installWindowsPortable(const std::string& tag) {
  const QString asset = QStringLiteral("vv-%1-x86_64-windows.exe").arg(QString::fromStdString(tag));
  QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (folder.isEmpty()) {
    folder = QDir::currentPath();
  }
  const QString destination = QDir(folder).filePath(asset);
  if (!download(assetUrl(tag, asset), destination)) {
    return false;
  }
  std::cout << "Downloaded " << QDir::toNativeSeparators(destination).toStdString()
            << "; run it in place of the previous vv .exe\n";
  return true;
}

bool installLinuxBinary(const std::string& tag, const QString& executable) {
  const QString name = QString::fromStdString(tag);
  const QByteArray appImage = qgetenv("APPIMAGE");
  const QString target = appImage.isEmpty() ? executable : QString::fromLocal8Bit(appImage);
  QTemporaryDir work(QFileInfo(target).absolutePath() + QStringLiteral("/.vv-update-XXXXXX"));
  if (!work.isValid()) {
    std::cerr << "vv: cannot write next to " << target.toStdString() << '\n';
    return false;
  }
  if (!appImage.isEmpty()) {
    const QString fresh = work.filePath(QStringLiteral("vv.AppImage"));
    return download(
               assetUrl(tag, QStringLiteral("vv-%1-x86_64-unknown-linux-gnu.AppImage").arg(name)),
               fresh) &&
           replaceFile(fresh, target);
  }
  const QString archive = work.filePath(QStringLiteral("vv.tar.gz"));
  return download(
             assetUrl(tag, QStringLiteral("vv-%1-x86_64-unknown-linux-gnu-bin.tar.gz").arg(name)),
             archive) &&
         extract(archive, work.path()) && replaceFile(work.filePath(QStringLiteral("vv")), target);
}

// Chosen without #if: every platform compiles all three installers, so one
// that another platform runs cannot break unnoticed.
bool install(const std::string& tag, const QString& executable) {
  switch (QOperatingSystemVersion::currentType()) {
  case QOperatingSystemVersion::MacOS:
    return installMacOSBundle(tag, executable);
  case QOperatingSystemVersion::Windows:
    return installWindowsPortable(tag);
  default:
    return installLinuxBinary(tag, executable);
  }
}

} // namespace

std::optional<bool> isNewerVersion(const std::string& candidate, const std::string& current) {
  const auto a = parseVersion(candidate);
  const auto b = parseVersion(current);
  if (!a || !b) {
    return std::nullopt;
  }
  return *a > *b;
}

int runSelfUpdate(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: vv update\n";
    return 1;
  }
  QCoreApplication app(argc, argv);
  const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();

  const auto tag = latestTag();
  if (!tag) {
    return 1;
  }
  const auto newer = isNewerVersion(*tag, VV_VERSION);
  if (!newer) {
    std::cerr << "vv: cannot compare release " << *tag << " with " << VV_VERSION << '\n';
    return 1;
  }
  if (!*newer) {
    std::cout << "vv " << VV_VERSION << " is up to date\n";
    return 0;
  }
  std::cout << "Updating vv " << VV_VERSION << " to " << *tag << '\n';
  if (!install(*tag, executable)) {
    return 1;
  }
  std::cout << "vv updated to " << *tag << '\n';
  return 0;
}
