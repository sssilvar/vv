#include "Colormaps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr std::array<double, 3> fromHex(unsigned hex) {
  return {static_cast<double>(hex >> 16 & 0xFF) / 255.0,
          static_cast<double>(hex >> 8 & 0xFF) / 255.0,
          static_cast<double>(hex & 0xFF) / 255.0};
}

// The HSV hue ramp vv drew before colormaps could be picked: vtkLookupTable at
// full saturation and value, whose default ramp shapes each channel with an
// S-curve. 256 stops match its 256 table entries one to one.
std::vector<std::array<double, 3>> vtkHueRamp(double hueSpan) {
  constexpr int kStops = 256;
  const double pi = std::acos(-1.0);
  std::vector<std::array<double, 3>> stops(kStops);
  for (size_t i = 0; i < stops.size(); ++i) {
    const double h6 = 6.0 * hueSpan * static_cast<double>(i) / (kStops - 1);
    const std::array<double, 3> rgb = {
        std::abs(h6 - 3.0) - 1.0, 2.0 - std::abs(h6 - 2.0), 2.0 - std::abs(h6 - 4.0)};
    for (size_t c = 0; c < 3; ++c) {
      stops[i][c] = 0.5 - 0.5 * std::cos(std::clamp(rgb[c], 0.0, 1.0) * pi);
    }
  }
  return stops;
}

} // namespace

