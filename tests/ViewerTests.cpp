#include "ColorBarWidget.h"
#include "ColorPanel.h"
#include "LayersPanel.h"
#include "ProbeCard.h"
#include "TemporalSource.h"
#include "TestMeshes.h"
#include "ViewerWindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVTKOpenGLNativeWidget.h>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkXMLPolyDataReader.h>

// Count submissions independently of GPU speed or the presence of a window server.
class CountingWindow : public vtkGenericOpenGLRenderWindow {
public:
  static CountingWindow* New();
  vtkTypeMacro(CountingWindow, vtkGenericOpenGLRenderWindow) void Render() override {
    ++renders;
  }
  int renders = 0;
};
vtkStandardNewMacro(CountingWindow);

class TestTemporalSource : public TemporalSource {
public:
  explicit TestTemporalSource(vtkDataSet* data) : source_(vtkSmartPointer<vtkPolyData>::New()) {
    source_->DeepCopy(data);
    timeValues_ = {0.0, 0.5, 1.0};
  }
  bool readStepInto(int step, vtkDataSet* target) override {
    if (!target || step < 0 || step >= steps()) {
      return false;
    }
    target->DeepCopy(source_);
    auto* values = target->GetPointData()->GetArray("voltage");
    for (vtkIdType i = 0; i < values->GetNumberOfTuples(); ++i) {
      values->SetComponent(i, 0, values->GetComponent(i, 0) + step);
    }
    return true;
  }
  bool sampledScalarRange(const std::string& name, double out[2], int) override {
    auto* values = source_->GetPointData()->GetArray(name.c_str());
    if (!values) {
      return false;
    }
    values->GetRange(out);
    if (name == "voltage") {
      out[1] += steps() - 1;
    }
    return true;
  }
  void setActiveArray(const std::string&) override {}

private:
  vtkSmartPointer<vtkPolyData> source_;
};

class ViewerTests : public QObject {
  Q_OBJECT

  static std::unique_ptr<ViewerWindow>
  viewer(CountingWindow* window, MeshLoadResult load, const ViewerOptions& options) {
    auto result =
        std::unique_ptr<ViewerWindow>(new ViewerWindow(std::move(load), options, window, nullptr));
    result->vtkWidget_->resize(1200, 800);
    window->SetSize(1200, 800);
    result->renderer_.resetCamera();
    result->layoutOverlays();
    return result;
  }

  static void paint(ViewerWindow& view) {
    vtkRenderer* renderer = view.renderer_.getRenderer();
    renderer->SetWorldPoint(0.33, 0.27, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view.vtkWidget_->devicePixelRatioF();
    view.strokeValue_ = 1;
    view.paintAtWidgetPos(
        QPointF(display[0] / scale, view.vtkWidget_->height() - display[1] / scale), 1);
  }

private slots:
  void initTestCase() {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  }

  void windowTitle_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<int>("files");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("documentPath");

    QTest::newRow("relative") << QStringLiteral("mesh.vtp") << 1
                              << QStringLiteral("mesh.vtp[*] - vv")
                              << QFileInfo("mesh.vtp").absoluteFilePath();
    QTest::newRow("nested") << QStringLiteral("models/heart.vtk") << 1
                            << QStringLiteral("heart.vtk[*] - vv")
                            << QFileInfo("models/heart.vtk").absoluteFilePath();
    QTest::newRow("absolute") << QStringLiteral("/data/heart.vtk") << 1
                              << QStringLiteral("heart.vtk[*] - vv")
                              << QFileInfo("/data/heart.vtk").absoluteFilePath();
    QTest::newRow("unicode") << QStringLiteral("models/cœur 左.vtk") << 1
                             << QStringLiteral("cœur 左.vtk[*] - vv")
                             << QFileInfo(QStringLiteral("models/cœur 左.vtk")).absoluteFilePath();
    QTest::newRow("stdin") << QStringLiteral("-") << 1 << QStringLiteral("Standard input[*] - vv")
                           << QString();
    QTest::newRow("two-files") << QStringLiteral("models/heart.vtk") << 2
                               << QStringLiteral("heart.vtk + 1 more[*] - vv") << QString();
    QTest::newRow("three-files") << QStringLiteral("models/heart.vtk") << 3
                                 << QStringLiteral("heart.vtk + 2 more[*] - vv") << QString();
    QTest::newRow("stdin-comparison")
        << QStringLiteral("-") << 2 << QStringLiteral("Standard input + 1 more[*] - vv")
        << QString();
    QTest::newRow("unnamed") << QString() << 1 << QStringLiteral("vv[*]") << QString();
  }

