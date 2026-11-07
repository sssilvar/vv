#pragma once

// How mesh surfaces are drawn, shared by the renderer and the Style panel. It
// depends on neither VTK nor Qt.

enum class SurfaceStyle { Solid, SolidWithEdges, Wireframe, XRay, Glass };

struct SurfaceLook {
  SurfaceStyle style = SurfaceStyle::Solid;
  double xrayOpacity = 0.35;  // surface opacity while style is XRay
  double glassDensity = 0.25; // opacity of the glass body while style is Glass; its rim is opaque
};

// The translucent styles, which show the surfaces behind.
constexpr bool seeThrough(SurfaceStyle style) {
  return style == SurfaceStyle::XRay || style == SurfaceStyle::Glass;
}