const std::vector<Colormap>& colormaps() {
  // Sampled at 32 evenly spaced points from matplotlib 3.11.2; RdBu is its RdBu_r,
  // blue at the low end.
  static const std::vector<Colormap> maps = {
      {"rainbow", "Rainbow", ColormapGroup::Sequential, vtkHueRamp(0.8)},
      {"viridis",
       "Viridis",
       ColormapGroup::Sequential,
       {{0.2670, 0.0049, 0.3294}, {0.2770, 0.0503, 0.3757}, {0.2823, 0.0950, 0.4173},
        {0.2829, 0.1359, 0.4534}, {0.2780, 0.1804, 0.4867}, {0.2693, 0.2188, 0.5096},
        {0.2573, 0.2561, 0.5266}, {0.2431, 0.2921, 0.5385}, {0.2259, 0.3308, 0.5473},
        {0.2105, 0.3637, 0.5522}, {0.1959, 0.3954, 0.5553}, {0.1823, 0.4262, 0.5571},
        {0.1681, 0.4600, 0.5581}, {0.1563, 0.4896, 0.5579}, {0.1448, 0.5191, 0.5566},
        {0.1337, 0.5485, 0.5535}, {0.1235, 0.5817, 0.5474}, {0.1194, 0.6111, 0.5390},
        {0.1248, 0.6405, 0.5271}, {0.1433, 0.6695, 0.5112}, {0.1807, 0.7014, 0.4882},
        {0.2264, 0.7289, 0.4628}, {0.2815, 0.7552, 0.4326}, {0.3441, 0.7800, 0.3974},
        {0.4219, 0.8058, 0.3519}, {0.4966, 0.8264, 0.3064}, {0.5756, 0.8446, 0.2564},
        {0.6576, 0.8602, 0.2031}, {0.7519, 0.8750, 0.1432}, {0.8353, 0.8860, 0.1026},
        {0.9162, 0.8961, 0.1007}, {0.9932, 0.9062, 0.1439}}},
      {"magma",
       "Magma",
       ColormapGroup::Sequential,
       {{0.0015, 0.0005, 0.0139}, {0.0137, 0.0118, 0.0687}, {0.0396, 0.0311, 0.1335},
        {0.0743, 0.0520, 0.2027}, {0.1184, 0.0665, 0.2863}, {0.1653, 0.0679, 0.3618},
        {0.2185, 0.0612, 0.4254}, {0.2720, 0.0610, 0.4657}, {0.3291, 0.0760, 0.4893},
        {0.3782, 0.0953, 0.5001}, {0.4269, 0.1154, 0.5057}, {0.4758, 0.1346, 0.5079},
        {0.5315, 0.1547, 0.5069}, {0.5818, 0.1716, 0.5028}, {0.6328, 0.1879, 0.4953},
        {0.6842, 0.2043, 0.4842}, {0.7420, 0.2240, 0.4670}, {0.7924, 0.2442, 0.4475},
        {0.8406, 0.2690, 0.4247}, {0.8847, 0.3005, 0.4000}, {0.9259, 0.3468, 0.3750},
        {0.9531, 0.3976, 0.3614}, {0.9716, 0.4542, 0.3610}, {0.9835, 0.5133, 0.3742},
        {0.9916, 0.5801, 0.4024}, {0.9955, 0.6390, 0.4366}, {0.9972, 0.6973, 0.4772},
        {0.9971, 0.7552, 0.5228}, {0.9954, 0.8199, 0.5791}, {0.9928, 0.8772, 0.6331},
        {0.9898, 0.9343, 0.6902}, {0.9871, 0.9914, 0.7495}}},
      {"inferno",
       "Inferno",
       ColormapGroup::Sequential,
       {{0.0015, 0.0005, 0.0139}, {0.0140, 0.0112, 0.0719}, {0.0423, 0.0281, 0.1411},
        {0.0820, 0.0433, 0.2153}, {0.1358, 0.0469, 0.2998}, {0.1904, 0.0393, 0.3614},
        {0.2450, 0.0371, 0.4000}, {0.2972, 0.0475, 0.4205}, {0.3540, 0.0669, 0.4309},
        {0.4039, 0.0856, 0.4332}, {0.4537, 0.1038, 0.4305}, {0.5035, 0.1216, 0.4234},
        {0.5596, 0.1413, 0.4101}, {0.6093, 0.1595, 0.3936}, {0.6585, 0.1790, 0.3727},
        {0.7065, 0.2007, 0.3478}, {0.7584, 0.2291, 0.3153}, {0.8019, 0.2587, 0.2831},
        {0.8420, 0.2929, 0.2486}, {0.8780, 0.3321, 0.2123}, {0.9130, 0.3816, 0.1698},
        {0.9387, 0.4301, 0.1304}, {0.9591, 0.4820, 0.0895}, {0.9742, 0.5368, 0.0484},
        {0.9846, 0.6011, 0.0236}, {0.9879, 0.6603, 0.0517}, {0.9856, 0.7208, 0.1122},
        {0.9775, 0.7823, 0.1859}, {0.9625, 0.8515, 0.2855}, {0.9487, 0.9105, 0.3953},
        {0.9517, 0.9606, 0.5242}, {0.9884, 0.9984, 0.6449}}},
      {"plasma",
       "Plasma",
       ColormapGroup::Sequential,
       {{0.0504, 0.0298, 0.5280}, {0.1324, 0.0223, 0.5633}, {0.1934, 0.0184, 0.5903},
        {0.2480, 0.0144, 0.6129}, {0.3062, 0.0089, 0.6337}, {0.3564, 0.0038, 0.6478},
        {0.4055, 0.0007, 0.6570}, {0.4537, 0.0028, 0.6603}, {0.5065, 0.0163, 0.6562},
        {0.5517, 0.0431, 0.6453}, {0.5950, 0.0772, 0.6279}, {0.6360, 0.1121, 0.6052},
        {0.6792, 0.1518, 0.5752}, {0.7149, 0.1873, 0.5463}, {0.7483, 0.2227, 0.5168},
        {0.7796, 0.2581, 0.4875}, {0.8126, 0.2979, 0.4553}, {0.8402, 0.3336, 0.4275},
        {0.8661, 0.3697, 0.4001}, {0.8903, 0.4064, 0.3731}, {0.9155, 0.4488, 0.3429},
        {0.9356, 0.4877, 0.3160}, {0.9534, 0.5280, 0.2889}, {0.9685, 0.5697, 0.2617},
        {0.9818, 0.6186, 0.2313}, {0.9899, 0.6638, 0.2049}, {0.9941, 0.7107, 0.1801},
        {0.9939, 0.7593, 0.1591}, {0.9876, 0.8160, 0.1444}, {0.9763, 0.8680, 0.1434},
        {0.9593, 0.9214, 0.1516}, {0.9400, 0.9752, 0.1313}}},
      {"cividis",
       "Cividis",
       ColormapGroup::Sequential,
       {{0.0000, 0.1351, 0.3048}, {0.0000, 0.1579, 0.3575}, {0.0000, 0.1788, 0.4148},
        {0.0179, 0.1985, 0.4412}, {0.1107, 0.2232, 0.4351}, {0.1597, 0.2452, 0.4295},
        {0.1998, 0.2671, 0.4255}, {0.2351, 0.2888, 0.4233}, {0.2716, 0.3133, 0.4228},
        {0.3022, 0.3350, 0.4242}, {0.3315, 0.3567, 0.4271}, {0.3599, 0.3786, 0.4315},
        {0.3912, 0.4035, 0.4381}, {0.4184, 0.4257, 0.4456}, {0.4451, 0.4482, 0.4549},
        {0.4715, 0.4710, 0.4664}, {0.5032, 0.4969, 0.4723}, {0.5328, 0.5201, 0.4724},
        {0.5630, 0.5437, 0.4705}, {0.5936, 0.5677, 0.4664}, {0.6286, 0.5951, 0.4596},
        {0.6601, 0.6199, 0.4515}, {0.6920, 0.6451, 0.4415}, {0.7243, 0.6709, 0.4292},
        {0.7611, 0.7004, 0.4126}, {0.7943, 0.7272, 0.3950}, {0.8280, 0.7546, 0.3743},
        {0.8621, 0.7825, 0.3500}, {0.9012, 0.8146, 0.3170}, {0.9367, 0.8438, 0.2809},
        {0.9731, 0.8736, 0.2347}, {0.9957, 0.9093, 0.2178}}},
      {"turbo",
       "Turbo",
       ColormapGroup::Sequential,
       {{0.1900, 0.0718, 0.2322}, {0.2250, 0.1635, 0.4510}, {0.2511, 0.2524, 0.6337},
        {0.2682, 0.3382, 0.7805}, {0.2767, 0.4313, 0.9025}, {0.2747, 0.5109, 0.9728},
        {0.2542, 0.5895, 0.9990}, {0.2071, 0.6687, 0.9742}, {0.1452, 0.7538, 0.9050},
        {0.1036, 0.8218, 0.8244}, {0.0945, 0.8784, 0.7427}, {0.1353, 0.9220, 0.6656},
        {0.2345, 0.9607, 0.5561}, {0.3504, 0.9848, 0.4500}, {0.4738, 0.9976, 0.3496},
        {0.5865, 0.9974, 0.2685}, {0.6849, 0.9794, 0.2160}, {0.7661, 0.9463, 0.2031},
        {0.8413, 0.8999, 0.2093}, {0.9061, 0.8434, 0.2219}, {0.9605, 0.7718, 0.2281},
        {0.9878, 0.7033, 0.2136}, {0.9968, 0.6209, 0.1830}, {0.9899, 0.5285, 0.1440},
        {0.9656, 0.4224, 0.0980}, {0.9313, 0.3348, 0.0622}, {0.8869, 0.2615, 0.0375},
        {0.8317, 0.1991, 0.0213}, {0.7556, 0.1373, 0.0094}, {0.6754, 0.0898, 0.0045},
        {0.5834, 0.0493, 0.0049}, {0.4796, 0.0158, 0.0106}}},
      {"coolwarm",
       "Coolwarm",
       ColormapGroup::Diverging,
       {{0.2298, 0.2987, 0.7537}, {0.2664, 0.3533, 0.8016}, {0.3042, 0.4069, 0.8453},
        {0.3433, 0.4594, 0.8841}, {0.3889, 0.5163, 0.9214}, {0.4305, 0.5649, 0.9489},
        {0.4731, 0.6111, 0.9706}, {0.5163, 0.6545, 0.9864}, {0.5652, 0.6994, 0.9966},
        {0.6085, 0.7357, 0.9994}, {0.6514, 0.7681, 0.9959}, {0.6933, 0.7963, 0.9863},
        {0.7388, 0.8226, 0.9683}, {0.7774, 0.8409, 0.9461}, {0.8137, 0.8543, 0.9185},
        {0.8474, 0.8625, 0.8855}, {0.8837, 0.8561, 0.8403}, {0.9128, 0.8367, 0.7945},
        {0.9358, 0.8122, 0.7472}, {0.9528, 0.7830, 0.6986}, {0.9648, 0.7446, 0.6432},
        {0.9692, 0.7058, 0.5937}, {0.9677, 0.6630, 0.5443}, {0.9605, 0.6163, 0.4955},
        {0.9459, 0.5596, 0.4415}, {0.9269, 0.5054, 0.3949}, {0.9027, 0.4479, 0.3497},
        {0.8734, 0.3870, 0.3063}, {0.8350, 0.3136, 0.2598}, {0.7959, 0.2418, 0.2208},
        {0.7527, 0.1576, 0.1843}, {0.7057, 0.0156, 0.1502}}},
      {"RdBu",
       "RdBu",
       ColormapGroup::Diverging,
       {{0.0196, 0.1882, 0.3804}, {0.0541, 0.2547, 0.4727}, {0.0885, 0.3211, 0.5649},
        {0.1230, 0.3875, 0.6572}, {0.1686, 0.4519, 0.7010}, {0.2105, 0.5073, 0.7293},
        {0.2523, 0.5626, 0.7576}, {0.3356, 0.6226, 0.7896}, {0.4450, 0.6918, 0.8270},
        {0.5422, 0.7533, 0.8602}, {0.6258, 0.7996, 0.8858}, {0.7033, 0.8390, 0.9080},
        {0.7905, 0.8833, 0.9329}, {0.8488, 0.9119, 0.9466}, {0.8956, 0.9340, 0.9552},
        {0.9423, 0.9562, 0.9638}, {0.9728, 0.9493, 0.9354}, {0.9802, 0.9148, 0.8764},
        {0.9875, 0.8804, 0.8173}, {0.9880, 0.8339, 0.7486}, {0.9755, 0.7592, 0.6531},
        {0.9645, 0.6927, 0.5682}, {0.9453, 0.6205, 0.4894}, {0.9084, 0.5356, 0.4242},
        {0.8669, 0.4401, 0.3509}, {0.8281, 0.3543, 0.2915}, {0.7839, 0.2657, 0.2497},
        {0.7396, 0.1772, 0.2078}, {0.6807, 0.0886, 0.1659}, {0.5885, 0.0591, 0.1511},
        {0.4962, 0.0295, 0.1363}, {0.4039, 0.0000, 0.1216}}},
      {"hue wheel", "Hue wheel", ColormapGroup::Cyclic, vtkHueRamp(1.0)},
      {"twilight",
       "Twilight",
       ColormapGroup::Cyclic,
       {{0.8858, 0.8500, 0.8880}, {0.8403, 0.8426, 0.8653}, {0.7619, 0.8071, 0.8303},
        {0.6613, 0.7563, 0.7970}, {0.5727, 0.7027, 0.7773}, {0.4947, 0.6416, 0.7639},
        {0.4390, 0.5807, 0.7539}, {0.3999, 0.5122, 0.7422}, {0.3806, 0.4440, 0.7264},
        {0.3724, 0.3677, 0.7006}, {0.3687, 0.2927, 0.6641}, {0.3615, 0.2164, 0.6110},
        {0.3414, 0.1409, 0.5291}, {0.3025, 0.0918, 0.4270}, {0.2469, 0.0691, 0.3179},
        {0.2035, 0.0674, 0.2421}, {0.2053, 0.0689, 0.2191}, {0.2600, 0.0701, 0.2431},
        {0.3404, 0.0846, 0.2783}, {0.4231, 0.1058, 0.3040}, {0.5076, 0.1407, 0.3145},
        {0.5772, 0.1894, 0.3134}, {0.6358, 0.2494, 0.3121}, {0.6874, 0.3207, 0.3187},
        {0.7265, 0.3926, 0.3377}, {0.7580, 0.4728, 0.3773}, {0.7795, 0.5501, 0.4371},
        {0.7980, 0.6316, 0.5255}, {0.8184, 0.7051, 0.6264}, {0.8471, 0.7761, 0.7401},
        {0.8722, 0.8285, 0.8330}, {0.8857, 0.8500, 0.8857}}},
  };
  return maps;
}

