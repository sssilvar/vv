#include "AnnotationBar.h"
#include "ColorBarWidget.h"
#include "ColorPanel.h"
#include "Colormaps.h"
#include "CommandPalette.h"
#include "FacetPages.h"
#include "FieldList.h"
#include "LayersPanel.h"
#include "LucideIcon.h"
#include "Notice.h"
#include "OutsidePress.h"
#include "OverlayStyle.h"
#include "PanelLabel.h"
#include "PlaybackBar.h"
#include "ProbeCard.h"
#include "ScalarPager.h"
#include "StylePanel.h"
#include "ToolRail.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QRectF>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QtTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

class UiTests : public QObject {
  Q_OBJECT

private:
  // Qt draws arcs as cubic Béziers, which stray from the circle by about 0.03% of the radius.
  static bool sameRect(const std::optional<QPainterPath>& path, const QRectF& expected) {
    if (!path) {
      return false;
    }
    const QRectF actual = path->boundingRect();
    return std::abs(actual.left() - expected.left()) < 0.01 &&
           std::abs(actual.top() - expected.top()) < 0.01 &&
           std::abs(actual.right() - expected.right()) < 0.01 &&
           std::abs(actual.bottom() - expected.bottom()) < 0.01;
  }

  // A left-button drag. QTest::mouseMove on a widget only moves the cursor on Qt 5,
  // which the offscreen platform never turns into an event, so the move is sent.
  static void drag(QWidget* widget, const QPoint& from, const QPoint& to) {
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent move(QEvent::MouseMove,
                     QPointF(to),
                     QPointF(widget->mapToGlobal(to)),
                     Qt::NoButton,
                     Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, to);
  }

  static QPoint handleCenter(const ColorBarWidget& legend, double value) {
    return QPointF(legend.valueToX(value), legend.barRect().center().y()).toPoint();
  }

  static void showRange(ColorPanel& panel) {
    panel.setRange(0.0, 100.0, std::vector<size_t>(48, 1));
    panel.setClipRange(0.0, 100.0);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
  }

  static QAbstractButton* buttonNamed(const QWidget& parent, const QString& name) {
    for (QAbstractButton* button : parent.findChildren<QAbstractButton*>()) {
      if (button->accessibleName() == name) {
        return button;
      }
    }
    return nullptr;
  }

  static void showLegend(ColorBarWidget& legend) {
    legend.setTitle(QStringLiteral("lat"));
    legend.setRange(0.0, 100.0);
    legend.setClipRange(0.0, 100.0);
    legend.resize(legend.sizeHint());
    legend.show();
    QVERIFY(QTest::qWaitForWindowExposed(&legend));
  }

  // a.vtk, b.vtk with Part 1 to 3, then c.vtk.
  static void showLayers(LayersPanel& panel) {
    panel.setFiles({{QStringLiteral("a.vtk"), QString(), {{QStringLiteral("a.vtk"), Qt::gray}}},
                    {QStringLiteral("b.vtk"),
                     QString(),
                     {{QStringLiteral("Part 1"), Qt::red},
                      {QStringLiteral("Part 2"), Qt::green},
                      {QStringLiteral("Part 3"), Qt::blue}}},
                    {QStringLiteral("c.vtk"), QString(), {{QStringLiteral("c.vtk"), Qt::gray}}}});
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
  }

  static void clickEye(const LayersPanel& panel,
                       const QString& name,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QAbstractButton* eye = buttonNamed(panel, QStringLiteral("Show or hide ") + name);
    QVERIFY(eye);
    QTest::mouseClick(eye, Qt::LeftButton, modifiers);
  }

  static SearchItem page(const QString& text, const QString& detail, bool current) {
    SearchItem item;
    item.text = text;
    item.detail = detail;
    item.current = current;
    return item;
  }

  // Over a view with an Esc shortcut that takes click focus, as the window's does.
  static void openFieldList(QWidget& view, FieldList& list) {
    view.resize(480, 600);
    view.setFocusPolicy(Qt::StrongFocus);
    auto* escape = new QAction(&view);
    escape->setObjectName("viewEscape");
    escape->setShortcut(QKeySequence(Qt::Key_Escape));
    view.addAction(escape);
    list.setGeometry(view.rect());
    list.setAnchor(QRect(60, 540, 360, 48));
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    list.open(
        {page(QStringLiteral("Geometry"), QStringLiteral("Part colors"), false),
         page(QStringLiteral("lat"), QStringLiteral("Points · continuous"), true),
         page(QStringLiteral("phase"), QStringLiteral("Points · cyclic"), false),
         page(QStringLiteral("af_sources (cells)"), QStringLiteral("Cells · 3 categories"), false),
         page(QStringLiteral("LAT_smoothed"), QStringLiteral("Points · continuous"), false)});
    QVERIFY(list.findChild<QLineEdit*>()->hasFocus());
  }

  static QStringList shownRows(const QWidget& list) {
    QStringList shown;
    const auto* rows = list.findChild<QListWidget*>();
    for (int row = 0; rows && row < rows->count(); ++row) {
      if (!rows->item(row)->isHidden()) {
        shown << rows->item(row)->text();
      }
    }
    return shown;
  }

  // Fields lat and conduction, colormaps Viridis and Coolwarm, then commands Layers, Color,
  // which is disabled, and Reset view; runs[i] counts the runs of the i-th.
  static std::vector<PaletteItem> paletteItems(std::vector<int>& runs) {
    struct Row {
      const char* group;
      const char* text;
      bool enabled;
    };
    const Row rows[] = {{"Fields", "lat", true},
                        {"Fields", "conduction", true},
                        {"Colormaps", "Viridis", true},
                        {"Colormaps", "Coolwarm", true},
                        {"Commands", "Layers", true},
                        {"Commands", "Color", false},
                        {"Commands", "Reset view", true}};
    runs.assign(std::size(rows), 0);
    std::vector<PaletteItem> items;
    for (size_t index = 0; index < std::size(rows); ++index) {
      PaletteItem item;
      item.row.group = QString::fromLatin1(rows[index].group);
      item.row.text = QString::fromLatin1(rows[index].text);
      item.row.enabled = rows[index].enabled;
      item.run = [&runs, index]() { ++runs[index]; };
      items.push_back(std::move(item));
    }
    return items;
  }

  // Over a view that takes click focus, as the window's does.
  static void openPalette(QWidget& view, CommandPalette& palette, std::vector<PaletteItem> items) {
    view.resize(700, 600);
    view.setFocusPolicy(Qt::StrongFocus);
    palette.setGeometry(view.rect());
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    palette.open(std::move(items));
    QVERIFY(palette.findChild<QLineEdit*>()->hasFocus());
  }

  static QListWidgetItem* rowNamed(const QWidget& list, const QString& text) {
    const auto* rows = list.findChild<QListWidget*>();
    for (int row = 0; rows && row < rows->count(); ++row) {
      if (rows->item(row)->text() == text) {
        return rows->item(row);
      }
    }
    return nullptr;
  }

  // Style sheets draw a rounded border as arcs and edges that overlap where they meet, so
  // a translucent border shines brighter at a join than along its straight top edge.
  static bool cornerOutshinesEdge(const QImage& image, const QRect& frame, int radius) {
    const int edge = qGray(image.pixel(frame.center().x(), frame.top()));
    for (int y = frame.top(); y <= frame.top() + radius / 2; ++y) {
      for (int x = frame.left(); x <= frame.left() + radius / 2; ++x) {
        if (qGray(image.pixel(x, y)) > edge) {
          return true;
        }
      }
    }
    return false;
  }

  static bool emitted(const QList<QVariant>& args, int file, int part, bool visible) {
    return args.at(0).toInt() == file && args.at(1).toInt() == part &&
           args.at(2).toBool() == visible;
  }

private slots:
  void svgPathData() {
    // On SVG's y-down canvas, sweep 1 turns clockwise: from the left point over the top.
    QVERIFY(sameRect(parseSvgPath("M2 12a10 10 0 0 1 20 0"), QRectF(2, 2, 20, 10)));
    QVERIFY(sameRect(parseSvgPath("M2 12a10 10 0 0 0 20 0"), QRectF(2, 12, 20, 10)));
    QVERIFY(sameRect(parseSvgPath("M2 12A10 10 0 1 0 12 2"), QRectF(2, 2, 20, 20)));
    // Pairs after a relative move are relative line-tos.
    QVERIFY(sameRect(parseSvgPath("m11 17-5-5 5-5"), QRectF(6, 7, 5, 10)));
    // A second decimal point starts the next number.
    QVERIFY(sameRect(parseSvgPath("M1.5.5l.5.5"), QRectF(1.5, 0.5, 0.5, 0.5)));
    // A move before an arc starts a subpath of its own.
    const std::optional<QPainterPath> moved = parseSvgPath("M0 0L1 1M5 5A1 1 0 0 1 7 5");
    QVERIFY(moved && moved->toSubpathPolygons().size() == 2);
    QVERIFY(!parseSvgPath("M1"));
    QVERIFY(!parseSvgPath("12 3"));
    QVERIFY(!parseSvgPath("M0 0Z 1 1"));
  }

  void lucideIconsFitTheirBox() {
    for (const Lucide icon :
         {Lucide::ChevronLeft, Lucide::ChevronRight, Lucide::ChevronDown,  Lucide::Check,
          Lucide::Search,      Lucide::Eye,          Lucide::EyeOff,       Lucide::Layers,
          Lucide::Palette,     Lucide::Box,          Lucide::Grid3x3,      Lucide::Globe,
          Lucide::ScanEye,     Lucide::Gem,          Lucide::Crosshair,    Lucide::Brush,
          Lucide::Rotate3d,    Lucide::Eraser,       Lucide::Undo2,        Lucide::Save,
          Lucide::X,           Lucide::Wind,         Lucide::RotateCcw,    Lucide::Camera,
          Lucide::Play,        Lucide::Pause,        Lucide::ChevronFirst, Lucide::ChevronLast,
          Lucide::Repeat}) {
      const QPainterPath path = lucidePath(icon);
      QVERIFY(!path.isEmpty());
      QVERIFY(QRectF(0, 0, 24, 24).contains(path.boundingRect()));
    }
  }

  void snapshotShowsTheFrameAroundLegendCorners() {
    QWidget view;
    view.resize(400, 300);
    auto* legend = new ColorBarWidget(&view);
    legend->setTitle(QStringLiteral("lat"));
    legend->setRange(0.0, 100.0);
    legend->setClipRange(0.0, 100.0);
    legend->setGeometry(QRect(QPoint(150, 120), legend->sizeHint()));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // A frame at twice the view's size, as on a Retina display.
    const QColor frameColor(10, 60, 30);
    QImage frame(view.size() * 2, QImage::Format_ARGB32_Premultiplied);
    frame.fill(frameColor);
    const QImage shot = snapshotWithLegends(frame, 2.0, {legend});
    QCOMPARE(shot.size(), frame.size());

    // Outside the card's rounded corners the frame shows, as it does on screen.
    const QRect card(legend->geometry().topLeft() * 2, legend->size() * 2);
    for (const QPoint& corner :
         {card.topLeft(), card.topRight(), card.bottomLeft(), card.bottomRight()}) {
      QCOMPARE(shot.pixelColor(corner), frameColor);
    }
    QVERIFY(shot.pixelColor(card.center()) != frameColor);
    PanelLabel identity(1, QStringLiteral("comparison.vtk"), &view);
    identity.setGeometry(8, 8, 384, 284);
    identity.show();
    const QImage identified = snapshotWithLegends(frame, 2.0, {legend}, {&identity});
    const QPoint badgeCenter = (identity.pos() + QPoint(15, 23)) * 2;
    QCOMPARE(identified.pixelColor(badgeCenter), overlay::identityColor(1));
    for (const QPoint& edge : {QPoint(0, 0), QPoint(shot.width() - 1, shot.height() - 1)}) {
      QCOMPARE(shot.pixelColor(edge), frameColor);
    }
  }

  void legendHandleDragClips() {
    ColorBarWidget legend;
    showLegend(legend);
    QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);

    const QPoint from = handleCenter(legend, 100.0);
    const QPoint to = handleCenter(legend, 50.0);
    drag(&legend, from, to);

