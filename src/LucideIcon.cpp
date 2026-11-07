#include "LucideIcon.h"

#include "OverlayStyle.h"

#include <QByteArray>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>

namespace {

class PathReader {
public:
  explicit PathReader(std::string_view data) : data_(data) {}

  bool atEnd() {
    skipSeparators();
    return pos_ >= data_.size();
  }

  // The next command letter, or 0 when numbers follow and the previous command repeats.
  char command() {
    skipSeparators();
    if (pos_ < data_.size() && std::isalpha(static_cast<unsigned char>(data_[pos_]))) {
      return data_[pos_++];
    }
    return 0;
  }

  // SVG numbers need no separator where the syntax is unambiguous: "1.5.5" is
  // 1.5 then .5, "0-1.66" is 0 then -1.66.
  std::optional<double> number() {
    skipSeparators();
    size_t end = pos_;
    if (end < data_.size() && (data_[end] == '+' || data_[end] == '-')) {
      ++end;
    }
    bool digits = false;
    bool decimalPoint = false;
    for (; end < data_.size(); ++end) {
      const char c = data_[end];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        digits = true;
      } else if (c == '.' && !decimalPoint) {
        decimalPoint = true;
      } else {
        break;
      }
    }
    if (!digits) {
      return std::nullopt;
    }
    if (end < data_.size() && (data_[end] == 'e' || data_[end] == 'E')) {
      size_t exponent = end + 1;
      if (exponent < data_.size() && (data_[exponent] == '+' || data_[exponent] == '-')) {
        ++exponent;
      }
      const size_t first = exponent;
      while (exponent < data_.size() && std::isdigit(static_cast<unsigned char>(data_[exponent]))) {
        ++exponent;
      }
      if (exponent > first) {
        end = exponent;
      }
    }
    bool ok = false;
    // QByteArray::toDouble ignores the locale, unlike strtod once Qt has called setlocale.
    const double value =
        QByteArray(std::string(data_.substr(pos_, end - pos_)).c_str()).toDouble(&ok);
    if (!ok) {
      return std::nullopt;
    }
    pos_ = end;
    return value;
  }

  // Arc flags are single digits and may run into the next number ("0 011 1").
  std::optional<bool> flag() {
    skipSeparators();
    if (pos_ < data_.size() && (data_[pos_] == '0' || data_[pos_] == '1')) {
      return data_[pos_++] == '1';
    }
    return std::nullopt;
  }

  std::optional<QPointF> point() {
    const auto x = number();
    const auto y = x ? number() : std::nullopt;
    if (!y) {
      return std::nullopt;
    }
    return QPointF(*x, *y);
  }

private:
  void skipSeparators() {
    while (pos_ < data_.size() &&
           (std::isspace(static_cast<unsigned char>(data_[pos_])) || data_[pos_] == ',')) {
      ++pos_;
    }
  }

  std::string_view data_;
  size_t pos_ = 0;
};