  void windowTitle() {
    QFETCH(QString, source);
    QFETCH(int, files);
    QFETCH(QString, title);
    QFETCH(QString, documentPath);
    auto load = testMeshes(files);
    for (auto& name : load.meshes.names) {
      name = source.toUtf8().toStdString();
    }
    vtkNew<CountingWindow> window;
    auto view = viewer(window, std::move(load), ViewerOptions{});

    QCOMPARE(view->windowTitle(), title);
    QCOMPARE(view->windowFilePath(), documentPath);
    view->setWindowModified(true);
    QVERIFY(view->isWindowModified());
    QCOMPARE(view->windowTitle(), title);
    view->setWindowModified(false);
  }

  void multipartWindowTitle() {
    auto load = testMeshes(2);
    load.meshes.groups = {{"mesh.vtp", {0, 1}}};
    vtkNew<CountingWindow> window;
    auto view = viewer(window, std::move(load), ViewerOptions{});

    QCOMPARE(view->windowTitle(), QStringLiteral("mesh.vtp[*] - vv"));
    QCOMPARE(view->windowFilePath(), QFileInfo("mesh.vtp").absoluteFilePath());
  }

  void annotationCloseDuringStroke() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.annotate = true;
    auto view = viewer(window, testMeshes(1), options);
    paint(*view);
    QVERIFY(!view->currentStroke_.empty());
    QVERIFY(!view->isWindowModified());
    bool prompted = false;
    QTimer::singleShot(0, view.get(), [&prompted]() {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        prompted = true;
        box->button(QMessageBox::Cancel)->click();
      }
    });
    QTimer guard;
    connect(&guard, &QTimer::timeout, []() {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        box->reject();
      }
    });
    guard.start(5000);
    QCloseEvent close;
    view->closeEvent(&close);

    QVERIFY(prompted);
    QVERIFY(!close.isAccepted());
    QVERIFY(view->isWindowModified());
    QVERIFY(view->currentStroke_.empty());
    QVERIFY(!view->strokeValue_);
    QCOMPARE(view->undoStack_.size(), size_t(1));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("labels.vtp");
    QTimer::singleShot(0, view.get(), [path]() {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog, "accept");
      }
    });
    connect(&guard, &QTimer::timeout, []() {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        dialog->reject();
      }
    });
    QVERIFY(view->saveAnnotations());
    QVERIFY(!view->isWindowModified());
    vtkNew<vtkXMLPolyDataReader> saved;
    saved->SetFileName(path.toUtf8().constData());
    saved->Update();
    auto* labels = saved->GetOutput()->GetCellData()->GetArray("label");
    QVERIFY(labels);
    double range[2];
    labels->GetRange(range);
    QCOMPARE(range[1], 1.0);

    QCloseEvent clean;
    view->closeEvent(&clean);
    QVERIFY(clean.isAccepted());
  }

  void hiddenPinVisibility() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.temporal = std::make_shared<TestTemporalSource>(load.meshes.meshes.front());
    auto view = viewer(window, std::move(load), ViewerOptions{});
    vtkRenderer* renderer = view->renderer_.getRenderer();
    renderer->SetWorldPoint(0.33, 0.27, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    view->pinProbe(QPointF(display[0] / scale, view->vtkWidget_->height() - display[1] / scale));
    QVERIFY(view->pin_);
    const auto pin = view->pin_;
    QAbstractButton* eye = nullptr;
    for (auto* button : view->layersPanel_->findChildren<QAbstractButton*>()) {
      if (button->accessibleName() == QStringLiteral("Show or hide mesh.vtp")) {
        eye = button;
        break;
      }
    }
    QVERIFY(eye);
    const int before = window->renders;
    eye->click();
    QCOMPARE(window->renders - before, 1);
    view->showProbe();
    QVERIFY(view->pin_);
    QVERIFY(!view->renderer_.probeAgain(*pin));
    QVERIFY(view->probeCards_.front()->isHidden());
    view->applyScalarAtIndex(1);
    view->showFrame(1);
    QCOMPARE(view->currentPlaybackStep_, 1);
    QVERIFY(!view->renderer_.probeAgain(*pin));
    eye->click();
    QVERIFY(view->renderer_.probeAgain(*pin));
    view->showProbe();
    QVERIFY(!view->probeCards_.front()->isHidden());
    const int beforeSolo = window->renders;
    QTest::mouseClick(eye, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(window->renders - beforeSolo, 1);
    QVERIFY(!view->renderer_.context.actors[1]->GetVisibility());
    const int beforeAll = window->renders;
    QTest::mouseClick(eye, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(window->renders - beforeAll, 1);
    QVERIFY(view->renderer_.context.actors[1]->GetVisibility());
  }

  void matchingProbeViews_data() {
    QTest::addColumn<bool>("exploded");
    QTest::newRow("files") << false;
    QTest::newRow("fields-and-files") << true;
  }

  void matchingProbeViews() {
    QFETCH(bool, exploded);
    auto load = testMeshes(3);
    for (const auto& mesh : load.meshes.meshes) {
      mesh->GetPointData()->RemoveArray("regions");
      vtkNew<vtkIntArray> regions;
      regions->SetName("regions");
      for (vtkIdType i = 0; i < mesh->GetNumberOfCells(); ++i) {
        regions->InsertNextValue(static_cast<int>(i % 20));
      }
      mesh->GetCellData()->AddArray(regions);
    }
    auto* reordered = vtkPolyData::SafeDownCast(load.meshes.meshes[1]);
    vtkNew<vtkCellArray> reversed;
    for (vtkIdType i = reordered->GetNumberOfCells(); i > 0; --i) {
      auto* cell = reordered->GetCell(i - 1);
      reversed->InsertNextCell(cell->GetPointIds());
    }
    reordered->SetPolys(reversed);
    auto* voltage = reordered->GetPointData()->GetArray("voltage");
    for (vtkIdType i = 0; i < voltage->GetNumberOfTuples(); ++i) {
      voltage->SetComponent(i, 0, voltage->GetComponent(i, 0) + 1000.0);
    }
    auto* shifted = vtkPolyData::SafeDownCast(load.meshes.meshes[2]);
    for (vtkIdType i = 0; i < shifted->GetNumberOfPoints(); ++i) {
      double point[3];
      shifted->GetPoint(i, point);
      shifted->GetPoints()->SetPoint(i, point[0], point[1], 1.0);
    }
    shifted->GetPoints()->Modified();
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = exploded;
    auto view = viewer(window, std::move(load), options);
    if (exploded) {
      view->updateFacetPages();
      view->renderer_.resetCamera();
    }
    vtkRenderer* renderer = view->renderer_.getRenderer();
    renderer->SetWorldPoint(0.331, 0.271, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    const QPointF point(display[0] / scale, view->vtkWidget_->height() - display[1] / scale);
    const int beforeHover = window->renders;
    view->requestProbe(point);
    view->showProbe();
    QCOMPARE(window->renders, beforeHover);

    const size_t expected = exploded ? 4 : 2;
    QCOMPARE(view->probeCards_.size(), expected);
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(!card->isHidden());
    }
    const int beforePin = window->renders;
    view->pinProbe(point);
    QCOMPARE(window->renders - beforePin, 1);
    QVERIFY(view->pin_);
    const auto matches = view->renderer_.matchingProbes(*view->pin_);
    QCOMPARE(matches.size(), expected);
    const auto source = view->renderer_.probeAgain(*view->pin_);
    QVERIFY(source);
    for (const auto& match : matches) {
      QVERIFY(match.mesh != 2);
      if (match.mesh == 1) {
        QVERIFY(match.cell != source->cell);
        if (match.field == "voltage") {
          QVERIFY(std::abs(match.value - source->value - 1000.0) < 0.01);
        }
      }
      if (match.field == "regions") {
        QVERIFY(match.category);
        QCOMPARE(match.value, static_cast<double>(match.cell % 20));
      }
    }
    // A scalar-only update reads the new values at the retained location.
    for (vtkIdType i = 0; i < voltage->GetNumberOfTuples(); ++i) {
      voltage->SetComponent(i, 0, voltage->GetComponent(i, 0) + 10.0);
    }
    const auto updated = view->renderer_.matchingProbes(*view->pin_);
    for (const auto& match : updated) {
      if (match.mesh == 1 && match.field == "voltage") {
        QVERIFY(std::abs(match.value - source->value - 1010.0) < 0.01);
      }
    }
    const size_t targetActor = exploded ? 2 : 1;
    const int beforeVisibility = window->renders;
    QVERIFY(view->renderer_.setPartsVisible({{targetActor, false}}));
    QCOMPARE(window->renders - beforeVisibility, 1);
    view->showProbe();
    const auto hidden = view->renderer_.matchingProbes(*view->pin_);
    QVERIFY(std::none_of(hidden.begin(), hidden.end(), [exploded](const auto& match) {
      return match.panel == (exploded ? 2 : 1);
    }));
    QVERIFY(view->renderer_.setPartsVisible({{targetActor, true}}));
    view->hideProbe();
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(card->isHidden());
    }
    view->showProbe();
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(!card->isHidden());
    }
    // Picking another file's categorical panel keeps that panel as the pin's source.
    if (exploded) {
      const auto target = std::find_if(matches.begin(), matches.end(), [](const auto& match) {
        return match.mesh == 1 && match.field == "regions";
      });
      QVERIFY(target != matches.end());
      const auto targetDisplay = view->renderer_.displayPosition(*target);
      QVERIFY(targetDisplay);
      view->pinProbe(QPointF((*targetDisplay)[0] / scale,
                             view->vtkWidget_->height() - (*targetDisplay)[1] / scale));
      QVERIFY(view->pin_);
      QCOMPARE(view->pin_->mesh, size_t(1));
      QCOMPARE(view->pin_->field, std::string("regions"));
      QCOMPARE(view->renderer_.matchingProbes(*view->pin_).size(), expected);
    }
    view->unpin();
    QVERIFY(!view->pin_);
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(card->isHidden());
    }
  }

  void unpinInteractions_data() {
    QTest::addColumn<bool>("exploded");
    QTest::newRow("single-view") << false;
    QTest::newRow("exploded") << true;
  }

  void unpinInteractions() {
    QFETCH(bool, exploded);
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = exploded;
    auto view = viewer(window, testMeshes(1), options);
    view->probeAction_->setChecked(true);
    vtkRenderer* renderer = view->renderer_.getRenderer();
    renderer->SetWorldPoint(0.331, 0.271, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    const QPointF point(display[0] / scale, view->vtkWidget_->height() - display[1] / scale);
    view->pinProbe(point);
    QVERIFY(view->pin_);
    QAction* escape = nullptr;
    for (QAction* action : view->actions()) {
      if (action->shortcut() == QKeySequence(Qt::Key_Escape)) {
        escape = action;
      }
    }
    QVERIFY(escape);
    view->styleAction_->setChecked(true);
    escape->trigger();
    QVERIFY(!view->styleAction_->isChecked());
    QVERIFY(view->pin_);
    escape->trigger();
    QVERIFY(!view->pin_);
    QVERIFY(view->probeAction_->isChecked());
    view->requestProbe(point);
    view->showProbe();
    QVERIFY(!view->probeCards_.front()->isHidden());
    QVERIFY(view->probeCards_.front()->testAttribute(Qt::WA_TransparentForMouseEvents));

    view->pinProbe(point);
    QVERIFY(view->pin_);
    auto* card = view->probeCards_.back();
    auto* close = card->findChild<QToolButton*>(QStringLiteral("probeUnpin"));
    QVERIFY(close);
    QVERIFY(!close->isHidden());
    const int before = window->renders;
    QTest::mouseClick(close, Qt::LeftButton);
    QVERIFY(!view->pin_);
    QCOMPARE(window->renders - before, 1);
    QVERIFY(view->probeAction_->isChecked());
    for (ProbeCard* other : view->probeCards_) {
      QVERIFY(other->isHidden());
    }
    view->requestProbe(point);
    view->showProbe();
    QVERIFY(!view->probeCards_.front()->isHidden());
  }

  void playbackPreservesComparisonGeometry() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.temporal = std::make_shared<TestTemporalSource>(load.meshes.meshes.front());
    auto view = viewer(window, std::move(load), ViewerOptions{});
    const auto field =
        std::find_if(view->scalarFields_.begin(),
                     view->scalarFields_.end(),
                     [](const auto& candidate) { return candidate.name == "voltage"; });
    QVERIFY(field != view->scalarFields_.end());
    view->applyScalarAtIndex(static_cast<int>(std::distance(view->scalarFields_.begin(), field)));
    auto* renderer = view->renderer_.getRenderer();
    renderer->SetWorldPoint(0.331, 0.271, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    view->pinProbe(QPointF(display[0] / scale, view->vtkWidget_->height() - display[1] / scale));
    QVERIFY(view->pin_);
    QCOMPARE(view->renderer_.matchingProbes(*view->pin_).size(), size_t(2));
    const double initial = view->renderer_.probeAgain(*view->pin_)->value;
    auto* comparison = view->load_.meshes.meshes[1].GetPointer();
    const auto modified = comparison->GetMTime();

    view->showFrame(1);
    view->showProbe();
    QCOMPARE(comparison->GetMTime(), modified);
    const auto matches = view->renderer_.matchingProbes(*view->pin_);
    QCOMPARE(matches.size(), size_t(2));
    QVERIFY(std::abs(matches[0].value - initial - 1.0) < 0.01);
    QVERIFY(std::abs(matches[1].value - initial) < 0.01);
  }

  void pinnedFacetPresentation() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = true;
    auto view = viewer(window, testMeshes(2), options);
    view->renderer_.setFacetPages(1, 0, 0);
    view->layoutOverlays();
    auto* renderer = view->renderer_.getRenderer();
    renderer->SetWorldPoint(0.331, 0.271, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    view->pinProbe(QPointF(display[0] / scale, view->vtkWidget_->height() - display[1] / scale));
    QVERIFY(view->pin_);

    const auto visibleMarkers = [&window]() {
      int count = 0;
      auto* renderers = window->GetRenderers();
      renderers->InitTraversal();
      while (auto* panel = renderers->GetNextItem()) {
        if (!panel->GetDraw()) {
          continue;
        }
        auto* actors = panel->GetActors();
        actors->InitTraversal();
        while (auto* actor = actors->GetNextActor()) {
          if (!actor->GetPickable() && actor->GetVisibility()) {
            ++count;
          }
        }
      }
      return count;
    };
    QCOMPARE(visibleMarkers(), 2);
    view->showFacetPage(1);
    QVERIFY(view->pin_);
    QTRY_COMPARE(visibleMarkers(), 2);
    QTRY_VERIFY(!view->probeCards_.front()->isHidden());
    auto* swatch = view->probeCards_.front()->findChild<QLabel*>(QStringLiteral("probeSwatch"));
    QVERIFY(swatch);
    QTRY_VERIFY(!swatch->isHidden());

    const auto matches = view->renderer_.matchingProbes(*view->pin_);
    QCOMPARE(matches.size(), size_t(2));
    QCOMPARE(matches.front().field, std::string("regions"));
    const int category = static_cast<int>(matches.front().value);
    const QColor color(Qt::magenta);
    const int before = window->renders;
    emit view->facetColorBars_[1]->categoryColorChanged(category, color);
    QCOMPARE(window->renders - before, 1);
    QTRY_VERIFY(swatch->styleSheet().contains(color.name()));
    for (auto* card : view->probeCards_) {
      auto* other = card->findChild<QLabel*>(QStringLiteral("probeSwatch"));
      QVERIFY(other);
      QVERIFY(other->styleSheet().contains(color.name()));
    }
    view->showFacetPage(0);
    QTRY_COMPARE(visibleMarkers(), 2);
    QTRY_VERIFY(swatch->isHidden());
  }

  void colorPanelTypedValuesKeepPrecision() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.fixedRange = std::array<double, 2>{0.003, 999.875};
    auto load = testMeshes(1);
    load.meshes.meshes.front()->GetPointData()->RemoveArray("regions");
    auto view = viewer(window, std::move(load), options);
    view->updateColorRange();
    auto* min = view->colorPanel_->findChild<QLineEdit*>(QStringLiteral("colorPanelMin"));
    auto* max = view->colorPanel_->findChild<QLineEdit*>(QStringLiteral("colorPanelMax"));
    QVERIFY(min && max);
    const auto enter = [](QLineEdit* edit, const QString& text) {
      edit->selectAll();
      QTest::keyClicks(edit, text);
      QTest::keyClick(edit, Qt::Key_Return);
    };

    enter(min, QStringLiteral("12.2500"));
    enter(max, QStringLiteral("999.87500"));
    view->updateColorRange();

    QCOMPARE(view->colorBar_->clipMin_, 12.25);
    QCOMPARE(view->colorBar_->valueText(view->colorBar_->clipMin_), QStringLiteral("12.25"));
    QCOMPARE(view->colorBar_->valueText(view->colorBar_->clipMax_), QStringLiteral("999.875"));

    const auto buttons = view->colorPanel_->findChildren<QToolButton*>();
    const auto reset = std::find_if(buttons.cbegin(), buttons.cend(), [](const auto* button) {
      return button->text() == QStringLiteral("Use data range");
    });
    QVERIFY(reset != buttons.cend());
    (*reset)->click();

    QCOMPARE(view->colorBar_->clipMin_, 0.003);
    QCOMPARE(view->colorBar_->valueText(view->colorBar_->clipMin_), QStringLiteral("0.003"));
    QCOMPARE(view->colorBar_->valueText(view->colorBar_->clipMax_), QStringLiteral("1000"));

    const int initialWidth = view->colorBar_->width();
    enter(min, QStringLiteral("0.12345678901234567"));
    enter(max, QStringLiteral("876.5432109876543"));
    QVERIFY(view->colorBar_->width() > initialWidth);
    QCOMPARE(view->colorBar_->size(), view->colorBar_->sizeHint());
    QVERIFY(view->vtkWidget_->rect().contains(view->colorBar_->geometry()));
    for (const QRectF& value : view->colorBar_->clipValueRects()) {
      QVERIFY(!value.isEmpty());
      QVERIFY(QRectF(view->colorBar_->rect()).contains(value));
    }

    enter(max, QStringLiteral("99999"));
    QCOMPARE(view->colorBar_->valueText(view->colorBar_->clipMax_), QStringLiteral("1000"));
    QCOMPARE(view->colorBar_->width(), initialWidth);
  }

  void facetColumnUpdates() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = true;
    auto load = testMeshes(3);
    for (auto& mesh : load.meshes.meshes) {
      auto* values = vtkFloatArray::SafeDownCast(mesh->GetPointData()->GetArray("voltage"));
      QVERIFY(values);
      values->SetValue(0, 0.006F);
      values->Modified();
    }
    auto view = viewer(window, std::move(load), options);
    ColorBarWidget* bar = view->facetColorBars_.front();
    QVERIFY(bar);
    int before = window->renders;
    emit bar->clipRangeChanged(100.0, 500.0);
    QCOMPARE(window->renders - before, 1);
    for (size_t i : {size_t(0), size_t(2), size_t(4)}) {
      FacetPanelInfo info;
      QVERIFY(view->renderer_.getFacetPanelInfo(i, info));
      QCOMPARE(info.clipRange[0], 100.0);
      QCOMPARE(info.clipRange[1], 500.0);
      QCOMPARE(view->facetColorBars_[i]->clipMin_, 100.0);
    }
    const auto windowFlags = bar->windowFlags();
    // Give the legend its own surface while the VTK parent stays offscreen.
    bar->setWindowFlags(Qt::Window);
    bar->show();
    QVERIFY(QTest::qWaitForWindowExposed(bar));
    const auto enter = [bar](size_t index, const QString& input) {
      const QPoint text = bar->clipValueRects()[index].center().toPoint();
      QTest::mouseDClick(bar, Qt::LeftButton, Qt::NoModifier, text);
      QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, text);
      auto* editor = bar->findChild<QLineEdit*>();
      if (!editor || !editor->isVisible())
        return false;
      QTest::keyClicks(editor, input);
      QTest::keyClick(editor, Qt::Key_Return);
      return true;
    };
    QVERIFY(enter(0, QStringLiteral("100.1234567890123")));
    QVERIFY(enter(1, QStringLiteral("500.1234567890123")));

    for (size_t i : {size_t(0), size_t(2), size_t(4)}) {
      auto* legend = view->facetColorBars_[i];
      QCOMPARE(legend->valueText(legend->clipMin_), QStringLiteral("100.1234567890123"));
      QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("500.1234567890123"));
      QCOMPARE(legend->width(), legend->sizeHint().width());
      for (const QRectF& value : legend->clipValueRects()) {
        QVERIFY(!value.isEmpty());
        QVERIFY(QRectF(legend->rect()).contains(value));
      }
    }
    QVERIFY(enter(0, QStringLiteral("0.004")));
    bar->setWindowFlags(windowFlags);
    view->layoutFacetColorBars();
    before = window->renders;
    emit bar->colormapChosen(QStringLiteral("viridis"), true);
    QCOMPARE(window->renders - before, 1);
    for (size_t i : {size_t(0), size_t(2), size_t(4)}) {
      auto* legend = view->facetColorBars_[i];
      QCOMPARE(legend->clipMin_, static_cast<double>(0.006F));
      QCOMPARE(legend->valueText(legend->clipMin_), QStringLiteral("0.006"));
      QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("500.1234567890123"));
    }
    before = window->renders;
    emit view->facetColorBars_[1]->paletteChosen(QStringLiteral("okabe-ito"));
    QCOMPARE(window->renders - before, 1);
    for (size_t i : {size_t(1), size_t(3), size_t(5)}) {
      FacetPanelInfo info;
      QVERIFY(view->renderer_.getFacetPanelInfo(i, info));
      QCOMPARE(info.analysis.coloring.palette, findCategoryPalette("okabe-ito"));
    }
    before = window->renders;
    emit view->facetColorBars_[1]->categoryColorChanged(19, QColor(Qt::magenta));
    QCOMPARE(window->renders - before, 1);
    for (size_t i : {size_t(1), size_t(3), size_t(5)}) {
      QCOMPARE(view->facetColorBars_[i]->catEntries_[19].second, QColor(Qt::magenta));
    }
    QVERIFY(!view->renderer_.setPartsVisible({{0, false}, {999, false}}));
    QVERIFY(view->renderer_.context.actors.front()->GetVisibility());
  }

  void missingFieldColumns() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.meshes.meshes.front()->GetPointData()->RemoveArray("voltage");
    ViewerOptions options;
    options.explodeView = true;
    auto view = viewer(window, std::move(load), options);
    size_t selected = 0;
    for (size_t i = 0; i < view->facetColorBars_.size(); ++i) {
      FacetPanelInfo info;
      QVERIFY(view->renderer_.getFacetPanelInfo(i, info));
      if (info.title == "voltage" && info.hasScalar) {
        selected = i;
      }
    }
    QVERIFY(view->facetColorBars_[selected]);
    const int before = window->renders;
    emit view->facetColorBars_[selected]->colormapChosen(QStringLiteral("hue wheel"), false);
    QCOMPARE(window->renders - before, 1);
    for (size_t i = 0; i < view->facetColorBars_.size(); ++i) {
      FacetPanelInfo info;
      QVERIFY(view->renderer_.getFacetPanelInfo(i, info));
      if (info.title == "voltage") {
        QCOMPARE(info.analysis.colormap.map, findColormap("hue wheel"));
        QCOMPARE(view->facetColorBars_[i] != nullptr, info.hasScalar);
      }
    }
  }

  void overflowCategoryEditing() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = true;
    auto view = viewer(window, testMeshes(2), options);
    ColorBarWidget* bar = view->facetColorBars_[1];
    bar->resize(bar->fitPanel(QSize(280, 200), 8));
    const auto layout = bar->categoryLayout();
    QVERIFY(layout.hidden > 0);
    const int entry = static_cast<int>(layout.entries.size()) - 1;
    QVERIFY(layout.entries.back().isEmpty());
    bool selected = false;
    QTimer pick;
    connect(&pick, &QTimer::timeout, view.get(), [entry, &selected]() {
      if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
        if (!selected) {
          for (QAction* action : menu->actions()) {
            if (action->data().isValid() && action->data().toInt() == entry) {
              selected = true;
              menu->setActiveAction(action);
              QTest::keyClick(menu, Qt::Key_Return);
              return;
            }
          }
        } else {
          for (auto* swatch : menu->findChildren<QToolButton*>()) {
            if (swatch->toolTip() == QStringLiteral("#ff00ff")) {
              swatch->click();
              return;
            }
          }
        }
      }
    });
    pick.start(10);
    QTimer guard;
    connect(&guard, &QTimer::timeout, []() {
      if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
      }
    });
    guard.start(5000);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, layout.more.center().toPoint());
    QVERIFY(selected);
    QCOMPARE(bar->catEntries_[static_cast<size_t>(entry)].second, QColor(Qt::magenta));
    QCOMPARE(view->facetColorBars_[3]->catEntries_[static_cast<size_t>(entry)].second,
             QColor(Qt::magenta));
    QVERIFY(view->facetColorBars_[0]->catEntries_.empty());
  }
  void benchmarkFacetSetup_data() {
    QTest::addColumn<int>("files");
    QTest::addColumn<int>("side");
    QTest::newRow("one-file-16k-points") << 1 << 128;
    QTest::newRow("four-files-16k-points") << 4 << 128;
    QTest::newRow("eight-files-16k-points") << 8 << 128;
    QTest::newRow("four-files-263k-points") << 4 << 512;
  }

  void benchmarkFacetSetup() {
    if (qEnvironmentVariableIntValue("VV_RUN_BENCHMARKS") != 1) {
      QTest::qSkip("Set VV_RUN_BENCHMARKS=1 to run performance measurements.", __FILE__, __LINE__);
      return;
    }
    QFETCH(int, files);
    QFETCH(int, side);
    vtkNew<CountingWindow> window;
    window->SetSize(1200, 800);
    const auto load = testMeshes(files, side);
    ViewerOptions options;
    options.explodeView = true;
    QBENCHMARK {
      // Each opened file starts without cached VTK scalar ranges.
      for (const auto& mesh : load.meshes.meshes) {
        auto* data = mesh->GetPointData();
        for (int i = 0; i < data->GetNumberOfArrays(); ++i) {
          data->GetArray(i)->Modified();
        }
      }
      ViewerWindow view(load, options, window, nullptr);
    }
  }

  void benchmarkColumnInteraction() {
    if (qEnvironmentVariableIntValue("VV_RUN_BENCHMARKS") != 1) {
      QTest::qSkip("Set VV_RUN_BENCHMARKS=1 to run performance measurements.", __FILE__, __LINE__);
      return;
    }
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.explodeView = true;
    auto view = viewer(window, testMeshes(8), options);
    int step = 0;
    QBENCHMARK {
      const double lower = 100.0 + (step++ % 100);
      emit view->facetColorBars_.front()->clipRangeChanged(lower, lower + 500.0);
      QApplication::processEvents();
    }
  }
};

QTEST_MAIN(ViewerTests)
#include "ViewerTests.moc"