const Colormap* findColormap(std::string_view id) {
  const std::vector<Colormap>& maps = colormaps();
  const auto found =
      std::find_if(maps.begin(), maps.end(), [id](const Colormap& map) { return map.id == id; });
  return found == maps.end() ? nullptr : &*found;
}

const Colormap& defaultColormap(ColormapGroup group) {
  const std::vector<Colormap>& maps = colormaps();
  return *std::find_if(
      maps.begin(), maps.end(), [group](const Colormap& map) { return map.group == group; });
}

std::array<double, 3> sampleColormap(const Colormap& map, double t, bool reversed) {
  // !(t > 0) also takes NaN to the low end.
  t = !(t > 0.0) ? 0.0 : std::min(t, 1.0);
  if (reversed) {
    t = 1.0 - t;
  }
  const size_t last = map.stops.size() - 1;
  const double x = t * static_cast<double>(last);
  const size_t index = std::min(static_cast<size_t>(x), last - 1);
  const double f = x - static_cast<double>(index);
  const std::array<double, 3>& a = map.stops[index];
  const std::array<double, 3>& b = map.stops[index + 1];
  // Weighted this way, f = 0 and f = 1 return a stop exactly.
  return {(1.0 - f) * a[0] + f * b[0], (1.0 - f) * a[1] + f * b[1], (1.0 - f) * a[2] + f * b[2]};
}