    QVERIFY(!clipped.isEmpty());
    const QList<QVariant> last = clipped.takeLast();
    QCOMPARE(last.at(0).toDouble(), 0.0);
    QVERIFY(std::abs(last.at(1).toDouble() - 50.0) < 1.0);
  }

  void legendValueFormatting_data() {
    QTest::addColumn<double>("minimum");
    QTest::addColumn<double>("maximum");
    QTest::addColumn<double>("value");
    QTest::addColumn<QString>("text");

    QTest::newRow("foo") << 0.006789 << 0.94321 << 0.006789 << QStringLiteral("0.0068");
    QTest::newRow("bar") << 0.006789 << 0.94321 << 0.94321 << QStringLiteral("0.94");
    QTest::newRow("integer") << 0.0 << 100.0 << 33.333333 << QStringLiteral("33");
    QTest::newRow("zero") << 0.0 << 9.87 << 0.0 << QStringLiteral("0");
    QTest::newRow("negative-small") << -9.87 << 1.0 << -0.006123 << QStringLiteral("-0.0061");
    QTest::newRow("cutoff-below") << 0.0 << 9.87 << 0.0000912345 << QStringLiteral("9.1e-05");
    QTest::newRow("cutoff-at") << 0.0 << 9.87 << 0.0001 << QStringLiteral("0.0001");
    QTest::newRow("narrow-high") << 1.2345678 << 1.2345679 << 1.2345679
                                 << QStringLiteral("1.2345679");
    QTest::newRow("large") << 1e8 << 9e8 << 3.14159e8 << QStringLiteral("3.1e+08");
    QTest::newRow("large-narrow") << 1e8 << (1e8 + 1.0) << (1e8 + 1.0)
                                  << QStringLiteral("1.00000001e+08");
  }

  void legendValueFormatting() {
    QFETCH(double, minimum);
    QFETCH(double, maximum);
    QFETCH(double, value);
    QFETCH(QString, text);
    ColorBarWidget legend;
    legend.setRange(minimum, maximum);

    QCOMPARE(legend.valueText(value), text);
    legend.setClipRange(minimum + (maximum - minimum) / 3.0, maximum);
    QCOMPARE(legend.valueText(value), text);
  }

  void legendDragRestoresRounding() {
    ColorBarWidget legend;
    showLegend(legend);
    legend.setRange(0.0003, 9.87);
    legend.setClipRange(0.0625, 8.765);
    legend.setForm(LegendForm::Compact);
    legend.resize(legend.sizeHint());
    legend.setClipValueFormat(ColorBarWidget::Bound::Lower, ColorBarWidget::ValueFormat::Exact);
    legend.setClipValueFormat(ColorBarWidget::Bound::Upper, ColorBarWidget::ValueFormat::Exact);

    drag(&legend, handleCenter(legend, legend.clipMax_), handleCenter(legend, 4.0));

    QCOMPARE(legend.valueText(legend.clipMin_), QStringLiteral("0.0625"));
    QCOMPARE(legend.valueText(legend.clipMax_), legend.roundedValueText(legend.clipMax_));

    drag(&legend, handleCenter(legend, legend.clipMin_), handleCenter(legend, 1.0));

    QCOMPARE(legend.valueText(legend.clipMin_), legend.roundedValueText(legend.clipMin_));
  }

  void legendLongBoundsStayVisible_data() {
    QTest::addColumn<double>("minimum");
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("label");
    QTest::newRow("accepted-bound")
        << 0.00625 << QStringLiteral("0.0062500") << QStringLiteral("0.00625");
    QTest::newRow("clamped-bound")
        << 0.0067891234567 << QStringLiteral("0.004") << QStringLiteral("0.0068");
    QTest::newRow("boundary-roundoff")
        << static_cast<double>(0.006F) << QStringLiteral("0.006") << QStringLiteral("0.006");
    QTest::newRow("long-bound") << 0.0123456789012345 << QStringLiteral("0.0123456789012345")
                                << QStringLiteral("0.0123456789012345");
  }

  void legendLongBoundsStayVisible() {
    QFETCH(double, minimum);
    QFETCH(QString, input);
    QFETCH(QString, label);
    ColorBarWidget legend;
    showLegend(legend);
    QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);

    for (const QSize panel : {QSize(300, 240), QSize(240, 200)}) {
      for (const size_t index : {size_t{0}, size_t{1}}) {
        const double sign = index == 0 ? 1.0 : -1.0;
        legend.setRange(index == 0 ? minimum : -9.87, index == 0 ? 9.87 : -minimum);
        legend.setClipRange(index == 0 ? 0.5 : -9.87, index == 0 ? 9.87 : -0.5);
        legend.resize(legend.fitPanel(panel, 8));
        clipped.clear();
        const QPoint text = legend.clipValueRects()[index].center().toPoint();
        QTest::mouseDClick(&legend, Qt::LeftButton, Qt::NoModifier, text);
        QTest::mouseRelease(&legend, Qt::LeftButton, Qt::NoModifier, text);
        auto* editor = legend.findChild<QLineEdit*>();
        QVERIFY(editor && editor->isVisible());
        QTest::keyClicks(editor, index == 0 ? input : QStringLiteral("-") + input);
        QTest::keyClick(editor, Qt::Key_Return);
        legend.resize(legend.fitPanel(panel, 8));

        legend.setRange(legend.globalMin_, legend.globalMax_);
        const auto values = legend.clipValueRects();
        for (const QRectF& value : values) {
          QVERIFY(!value.isEmpty());
          QVERIFY(QRectF(legend.rect()).contains(value));
          QVERIFY(!value.intersects(legend.barRect()));
        }
        const double bound = index == 0 ? legend.clipMin_ : legend.clipMax_;
        QCOMPARE(bound, sign * minimum);
        QCOMPARE(clipped.count(), 1);
        QCOMPARE(clipped.first().at(static_cast<int>(index)).toDouble(), sign * minimum);
        QCOMPARE(legend.valueText(bound), index == 0 ? label : QStringLiteral("-") + label);
      }
    }
  }

  void legendTypedValueClips() {
    ColorBarWidget legend;
    showLegend(legend);
    QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);
    auto* editor = legend.findChild<QLineEdit*>();
    QVERIFY(editor);

    QTest::mouseDClick(&legend, Qt::LeftButton, Qt::NoModifier, handleCenter(legend, 100.0));
    QTest::mouseRelease(&legend, Qt::LeftButton);
    QVERIFY(!editor->isVisible());

    for (const size_t index : {size_t{0}, size_t{1}}) {
      legend.setClipRange(50.0, 50.0);
      clipped.clear();
      const QPoint text = legend.clipValueRects()[index].center().toPoint();
      QTest::mouseMove(&legend, text);
      QCOMPARE(legend.cursor().shape(), Qt::IBeamCursor);
      QTest::mouseDClick(&legend, Qt::RightButton, Qt::NoModifier, text);
      QTest::mouseRelease(&legend, Qt::RightButton, Qt::NoModifier, text);
      QVERIFY(!editor->isVisible());
      QTest::mouseClick(&legend, Qt::LeftButton, Qt::NoModifier, text);
      QVERIFY(!editor->isVisible());
      QTest::mouseDClick(&legend, Qt::LeftButton, Qt::NoModifier, text);
      QTest::mouseRelease(&legend, Qt::LeftButton, Qt::NoModifier, text);
      QVERIFY(editor->isVisible());
      QVERIFY(editor->geometry().contains(text));
      QTest::keyClicks(editor, index == 0 ? QStringLiteral("40") : QStringLiteral("60"));
      QTest::keyClick(editor, Qt::Key_Return);

      QCOMPARE(clipped.count(), 1);
      QCOMPARE(clipped.first().at(0).toDouble(), index == 0 ? 40.0 : 50.0);
      QCOMPARE(clipped.first().at(1).toDouble(), index == 0 ? 50.0 : 60.0);
    }
  }

  void legendUntouchedValueStays() {
    ColorBarWidget legend;
    showLegend(legend);
    const double third = 100.0 / 3.0;
    legend.setClipRange(0.0, third);
    QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);

    const QPoint text = legend.clipValueRects()[1].center().toPoint();
    QHelpEvent hover(QEvent::ToolTip, text, legend.mapToGlobal(text));
    QApplication::sendEvent(&legend, &hover);
    QCOMPARE(QToolTip::text().section(QLatin1Char('\n'), 0, 0).toDouble(), third);
    QToolTip::hideText();
    QTRY_VERIFY(!QToolTip::isVisible());
    QTest::mouseDClick(
        &legend, Qt::LeftButton, Qt::NoModifier, legend.clipValueRects()[1].center().toPoint());
    auto* editor = legend.findChild<QLineEdit*>();
    QVERIFY(editor && editor->isVisible());
    QCOMPARE(editor->text().toDouble(), third);
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!editor->isVisible());
    QCOMPARE(clipped.count(), 0);
  }

  void legendEditorReturnsFocus() {
    QWidget view;
    view.setFocusPolicy(Qt::StrongFocus);
    view.resize(400, 300);
    ColorBarWidget legend(&view);
    legend.setFocusPolicy(Qt::NoFocus);
    // Next in the focus chain, as the Color panel's Min field is in the window.
    QLineEdit next(&view);
    next.move(0, 200);
    legend.setRange(0.0, 100.0);
    legend.setClipRange(0.0, 100.0);
    legend.resize(legend.sizeHint());
    view.show();
    QVERIFY(QTest::qWaitForWindowActive(&view));

    QTest::mouseDClick(
        &legend, Qt::LeftButton, Qt::NoModifier, legend.clipValueRects()[1].center().toPoint());
    auto* editor = legend.findChild<QLineEdit*>();
    QVERIFY(editor && editor->hasFocus());
    QTest::keyClicks(editor, QStringLiteral("40"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(view.hasFocus());
  }

  // Smaller than its labels and editor, as a facet panel's legend can get.
  void legendFitsTinySize() {
    ColorBarWidget legend;
    legend.setRange(-1000.0, 1000.0);
    legend.setClipRange(-999.5, 999.5);
    legend.resize(40, 20);
    legend.show();
    QVERIFY(QTest::qWaitForWindowExposed(&legend));
    QVERIFY(!legend.grab().isNull());

    legend.showInlineEditor(ColorBarWidget::Upper);
    auto* editor = legend.findChild<QLineEdit*>();
    QVERIFY(editor);
    QVERIFY(editor->x() >= 0 && editor->y() >= 0);
  }

  void legendGrowsWithCategories() {
    ColorBarWidget legend;
    legend.setTitle(QStringLiteral("af_sources"));
    legend.setCategorical({{QStringLiteral("0"), Qt::blue}, {QStringLiteral("1"), Qt::red}});
    const int two = legend.sizeHint().height();
    legend.setCategorical({{QStringLiteral("0"), Qt::blue},
                           {QStringLiteral("1"), Qt::red},
                           {QStringLiteral("2"), Qt::green}});
    QVERIFY(legend.sizeHint().height() > two);
  }

  void legendFormFollowsPanelSize() {
    QVERIFY(legendFormFor(QSize(1200, 800)) == LegendForm::Full);
    QVERIFY(legendFormFor(QSize(560, 400)) == LegendForm::Full);
    QVERIFY(legendFormFor(QSize(559, 400)) == LegendForm::Compact);
    QVERIFY(legendFormFor(QSize(900, 399)) == LegendForm::Compact);
    QVERIFY(legendFormFor(QSize(280, 220)) == LegendForm::Compact);
    QVERIFY(legendFormFor(QSize(260, 160)) == LegendForm::Compact);
    QVERIFY(legendFormFor(QSize(259, 300)) == LegendForm::Minimal);
    QVERIFY(legendFormFor(QSize(600, 159)) == LegendForm::Minimal);
    QVERIFY(legendFormFor(QSize(120, 90)) == LegendForm::Minimal);
  }

  // What the compact and minimal forms draw stays inside the size they were fitted to, and
  // inside a quarter of the panel and below its middle.
  void compactLegendsFitTheirBounds() {
    const auto categories = [](int count) {
      std::vector<std::pair<QString, QColor>> entries;
      for (int i = 0; i < count; ++i) {
        entries.emplace_back(QStringLiteral("region %1").arg(i), QColor::fromHsv(i * 17, 200, 220));
      }
      return entries;
    };
    for (const int count : {0, 2, 20}) {
      ColorBarWidget legend;
      legend.setTitle(QStringLiteral("local_activation_time_after_ablation_smoothed_v2"));
      legend.setSubtitle(count == 0 ? QStringLiteral("continuous")
                                    : QStringLiteral("%1 categories").arg(count));
      legend.setRange(-99.53, 102.0);
      legend.setClipRange(-99.53, 102.0);
      if (count > 0) {
        legend.setCategorical(categories(count));
      }
      legend.show();
      QVERIFY(QTest::qWaitForWindowExposed(&legend));

      for (const QSize panel : {QSize(540, 380),
                                QSize(300, 240),
                                QSize(260, 160),
                                QSize(250, 200),
                                QSize(180, 120),
                                QSize(120, 80)}) {
        const QSize size = legend.fitPanel(panel, 8);
        QVERIFY(legend.form() != LegendForm::Full);
        QVERIFY(size.width() <= panel.width() - 16);
        QVERIFY(size.height() <= panel.height() / 2 - 8);
        QVERIFY(size.width() * size.height() <= panel.width() * panel.height() / 4);

        for (const QSize bounds : {size, QSize(150, 60), QSize(200, 120)}) {
          const QSize fitted = legend.fittedSize(bounds);
          QVERIFY(fitted.width() <= bounds.width() && fitted.height() <= bounds.height());
        }

        legend.resize(size);
        const QRectF card(legend.rect());
        if (count == 0) {
          const QRectF bar = legend.barRect();
          QVERIFY(card.contains(bar.adjusted(-5.0, 0.0, 5.0, 0.0)));
          QVERIFY(bar.width() >= 20.0);
          continue;
        }
        const ColorBarWidget::CategoryLayout layout = legend.categoryLayout();
        int shown = 0;
        for (const QRectF& entry : layout.entries) {
          if (!entry.isEmpty()) {
            QVERIFY(card.contains(entry));
            ++shown;
          }
        }
        QVERIFY(shown > 0);
        QCOMPARE(shown + layout.hidden, count);
        if (layout.hidden > 0) {
          QVERIFY(card.contains(layout.more));
        }
      }
    }
  }

  void panelLabelsKeepIdentityAndCameraInput() {
    QWidget view;
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::black);
    view.setPalette(palette);
    view.setAutoFillBackground(true);
    view.resize(600, 400);
    PanelLabel frame(26, QStringLiteral("very_long_mesh_filename_for_comparison.vtk"), &view);
    frame.setGeometry(20, 20, 260, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto labels = frame.findChildren<QLabel*>();
    QCOMPARE(labels.size(), 3);
    QCOMPARE(labels.front()->text(), QStringLiteral("AA"));
    QVERIFY(labels.at(1)->isVisible());
    QVERIFY(labels.at(1)->text().contains(QChar(0x2026)));
    QVERIFY(view.childAt(40, 40) == nullptr);
    const QImage image = view.grab().toImage();
    const QPoint edge = frame.pos() + QPoint(frame.width() / 2, frame.height() - 1);
    QCOMPARE(image.pixelColor(edge), QColor(Qt::black));
    frame.setCaption(PanelCaption::Badge);
    QVERIFY(!labels.at(1)->isVisible());
    QVERIFY(labels.front()->isVisible());
    frame.resize(120, 160);
    frame.setCaption(PanelCaption::Filename);
    QVERIFY(frame.rect().contains(labels.at(1)->geometry()));
    QVERIFY(labels.at(1)->isVisible());
    frame.setStatus(QStringLiteral("No voltage"));
    QVERIFY(labels.at(2)->isVisible());
    QCOMPARE(frame.accessibleDescription(), QStringLiteral("No voltage"));
    frame.setStatus(QString());
    QVERIFY(!labels.at(2)->isVisible());
  }

  void toolPanelsStayAboveLegends() {
    QWidget view;
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::black);
    view.setPalette(palette);
    view.setAutoFillBackground(true);
    view.resize(620, 440);
    StylePanel panel(&view);
    panel.setGeometry(20, 20, panel.sizeHint().width(), panel.sizeHint().height());
    ColorBarWidget legend(&view);
    legend.setTitle(QStringLiteral("regions"));
    legend.setCategorical({{QStringLiteral("1"), Qt::red}});
    legend.setGeometry(90, 50, 220, 100);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const QPoint overlap(110, 70);
    QCOMPARE(view.childAt(overlap), &legend);

    overlay::stackOverlays({&legend, &panel});

    QWidget* target = view.childAt(overlap);
    QVERIFY(target == &panel || panel.isAncestorOf(target));
    const QImage withLegend = view.grab().toImage().copy(panel.geometry());
    legend.hide();
    const QImage withoutLegend = view.grab().toImage().copy(panel.geometry());
    QCOMPARE(withLegend, withoutLegend);
  }

  void categoricalLegendKeepsMeshSpace() {
    ColorBarWidget legend;
    legend.setTitle(QStringLiteral("regions"));
    legend.setSubtitle(QStringLiteral("9 categories"));
    std::vector<std::pair<QString, QColor>> entries;
    for (int i = 1; i <= 9; ++i) {
      entries.emplace_back(QString::number(i), QColor::fromHsv(i * 35, 220, 230));
    }
    legend.setCategorical(entries);
    for (const QSize panel : {QSize(560, 400), QSize(620, 500), QSize(1200, 800)}) {
      const QSize size = legend.fitPanel(panel, 8);
      QVERIFY(size.height() <= panel.height() / 5);
      legend.resize(size);
      const auto layout = legend.categoryLayout();
      const QRectF card(legend.rect());
      int shown = 0;
      for (const QRectF& entry : layout.entries) {
        if (!entry.isEmpty()) {
          QVERIFY(card.contains(entry));
          ++shown;
        }
      }
      QCOMPARE(shown + layout.hidden, 9);
      QVERIFY(shown > 0);
    }
  }

  void legendGradientChoosesColormap() {
    ColorBarWidget legend;
    showLegend(legend);
    QSignalSpy chosen(&legend, &ColorBarWidget::colormapChosen);
    QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);
    QTimer guard;
    connect(&guard, &QTimer::timeout, &guard, []() {
      if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
      }
    });
    guard.start(2000);
    for (const QSize panel : {QSize(1200, 800), QSize(300, 240), QSize(200, 120)}) {
      legend.resize(legend.fitPanel(panel, 8));
      QTimer::singleShot(0, &legend, []() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
          return;
        }
        for (QAction* section : menu->actions()) {
          if (!section->menu()) {
            continue;
          }
          for (QAction* action : section->menu()->actions()) {
            if (action->text() == QStringLiteral("Viridis")) {
              action->trigger();
              menu->close();
              return;
            }
          }
        }
      });
      QTest::mouseClick(
          &legend, Qt::LeftButton, Qt::NoModifier, legend.barRect().center().toPoint());
    }
    QCOMPARE(chosen.count(), 3);
    QCOMPARE(chosen.last().at(0).toString(), QStringLiteral("viridis"));
    QVERIFY(!chosen.last().at(1).toBool());
    legend.setColormap({findColormap("viridis"), false});
    QTimer::singleShot(0, &legend, []() {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Reverse")) {
          menu->setActiveAction(action);
          QTest::keyClick(menu, Qt::Key_Return);
        }
      }
    });
    QContextMenuEvent context(
        QContextMenuEvent::Mouse, QPoint(5, 5), legend.mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(&legend, &context);
    QCOMPARE(chosen.count(), 4);
    QCOMPARE(chosen.last().at(0).toString(), QStringLiteral("viridis"));
    QVERIFY(chosen.last().at(1).toBool());
    QVERIFY(clipped.isEmpty());
  }

  void legendColormapMenuKeyboardNavigation() {
    ColorBarWidget legend;
    showLegend(legend);
    legend.setColormap({findColormap("magma"), true});
    QSignalSpy chosen(&legend, &ColorBarWidget::colormapChosen);
    QTimer guard;
    connect(&guard, &QTimer::timeout, &guard, []() {
      for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* menu = qobject_cast<QMenu*>(widget)) {
          menu->close();
        }
      }
    });
    guard.start(2000);
    // Qt 5's singleShotImpl owns this functor; Clang 14 cannot see the handoff.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    QTimer::singleShot(0, &legend, []() {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      QVERIFY(menu);
      QTest::keyClick(menu, Qt::Key_Down);
      QCOMPARE(menu->activeAction(), menu->actions().front());
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* submenu = menu->actions().front()->menu();
      QTRY_VERIFY_WITH_TIMEOUT(submenu->isVisible(), 500);
      QVERIFY(submenu->actions().at(2)->isChecked());
      QVERIFY(submenu->actions().at(2)->actionGroup());
      QVERIFY(submenu->actions().at(2)->actionGroup()->isExclusive());
      QTest::keyClick(submenu, Qt::Key_Home);
      QTest::keyClick(submenu, Qt::Key_Down);
      QCOMPARE(submenu->activeAction()->text(), QStringLiteral("Viridis"));
      QTest::keyClick(submenu, Qt::Key_Return);
    });
    QTest::mouseClick(&legend, Qt::LeftButton, Qt::NoModifier, legend.barRect().center().toPoint());

    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.front().at(0).toString(), QStringLiteral("viridis"));
    QVERIFY(chosen.front().at(1).toBool());

    chosen.clear();
    QTimer::singleShot(0, &legend, []() {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      QVERIFY(menu);
      QTest::keyClick(menu, Qt::Key_Down);
      QTest::keyClick(menu, Qt::Key_Right);
      QMenu* submenu = menu->actions().front()->menu();
      QTRY_VERIFY_WITH_TIMEOUT(submenu->isVisible(), 500);
      QTest::keyClick(submenu, Qt::Key_Escape);
      QVERIFY(!submenu->isVisible());
      QVERIFY(menu->isVisible());
      QTest::keyClick(menu, Qt::Key_Escape);
    });
    QTest::mouseClick(&legend, Qt::LeftButton, Qt::NoModifier, legend.barRect().center().toPoint());

    QVERIFY(chosen.isEmpty());
  }

  void compactLegendHandleDragClips() {
    ColorBarWidget legend;
    legend.setTitle(QStringLiteral("lat"));
    legend.setRange(0.0, 100.0);
    legend.setClipRange(0.0, 100.0);
    for (const QSize panel : {QSize(300, 240), QSize(200, 120)}) {
      legend.resize(legend.fitPanel(panel, 8));
      legend.show();
      QVERIFY(QTest::qWaitForWindowExposed(&legend));
      legend.setClipRange(0.0, 100.0);
      QSignalSpy clipped(&legend, &ColorBarWidget::clipRangeChanged);

      drag(&legend, handleCenter(legend, 100.0), handleCenter(legend, 50.0));

      QVERIFY(!clipped.isEmpty());
      QVERIFY(std::abs(clipped.takeLast().at(1).toDouble() - 50.0) < 2.0);
    }
  }

  void colormapSampling() {
    for (const Colormap& map : colormaps()) {
      QVERIFY(sampleColormap(map, 0.0, false) == map.stops.front());
      QVERIFY(sampleColormap(map, 1.0, false) == map.stops.back());
      QVERIFY(sampleColormap(map, -1.0, false) == map.stops.front());
      QVERIFY(sampleColormap(map, 0.0, true) == map.stops.back());
      const std::array<double, 3> forward = sampleColormap(map, 0.3, false);
      const std::array<double, 3> reversed = sampleColormap(map, 0.7, true);
      for (size_t c = 0; c < 3; ++c) {
        QVERIFY(std::abs(forward[c] - reversed[c]) < 1e-12);
      }
    }
    QVERIFY(defaultColormap(ColormapGroup::Sequential).id == "rainbow");
    QVERIFY(defaultColormap(ColormapGroup::Cyclic).id == "hue wheel");
    QVERIFY(findColormap("RdBu") && !findColormap("jet"));
  }

  void categoryPalettesExtendPastTheirColors() {
    QVERIFY(categoryPalettes().front().id == "default");
    QVERIFY(findCategoryPalette("okabe-ito") && !findCategoryPalette(kCustomPaletteId));
    // Tab10 keeps the colors categories had before palettes could be picked.
    QVERIFY(findCategoryPalette("tab10")->colors.front() ==
            (std::array<double, 3>{0x1f / 255.0, 0x77 / 255.0, 0xb4 / 255.0}));
    const auto mixed = [](std::array<double, 3> color, double target, double amount) {
      for (double& channel : color) {
        channel += amount * (target - channel);
      }
      return color;
    };
    const auto near = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
      return std::abs(a[0] - b[0]) < 1e-12 && std::abs(a[1] - b[1]) < 1e-12 &&
             std::abs(a[2] - b[2]) < 1e-12;
    };
    for (const CategoryPalette& palette : categoryPalettes()) {
      const size_t size = palette.colors.size();
      const std::array<double, 3>& first = palette.colors.front();
      QVERIFY(paletteColor(palette, size - 1) == palette.colors.back());
      QVERIFY(near(paletteColor(palette, size), mixed(first, 1.0, 0.35)));
      QVERIFY(near(paletteColor(palette, 2 * size), mixed(first, 0.0, 0.35)));
      QVERIFY(near(paletteColor(palette, 3 * size), mixed(first, 1.0, 0.6)));
      QVERIFY(near(paletteColor(palette, 4 * size), mixed(first, 0.0, 0.6)));
      QVERIFY(paletteColor(palette, 5 * size) == first);
      // No color repeats before then.
      std::set<std::array<double, 3>> colors;
      for (size_t rank = 0; rank < 5 * size; ++rank) {
        colors.insert(paletteColor(palette, rank));
      }
      QCOMPARE(colors.size(), 5 * size);
    }
  }

  void categoryColoringKeepsCustomColors() {
    const CategoryPalette& defaults = categoryPalettes().front();
    const CategoryPalette& okabeIto = *findCategoryPalette("okabe-ito");
    const CategoryColors file = {{1.0, {1.0, 1.0, 1.0}}, {2.0, {0.0, 0.0, 0.5}}};
    CategoryColoring coloring;
    // Default draws the file's colors, where it has them.
    QVERIFY(categoryColor(coloring, file, 2.0, 1) == file.at(2.0));
    QVERIFY(categoryColor(coloring, {}, 2.0, 1) == defaults.colors[1]);

    // A recolor makes the colors custom, over the palette shown.
    QVERIFY(pickPalette(coloring, "okabe-ito"));
    const std::array<double, 3> red = {1.0, 0.0, 0.0};
    recolorCategory(coloring, 2.0, red);
    QVERIFY(coloring.custom && coloring.customBase == &okabeIto);
    QVERIFY(categoryColor(coloring, file, 2.0, 1) == red);
    QVERIFY(categoryColor(coloring, file, 1.0, 0) == okabeIto.colors[0]);

    // Another palette keeps them for Custom.
    QVERIFY(pickPalette(coloring, "tab10") && !coloring.custom);
    QVERIFY(categoryColor(coloring, file, 2.0, 1) == findCategoryPalette("tab10")->colors[1]);
    QVERIFY(pickPalette(coloring, kCustomPaletteId) && coloring.custom);
    QVERIFY(categoryColor(coloring, file, 2.0, 1) == red);

    // Resetting the last own color leaves the palette under it, with no Custom to pick.
    recolorCategory(coloring, 2.0, std::nullopt);
    QVERIFY(!coloring.custom && coloring.palette == &okabeIto);
    QVERIFY(!pickPalette(coloring, kCustomPaletteId) && !pickPalette(coloring, "jet"));
  }

  void colorPanelEmitsPaletteAndRecolor() {
    ColorPanel panel;
    panel.setField(QStringLiteral("regions"));
    const std::vector<std::pair<QString, QColor>> entries = {{QStringLiteral("1"), Qt::red},
                                                             {QStringLiteral("2"), Qt::green},
                                                             {QStringLiteral("3"), Qt::blue}};
    std::vector<PaletteOption> palettes = {
        {QStringLiteral("default"), QStringLiteral("Default"), QString(), {Qt::red, Qt::green}},
        {QStringLiteral("okabe-ito"), QStringLiteral("Okabe-Ito"), QString(), {Qt::yellow}}};
    panel.setCategories(palettes, QStringLiteral("default"), entries);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const auto tile = [&panel](const QString& text) -> QToolButton* {
      for (QToolButton* button : panel.findChildren<QToolButton*>()) {
        if (button->text() == text) {
          return button;
        }
      }
      return nullptr;
    };
    // A continuous field's sections hide.
    QVERIFY(tile(QStringLiteral("Viridis")));
    QVERIFY(!tile(QStringLiteral("Viridis"))->isVisible());
    QVERIFY(!panel.findChild<QCheckBox*>()->isVisible());

    QSignalSpy chosen(&panel, &ColorPanel::paletteChosen);
    QToolButton* okabeIto = tile(QStringLiteral("Okabe-Ito"));
    QVERIFY(okabeIto);
    QVERIFY(okabeIto->isVisible() && !okabeIto->isChecked());
    QTest::mouseClick(okabeIto, Qt::LeftButton);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.last().at(0).toString(), QStringLiteral("okabe-ito"));

    palettes.push_back(
        {QStringLiteral("custom"), QStringLiteral("Custom"), QString(), {Qt::white}});
    panel.setCategories(palettes, QStringLiteral("custom"), entries);
    QVERIFY(tile(QStringLiteral("Custom")));
    QVERIFY(tile(QStringLiteral("Custom"))->isVisible() &&
            tile(QStringLiteral("Custom"))->isChecked());

    // A category opens the legend's color menu.
    std::vector<std::pair<int, std::optional<QColor>>> recolored;
    connect(&panel,
            &ColorPanel::categoryColorChanged,
            this,
            [&recolored](int entry, const std::optional<QColor>& color) {
              recolored.emplace_back(entry, color);
            });
    // Closes a menu left open, so a regression fails instead of waiting in QMenu::exec.
    QTimer guard;
    connect(&guard, &QTimer::timeout, &guard, []() {
      if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
      }
    });
    guard.start(2000);
    QAbstractButton* second = buttonNamed(panel, QStringLiteral("Color of 2"));
    QVERIFY(second);
    QTimer::singleShot(0, &panel, []() {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Reset to default")) {
          menu->setActiveAction(action);
          QTest::keyClick(menu, Qt::Key_Return);
        }
      }
    });
    QTest::mouseClick(second, Qt::LeftButton);
    QVERIFY(recolored.size() == 1);
    QCOMPARE(recolored.back().first, 1);
    QVERIFY(!recolored.back().second);

    QTimer::singleShot(0, &panel, []() {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) {
        return;
      }
      for (QToolButton* swatch : menu->findChildren<QToolButton*>()) {
        if (swatch->toolTip() == QStringLiteral("#ff00ff")) {
          swatch->click();
        }
      }
    });
    QTest::mouseClick(second, Qt::LeftButton);
    QVERIFY(recolored.size() == 2);
    QCOMPARE(recolored.back().first, 1);
    QVERIFY(recolored.back().second == QColor(Qt::magenta));

    panel.setColormap(ColormapChoice());
    QVERIFY(tile(QStringLiteral("Viridis"))->isVisible() && !okabeIto->isVisible());
  }

  void colorPanelEmitsChoice() {
    ColorPanel panel;
    panel.setField(QStringLiteral("lat"));
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QSignalSpy chosen(&panel, &ColorPanel::colormapChosen);

    QToolButton* viridis = nullptr;
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
      if (button->text() == QStringLiteral("Viridis")) {
        viridis = button;
      }
    }
    QVERIFY(viridis);
    QTest::mouseClick(viridis, Qt::LeftButton);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.last().at(0).toString(), QStringLiteral("viridis"));
    QCOMPARE(chosen.last().at(1).toBool(), false);

    QTest::mouseClick(panel.findChild<QCheckBox*>(), Qt::LeftButton);
    QCOMPARE(chosen.count(), 2);
    QCOMPARE(chosen.last().at(0).toString(), QStringLiteral("viridis"));
    QCOMPARE(chosen.last().at(1).toBool(), true);

    // Showing another field's choice is not a choice.
    panel.setColormap({&defaultColormap(ColormapGroup::Cyclic), false});
    QCOMPARE(chosen.count(), 2);
  }

  void colorPanelTypedBoundsClip() {
    ColorPanel panel;
    showRange(panel);
    QSignalSpy clipped(&panel, &ColorPanel::clipRangeChanged);
    auto* min = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMin"));
    auto* max = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMax"));
    QVERIFY(min && max);

    max->selectAll();
    QTest::keyClicks(max, QStringLiteral("40"));
    QTest::keyClick(max, Qt::Key_Return);
    QCOMPARE(clipped.count(), 1);
    QCOMPARE(clipped.last().at(0).toDouble(), 0.0);
    QCOMPARE(clipped.last().at(1).toDouble(), 40.0);

    // A minimum above the maximum is refused, and the field shows the range in force.
    min->selectAll();
    QTest::keyClicks(min, QStringLiteral("60"));
    QTest::keyClick(min, Qt::Key_Return);
    QCOMPARE(clipped.count(), 1);
    QCOMPARE(min->text(), QStringLiteral("0"));
  }

  void colorPanelUntouchedBoundsStay() {
    ColorPanel panel;
    const double third = 100.0 / 3.0;
    panel.setRange(0.0, third, std::vector<size_t>(48, 1));
    panel.setClipRange(0.0, third);
    panel.show();
    QVERIFY(QTest::qWaitForWindowActive(&panel));
    QSignalSpy clipped(&panel, &ColorPanel::clipRangeChanged);
    auto* min = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMin"));
    auto* max = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMax"));
    QVERIFY(min && max);

    // Max shows 33.3333, which read back would clip the field there. Qt 5 commits as a
    // field loses the focus, both versions on Return.
    max->setFocus();
    min->setFocus();
    QTest::keyClick(min, Qt::Key_Return);
    QCOMPARE(clipped.count(), 0);
  }

  void colorPanelKeepsTypedBounds() {
    ColorPanel panel;
    showRange(panel);
    QSignalSpy clipped(&panel, &ColorPanel::clipRangeChanged);
    auto* min = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMin"));
    auto* max = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMax"));
    QVERIFY(min && max);

    min->selectAll();
    QTest::keyClicks(min, QStringLiteral("12"));
    // A playback frame refreshes the range while the user types.
    panel.setRange(0.0, 100.0, std::vector<size_t>(48, 2));
    panel.setClipRange(0.0, 100.0);
    QCOMPARE(min->text(), QStringLiteral("12"));
    QCOMPARE(max->text(), QStringLiteral("100"));

    QTest::keyClick(min, Qt::Key_Return);
    QCOMPARE(clipped.count(), 1);
    QCOMPARE(clipped.last().at(0).toDouble(), 12.0);
    QCOMPARE(clipped.last().at(1).toDouble(), 100.0);
  }

  void colorPanelHistogramDragClips() {
    ColorPanel panel;
    showRange(panel);
    QSignalSpy clipped(&panel, &ColorPanel::clipRangeChanged);
    auto* histogram = panel.findChild<RangeHistogram*>();
    QVERIFY(histogram);
    const auto handle = [histogram](double value) {
      return QPointF(histogram->valueToX(value), histogram->plotRect().bottom()).toPoint();
    };

    drag(histogram, handle(100.0), handle(50.0));

    QVERIFY(!clipped.isEmpty());
    QCOMPARE(clipped.last().at(0).toDouble(), 0.0);
    QVERIFY(std::abs(clipped.last().at(1).toDouble() - 50.0) < 1.0);
    auto* max = panel.findChild<QLineEdit*>(QStringLiteral("colorPanelMax"));
    QVERIFY(std::abs(max->text().toDouble() - 50.0) < 1.0);
  }

  void probeCardReadsValues() {
    class Surface : public QWidget {
    public:
      int presses = 0;

    protected:
      void mousePressEvent(QMouseEvent*) override {
        ++presses;
      }
    };
    Surface view;
    view.resize(400, 300);
    view.setCursor(Qt::CrossCursor);
    ProbeCard card(&view);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto* value = card.findChild<QLabel*>(QStringLiteral("probeValue"));
    auto* swatch = card.findChild<QLabel*>(QStringLiteral("probeSwatch"));
    QVERIFY(value && swatch);

    ProbeReading reading;
    reading.field = QStringLiteral("lat");
    reading.value = -12.3456;
    card.setReading(reading);
    QCOMPARE(value->text(), QStringLiteral("-12.35"));
    QVERIFY(swatch->isHidden());

    reading.value = 2.0;
    reading.category = {{QStringLiteral("2"), Qt::red}};
    card.setReading(reading);
    QCOMPARE(value->text(), QStringLiteral("2"));
    QVERIFY(!swatch->isHidden());

    reading.value = std::numeric_limits<double>::quiet_NaN();
    reading.category.reset();
    card.setReading(reading);
    QCOMPARE(value->text(), QStringLiteral("no value"));
    QVERIFY(swatch->isHidden());
    // The card grows with a longer value instead of clipping it.
    QVERIFY(value->width() >= value->sizeHint().width());

    auto* close = card.findChild<QToolButton*>(QStringLiteral("probeUnpin"));
    QVERIFY(close);
    QVERIFY(close->isHidden());
    QVERIFY(card.testAttribute(Qt::WA_TransparentForMouseEvents));
    QSignalSpy unpin(&card, &ProbeCard::unpinRequested);
    auto* body = card.findChild<QWidget*>(QStringLiteral("probeCardBody"));
    QVERIFY(body);
    card.layout()->activate();
    const QSize hoverSize = body->size();
    card.setPinned(true);
    card.setReading(reading);
    QVERIFY(!close->isHidden());
    QCOMPARE(body->size(), hoverSize);
    QVERIFY(card.rect().contains(close->geometry()));
    QVERIFY(close->geometry().contains(body->geometry().topRight()));
    QVERIFY(close->geometry().intersects(body->geometry()));
    QVERIFY(!card.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(close->accessibleName(), QStringLiteral("Unpin probe"));
    QCOMPARE(card.cursor().shape(), Qt::ArrowCursor);
    const QPoint center = close->mapTo(&view, close->rect().center());
    QWidget* target = view.childAt(center);
    QCOMPARE(target, close);
    const QPoint corner = close->mapTo(&view, QPoint(1, 1));
    QWidget* outside = view.childAt(corner);
    QVERIFY(outside);
    QTest::mouseClick(outside, Qt::LeftButton, Qt::NoModifier, outside->mapFrom(&view, corner));
    QCOMPARE(unpin.count(), 0);
    QCOMPARE(view.presses, 0);
    drag(close, close->rect().center(), QPoint(1, 1));
    QCOMPARE(unpin.count(), 0);
    QVERIFY(!close->isDown());
    QVERIFY(!close->hasFocus());
    QCOMPARE(close->cursor().shape(), Qt::ArrowCursor);
    QMouseEvent hover(QEvent::MouseMove,
                      QPointF(close->rect().center()),
                      QPointF(close->mapToGlobal(close->rect().center())),
                      Qt::NoButton,
                      Qt::NoButton,
                      Qt::NoModifier);
    QApplication::sendEvent(close, &hover);
    QCOMPARE(close->cursor().shape(), Qt::PointingHandCursor);
    QWidget* content = view.childAt(body->mapTo(&view, body->rect().center()));
    QVERIFY(content);
    QTest::mouseClick(content, Qt::LeftButton);
    QCOMPARE(view.presses, 0);
    QCOMPARE(unpin.count(), 0);
    QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, target->mapFrom(&view, center));
    QCOMPARE(unpin.count(), 1);
    card.setPinned(false);
    QVERIFY(close->isHidden());
    QVERIFY(card.testAttribute(Qt::WA_TransparentForMouseEvents));
  }

  void stylePanelEmitsChoices() {
    StylePanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    std::optional<SurfaceStyle> chosen;
    connect(
        &panel, &StylePanel::styleChosen, this, [&chosen](SurfaceStyle style) { chosen = style; });
    QSignalSpy opacity(&panel, &StylePanel::xrayOpacityChosen);
    QSignalSpy density(&panel, &StylePanel::glassDensityChosen);

    // Solid, Solid with edges and Wireframe on a row, X-ray and Glass under them, as wide.
    std::vector<QToolButton*> tiles;
    for (const QString& name : {QStringLiteral("Solid"),
                                QStringLiteral("Solid with edges"),
                                QStringLiteral("Wireframe"),
                                QStringLiteral("X-ray"),
                                QStringLiteral("Glass")}) {
      const QList<QToolButton*> buttons = panel.findChildren<QToolButton*>();
      const auto tile = std::find_if(
          buttons.begin(), buttons.end(), [&name](QToolButton* b) { return b->text() == name; });
      QVERIFY(tile != buttons.end());
      QVERIFY((*tile)->height() >= 44);
      QVERIFY((*tile)->fontMetrics().horizontalAdvance(name) < (*tile)->width());
      tiles.push_back(*tile);
    }
    QCOMPARE(tiles[1]->y(), tiles[0]->y());
    QCOMPARE(tiles[2]->y(), tiles[0]->y());
    QCOMPARE(tiles[4]->y(), tiles[3]->y());
    QVERIFY(tiles[3]->y() > tiles[0]->geometry().bottom());
    QCOMPARE(tiles[3]->x(), tiles[0]->x());
    QCOMPARE(tiles[4]->geometry().right(), tiles[2]->geometry().right());
    QToolButton* xray = tiles[3];
    QToolButton* glass = tiles[4];
    QCOMPARE(xray->toolTip(), QStringLiteral("X-ray (X)"));
    QCOMPARE(glass->toolTip(), QStringLiteral("Glass (G)"));

    QSlider* opacitySlider = nullptr;
    QSlider* densitySlider = nullptr;
    for (QSlider* slider : panel.findChildren<QSlider*>()) {
      if (slider->toolTip().contains(QStringLiteral("X-ray"))) {
        opacitySlider = slider;
      } else {
        densitySlider = slider;
      }
    }
    QVERIFY(opacitySlider);
    QVERIFY(densitySlider);
    QVERIFY(!opacitySlider->isVisible());
    QVERIFY(!densitySlider->isVisible());
    const auto shownValue = [](const QSlider* slider) {
      const auto* value = slider->parentWidget()->findChild<QLabel*>("stylePanelValue");
      return value ? value->text() : QString();
    };
    const auto labelShows = [&panel](const QString& text) {
      const QList<QLabel*> labels = panel.findChildren<QLabel*>();
      return std::any_of(labels.begin(), labels.end(), [&text](const QLabel* label) {
        return label->isVisible() && label->text() == text;
      });
    };

    QTest::mouseClick(xray, Qt::LeftButton);
    QVERIFY(chosen == SurfaceStyle::XRay);
    QVERIFY(opacitySlider->isVisible());
    QVERIFY(!densitySlider->isVisible());
    QVERIFY(labelShows(QStringLiteral("Opacity")));
    QVERIFY(!labelShows(QStringLiteral("Density")));
    QCOMPARE(shownValue(opacitySlider), QStringLiteral("35 %"));
    QTest::keyClick(opacitySlider, Qt::Key_Right);
    QCOMPARE(opacity.count(), 1);
    QCOMPARE(opacity.last().at(0).toDouble(), 0.36);
    QCOMPARE(shownValue(opacitySlider), QStringLiteral("36 %"));

    QTest::mouseClick(glass, Qt::LeftButton);
    QVERIFY(chosen == SurfaceStyle::Glass);
    QVERIFY(densitySlider->isVisible());
    QVERIFY(!opacitySlider->isVisible());
    QVERIFY(labelShows(QStringLiteral("Density")));
    QVERIFY(!labelShows(QStringLiteral("Opacity")));
    QCOMPARE(shownValue(densitySlider), QStringLiteral("25 %"));
    QTest::keyClick(densitySlider, Qt::Key_Right);
    QCOMPARE(density.count(), 1);
    QCOMPARE(density.last().at(0).toDouble(), 0.26);
    QCOMPARE(shownValue(densitySlider), QStringLiteral("26 %"));
    QCOMPARE(opacity.count(), 1);

    // The owner pushes the look back; each slider keeps its own value.
    panel.setLook({SurfaceStyle::XRay, 0.36, 0.26});
    QVERIFY(xray->isChecked());
    QVERIFY(opacitySlider->isVisible());
    QVERIFY(!densitySlider->isVisible());
    QCOMPARE(shownValue(opacitySlider), QStringLiteral("36 %"));
    QCOMPARE(shownValue(densitySlider), QStringLiteral("26 %"));
    panel.setLook({SurfaceStyle::Wireframe, 0.36, 0.26});
    QVERIFY(!opacitySlider->isVisible());
    QVERIFY(!densitySlider->isVisible());
    QCOMPARE(opacity.count(), 1);
    QCOMPARE(density.count(), 1);
  }

  // The key cap on the checked tile keeps a border that stands out from the tile's fill.
  void styleKeyCapShowsOnCheckedTile() {
    QWidget view;
    QPalette black;
    black.setColor(QPalette::Window, Qt::black);
    view.setPalette(black);
    view.setAutoFillBackground(true);
    view.resize(400, 300);
    auto* panel = new StylePanel(&view);
    panel->move(20, 20);
    panel->resize(panel->sizeHint());
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QList<QLabel*> keys = panel->findChildren<QLabel*>("stylePanelKey");
    const auto key = std::find_if(
        keys.begin(), keys.end(), [](const QLabel* label) { return label->text() == "1"; });
    QVERIFY(key != keys.end());
    const auto* tile = qobject_cast<QToolButton*>((*key)->parentWidget());
    QVERIFY(tile);
    QVERIFY(tile->isChecked());
    const QImage image = view.grab().toImage();
    const auto luminance = [&image](const QPoint& at) {
      const QColor color = image.pixelColor(at * image.devicePixelRatio());
      const auto linear = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
      };
      return 0.2126 * linear(static_cast<double>(color.redF())) +
             0.7152 * linear(static_cast<double>(color.greenF())) +
             0.0722 * linear(static_cast<double>(color.blueF()));
    };
    const QRect cap((*key)->mapTo(&view, QPoint(0, 0)), (*key)->size());
    const double border = luminance(QPoint(cap.left(), cap.center().y()));
    const double fill = luminance(QPoint(cap.left() - 3, cap.center().y()));
    QVERIFY((border + 0.05) / (fill + 0.05) >= 2.5);
  }

  void noticeLetsClicksThroughAndFades() {
    QWidget view;
    view.resize(400, 300);
    auto* under = new QPushButton(&view);
    under->setGeometry(view.rect());
    auto* notice = new Notice(&view);
    notice->setDuration(NoticeKind::Info, std::chrono::milliseconds(60));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(!notice->isVisible());

    notice->post(QStringLiteral("Saved /tmp/vv.png"), NoticeKind::Info);
    notice->setGeometry(QRect(QPoint(100, 16), notice->sizeHint()));
    QVERIFY(notice->isVisible());
    QCOMPARE(notice->text(), QStringLiteral("Saved /tmp/vv.png"));
    QVERIFY(notice->width() > QFontMetricsF(notice->textFont()).horizontalAdvance(notice->text()));
    QSignalSpy clicked(under, &QPushButton::clicked);
    QTest::mouseClick(
        view.windowHandle(), Qt::LeftButton, Qt::NoModifier, notice->geometry().center());
    QCOMPARE(clicked.count(), 1);

    QTRY_VERIFY_WITH_TIMEOUT(!notice->isVisible(), 3000);
    // A later notice shows again, and an error stays longer than the info did.
    notice->setDuration(NoticeKind::Error, std::chrono::milliseconds(600));
    notice->post(QStringLiteral("No vector field in this mesh"), NoticeKind::Error);
    QVERIFY(notice->isVisible());
    QTest::qWait(450);
    QVERIFY(notice->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!notice->isVisible(), 3000);
  }

  void roundedBordersJoinWithoutBrightSpots() {
    // Over black, like the empty view behind the overlays.
    QWidget view;
    QPalette black;
    black.setColor(QPalette::Window, Qt::black);
    view.setPalette(black);
    view.setAutoFillBackground(true);
    view.resize(400, 600);
    auto* panel = new StylePanel(&view);
    panel->move(80, 20);
    panel->resize(panel->sizeHint());
    auto* rail = new ToolRail(&view);
    auto* tool = new QAction(QStringLiteral("Tool"), rail);
    tool->setCheckable(true);
    tool->setChecked(true);
    rail->addTool(tool);
    rail->move(10, 20);
    rail->resize(rail->sizeHint());
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QImage image = view.grab().toImage();
    QVERIFY(!cornerOutshinesEdge(image, panel->geometry(), overlay::kPanelRadius));
    const auto* button = rail->findChild<QToolButton*>();
    QVERIFY(button);
    constexpr int kRailButtonRadius = 11; // ToolRail's style sheet
    QVERIFY(
        !cornerOutshinesEdge(image, button->geometry().translated(rail->pos()), kRailButtonRadius));
  }

  void menuHighlightsStayInsetAndDistinguishChoicesFromToggles() {
    for (const double scale : {1.0, 1.6}) {
      QMenu menu;
      overlay::styleMenu(&menu);
      QFont font = menu.font();
      font.setPointSizeF(font.pointSizeF() * scale);
      menu.setFont(font);
      QActionGroup choices(&menu);
      QAction* choice = menu.addAction(QStringLiteral("Magma"));
      choice->setCheckable(true);
      choices.addAction(choice);
      choice->setChecked(true);
      QAction* separator = menu.addSeparator();
      QAction* disabled = menu.addAction(QStringLiteral("Unavailable"));
      disabled->setEnabled(false);
      QAction* toggle = menu.addAction(QStringLiteral("Reverse"));
      toggle->setCheckable(true);
      toggle->setChecked(true);
      menu.popup(QPoint(40, 40));
      QVERIFY(QTest::qWaitForWindowExposed(&menu));

      const QRect choiceRect = menu.actionGeometry(choice);
      const QRect toggleRect = menu.actionGeometry(toggle);
      QVERIFY(choiceRect.left() >= 4);
      QVERIFY(menu.width() - choiceRect.right() >= 4);
      QVERIFY(choiceRect.height() >= menu.fontMetrics().height() + 12);
      QVERIFY(menu.actionGeometry(separator).height() >= 9);
      const auto background = [](const QImage& image, const QRect& row) {
        const qreal dpr = image.devicePixelRatio();
        return image.pixelColor(qRound((row.right() - 8) * dpr), qRound(row.center().y() * dpr));
      };
      QImage image = menu.grab().toImage();
      QVERIFY(background(image, choiceRect) != overlay::kPopupSurface);
      QCOMPARE(background(image, toggleRect), overlay::kPopupSurface);

      QTest::keyClick(&menu, Qt::Key_Down);
      QCOMPARE(menu.activeAction(), choice);
      QTest::keyClick(&menu, Qt::Key_Down);
      QCOMPARE(menu.activeAction(), toggle);
      image = menu.grab().toImage();
      QVERIFY(background(image, toggleRect) != overlay::kPopupSurface);
      QCOMPARE(background(image, menu.actionGeometry(disabled)), overlay::kPopupSurface);
      QTest::keyClick(&menu, Qt::Key_Escape);

      QVERIFY(!menu.isVisible());
      QVERIFY(toggle->isChecked());
    }
  }

  void outsidePressSkipsOverlays() {
    QWidget view;
    view.resize(400, 300);
    auto* panel = new QWidget(&view);
    panel->setGeometry(60, 10, 200, 120);
    panel->setAttribute(Qt::WA_NoMousePropagation, true);
    auto* panelButton = new QToolButton(panel);
    panelButton->setGeometry(10, 10, 60, 30);
    auto* rail = new QWidget(&view);
    rail->setGeometry(10, 10, 40, 200);
    rail->setAttribute(Qt::WA_NoMousePropagation, true);
    auto* railButton = new QToolButton(rail);
    railButton->setGeometry(4, 4, 32, 32);
    auto* legend = new QToolButton(&view);
    legend->setGeometry(300, 240, 80, 40);
    int outside = 0;
    OutsidePress filter(&view, {panel, rail, nullptr}, [&outside]() { ++outside; });
    qApp->installEventFilter(&filter);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QTest::mouseClick(panelButton, Qt::LeftButton);
    QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier, QPoint(150, 100));
    QTest::mouseClick(railButton, Qt::LeftButton);
    QCOMPARE(outside, 0);

    // The press still reaches what it was headed for.
    QSignalSpy clicked(legend, &QToolButton::clicked);
    QTest::mouseClick(legend, Qt::LeftButton);
    QCOMPARE(outside, 1);
    QCOMPARE(clicked.count(), 1);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(300, 100));
    QCOMPARE(outside, 2);

    QWidget elsewhere;
    elsewhere.show();
    QVERIFY(QTest::qWaitForWindowExposed(&elsewhere));
    QTest::mouseClick(&elsewhere, Qt::LeftButton);
    QCOMPARE(outside, 2);
  }

  void pagerShowsPositionInBadge() {
    ScalarPager pager(PagerPlacement::Capsule);
    pager.setPage(QStringLiteral("lat"), QStringLiteral("Points · continuous"), 3, 12);
    pager.resize(pager.sizeHint());
    pager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pager));
    const auto* title = pager.findChild<QLabel*>("scalarPagerTitle");
    const auto* badge = pager.findChild<QLabel*>("scalarPagerPosition");
    QVERIFY(title);
    QVERIFY(badge);
    QCOMPARE(title->text(), QStringLiteral("lat"));
    QCOMPARE(badge->text(), QStringLiteral("3 / 12"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    // Tabular digits, kept through the style sheet's font size: paging does not shift them.
    const QFontMetricsF digits(badge->font());
    QCOMPARE(digits.horizontalAdvance(QStringLiteral("1")),
             digits.horizontalAdvance(QStringLiteral("8")));
#endif
    const QSize size = pager.sizeHint();

    const QString name = QStringLiteral("local_activation_time_after_ablation_smoothed_v2");
    pager.setPage(name, QStringLiteral("Points · continuous"), 11, 12);
    QApplication::processEvents();
    QVERIFY(title->text() != name);
    QVERIFY(title->text().startsWith(QStringLiteral("local_")));
    QVERIFY(title->text().endsWith(QStringLiteral("_v2")));
    QCOMPARE(badge->text(), QStringLiteral("11 / 12"));
    // The capsule keeps its size, and the badge shows whole beside the shortened name.
    QCOMPARE(pager.sizeHint(), size);
    const QRect shown(badge->mapTo(&pager, QPoint(0, 0)), badge->size());
    QVERIFY(pager.rect().contains(shown));
    QVERIFY(badge->width() >= badge->sizeHint().width());
    QVERIFY(title->mapTo(&pager, QPoint(title->width(), 0)).x() < shown.left());
  }

  void facetPagesKeepPanelsUsable() {
    constexpr int kPager = 74;
    const auto pageSize = [](size_t fields, size_t files, int width, int height) {
      return static_cast<int>(facetPageSize(fields, files, width, height, kPager));
    };
    // One file: as many fields as keep panels at least 280 x 220, at most 9, evened out.
    QCOMPARE(pageSize(13, 1, 1300, 925), 7);
    QCOMPARE(pageSize(13, 1, 1000, 612), 5);
    QCOMPARE(pageSize(13, 1, 2560, 1400), 7);
    QCOMPARE(pageSize(30, 1, 2560, 1400), 8);
    QCOMPARE(pageSize(4, 1, 1300, 925), 4);
    QCOMPARE(pageSize(13, 1, 400, 300), 1);
    // Several files: a row each, as many field columns as fit across.
    QCOMPARE(pageSize(14, 3, 1300, 925), 4);
    QCOMPARE(pageSize(14, 2, 1700, 1000), 5);
    QCOMPARE(pageSize(3, 3, 1300, 925), 3);
    QCOMPARE(pageSize(0, 1, 1300, 925), 0);

    for (int width = 300; width <= 2600; width += 230) {
      for (int height = 240; height <= 1600; height += 170) {
        const size_t size = facetPageSize(13, 1, width, height, kPager);
        QVERIFY(size >= 1 && size <= kMaxFacetPage);
        if (size > 1) {
          const int area = height - kPager;
          const size_t cols = adaptiveGridCols(size, width, area);
          const size_t rows = (size + cols - 1) / cols;
          QVERIFY(width / static_cast<int>(cols) >= kMinFacetPanelWidth);
          QVERIFY(area / static_cast<int>(rows) >= kMinFacetPanelHeight);
        }
      }
    }
  }

  void facetPagerNamesPages() {
    QCOMPARE(facetPageLabel(0, 6, 12), QStringLiteral("Fields 1–6 of 12"));
    QCOMPARE(facetPageLabel(7, 6, 13), QStringLiteral("Fields 8–13 of 13"));
    QCOMPARE(facetPageLabel(12, 1, 13), QStringLiteral("Field 13 of 13"));

    ScalarPager pager(PagerPlacement::Capsule);
    pager.setStepNames(QStringLiteral("Previous page"), QStringLiteral("Next page"));
    QAbstractButton* next = buttonNamed(pager, QStringLiteral("Next page"));
    QVERIFY(next);
    QCOMPARE(next->toolTip(), QStringLiteral("Next page (Space)"));
    QVERIFY(buttonNamed(pager, QStringLiteral("Previous page")));
    QVERIFY(!buttonNamed(pager, QStringLiteral("Next field")));
  }

  void layersEyeHidesAndShows() {
    LayersPanel panel;
    showLayers(panel);
    QSignalSpy changed(&panel, &LayersPanel::visibilityChanged);

    clickEye(panel, QStringLiteral("a.vtk"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(emitted(changed.at(0), 0, 0, false));

    // A file's eye hides each of its parts.
    clickEye(panel, QStringLiteral("b.vtk"));
    QCOMPARE(changed.count(), 4);
    for (int part = 0; part < 3; ++part) {
      QVERIFY(emitted(changed.at(1 + part), 1, part, false));
    }

    // A partly shown file shows whole, as a partly checked box checks.
    clickEye(panel, QStringLiteral("Part 2"));
    clickEye(panel, QStringLiteral("b.vtk"));
    QCOMPARE(changed.count(), 7);
    QVERIFY(emitted(changed.at(4), 1, 1, true));
    QVERIFY(emitted(changed.at(5), 1, 0, true));
    QVERIFY(emitted(changed.at(6), 1, 2, true));
  }

  void layersOptionClickShowsOnlyOne() {
    LayersPanel panel;
    showLayers(panel);
    QSignalSpy changed(&panel, &LayersPanel::visibilityChanged);

    clickEye(panel, QStringLiteral("b.vtk"), Qt::AltModifier);
    QCOMPARE(changed.count(), 2);
    QVERIFY(emitted(changed.at(0), 0, 0, false));
    QVERIFY(emitted(changed.at(1), 2, 0, false));

    clickEye(panel, QStringLiteral("Part 2"), Qt::AltModifier);
    QCOMPARE(changed.count(), 4);
    QVERIFY(emitted(changed.at(2), 1, 0, false));
    QVERIFY(emitted(changed.at(3), 1, 2, false));

    // Again on what already shows alone, everything shows.
    clickEye(panel, QStringLiteral("Part 2"), Qt::AltModifier);
    QCOMPARE(changed.count(), 8);
    for (int i = 4; i < 8; ++i) {
      QVERIFY(changed.at(i).at(2).toBool());
    }
  }

  void layersShowAll() {
    LayersPanel panel;
    showLayers(panel);
    auto* showAll = panel.findChild<QToolButton*>(QStringLiteral("layersShowAll"));
    QVERIFY(showAll);
    QVERIFY(!showAll->isEnabled());
    clickEye(panel, QStringLiteral("a.vtk"));
    clickEye(panel, QStringLiteral("Part 3"));
    QVERIFY(showAll->isEnabled());
    QSignalSpy changed(&panel, &LayersPanel::visibilityChanged);

    QTest::mouseClick(showAll, Qt::LeftButton);
    QCOMPARE(changed.count(), 2);
    QVERIFY(emitted(changed.at(0), 0, 0, true));
    QVERIFY(emitted(changed.at(1), 1, 2, true));
    QVERIFY(!showAll->isEnabled());
  }

  void layersCollapseHidesParts() {
    LayersPanel panel;
    showLayers(panel);
    QSignalSpy expansion(&panel, &LayersPanel::expansionChanged);
    QAbstractButton* part = buttonNamed(panel, QStringLiteral("Show or hide Part 1"));
    QVERIFY(part);
    QVERIFY(part->isVisible());
    const int expanded = panel.sizeHint().height();

    QTest::mouseClick(buttonNamed(panel, QStringLiteral("Parts of b.vtk")), Qt::LeftButton);
    QVERIFY(!part->isVisible());
    QCOMPARE(expansion.count(), 1);
    QVERIFY(panel.sizeHint().height() < expanded);
  }

  void fileLetters() {
    QCOMPARE(fileLetter(0), QStringLiteral("A"));
    QCOMPARE(fileLetter(25), QStringLiteral("Z"));
    QCOMPARE(fileLetter(26), QStringLiteral("AA"));
  }

  void fieldListFilters() {
    QWidget view;
    FieldList list(&view);
    openFieldList(view, list);
    auto* search = list.findChild<QLineEdit*>();
    auto* empty = list.findChild<QLabel*>(QStringLiteral("searchListEmpty"));
    QVERIFY(search && empty);
    QCOMPARE(shownRows(list).size(), 5);

    QTest::keyClicks(search, QStringLiteral("LaT"));
    QCOMPARE(shownRows(list), QStringList({QStringLiteral("lat"), QStringLiteral("LAT_smoothed")}));
    QVERIFY(!empty->isVisible());

    QTest::keyClicks(search, QStringLiteral("x"));
    QVERIFY(shownRows(list).isEmpty());
    QVERIFY(empty->isVisible());
  }

  void fieldListReturnPicksHighlighted() {
    QWidget view;
    FieldList list(&view);
    openFieldList(view, list);
    QSignalSpy chosen(&list, &FieldList::pageChosen);
    auto* search = list.findChild<QLineEdit*>();
    QVERIFY(search);

    // With nothing typed, the shown page is highlighted.
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.last().at(0).toInt(), 1);
    QVERIFY(list.isHidden());

    // Down steps over the rows the search hides.
    list.open({page(QStringLiteral("Geometry"), QString(), true),
               page(QStringLiteral("lat"), QString(), false),
               page(QStringLiteral("phase"), QString(), false),
               page(QStringLiteral("lat_smoothed"), QString(), false)});
    QTest::keyClicks(search, QStringLiteral("lat"));
    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(chosen.count(), 2);
    QCOMPARE(chosen.last().at(0).toInt(), 3);

    list.open({page(QStringLiteral("Geometry"), QString(), true),
               page(QStringLiteral("lat"), QString(), false)});
    auto* rows = list.findChild<QListWidget*>();
    QTest::mouseClick(rows->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      rows->visualItemRect(rows->item(1)).center());
    QCOMPARE(chosen.count(), 3);
    QCOMPARE(chosen.last().at(0).toInt(), 1);
  }

  void fieldListEscEmitsNothing() {
    QWidget view;
    FieldList list(&view);
    openFieldList(view, list);
    QSignalSpy chosen(&list, &FieldList::pageChosen);
    QSignalSpy closed(&list, &FieldList::closed);
    QSignalSpy viewEscape(view.findChild<QAction*>(QStringLiteral("viewEscape")),
                          &QAction::triggered);
    auto* search = list.findChild<QLineEdit*>();
    QVERIFY(search);

    QTest::keyClicks(search, QStringLiteral("lat"));
    QTest::keyClick(search, Qt::Key_Escape);
    QCOMPARE(chosen.count(), 0);
    QCOMPARE(closed.count(), 1);
    QVERIFY(list.isHidden());
    // The list's Esc, not the window's.
    QCOMPARE(viewEscape.count(), 0);
  }

  // A press on the card's padding goes to no row: the view must not take the focus.
  void fieldListKeepsFocus() {
    QWidget view;
    FieldList list(&view);
    openFieldList(view, list);
    auto* search = list.findChild<QLineEdit*>();
    auto* card = list.findChild<QWidget*>(QStringLiteral("fieldListCard"));
    QVERIFY(search);
    QVERIFY(card);

    QTest::mouseClick(card, Qt::LeftButton, Qt::NoModifier, QPoint(card->width() / 2, 1));
    QVERIFY(search->hasFocus());
    QVERIFY(list.isVisible());
  }

  void paletteKeepsFocus() {
    QWidget view;
    CommandPalette palette(&view);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    auto* rows = palette.findChild<QListWidget*>();
    auto* card = palette.findChild<QWidget*>(QStringLiteral("commandPaletteCard"));
    QVERIFY(search);
    QVERIFY(rows && card);

    // A group header, a disabled row, then the footer.
    for (const QString& row : {QStringLiteral("Commands"), QStringLiteral("Color")}) {
      QTest::mouseClick(rows->viewport(),
                        Qt::LeftButton,
                        Qt::NoModifier,
                        rows->visualItemRect(rowNamed(palette, row)).center());
      QVERIFY(search->hasFocus());
    }
    QTest::mouseClick(
        card, Qt::LeftButton, Qt::NoModifier, QPoint(card->width() - 4, card->height() - 4));
    QVERIFY(search->hasFocus());
    QVERIFY(palette.isVisible());
  }

  void paletteFiltersAcrossGroups() {
    QWidget view;
    CommandPalette palette(&view);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    QVERIFY(search);
    QCOMPARE(shownRows(palette).size(), 10); // 7 rows under 3 headers

    QTest::keyClicks(search, QStringLiteral("co"));
    QCOMPARE(shownRows(palette),
             QStringList({QStringLiteral("Fields"),
                          QStringLiteral("conduction"),
                          QStringLiteral("Colormaps"),
                          QStringLiteral("Coolwarm"),
                          QStringLiteral("Commands"),
                          QStringLiteral("Color")}));

    // A group without a match loses its header.
    QTest::keyClicks(search, QStringLiteral("l"));
    QCOMPARE(shownRows(palette),
             QStringList({QStringLiteral("Commands"), QStringLiteral("Color")}));
  }

  void paletteHighlightsMatch() {
    QWidget view;
    CommandPalette palette(&view);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    auto* rows = palette.findChild<QListWidget*>();
    QVERIFY(search && rows);
    // Pixels of the accent, the matched run's color: blue well above red.
    const auto accentPixels = [&palette, rows]() {
      const QRect row = rows->visualItemRect(rowNamed(palette, QStringLiteral("Reset view")));
      const QImage image = rows->viewport()->grab(row).toImage();
      int count = 0;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          const QColor pixel = image.pixelColor(x, y);
          count += pixel.blue() - pixel.red() > 100 ? 1 : 0;
        }
      }
      return count;
    };
    QCOMPARE(accentPixels(), 0);

    QTest::keyClicks(search, QStringLiteral("VIEW"));
    QVERIFY(accentPixels() > 10);
  }

  void paletteRunsChosenRow() {
    QWidget view;
    CommandPalette palette(&view);
    QSignalSpy closed(&palette, &CommandPalette::closed);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    QVERIFY(search);

    QTest::keyClicks(search, QStringLiteral("reset"));
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(runs == std::vector<int>({0, 0, 0, 0, 0, 0, 1}));
    QVERIFY(palette.isHidden());
    QCOMPARE(closed.count(), 1);

    palette.open(paletteItems(runs));
    auto* rows = palette.findChild<QListWidget*>();
    QTest::mouseClick(rows->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      rows->visualItemRect(rowNamed(palette, QStringLiteral("Layers"))).center());
    QVERIFY(runs == std::vector<int>({0, 0, 0, 0, 1, 0, 0}));
  }

  void paletteNeverRunsDisabled() {
    QWidget view;
    CommandPalette palette(&view);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    auto* rows = palette.findChild<QListWidget*>();
    QVERIFY(search && rows);

    QTest::keyClicks(search, QStringLiteral("col"));
    QTest::keyClick(search, Qt::Key_Return);
    QTest::mouseClick(rows->viewport(),
                      Qt::LeftButton,
                      Qt::NoModifier,
                      rows->visualItemRect(rowNamed(palette, QStringLiteral("Color"))).center());
    QVERIFY(runs == std::vector<int>(7, 0));
    QVERIFY(palette.isVisible());

    // Down steps over it.
    search->clear();
    QTest::keyClicks(search, QStringLiteral("co"));
    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(runs == std::vector<int>({0, 0, 0, 1, 0, 0, 0}));
  }

  void paletteEscCloses() {
    QWidget view;
    CommandPalette palette(&view);
    QSignalSpy closed(&palette, &CommandPalette::closed);
    std::vector<int> runs;
    openPalette(view, palette, paletteItems(runs));
    auto* search = palette.findChild<QLineEdit*>();
    QVERIFY(search);

    QTest::keyClicks(search, QStringLiteral("lat"));
    QTest::keyClick(search, Qt::Key_Escape);
    QVERIFY(palette.isHidden());
    QCOMPARE(closed.count(), 1);
    QVERIFY(runs == std::vector<int>(7, 0));
  }

  // count frames, step apart from 0.
  static std::vector<double> frameTimes(int count, double step) {
    std::vector<double> times;
    for (int frame = 0; frame < count; ++frame) {
      times.push_back(frame * step);
    }
    return times;
  }

  static bool sameTicks(const std::vector<double>& actual, const std::vector<double>& expected) {
    return std::equal(
        actual.begin(), actual.end(), expected.begin(), expected.end(), [](double a, double b) {
          return std::abs(a - b) < 1e-9;
        });
  }

  static QPoint onTrack(const PlaybackTimeline& timeline, int frame) {
    return QPointF(timeline.frameX(frame), timeline.trackRect().center().y()).toPoint();
  }

  void playbackBarEmitsIntent() {
    PlaybackBar bar(frameTimes(10, 0.0125));
    bar.resize(900, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    QSignalSpy play(&bar, &PlaybackBar::playToggled);
    QSignalSpy step(&bar, &PlaybackBar::stepRequested);
    QSignalSpy speed(&bar, &PlaybackBar::speedChanged);
    QSignalSpy loop(&bar, &PlaybackBar::loopToggled);

    QAbstractButton* playButton = buttonNamed(bar, QStringLiteral("Play"));
    QVERIFY(playButton);
    QVERIFY(playButton->toolTip().contains(QStringLiteral("Space")));
    QTest::mouseClick(playButton, Qt::LeftButton);
    QCOMPARE(play.count(), 1);
    QCOMPARE(play.last().at(0).toBool(), true);
    QVERIFY(bar.isPlaying());
    QCOMPARE(playButton->accessibleName(), QStringLiteral("Pause"));
    QTest::mouseClick(playButton, Qt::LeftButton);
    QCOMPARE(play.last().at(0).toBool(), false);

    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Next frame")), Qt::LeftButton);
    QCOMPARE(step.count(), 1);
    QCOMPARE(step.last().at(0).toInt(), 1);
    // Showing a frame is not a request; past the last, Next asks for nothing.
    bar.setStep(9);
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Next frame")), Qt::LeftButton);
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Previous frame")), Qt::LeftButton);
    QCOMPARE(step.count(), 2);
    QCOMPARE(step.last().at(0).toInt(), 8);

    // A click on the track seeks to the frame under it, a drag follows the pointer and a
    // press on the header seeks nowhere.
    auto* timeline = bar.findChild<PlaybackTimeline*>();
    QVERIFY(timeline);
    QCOMPARE(timeline->accessibleName(), QStringLiteral("Frame"));
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, onTrack(*timeline, 3));
    QCOMPARE(step.count(), 3);
    QCOMPARE(step.last().at(0).toInt(), 3);
    drag(timeline, onTrack(*timeline, 5), onTrack(*timeline, 7));
    QCOMPARE(step.count(), 5);
    QCOMPARE(step.at(3).at(0).toInt(), 5);
    QCOMPARE(step.last().at(0).toInt(), 7);
    QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(timeline->width() / 2, 2));
    QCOMPARE(step.count(), 5);
    QCOMPARE(bar.currentStep(), 7);

    QVERIFY(bar.loopEnabled());
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Loop")), Qt::LeftButton);
    QCOMPARE(loop.count(), 1);
    QCOMPARE(loop.last().at(0).toBool(), false);
    QVERIFY(!bar.loopEnabled());

    auto* menu = bar.findChild<QMenu*>();
    QVERIFY(menu);
    QVERIFY(menu->actions().size() == 5);
    menu->actions().at(2)->trigger();
    QCOMPARE(speed.count(), 1);
    QCOMPARE(speed.last().at(0).toDouble(), 4.0);
    QCOMPARE(bar.speedMultiplier(), 4.0);
    QCOMPARE(buttonNamed(bar, QStringLiteral("Playback speed"))->text(), QStringLiteral("4×"));
  }

  void playbackHeaderShowsTimeAndFrame() {
    PlaybackBar bar(frameTimes(250, 4.0));
    bar.setTraceLabel(QStringLiteral("vm"));
    bar.resize(1100, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    auto* timeline = bar.findChild<PlaybackTimeline*>();
    QVERIFY(timeline);

    bar.setStep(102);
    QVERIFY(timeline->header().label == QStringLiteral("vm"));
    QVERIFY(timeline->header().time == QStringLiteral("408"));
    QVERIFY(timeline->header().frame == QStringLiteral("frame 103 of 250"));
    QVERIFY(timeline->ticksShown());

    // The timeline keeps four significant digits.
    PlaybackBar fine(std::vector<double>{0.0, 0.123456, 1.0});
    fine.resize(1100, fine.sizeHint().height());
    fine.setStep(1);
    QVERIFY(fine.findChild<PlaybackTimeline*>()->header().time == QStringLiteral("0.1235"));

    // At its narrowest the bar keeps the time, dropping the ticks and then the frame number.
    bar.resize(bar.minimumSizeHint().width(), bar.height());
    QApplication::processEvents();
    QVERIFY(!timeline->ticksShown());
    QVERIFY(timeline->header().frame.isEmpty());
    QVERIFY(timeline->header().time == QStringLiteral("408"));
    const QWidget* switcher = bar.pager();
    QVERIFY(QRect(switcher->mapTo(&bar, QPoint(0, 0)), switcher->size()).right() <
            timeline->geometry().left());
  }

  void playbackTraceSplitsAtPlayhead() {
    PlaybackBar bar(frameTimes(11, 1.0));
    bar.resize(1100, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    auto* timeline = bar.findChild<PlaybackTimeline*>();
    QVERIFY(timeline);
    // Frame 2 is still unread, so the line passes over it; frame 8 has no value, a gap.
    std::vector<std::optional<double>> values(11, 0.5);
    values[2] = std::nullopt;
    values[8] = std::numeric_limits<double>::quiet_NaN();
    bar.setTrace(values, 0.0, 1.0);
    bar.setStep(5);

    // On black, as over the bar's dark surface.
    const auto drawn = [timeline]() {
      QImage image(timeline->size(), QImage::Format_ARGB32);
      image.fill(Qt::black);
      timeline->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);
      return image;
    };
    const QRectF area = timeline->traceRect();
    // What the trace draws in a column around its line at 0.5: the accent, the muted gray
    // or nothing. The rows end above the knob's focus ring.
    enum class Ink { Past, Ahead, None };
    const auto inkAt = [&area](const QImage& image, double x) {
      Ink ink = Ink::None;
      for (int y = static_cast<int>(area.top()); y < area.center().y() + 4.0; ++y) {
        const QColor pixel = image.pixelColor(static_cast<int>(x), y);
        if (pixel.blue() - pixel.red() > 60) {
          return Ink::Past;
        }
        if (pixel.red() > 24 && std::abs(pixel.blue() - pixel.red()) < 12) {
          ink = Ink::Ahead;
        }
      }
      return ink;
    };
    const QImage image = drawn();
    const double playhead = timeline->frameX(5);
    QVERIFY(inkAt(image, timeline->frameX(1)) == Ink::Past);
    QVERIFY(inkAt(image, timeline->frameX(2)) == Ink::Past);
    QVERIFY(inkAt(image, playhead - 4.0) == Ink::Past);
    QVERIFY(inkAt(image, playhead + 4.0) == Ink::Ahead);
    QVERIFY(inkAt(image, timeline->frameX(6)) == Ink::Ahead);
    QVERIFY(inkAt(image, timeline->frameX(8)) == Ink::None);
    QVERIFY(inkAt(image, timeline->frameX(9) + 4.0) == Ink::Ahead);

    bar.setTrace({}, 0.0, 1.0);
    QVERIFY(inkAt(drawn(), timeline->frameX(1)) == Ink::None);
  }

  void timeTicksAreRound() {
    QVERIFY(sameTicks(timeTicks(0.0, 1000.0, 5), {0.0, 250.0, 500.0, 750.0, 1000.0}));
    QVERIFY(sameTicks(timeTicks(0.0, 1000.0, 6), {0.0, 200.0, 400.0, 600.0, 800.0, 1000.0}));
    QVERIFY(sameTicks(timeTicks(0.0, 1.4875, 6), {0.0, 0.25, 0.5, 0.75, 1.0, 1.25}));
    QVERIFY(sameTicks(timeTicks(3.2, 7.9, 5), {4.0, 5.0, 6.0, 7.0}));
    QVERIFY(sameTicks(timeTicks(-80.0, 40.0, 6), {-75.0, -50.0, -25.0, 0.0, 25.0}));
    QVERIFY(timeTicks(1.0, 1.0, 5).empty());
    QVERIFY(timeTicks(2.0, 1.0, 5).empty());
    QVERIFY(timeTicks(0.0, std::numeric_limits<double>::quiet_NaN(), 5).empty());
  }

  void spaceKeyPlaysInATimeSeries() {
    QVERIFY(spaceAction(Qt::NoModifier, true) == SpaceAction::PlayPause);
    QVERIFY(spaceAction(Qt::ShiftModifier, true) == SpaceAction::NextField);
    QVERIFY(spaceAction(kStepBackModifier, true) == SpaceAction::PreviousField);
    QVERIFY(spaceAction(Qt::NoModifier, false) == SpaceAction::NextField);
    QVERIFY(spaceAction(Qt::ShiftModifier, false) == SpaceAction::NextField);
    QVERIFY(spaceAction(kStepBackModifier, false) == SpaceAction::PreviousField);

    // The playback bar's field buttons name the keys that step fields there.
    PlaybackBar bar(frameTimes(3, 1.0));
    bar.pager()->setPage(QStringLiteral("vm"), QStringLiteral("Points · continuous"), 1, 4);
    const QString next = QKeySequence(static_cast<int>(Qt::SHIFT) | Qt::Key_Space)
                             .toString(QKeySequence::NativeText);
    QCOMPARE(buttonNamed(bar, QStringLiteral("Next field"))->toolTip(),
             QStringLiteral("Next field (%1)").arg(next));
    ScalarPager capsule(PagerPlacement::Capsule);
    QCOMPARE(buttonNamed(capsule, QStringLiteral("Next field"))->toolTip(),
             QStringLiteral("Next field (Space)"));
  }

  void playbackBarKeepsItsMargins() {
    constexpr int kMargin = 16;
    QWidget view;
    auto* bar = new PlaybackBar({0.0, 0.5, 1.0}, &view);
    for (const QSize& size : {QSize(1100, 572), playbackBarViewMinimum(*bar, kMargin)}) {
      view.resize(size);
      bar->setGeometry(playbackBarGeometry(view.size(), *bar, kMargin));
      const QRect placed = bar->geometry();
      QCOMPARE(placed.left(), kMargin);
      QCOMPARE(view.width() - 1 - placed.right(), kMargin);
      QCOMPARE(view.height() - 1 - placed.bottom(), kMargin);
      QVERIFY(placed.top() >= kMargin);
    }
  }

  void playbackPagerShowsBadgeUnderTheName() {
    PlaybackBar bar(frameTimes(3, 1.0));
    bar.pager()->setPage(QStringLiteral("vm"), QStringLiteral("Points · continuous"), 1, 4);
    bar.resize(1100, bar.sizeHint().height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    const auto* title = bar.findChild<QLabel*>("scalarPagerTitle");
    const auto* detail = bar.findChild<QLabel*>("scalarPagerDetail");
    const auto* badge = bar.findChild<QLabel*>("scalarPagerPosition");
    QVERIFY(title);
    QVERIFY(detail);
    QVERIFY(badge);
    QCOMPARE(badge->text(), QStringLiteral("1 / 4"));
    QCOMPARE(detail->text(), QStringLiteral("Points · continuous"));
    const QRect titleRect(title->mapTo(&bar, QPoint(0, 0)), title->size());
    const QRect detailRect(detail->mapTo(&bar, QPoint(0, 0)), detail->size());
    const QRect badgeRect(badge->mapTo(&bar, QPoint(0, 0)), badge->size());
    QVERIFY(badgeRect.top() > titleRect.center().y());
    QVERIFY(badgeRect.left() > detailRect.right());
    QVERIFY(qAbs(badgeRect.center().y() - detailRect.center().y()) <= 2);
  }

  void annotationBarEmitsIntent() {
    AnnotationBar bar;
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    std::optional<AnnotationTool> tool;
    connect(
        &bar, &AnnotationBar::toolChosen, this, [&tool](AnnotationTool chosen) { tool = chosen; });
    QSignalSpy label(&bar, &AnnotationBar::labelChosen);
    QSignalSpy rings(&bar, &AnnotationBar::brushRingsChosen);
    QSignalSpy undo(&bar, &AnnotationBar::undoRequested);
    QSignalSpy leave(&bar, &AnnotationBar::leaveRequested);

    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Erase")), Qt::LeftButton);
    QVERIFY(tool == AnnotationTool::Erase);
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Paint")), Qt::LeftButton);
    QVERIFY(tool == AnnotationTool::Paint);

    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Label 3")), Qt::LeftButton);
    QCOMPARE(label.count(), 1);
    QCOMPARE(label.last().at(0).toInt(), 3);

    auto* slider = bar.findChild<QSlider*>();
    QVERIFY(slider);
    QTest::keyClick(slider, Qt::Key_Right);
    QCOMPARE(rings.count(), 1);
    QCOMPARE(rings.last().at(0).toInt(), 4);
    // Showing a size, e.g. after + or -, is not a choice.
    bar.setBrushRings(7);
    QCOMPARE(rings.count(), 1);

    // Nothing to undo until a stroke is pushed.
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Undo")), Qt::LeftButton);
    QCOMPARE(undo.count(), 0);
    bar.setUndoAvailable(true);
    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Undo")), Qt::LeftButton);
    QCOMPARE(undo.count(), 1);

    QTest::mouseClick(buttonNamed(bar, QStringLiteral("Leave annotate")), Qt::LeftButton);
    QCOMPARE(leave.count(), 1);
  }

  void annotationBarWrapsToFit() {
    AnnotationBar bar;
    const QSize wide = bar.fitWidth(QWIDGETSIZE_MAX);
    const QSize narrow = bar.fitWidth(wide.width() / 2);
    QVERIFY(narrow.width() <= wide.width() / 2);
    QVERIFY(narrow.height() > wide.height());
    QCOMPARE(bar.fitWidth(QWIDGETSIZE_MAX), wide);
  }

  void strokeLabels() {
    QVERIFY(strokeLabel(AnnotationTool::Paint, Qt::LeftButton, 3) == 3);
    QVERIFY(strokeLabel(AnnotationTool::Paint, Qt::RightButton, 3) == 0);
    QVERIFY(strokeLabel(AnnotationTool::Erase, Qt::LeftButton, 3) == 0);
    QVERIFY(strokeLabel(AnnotationTool::Erase, Qt::RightButton, 3) == 0);
    QVERIFY(!strokeLabel(AnnotationTool::Rotate, Qt::LeftButton, 3));
    QVERIFY(!strokeLabel(AnnotationTool::Paint, Qt::MiddleButton, 3));
  }

  void strokeReleaseMissedWithoutButton() {
    QVERIFY(strokeReleaseMissed(QEvent::MouseMove, Qt::NoButton, Qt::NoButton));
    QVERIFY(!strokeReleaseMissed(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton));
    QVERIFY(strokeReleaseMissed(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton));
    QVERIFY(!strokeReleaseMissed(
        QEvent::MouseButtonPress, Qt::RightButton, Qt::LeftButton | Qt::RightButton));
    QVERIFY(!strokeReleaseMissed(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton));
  }
};

QTEST_MAIN(UiTests)
#include "UiTests.moc"