// SVG's endpoint arc, converted to the center form QPainterPath draws
// (SVG 1.1 implementation notes, F.6.5 and F.6.6).
void appendArc(QPainterPath& path,
               const QPointF& from,
               double rx,
               double ry,
               double rotation,
               bool largeArc,
               bool sweep,
               const QPointF& to) {
  if (from == to) {
    return;
  }
  rx = std::abs(rx);
  ry = std::abs(ry);
  if (rx == 0.0 || ry == 0.0) {
    path.lineTo(to);
    return;
  }
  const double phi = qDegreesToRadians(rotation);
  const double cosPhi = std::cos(phi);
  const double sinPhi = std::sin(phi);
  const double dx = (from.x() - to.x()) / 2.0;
  const double dy = (from.y() - to.y()) / 2.0;
  const double x1 = cosPhi * dx + sinPhi * dy;
  const double y1 = -sinPhi * dx + cosPhi * dy;
  // Radii too small to reach the endpoint grow just enough to do so.
  const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (lambda > 1.0) {
    rx *= std::sqrt(lambda);
    ry *= std::sqrt(lambda);
  }
  const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  const double coef =
      (largeArc == sweep ? -1.0 : 1.0) * std::sqrt(std::max(0.0, numerator / denominator));
  const double cx1 = coef * rx * y1 / ry;
  const double cy1 = -coef * ry * x1 / rx;
  const double cx = cosPhi * cx1 - sinPhi * cy1 + (from.x() + to.x()) / 2.0;
  const double cy = sinPhi * cx1 + cosPhi * cy1 + (from.y() + to.y()) / 2.0;

  const double ux = (x1 - cx1) / rx;
  const double uy = (y1 - cy1) / ry;
  const double vx = (-x1 - cx1) / rx;
  const double vy = (-y1 - cy1) / ry;
  const double start = std::atan2(uy, ux);
  double span = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  if (!sweep && span > 0.0) {
    span -= 2.0 * M_PI;
  } else if (sweep && span < 0.0) {
    span += 2.0 * M_PI;
  }

  // Qt's angles turn the other way from SVG's on the same y-down canvas.
  const QRectF ellipse(-rx, -ry, 2.0 * rx, 2.0 * ry);
  QPainterPath arc;
  arc.arcMoveTo(ellipse, -qRadiansToDegrees(start));
  arc.arcTo(ellipse, -qRadiansToDegrees(start), -qRadiansToDegrees(span));
  // Only the curves, which start where path is: connectPath would drop a move-to just
  // before the arc and draw a line from the previous subpath instead.
  const QTransform toPath = QTransform().translate(cx, cy).rotate(rotation);
  for (int i = 0; i + 2 < arc.elementCount(); ++i) {
    if (arc.elementAt(i).type == QPainterPath::CurveToElement) {
      path.cubicTo(toPath.map(QPointF(arc.elementAt(i))),
                   toPath.map(QPointF(arc.elementAt(i + 1))),
                   toPath.map(QPointF(arc.elementAt(i + 2))));
      i += 2;
    }
  }
}

// Appends one SVG path element; like a separate <path>, it starts at the origin.
bool appendSvgPath(QPainterPath& path, std::string_view data) {
  PathReader in(data);
  QPointF current;
  QPointF subpathStart;
  QPointF lastControl; // second control point of the previous cubic, which S mirrors
  bool previousCubic = false;
  char command = 0;
  while (!in.atEnd()) {
    if (const char next = in.command()) {
      command = next;
    } else if (command == 0) {
      return false; // numbers before any command, or after Z
    }
    const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
    const QPointF origin = relative ? current : QPointF();
    bool cubic = false;
    switch (std::toupper(static_cast<unsigned char>(command))) {
    case 'M': {
      const auto to = in.point();
      if (!to) {
        return false;
      }
      current = origin + *to;
      subpathStart = current;
      path.moveTo(current);
      command = relative ? 'l' : 'L'; // further pairs are implicit line-tos
      break;
    }
    case 'L': {
      const auto to = in.point();
      if (!to) {
        return false;
      }
      current = origin + *to;
      path.lineTo(current);
      break;
    }
    case 'H': {
      const auto x = in.number();
      if (!x) {
        return false;
      }
      current.setX(origin.x() + *x);
      path.lineTo(current);
      break;
    }
    case 'V': {
      const auto y = in.number();
      if (!y) {
        return false;
      }
      current.setY(origin.y() + *y);
      path.lineTo(current);
      break;
    }
    case 'C':
    case 'S': {
      QPointF first = previousCubic ? 2.0 * current - lastControl : current;
      if (std::toupper(static_cast<unsigned char>(command)) == 'C') {
        const auto control = in.point();
        if (!control) {
          return false;
        }
        first = origin + *control;
      }
      const auto second = in.point();
      const auto to = second ? in.point() : std::nullopt;
      if (!to) {
        return false;
      }
      lastControl = origin + *second;
      current = origin + *to;
      path.cubicTo(first, lastControl, current);
      cubic = true;
      break;
    }
    case 'A': {
      const auto rx = in.number();
      const auto ry = rx ? in.number() : std::nullopt;
      const auto rotation = ry ? in.number() : std::nullopt;
      const auto largeArc = rotation ? in.flag() : std::nullopt;
      const auto sweep = largeArc ? in.flag() : std::nullopt;
      const auto to = sweep ? in.point() : std::nullopt;
      if (!to) {
        return false;
      }
      const QPointF end = origin + *to;
      appendArc(path, current, *rx, *ry, *rotation, *largeArc, *sweep, end);
      current = end;
      break;
    }
    case 'Z':
      path.closeSubpath();
      current = subpathStart;
      command = 0;
      break;
    default:
      return false;
    }
    previousCubic = cubic;
  }
  return true;
}

} // namespace