const std::vector<CategoryPalette>& categoryPalettes() {
  static const std::vector<CategoryPalette> palettes = {
      // vv's own, for lit surfaces on black: OKLCH lightness 0.61 to 0.89 keeps a slanted
      // face clear of the background, gray is left to fields without a value, and any two
      // of the first six stay apart with protanopia or deuteranopia.
      {"default",
       "Default",
       "Ten colors made for the dark view",
       {fromHex(0x5286d3),
        fromHex(0xe39420),
        fromHex(0x62d286),
        fromHex(0xd55759),
        fromHex(0xcfa4fd),
        fromHex(0xf5db59),
        fromHex(0x16a7a3),
        fromHex(0xb25eb0),
        fromHex(0x5fc0f6),
        fromHex(0xfe9aa3)}},
      // As matplotlib 3.11.2 defines it.
      {"tab10",
       "Tab10",
       "Matplotlib's ten",
       {fromHex(0x1f77b4),
        fromHex(0xff7f0e),
        fromHex(0x2ca02c),
        fromHex(0xd62728),
        fromHex(0x9467bd),
        fromHex(0x8c564b),
        fromHex(0xe377c2),
        fromHex(0x7f7f7f),
        fromHex(0xbcbd22),
        fromHex(0x17becf)}},
      // As matplotlib 3.11.2 defines it: Tab10 with a tint after each color.
      {"tab20",
       "Tab20",
       "Tab10 with a tint of each",
       {fromHex(0x1f77b4), fromHex(0xaec7e8), fromHex(0xff7f0e), fromHex(0xffbb78),
        fromHex(0x2ca02c), fromHex(0x98df8a), fromHex(0xd62728), fromHex(0xff9896),
        fromHex(0x9467bd), fromHex(0xc5b0d5), fromHex(0x8c564b), fromHex(0xc49c94),
        fromHex(0xe377c2), fromHex(0xf7b6d2), fromHex(0x7f7f7f), fromHex(0xc7c7c7),
        fromHex(0xbcbd22), fromHex(0xdbdb8d), fromHex(0x17becf), fromHex(0x9edae5)}},
      // Okabe and Ito (2008) as R 4.0 and later list it in palette.colors(), without black.
      {"okabe-ito",
       "Okabe-Ito",
       "Colorblind-safe",
       {fromHex(0xe69f00),
        fromHex(0x56b4e9),
        fromHex(0x009e73),
        fromHex(0xf0e442),
        fromHex(0x0072b2),
        fromHex(0xd55e00),
        fromHex(0xcc79a7),
        fromHex(0x999999)}},
      // ColorBrewer's, as matplotlib 3.11.2 ships it.
      {"set2",
       "Set2",
       "ColorBrewer, soft",
       {fromHex(0x66c2a5),
        fromHex(0xfc8d62),
        fromHex(0x8da0cb),
        fromHex(0xe78ac3),
        fromHex(0xa6d854),
        fromHex(0xffd92f),
        fromHex(0xe5c494),
        fromHex(0xb3b3b3)}},
      // ColorBrewer's, as matplotlib 3.11.2 ships it.
      {"paired",
       "Paired",
       "ColorBrewer, light and dark pairs",
       {fromHex(0xa6cee3),
        fromHex(0x1f78b4),
        fromHex(0xb2df8a),
        fromHex(0x33a02c),
        fromHex(0xfb9a99),
        fromHex(0xe31a1c),
        fromHex(0xfdbf6f),
        fromHex(0xff7f00),
        fromHex(0xcab2d6),
        fromHex(0x6a3d9a),
        fromHex(0xffff99),
        fromHex(0xb15928)}},
      // ColorBrewer's, as matplotlib 3.11.2 ships it.
      {"dark2",
       "Dark2",
       "ColorBrewer, deep",
       {fromHex(0x1b9e77),
        fromHex(0xd95f02),
        fromHex(0x7570b3),
        fromHex(0xe7298a),
        fromHex(0x66a61e),
        fromHex(0xe6ab02),
        fromHex(0xa6761d),
        fromHex(0x666666)}},
  };
  return palettes;
}

