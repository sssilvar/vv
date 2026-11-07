#pragma once

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <optional>
#include <string_view>

// Icons from Lucide (https://lucide.dev, ISC license), kept as their SVG path
// data and stroked with QPainterPath, so vv needs no SVG module.
enum class Lucide {
  ChevronLeft,
  ChevronRight,
  ChevronDown,
  Check,
  Search,
  Eye,
  EyeOff,
  Layers,
  Palette,
  Box,
  Grid3x3,
  Globe,
  ScanEye,
  Gem,
  Crosshair,
  Brush,
  Rotate3d,
  Eraser,
  Undo2,
  Save,
  X,
  Wind,
  RotateCcw,
  Camera,
  Play,
  Pause,
  ChevronFirst,
  ChevronLast,
  Repeat
};

// SVG path data with the commands Lucide uses: M L H V C S A Z, absolute and
// relative. std::nullopt when the data is malformed.
std::optional<QPainterPath> parseSvgPath(std::string_view data);

// The icon's centerline in Lucide's 24x24 box.
QPainterPath lucidePath(Lucide icon);

// The icon at size x size logical pixels, drawn for 1x to 3x displays, with
// distinct colors when enabled, checked and disabled.
QIcon lucideIcon(Lucide icon, int size);

// The icon in one color, which may be translucent.
QIcon lucideIcon(Lucide icon, int size, const QColor& color);

// The icon's outline filled in one color and not stroked, for a glyph on a filled button.
QIcon lucideSolidIcon(Lucide icon, int size, const QColor& color);