std::optional<QPainterPath> parseSvgPath(std::string_view data) {
  QPainterPath path;
  if (!appendSvgPath(path, data)) {
    return std::nullopt;
  }
  return path;
}

// Path data copied from lucide-static 1.70.0.
QPainterPath lucidePath(Lucide icon) {
  QPainterPath path;
  const auto add = [&path](std::string_view data) { appendSvgPath(path, data); };
  switch (icon) {
  case Lucide::ChevronLeft:
    add("m15 18-6-6 6-6");
    break;
  case Lucide::ChevronRight:
    add("m9 18 6-6-6-6");
    break;
  case Lucide::ChevronDown:
    add("m6 9 6 6 6-6");
    break;
  case Lucide::Check:
    add("M20 6 9 17l-5-5");
    break;
  case Lucide::Search:
    add("m21 21-4.34-4.34");
    path.addEllipse(QPointF(11.0, 11.0), 8.0, 8.0);
    break;
  case Lucide::Eye:
    add("M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 "
        "10.75 10.75 0 0 1-19.876 0");
    path.addEllipse(QPointF(12.0, 12.0), 3.0, 3.0);
    break;
  case Lucide::EyeOff:
    add("M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 "
        "2.49");
    add("M14.084 14.158a3 3 0 0 1-4.242-4.242");
    add("M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 "
        "4.446-5.143");
    add("m2 2 20 20");
    break;
  case Lucide::Layers:
    add("M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 "
        "0l8.58-3.9a1 "
        "1 0 0 0 0-1.83z");
    add("M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12");
    add("M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17");
    break;
  case Lucide::Palette:
    add("M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 0-1.4 2.8l.3.4"
        "a1.75 1.75 0 0 1-1.4 2.8z");
    for (const QPointF center :
         {QPointF(13.5, 6.5), QPointF(17.5, 10.5), QPointF(6.5, 12.5), QPointF(8.5, 7.5)}) {
      path.addEllipse(center, 0.5, 0.5);
    }
    break;
  case Lucide::Box:
    add("M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 "
        "0 "
        "0 2 0l7-4A2 2 0 0 0 21 16Z");
    add("m3.3 7 8.7 5 8.7-5");
    add("M12 22V12");
    break;
  case Lucide::Grid3x3:
    path.addRoundedRect(QRectF(3.0, 3.0, 18.0, 18.0), 2.0, 2.0);
    add("M3 9h18");
    add("M3 15h18");
    add("M9 3v18");
    add("M15 3v18");
    break;
  case Lucide::Globe:
    path.addEllipse(QPointF(12.0, 12.0), 10.0, 10.0);
    add("M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20");
    add("M2 12h20");
    break;
  case Lucide::ScanEye:
    add("M3 7V5a2 2 0 0 1 2-2h2");
    add("M17 3h2a2 2 0 0 1 2 2v2");
    add("M21 17v2a2 2 0 0 1-2 2h-2");
    add("M7 21H5a2 2 0 0 1-2-2v-2");
    path.addEllipse(QPointF(12.0, 12.0), 1.0, 1.0);
    add("M18.944 12.33a1 1 0 0 0 0-.66 7.5 7.5 0 0 0-13.888 0 1 1 0 0 0 0 .66 7.5 7.5 0 0 0 "
        "13.888 0");
    break;
  case Lucide::Gem:
    add("M10.5 3 8 9l4 13 4-13-2.5-6");
    add("M17 3a2 2 0 0 1 1.6.8l3 4a2 2 0 0 1 .013 2.382l-7.99 10.986a2 2 0 0 1-3.247 "
        "0l-7.99-10.986A2 2 0 0 1 2.4 7.8l2.998-3.997A2 2 0 0 1 7 3z");
    add("M2 9h20");
    break;
  case Lucide::Crosshair:
    path.addEllipse(QPointF(12.0, 12.0), 10.0, 10.0);
    add("M22 12L18 12");
    add("M6 12L2 12");
    add("M12 6L12 2");
    add("M12 22L12 18");
    break;
  case Lucide::Brush:
    add("m11 10 3 3");
    add("M6.5 21A3.5 3.5 0 1 0 3 17.5a2.62 2.62 0 0 1-.708 1.792A1 1 0 0 0 3 21z");
    add("M9.969 17.031 21.378 5.624a1 1 0 0 0-3.002-3.002L6.967 14.031");
    break;
  case Lucide::Rotate3d:
    add("m15.194 13.707 3.814 1.86-1.86 3.814");
    add("M16.47214 7.52786 A 5 10 0 1 0 13 21.79796");
    add("M21.79796 11 A 10 5 0 1 0 19 15.57071");
    break;
  case Lucide::Eraser:
    add("M21 21H8a2 2 0 0 1-1.42-.587l-3.994-3.999a2 2 0 0 1 0-2.828l10-10a2 2 0 0 1 2.829 0l5.999 "
        "6a2 2 0 0 1 0 2.828L12.834 21");
    add("m5.082 11.09 8.828 8.828");
    break;
  case Lucide::Undo2:
    add("M9 14 4 9l5-5");
    add("M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11");
    break;
  case Lucide::Save:
    add("M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 "
        "0 0 1 2-2z");
    add("M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7");
    add("M7 3v4a1 1 0 0 0 1 1h7");
    break;
  case Lucide::X:
    add("M18 6 6 18");
    add("m6 6 12 12");
    break;
  case Lucide::Wind:
    add("M12.8 19.6A2 2 0 1 0 14 16H2");
    add("M17.5 8a2.5 2.5 0 1 1 2 4H2");
    add("M9.8 4.4A2 2 0 1 1 11 8H2");
    break;
  case Lucide::RotateCcw:
    add("M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8");
    add("M3 3v5h5");
    break;
  case Lucide::Camera:
    add("M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 "
        "2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 "
        "4z");
    path.addEllipse(QPointF(12.0, 13.0), 3.0, 3.0);
    break;
  case Lucide::Play:
    add("M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z");
    break;
  case Lucide::Pause:
    path.addRoundedRect(QRectF(14.0, 3.0, 5.0, 18.0), 1.0, 1.0);
    path.addRoundedRect(QRectF(5.0, 3.0, 5.0, 18.0), 1.0, 1.0);
    break;
  case Lucide::ChevronFirst:
    add("m17 18-6-6 6-6");
    add("M7 6v12");
    break;
  case Lucide::ChevronLast:
    add("m7 18 6-6-6-6");
    add("M17 6v12");
    break;
  case Lucide::Repeat:
    add("m17 2 4 4-4 4");
    add("M3 11v-1a4 4 0 0 1 4-4h14");
    add("m7 22-4-4 4-4");
    add("M21 13v1a4 4 0 0 1-4 4H3");
    break;
  }
  return path;
}