const CategoryPalette* findCategoryPalette(std::string_view id) {
  const std::vector<CategoryPalette>& palettes = categoryPalettes();
  const auto found =
      std::find_if(palettes.begin(), palettes.end(), [id](const CategoryPalette& palette) {
        return palette.id == id;
      });
  return found == palettes.end() ? nullptr : &*found;
}

std::array<double, 3> paletteColor(const CategoryPalette& palette, size_t rank) {
  std::array<double, 3> color = palette.colors[rank % palette.colors.size()];
  const size_t round = rank / palette.colors.size() % 5;
  if (round == 0) {
    return color;
  }
  const double amount = round <= 2 ? 0.35 : 0.6;
  const double target = round % 2 == 1 ? 1.0 : 0.0;
  for (double& channel : color) {
    channel += amount * (target - channel);
  }
  return color;
}

std::array<double, 3> categoryColor(const CategoryColoring& coloring,
                                    const CategoryColors& fileColors,
                                    double value,
                                    size_t rank) {
  if (coloring.custom) {
    if (const auto own = coloring.customColors.find(value); own != coloring.customColors.end()) {
      return own->second;
    }
  }
  const CategoryPalette& palette = coloring.custom ? *coloring.customBase : *coloring.palette;
  if (&palette == &categoryPalettes().front()) {
    if (const auto given = fileColors.find(value); given != fileColors.end()) {
      return given->second;
    }
  }
  return paletteColor(palette, rank);
}

bool pickPalette(CategoryColoring& coloring, std::string_view id) {
  if (id == kCustomPaletteId) {
    coloring.custom = !coloring.customColors.empty();
    return coloring.custom;
  }
  const CategoryPalette* palette = findCategoryPalette(id);
  if (!palette) {
    return false;
  }
  coloring.palette = palette;
  coloring.custom = false;
  return true;
}

void recolorCategory(CategoryColoring& coloring,
                     double value,
                     const std::optional<std::array<double, 3>>& rgb) {
  if (rgb) {
    if (!coloring.custom) {
      coloring.custom = true;
      coloring.customBase = coloring.palette;
      coloring.customColors.clear();
    }
    coloring.customColors[value] = *rgb;
  } else if (coloring.custom) {
    coloring.customColors.erase(value);
    if (coloring.customColors.empty()) {
      coloring.custom = false;
      coloring.palette = coloring.customBase;
    }
  }
}