namespace {

enum class Paint { Stroke, Fill };

void addPixmaps(QIcon& icon,
                const QPainterPath& path,
                int size,
                const QColor& color,
                QIcon::Mode mode,
                QIcon::State state,
                Paint paint) {
  // One pixmap per display scale, each drawn at its exact pixel size so the
  // strokes are never resampled.
  for (const int scale : {1, 2, 3}) {
    const int pixels = size * scale;
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(pixels / 24.0, pixels / 24.0);
    if (paint == Paint::Fill) {
      painter.fillPath(path, color);
    } else {
      painter.strokePath(path, QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.end();
    icon.addPixmap(pixmap, mode, state);
  }
}

} // namespace

QIcon lucideIcon(Lucide icon, int size) {
  const QPainterPath path = lucidePath(icon);
  QIcon result;
  addPixmaps(result, path, size, overlay::kIcon, QIcon::Normal, QIcon::Off, Paint::Stroke);
  addPixmaps(result, path, size, overlay::kIconChecked, QIcon::Normal, QIcon::On, Paint::Stroke);
  addPixmaps(
      result, path, size, overlay::kIconDisabled, QIcon::Disabled, QIcon::Off, Paint::Stroke);
  addPixmaps(result, path, size, overlay::kIconDisabled, QIcon::Disabled, QIcon::On, Paint::Stroke);
  return result;
}

QIcon lucideIcon(Lucide icon, int size, const QColor& color) {
  QIcon result;
  addPixmaps(result, lucidePath(icon), size, color, QIcon::Normal, QIcon::Off, Paint::Stroke);
  return result;
}

QIcon lucideSolidIcon(Lucide icon, int size, const QColor& color) {
  QIcon result;
  addPixmaps(result, lucidePath(icon), size, color, QIcon::Normal, QIcon::Off, Paint::Fill);
  return result;
}
