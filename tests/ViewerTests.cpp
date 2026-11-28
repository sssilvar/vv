#include "ColorBarWidget.h"
#include "ColorPanel.h"
#include "FieldChip.h"
#include "FieldList.h"
#include "LayersPanel.h"
#include "PanelLabel.h"
#include "PlaybackBar.h"
#include "ProbeCard.h"
#include "RegionBorders.h"
#include "ScalarPager.h"
#include "SliceView.h"
#include "TemporalSource.h"
#include "TestMeshes.h"
#include "ToolRail.h"
#include "ViewerWindow.h"
#include "VolumeScene.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVTKOpenGLNativeWidget.h>
#include <QVariant>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <vtkActor.h>
#include <vtkActorCollection.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataSetReader.h>
#include <vtkDataSetWriter.h>
#include <vtkFloatArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkXMLPolyDataReader.h>

#if __has_include(<vtkHDFWriter.h>)
#include <vtkHDFWriter.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkPolyDataAlgorithm.h>
#include <vtkStreamingDemandDrivenPipeline.h>

class TemporalTestMesh : public vtkPolyDataAlgorithm {
public:
  static TemporalTestMesh* New();

protected:
  TemporalTestMesh() {
    SetNumberOfInputPorts(0);
  }

  int RequestInformation(vtkInformation*,
                         vtkInformationVector**,
                         vtkInformationVector* output) override {
    const double times[] = {0.0, 1.0};
    auto* info = output->GetInformationObject(0);
    info->Set(vtkStreamingDemandDrivenPipeline::TIME_STEPS(), times, 2);
    info->Set(vtkStreamingDemandDrivenPipeline::TIME_RANGE(), times, 2);
    return 1;
  }

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector* output) override {
    auto* info = output->GetInformationObject(0);
    const double time = info->Get(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP());
    const auto fixture = testMeshes(1);
    auto* mesh = vtkPolyData::GetData(output);
    mesh->ShallowCopy(fixture.meshes.meshes.front());
    auto* values = mesh->GetPointData()->GetArray("voltage");
    for (vtkIdType i = 0; i < values->GetNumberOfTuples(); ++i) {
      values->SetTuple1(i, values->GetTuple1(i) + 100.0 * time);
    }
    return 1;
  }
};
vtkStandardNewMacro(TemporalTestMesh);
#endif

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
    for (int i = target->GetPointData()->GetNumberOfArrays(); i-- > 0;) {
      const char* name = target->GetPointData()->GetArrayName(i);
      if (std::find(active_.begin(), active_.end(), name) == active_.end()) {
        target->GetPointData()->RemoveArray(i);
      }
    }
    return true;
  }
  bool sampledScalarRange(const ScalarField& field, double out[2], int) override {
    auto* values = source_->GetPointData()->GetArray(field.name.c_str());
    if (!values || std::find(active_.begin(), active_.end(), field.name) == active_.end()) {
      return false;
    }
    values->GetRange(out);
    if (field.name == "voltage") {
      out[1] += steps() - 1;
    }
    return true;
  }
  void setActiveArrays(const std::vector<std::string>& names) override {
    active_ = names;
  }

private:
  vtkSmartPointer<vtkPolyData> source_;
  std::vector<std::string> active_;
};

class ViewerTests : public QObject {
  Q_OBJECT

  static std::unique_ptr<ViewerWindow>
  viewer(CountingWindow* window, MeshLoadResult load, const ViewerOptions& options) {
    auto result =
        std::unique_ptr<ViewerWindow>(new ViewerWindow(std::move(load), options, window, nullptr));
    result->vtkWidget_->resize(1200, 800);
    window->SetSize(1200, 800);
    result->onViewportResize();
    result->renderer_.resetCamera();
    return result;
  }

  // The legends of the tiles showing the field of this name, in tile order.
  static std::vector<ColorBarWidget*> legendsOf(const ViewerWindow& view, const std::string& name) {
    std::vector<ColorBarWidget*> legends;
    const auto& tiles = view.renderer_.tiles();
    for (size_t tile = 0; tile < tiles.size(); ++tile) {
      if (tiles[tile].field && view.renderer_.fields()[*tiles[tile].field].field.name == name) {
        legends.push_back(view.legends_[tile]);
      }
    }
    return legends;
  }

  static size_t fieldNamed(const ViewerWindow& view, const std::string& name) {
    const auto& fields = view.renderer_.fields();
    const auto found = std::find_if(fields.begin(), fields.end(), [&name](const auto& field) {
      return field.field.name == name;
    });
    return static_cast<size_t>(std::distance(fields.begin(), found));
  }

  // The widget point showing world point (x, y, 0) in the first tile.
  static QPointF widgetPointAt(const ViewerWindow& view, double x, double y) {
    vtkRenderer* renderer = view.renderer_.getRenderer();
    if (!renderer) {
      qFatal("The viewer test needs a renderer in the first tile");
    }
    renderer->SetWorldPoint(x, y, 0.0, 1.0);
    renderer->WorldToDisplay();
    const double* display = renderer->GetDisplayPoint();
    const double scale = view.vtkWidget_->devicePixelRatioF();
    return {display[0] / scale, view.vtkWidget_->height() - display[1] / scale};
  }

  // A zero-filled voxel grid in voxel coordinates.
  static LoadedVolume volume(VolumeKind kind,
                             int scalarType,
                             const std::array<int, 3>& dims,
                             std::vector<double> labels) {
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(dims.data());
    image->AllocateScalars(scalarType, 1);
    image->GetPointData()->GetScalars()->Fill(0.0);
    return {
        {kind, image, vtkSmartPointer<vtkMatrix4x4>::New(), std::move(labels)}, "volume.nii", 0};
  }

  QTemporaryDir configHome_;

  static void paint(ViewerWindow& view) {
    view.strokeValue_ = 1;
    view.paintAtWidgetPos(widgetPointAt(view, 0.33, 0.27), 1);
  }

private slots:
  void initTestCase() {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    // Keeps the replace confirmation's setting out of the user's settings, which are
    // the registry or a plist rather than a file on Windows and macOS.
    QVERIFY(configHome_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configHome_.path());
  }

  // A label map on a grid shifted one voxel in x lands on the intensity grid by world
  // position; outlines keep only voxels whose in-plane neighbours differ.
  void volumeSceneResamplesLabelsOntoReference() {
    const LoadedVolume intensity = volume(VolumeKind::Intensity, VTK_FLOAT, {6, 6, 6}, {});
    const LoadedVolume labels = volume(VolumeKind::Labels, VTK_UNSIGNED_CHAR, {6, 6, 6}, {7.0});
    labels.volume.toWorld->SetElement(0, 3, 1.0);
    for (int z = 0; z < 6; ++z) {
      for (int y = 1; y < 5; ++y) {
        for (int x = 0; x < 4; ++x) {
          labels.volume.image->SetScalarComponentFromDouble(x, y, z, 0, 7.0);
        }
      }
    }

    VolumeScene scene({intensity, labels}, {{1.0, 0.0, 0.0}});
    QVERIFY(scene.intensity() == intensity.volume.image.GetPointer());
    QVERIFY(scene.hasLabels());

    vtkNew<vtkImageData> slice;
    const auto at = [&slice](int x, int y) {
      return static_cast<int>(slice->GetScalarComponentAsDouble(x, y, 2, 0));
    };
    scene.setOutline(false);
    scene.labelSlice(2, 2, slice);
    QCOMPARE(at(0, 2), 0);
    QCOMPARE(at(1, 2), 1);
    QCOMPARE(at(3, 2), 1);
    QCOMPARE(at(4, 2), 1);
    QCOMPARE(at(5, 2), 0);
    QCOMPARE(at(2, 0), 0);
    scene.setOutline(true);
    scene.labelSlice(2, 2, slice);
    QCOMPARE(at(1, 2), 1);
    QCOMPARE(at(2, 2), 0);
    QCOMPARE(at(2, 1), 1);

    QVERIFY(scene.setCursor({-5, 99, 2}));
    QCOMPARE(scene.cursor(), (std::array<int, 3>{0, 5, 2}));
    QVERIFY(!scene.setCursor({0, 5, 2}));
    QCOMPARE(scene.worldToVoxel({2.4, -3.0, 9.0}), (std::array<int, 3>{2, 0, 5}));
  }

  // At about two pixels a voxel, a click still picks the voxel under the pointer.
  void sliceViewClickPicksVoxelUnderPointer() {
    VolumeScene scene({volume(VolumeKind::Intensity, VTK_FLOAT, {4, 200, 4}, {})}, {});
    SliceView view(scene, SliceOrientation::Axial, nullptr);
    view.renderWindow()->SetSize(view.width(), view.height());
    vtkRenderer* renderer = view.renderWindow()->GetRenderers()->GetFirstRenderer();

    for (int y = 0; y < 200; ++y) {
      renderer->SetWorldPoint(2.0, y, scene.cursor()[2], 1.0);
      renderer->WorldToDisplay();
      const double* display = renderer->GetDisplayPoint();
      QTest::mouseClick(&view,
                        Qt::LeftButton,
                        Qt::NoModifier,
                        QPoint(static_cast<int>(std::lround(display[0])),
                               view.height() - 1 - static_cast<int>(std::lround(display[1]))));
      QCOMPARE(scene.cursor(), (std::array<int, 3>{2, y, 2}));
    }
  }

  // A focused slice view keeps the viewer's own keys.
  void sliceViewKeepsViewerKeys() {
    vtkNew<CountingWindow> window;
    MeshLoadResult load = testMeshes(1);
    load.volumes.push_back(volume(VolumeKind::Intensity, VTK_FLOAT, {4, 4, 4}, {}));
    auto view = viewer(window, std::move(load), {});
    QCOMPARE(view->sliceViews_.size(), size_t{3});
    const int before = view->renderer_.panes().front().first;

    QTest::keyClick(view->sliceViews_.front(), Qt::Key_Space);

    QVERIFY(view->renderer_.panes().front().first != before);
  }

  // Paint holds claim only bare keys while annotating: Ctrl+K stays the palette's.
  void paintHoldsLeaveShortcuts() {
    vtkNew<CountingWindow> window;
    auto view = viewer(window, testMeshes(1), {});
    const auto claimed = [&view](Qt::KeyboardModifiers modifiers) {
      QKeyEvent event(QEvent::ShortcutOverride, Qt::Key_K, modifiers);
      event.ignore();
      QApplication::sendEvent(view->vtkWidget_, &event);
      return event.isAccepted();
    };

    QVERIFY(!claimed(Qt::NoModifier));
    view->annotateAction_->setChecked(true);
    QVERIFY(claimed(Qt::NoModifier));
    QVERIFY(!claimed(Qt::ControlModifier));
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
    connect(&guard, &QTimer::timeout, &guard, []() {
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
    connect(&guard, &QTimer::timeout, &guard, []() {
      if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        dialog->reject();
      }
    });
    QVERIFY(view->saveAnnotations(ViewerWindow::SaveMode::NewFile));
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

  // Writes the first test mesh as a legacy file with a table on regions, its active
  // scalars, and opens it in a viewer showing regions.
  static std::unique_ptr<ViewerWindow> openLegacy(CountingWindow* window, const QString& path) {
    auto load = testMeshes(1);
    auto* points = load.meshes.meshes.front()->GetPointData();
    vtkNew<vtkLookupTable> table;
    table->SetNumberOfTableValues(20);
    table->Build();
    table->SetTableValue(1, 0.8, 0.1, 0.5, 1.0);
    points->GetArray("regions")->SetLookupTable(table);
    points->SetActiveScalars("regions");
    vtkNew<vtkDataSetWriter> writer;
    writer->SetFileName(path.toUtf8().constData());
    writer->SetInputData(load.meshes.meshes.front());
    if (writer->Write() == 0) {
      return nullptr;
    }
    load.meshes.names = {path.toStdString()};
    load.meshes.groups = {{path.toStdString(), {0}}};
    auto view = viewer(window, std::move(load), ViewerOptions{});
    const auto& fields = view->renderer_.panes().front().fields;
    const auto field = std::find(fields.begin(), fields.end(), fieldNamed(*view, "regions"));
    view->showField(0, static_cast<int>(std::distance(fields.begin(), field)));
    return view;
  }

  // Save replaces the opened file after a confirmation that "Don't ask again" skips,
  // keeping the file's table and leaving other files alone.
  void annotationSavesInPlace() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("mesh.vtk");
    QFile sibling(path + ".partial");
    QVERIFY(sibling.open(QIODevice::WriteOnly) && sibling.write("keep") == 4);
    sibling.close();
    vtkNew<CountingWindow> window;
    auto view = openLegacy(window, path);
    QVERIFY(view);
    view->annotateAction_->setChecked(true);
    QCOMPARE(view->inPlacePath(), QFileInfo(path).absoluteFilePath());
    int prompts = 0;
    const auto replace = [&prompts]() {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        ++prompts;
        box->checkBox()->setChecked(true);
        box->button(QMessageBox::Save)->click();
      }
    };

    // A "Save failed" warning would wait for a click; close it and report it instead.
    QStringList warnings;
    QTimer guard;
    connect(&guard, &QTimer::timeout, &guard, [&warnings]() {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box && box->icon() == QMessageBox::Warning) {
        warnings << box->text();
        box->accept();
      }
    });
    guard.start(100);

    paint(*view);
    QTimer::singleShot(0, view.get(), replace);
    QVERIFY2(view->saveAnnotations(ViewerWindow::SaveMode::InPlace),
             qPrintable(warnings.join('\n')));
    view->undoStroke();
    QVERIFY(view->saveAnnotations(ViewerWindow::SaveMode::InPlace));
    paint(*view);
    QVERIFY(view->saveAnnotations(ViewerWindow::SaveMode::InPlace));

    QCOMPARE(prompts, 1);
    QVERIFY(!view->isWindowModified());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden),
             QStringList({"mesh.vtk", "mesh.vtk.partial"}));
    QVERIFY(sibling.open(QIODevice::ReadOnly));
    QCOMPARE(sibling.readAll(), QByteArray("keep"));
    vtkNew<vtkDataSetReader> saved;
    saved->SetFileName(path.toUtf8().constData());
    saved->Update();
    auto* table = saved->GetOutput()->GetPointData()->GetScalars()->GetLookupTable();
    QVERIFY(table);
    double rgba[4];
    table->GetTableValue(1, rgba);
    QCOMPARE(rgba[0], 0.8);
    auto* labels = saved->GetOutput()->GetCellData()->GetArray("label");
    QVERIFY(labels);
    double range[2];
    labels->GetRange(range);
    QCOMPARE(range[1], 1.0);
  }

  // Filled-in swatches are only values the array stores exactly, filling them ends where
  // doubles no longer step by one, and gaps take precedence over the next value.
  void annotationSwatchesFitTheArray_data() {
    QTest::addColumn<int>("type");
    QTest::addColumn<QList<double>>("values");
    QTest::addColumn<QList<double>>("swatches");
    QTest::newRow("unsigned-char")
        << VTK_UNSIGNED_CHAR << QList<double>{254.0, 255.0} << QList<double>{254.0, 255.0};
    const double big = 9007199254740992.0; // 2^53
    QTest::newRow("beyond-double-steps")
        << VTK_DOUBLE << QList<double>{big, big + 2.0} << QList<double>{big, big + 2.0};
    QTest::newRow("gap-and-next") << VTK_INT << QList<double>{1.0, 3.0}
                                  << QList<double>{1.0, 2.0, 3.0, 4.0};
    QTest::newRow("from-zero") << VTK_INT << QList<double>{0.0, 2.0}
                               << QList<double>{0.0, 1.0, 2.0, 3.0};
    QTest::newRow("gap-fills-the-bar")
        << VTK_CHAR << QList<double>{1.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0}
        << QList<double>{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
  }

  void annotationSwatchesFitTheArray() {
    QFETCH(int, type);
    QFETCH(QList<double>, values);
    QFETCH(QList<double>, swatches);
    vtkNew<CountingWindow> window;
    auto load = testMeshes(1);
    auto& mesh = load.meshes.meshes.front();
    vtkSmartPointer<vtkDataArray> zones;
    zones.TakeReference(vtkDataArray::CreateDataArray(type));
    zones->SetName("zones");
    zones->SetNumberOfTuples(mesh->GetNumberOfPoints());
    QVERIFY(zones->GetNumberOfTuples() >= values.size());
    for (vtkIdType i = 0; i < zones->GetNumberOfTuples(); ++i) {
      zones->SetTuple1(i, values[static_cast<int>(i % values.size())]);
    }
    mesh->GetPointData()->AddArray(zones);
    auto view = viewer(window, std::move(load), ViewerOptions{});
    const auto& fields = view->renderer_.panes().front().fields;
    const auto field = std::find(fields.begin(), fields.end(), fieldNamed(*view, "zones"));
    QVERIFY(field != fields.end());
    view->showField(0, static_cast<int>(std::distance(fields.begin(), field)));

    view->annotateAction_->setChecked(true);

    const QList<double> offered(view->labelValues_.begin() + 1, view->labelValues_.end());
    QCOMPARE(offered, swatches);
    const bool shortValues = std::all_of(swatches.begin(), swatches.end(), [](double v) {
      return QString::number(v, 'g', 15).size() <= 2;
    });
    QStringList texts;
    for (QAbstractButton* button : view->annotationBar_->findChildren<QAbstractButton*>()) {
      if (button->isVisibleTo(view->annotationBar_) &&
          button->accessibleName().startsWith(QStringLiteral("Label "))) {
        texts << button->text();
      }
    }
    QStringList expected;
    for (int i = 0; i < swatches.size(); ++i) {
      expected << (shortValues ? QString::number(swatches[i]) : QString::number(i + 1));
    }
    QCOMPARE(texts, expected);
  }

#ifdef Q_OS_UNIX
  // A save that cannot write keeps the original file and the unsaved state. Unix only:
  // a read-only directory on Windows still takes new files.
  void annotationFailedSaveKeepsTheFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("mesh.vtk");
    vtkNew<CountingWindow> window;
    auto view = openLegacy(window, path);
    QVERIFY(view);
    view->annotateAction_->setChecked(true);
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray bytes = original.readAll();
    original.close();
    QSettings(QSettings::defaultFormat(), QSettings::UserScope, "vv", "vv")
        .setValue("annotation/confirmReplace", false);
    QVERIFY(QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::ExeOwner));
    if (QFileInfo(directory.path()).isWritable()) {
      // Root writes anyway; nothing to check.
      QFile::setPermissions(directory.path(),
                            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
      return;
    }
    QTimer guard;
    connect(&guard, &QTimer::timeout, &guard, []() {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        box->accept();
      }
    });
    guard.start(50);

    paint(*view);
    view->endStroke();
    const bool saved = view->saveAnnotations(ViewerWindow::SaveMode::InPlace);

    QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSettings(QSettings::defaultFormat(), QSettings::UserScope, "vv", "vv")
        .remove("annotation/confirmReplace");
    QVERIFY(!saved);
    QVERIFY(view->isWindowModified());
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
  }
#endif

  // The brush preview lies on the mesh; only an offset of its own beyond the mesh's keeps
  // the two from z-fighting.
  void brushPreviewDrawsOverTheMesh() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.annotate = true;
    auto view = viewer(window, testMeshes(1), options);

    view->updateBrushPreview(widgetPointAt(*view, 0.33, 0.27));

    QVERIFY(view->previewActor_ && view->previewActor_->GetVisibility());
    double factor = 0.0;
    double units = 0.0;
    view->previewActor_->GetMapper()->GetRelativeCoincidentTopologyPolygonOffsetParameters(factor,
                                                                                           units);
    QVERIFY(factor < 0.0 && units < 0.0);
  }

  // Compare puts the painted view on the left and maps of the file's other fields on the
  // right, each with its legend and chip. Only the painted view takes strokes; the maps
  // outline what is painted.
  void annotationComparesMaps() {
    vtkNew<CountingWindow> window;
    auto view = viewer(window, testMeshes(1), ViewerOptions{});
    view->annotateAction_->setChecked(true);
    const std::vector<Tile>& tiles = view->renderer_.tiles();
    // The first map shows the field shown before, then the next one.
    const Pane& pane = view->renderer_.panes().front();
    const size_t shown = pane.fields[static_cast<size_t>(pane.first)];
    const size_t other = pane.fields[pane.first == 0 ? 1 : 0];

    // The file has two fields besides the painted label array, so two maps at most.
    view->setCompareMaps(3);
    view->panesChanged();
    QCOMPARE(view->compareCount_, size_t(2));
    QVERIFY(!tiles[0].reference && tiles[1].reference && tiles[2].reference);
    QVERIFY(tiles[1].field == shown && tiles[2].field == other);
    QVERIFY(view->legends_[0]->isHidden());
    QVERIFY(!view->legends_[1]->isHidden() && !view->legends_[2]->isHidden());
    QVERIFY(!view->compareChips_[0]->isHidden() && view->compareChips_[2]->isHidden());
    QCOMPARE(view->compareChips_[0]->text(),
             QString::fromStdString(view->renderer_.fields()[shown].field.name));

    // The brush preview shows in the maps as in the painted view.
    view->updateBrushPreview(widgetPointAt(*view, 0.33, 0.27));
    QVERIFY(view->previewActor_->GetVisibility());
    QVERIFY(tiles[0].renderer->HasViewProp(view->previewActor_));
    QVERIFY(tiles[1].renderer->HasViewProp(view->previewActor_));

    // The same mesh point, in the top map: no stroke lands there.
    vtkRenderer* map = tiles[1].renderer;
    map->SetWorldPoint(0.33, 0.27, 0.0, 1.0);
    map->WorldToDisplay();
    const double* display = map->GetDisplayPoint();
    const double scale = view->vtkWidget_->devicePixelRatioF();
    view->strokeValue_ = 1;
    view->paintAtWidgetPos({display[0] / scale, view->vtkWidget_->height() - display[1] / scale},
                           1);
    view->endStroke();
    QVERIFY(view->undoStack_.empty());
    QCOMPARE(view->regionBorders_->lines()->GetNumberOfLines(), vtkIdType(0));
    paint(*view);
    view->endStroke();
    QCOMPARE(view->undoStack_.size(), size_t(1));
    QVERIFY(view->regionBorders_->lines()->GetNumberOfLines() > 0);
    view->undoStroke();
    QCOMPARE(view->regionBorders_->lines()->GetNumberOfLines(), vtkIdType(0));

    // A field another map shows trades places, and back.
    view->openCompareList(1);
    QVERIFY(!view->fieldList_->isHidden());
    const std::vector<size_t> rows = view->compareCandidates();
    emit view->fieldList_->pageChosen(
        static_cast<int>(std::find(rows.begin(), rows.end(), shown) - rows.begin()));
    QVERIFY(tiles[1].field == other && tiles[2].field == shown);
    view->openCompareList(0);
    emit view->fieldList_->pageChosen(
        static_cast<int>(std::find(rows.begin(), rows.end(), shown) - rows.begin()));
    QVERIFY(tiles[1].field == shown && tiles[2].field == other);

    // T steps through the counts.
    const auto compareKey = std::find_if(
        view->annotateKeys_.begin(), view->annotateKeys_.end(), [](const QAction* action) {
          return action->shortcut() == QKeySequence(Qt::Key_T);
        });
    QVERIFY(compareKey != view->annotateKeys_.end() && !view->splitAction_->isEnabled());
    (*compareKey)->trigger();
    QCOMPARE(view->compareCount_, size_t(0));
    QVERIFY(!tiles[1].shown && view->compareChips_[0]->isHidden());
    (*compareKey)->trigger();
    QCOMPARE(view->compareCount_, size_t(1));
    QVERIFY(tiles[1].shown && tiles[1].field == shown);

    // Leaving shows the field alone again and keeps the maps for the next entry.
    view->annotateAction_->setChecked(false);
    QVERIFY(!tiles[1].shown && view->compareChips_[0]->isHidden());
    QVERIFY(!view->legends_[0]->isHidden() && !(*compareKey)->isEnabled());
    view->annotateAction_->setChecked(true);
    QVERIFY(tiles[1].shown && tiles[1].field == shown);
  }

  // A shown categorical field is fixed in place, point by point, its categories on the
  // swatches in order: any values, NaN left unlabeled and written back by Erase.
  void annotationFixesCategoricalField() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(1);
    auto& mesh = load.meshes.meshes.front();
    vtkNew<vtkFloatArray> sources;
    sources->SetName("sources");
    sources->SetNumberOfTuples(mesh->GetNumberOfPoints());
    for (vtkIdType i = 0; i < sources->GetNumberOfTuples(); ++i) {
      sources->SetValue(i,
                        i % 4 == 0 ? std::numeric_limits<float>::quiet_NaN()
                                   : 100.0F + static_cast<float>(i % 3));
    }
    mesh->GetPointData()->AddArray(sources);
    auto view = viewer(window, std::move(load), ViewerOptions{});
    const auto& fields = view->renderer_.panes().front().fields;
    const auto field = std::find(fields.begin(), fields.end(), fieldNamed(*view, "sources"));
    QVERIFY(field != fields.end());
    view->showField(0, static_cast<int>(std::distance(fields.begin(), field)));
    view->annotateAction_->setChecked(true);
    // 100 to 102 and the next value, for a category this file lacks.
    QCOMPARE(view->labelValues_.size(), size_t(5));
    QCOMPARE(view->labelValues_.back(), 103.0);

    view->paintAtWidgetPos(widgetPointAt(*view, 0.33, 0.27), 3);
    view->endStroke();

    auto* data = view->renderer_.getPrimaryMesh();
    QVERIFY(!data->GetCellData()->GetArray("label"));
    auto* edited = data->GetPointData()->GetArray("sources");
    vtkIdType painted = 0;
    for (vtkIdType i = 0; i < edited->GetNumberOfTuples(); ++i) {
      painted += edited->GetTuple1(i) == 102.0 && i % 3 != 2 ? 1 : 0;
    }
    QVERIFY(painted > 0);

    view->paintAtWidgetPos(widgetPointAt(*view, 0.33, 0.27), 0);
    view->endStroke();
    painted = 0;
    for (vtkIdType i = 0; i < edited->GetNumberOfTuples(); ++i) {
      painted += edited->GetTuple1(i) == 102.0 && i % 3 != 2 ? 1 : 0;
    }
    QCOMPARE(painted, vtkIdType(0));
  }

  void hiddenPinVisibility() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.temporal = std::make_shared<TestTemporalSource>(load.meshes.meshes.front());
    auto view = viewer(window, std::move(load), ViewerOptions{});
    view->pinProbe(widgetPointAt(*view, 0.33, 0.27));
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
    view->showField(0, 1);
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
    const auto& tiles = view->renderer_.tiles();
    vtkActor* other =
        view->renderer_.context.actors[tiles[view->renderer_.panes()[1].firstTile].firstActor];
    QVERIFY(!other->GetVisibility());
    const int beforeAll = window->renders;
    QTest::mouseClick(eye, Qt::LeftButton, Qt::AltModifier);
    QCOMPARE(window->renders - beforeAll, 1);
    QVERIFY(other->GetVisibility());
  }

  void matchingProbeViews_data() {
    QTest::addColumn<bool>("split");
    QTest::newRow("files") << false;
    QTest::newRow("fields-and-files") << true;
  }

  void matchingProbeViews() {
    QFETCH(bool, split);
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
    options.mode = split ? PaneMode::Split : PaneMode::Single;
    auto view = viewer(window, std::move(load), options);
    // Every file shows voltage, or a tile for each of its two fields.
    view->showField(0, 0);
    const double scale = view->vtkWidget_->devicePixelRatioF();
    const QPointF point = widgetPointAt(*view, 0.331, 0.271);
    const int beforeHover = window->renders;
    view->requestProbe(point);
    view->showProbe();
    QCOMPARE(window->renders, beforeHover);

    const size_t expected = split ? 4 : 2;
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
    const int beforeVisibility = window->renders;
    QVERIFY(view->renderer_.setPartsVisible({{1, false}}));
    QCOMPARE(window->renders - beforeVisibility, 1);
    view->showProbe();
    const auto hidden = view->renderer_.matchingProbes(*view->pin_);
    QCOMPARE(hidden.size(), expected / 2);
    QVERIFY(std::none_of(
        hidden.begin(), hidden.end(), [](const auto& match) { return match.mesh == 1; }));
    QVERIFY(view->renderer_.setPartsVisible({{1, true}}));
    view->hideProbe();
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(card->isHidden());
    }
    view->showProbe();
    for (ProbeCard* card : view->probeCards_) {
      QVERIFY(!card->isHidden());
    }
    // Picking another file's categorical tile keeps that tile as the pin's source.
    if (split) {
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
    QTest::addColumn<bool>("split");
    QTest::newRow("single") << false;
    QTest::newRow("split") << true;
  }

  void unpinInteractions() {
    QFETCH(bool, split);
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.mode = split ? PaneMode::Split : PaneMode::Single;
    auto view = viewer(window, testMeshes(1), options);
    view->probeAction_->setChecked(true);
    const QPointF point = widgetPointAt(*view, 0.331, 0.271);
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
    // Files with the same fields are linked: the comparison follows to voltage.
    QVERIFY(view->renderer_.fieldsLinked());
    const auto& fields = view->renderer_.panes().front().fields;
    const auto field = std::find(fields.begin(), fields.end(), fieldNamed(*view, "voltage"));
    QVERIFY(field != fields.end());
    view->showField(0, static_cast<int>(std::distance(fields.begin(), field)));
    QCOMPARE(legendsOf(*view, "voltage").size(), size_t(2));
    view->pinProbe(widgetPointAt(*view, 0.331, 0.271));
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

  void pinnedPanePresentation() {
    vtkNew<CountingWindow> window;
    auto view = viewer(window, testMeshes(2), ViewerOptions{});
    view->pinProbe(widgetPointAt(*view, 0.331, 0.271));
    QVERIFY(view->pin_);

    const auto visibleMarkers = [&window]() {
      int count = 0;
      auto* renderers = window->GetRenderers();
      renderers->InitTraversal();
      while (auto* tile = renderers->GetNextItem()) {
        if (!tile->GetDraw()) {
          continue;
        }
        auto* actors = tile->GetActors();
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
    // Both files step from regions to voltage, a continuous field without a swatch.
    view->stepPane(0, 1);
    QVERIFY(view->pin_);
    QTRY_COMPARE(visibleMarkers(), 2);
    QTRY_VERIFY(!view->probeCards_.front()->isHidden());
    auto* swatch = view->probeCards_.front()->findChild<QLabel*>(QStringLiteral("probeSwatch"));
    QVERIFY(swatch);
    QTRY_VERIFY(swatch->isHidden());
    view->stepPane(0, -1);
    QTRY_VERIFY(!swatch->isHidden());

    const auto matches = view->renderer_.matchingProbes(*view->pin_);
    QCOMPARE(matches.size(), size_t(2));
    QCOMPARE(matches.front().field, std::string("regions"));
    const int category = static_cast<int>(matches.front().value);
    const QColor color(Qt::magenta);
    const int before = window->renders;
    emit legendsOf(*view, "regions").back()->categoryColorChanged(category, color);
    QCOMPARE(window->renders - before, 1);
    QTRY_VERIFY(swatch->styleSheet().contains(color.name()));
    for (auto* card : view->probeCards_) {
      auto* other = card->findChild<QLabel*>(QStringLiteral("probeSwatch"));
      QVERIFY(other);
      QVERIFY(other->styleSheet().contains(color.name()));
    }
    // The pin follows its mesh into the tiles of a split pane and back.
    view->setPaneSplit(0, true);
    QTRY_COMPARE(visibleMarkers(), 3);
    view->setPaneSplit(0, false);
    QTRY_COMPARE(visibleMarkers(), 2);
  }

  // Two files with different fields: each pane pages its own, in its own mode.
  void panesShowTheirOwnFields() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.meshes.meshes[0]->GetPointData()->RemoveArray("regions");
    vtkNew<vtkFloatArray> dispersion;
    dispersion->SetName("dispersion");
    dispersion->DeepCopy(load.meshes.meshes[1]->GetPointData()->GetArray("voltage"));
    dispersion->SetName("dispersion");
    load.meshes.meshes[1]->GetPointData()->AddArray(dispersion);
    auto view = viewer(window, std::move(load), ViewerOptions{});
    const auto& panes = view->renderer_.panes();
    const auto title = [&view](size_t pane) {
      return view->legends_[view->renderer_.panes()[pane].firstTile]->title_;
    };

    QVERIFY(!view->renderer_.fieldsLinked());
    QVERIFY(view->linkAction_ && !view->linkAction_->isChecked());
    QCOMPARE(title(0), QStringLiteral("voltage"));
    QCOMPARE(title(1), QStringLiteral("dispersion"));
    QVERIFY(view->paneUi_[0].pager != view->paneUi_[1].pager);
    for (const auto& ui : view->paneUi_) {
      QVERIFY(ui.label && ui.pager);
      QVERIFY(view->vtkWidget_->rect().contains(ui.pager->geometry()));
    }
    QVERIFY(view->paneUi_[0].pager->geometry().right() < view->paneUi_[1].pager->geometry().left());

    // Stepping the right pane leaves the left one alone and makes the right one active.
    view->stepPane(1, 1);
    QCOMPARE(title(0), QStringLiteral("voltage"));
    QCOMPARE(title(1), QStringLiteral("regions"));
    QCOMPARE(view->activePane_, size_t(1));
    QVERIFY(view->paneUi_[1].pager->property("active").toBool());
    QVERIFY(!view->paneUi_[0].pager->property("active").toBool());
    QVERIFY(view->colorAction_->isEnabled());
    QCOMPARE(view->colorField(), std::optional<size_t>(fieldNamed(*view, "regions")));

    // A right click on the left mesh offers its view; splitting it leaves the right pane single.
    const QRect left = view->paneUi_[0].label->geometry();
    QTest::mouseClick(view->vtkWidget_, Qt::RightButton, Qt::NoModifier, left.center());
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(menu);
    QVERIFY(menu->actions().contains(view->splitAction_));
    QVERIFY(menu->actions().contains(view->linkAction_));
    QCOMPARE(view->activePane_, size_t(0));
    QVERIFY(!view->splitAction_->isChecked());
    menu->close();
    view->splitAction_->trigger();
    QCOMPARE(panes[0].mode, PaneMode::Split);
    QCOMPARE(panes[1].mode, PaneMode::Single);
    QVERIFY(view->splitAction_->isChecked());
    QVERIFY(!view->colorAction_->isEnabled());
    QCOMPARE(title(1), QStringLiteral("regions"));
    // The split pane's tiles end above its pager; the single pane's legend stays in its pane.
    const QRect pager = view->paneUi_[0].pager->geometry();
    const QRect legend = view->legends_[panes[0].firstTile]->geometry();
    QVERIFY(left.contains(pager));
    QVERIFY(legend.bottom() < pager.top());
    QVERIFY(view->paneUi_[1].label->geometry().contains(
        view->legends_[panes[1].firstTile]->geometry()));

    // Back on the right pane, the same key acts on it.
    view->setActivePane(1);
    QVERIFY(!view->splitAction_->isChecked());
    QVERIFY(view->colorAction_->isEnabled());
    emit view->paneUi_[0].pager->splitRequested(false);
    QCOMPARE(panes[0].mode, PaneMode::Single);
    QCOMPARE(view->activePane_, size_t(0));

    // Linked, a field both files have shows in both.
    view->linkAction_->setChecked(true);
    const auto& fields = panes[1].fields;
    const auto voltage = std::find(fields.begin(), fields.end(), fieldNamed(*view, "voltage"));
    view->showField(1, static_cast<int>(std::distance(fields.begin(), voltage)));
    QCOMPARE(title(0), QStringLiteral("voltage"));
    QCOMPARE(title(1), QStringLiteral("voltage"));
  }

  void narrowPanePagersStayInsideTheirPanes() {
    vtkNew<CountingWindow> window;
    auto view = viewer(window, testMeshes(2), ViewerOptions{});
    view->resize(600, 480);
    view->show();
    QTRY_COMPARE(view->vtkWidget_->size(), QSize(600, 480));
    window->SetSize(600, 480);
    view->onViewportResize();

    for (const auto& ui : view->paneUi_) {
      QVERIFY(!ui.pager->isHidden());
      QVERIFY(ui.label->geometry().contains(ui.pager->geometry()));
      QVERIFY(!view->rail_->geometry().intersects(ui.pager->geometry()));
      const auto buttons =
          ui.pager->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
      QCOMPARE(buttons.size(), 4);
      for (const auto* button : buttons) {
        QTRY_VERIFY(ui.pager->rect().contains(button->geometry()));
      }
    }
    auto* rightPager = view->paneUi_[1].pager;
    const auto buttons =
        rightPager->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
    const auto split = std::find_if(buttons.begin(), buttons.end(), [](const QToolButton* button) {
      return button->accessibleName() == QStringLiteral("Split view");
    });
    QVERIFY(split != buttons.end());
    QTest::mouseClick(*split, Qt::LeftButton);
    QCOMPARE(view->renderer_.panes()[1].mode, PaneMode::Split);
    QCOMPARE(view->renderer_.panes()[0].mode, PaneMode::Single);
  }

  // A time series beside a static file: the bar spans the window under both panes, each
  // pane keeps its own pager, and frames bring the arrays the series' tiles show.
  void playbackBesideStaticPane() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.meshes.meshes[1]->GetPointData()->RemoveArray("voltage");
    load.temporal = std::make_shared<TestTemporalSource>(load.meshes.meshes.front());
    auto view = viewer(window, std::move(load), ViewerOptions{});

    QVERIFY(view->playbackBar_);
    QVERIFY(view->playbackBar_->pager()->parentWidget()->isHidden());
    for (const auto& ui : view->paneUi_) {
      QVERIFY(ui.pager && ui.pager->parentWidget() == view->vtkWidget_);
      QVERIFY(ui.pager->geometry().bottom() < view->playbackBar_->geometry().top());
    }
    QCOMPARE(view->streamed_, std::vector<std::string>{"regions"});
    view->showField(0, 1);
    QCOMPARE(view->streamed_, std::vector<std::string>{"voltage"});
    QVERIFY(view->renderer_.context.actors.front()->GetMapper()->GetScalarVisibility());
    // The series' range covers its sampled frames; the static file's field keeps its own.
    QCOMPARE(view->renderer_.fields()[fieldNamed(*view, "voltage")].globalRange[1], 962.0);
    view->setPaneSplit(0, true);
    QCOMPARE(view->streamed_, (std::vector<std::string>{"regions", "voltage"}));
    view->stepPane(1, 1);
    QCOMPARE(view->streamed_, (std::vector<std::string>{"regions", "voltage"}));

    // A pin on the series traces the field of its tile.
    view->setPaneSplit(0, false);
    view->pinProbe(widgetPointAt(*view, 0.331, 0.271));
    QVERIFY(view->pin_);
    QCOMPARE(view->traced_, std::optional<size_t>(fieldNamed(*view, "regions")));
    view->showField(0, 1);
    QCOMPARE(view->traced_, std::optional<size_t>(fieldNamed(*view, "voltage")));
    view->showField(0, -1);
    QVERIFY(!view->traced_);
  }

  void playbackRangeIncludesStaticData() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    auto* values = load.meshes.meshes[1]->GetPointData()->GetArray("voltage");
    values->SetTuple1(0, -5.0);
    values->Modified();
    load.temporal = std::make_shared<TestTemporalSource>(load.meshes.meshes.front());
    auto view = viewer(window, std::move(load), ViewerOptions{});
    view->showField(0, 1);

    const auto& field = view->renderer_.fields()[fieldNamed(*view, "voltage")];
    QCOMPARE(field.globalRange[0], -5.0);
    QCOMPARE(field.globalRange[1], 962.0);
  }

  void vtkhdfFieldSwitchAtCurrentStep() {
#if __has_include(<vtkHDFWriter.h>)
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = QFile::encodeName(dir.filePath(QStringLiteral("stimulation.vtkhdf")));
    vtkNew<TemporalTestMesh> data;
    vtkNew<vtkHDFWriter> writer;
    writer->SetFileName(path.constData());
    writer->SetInputConnection(data->GetOutputPort());
    writer->SetWriteAllTimeSteps(true);
    QVERIFY(writer->Write());
    auto load = loadMeshes({path.toStdString()});
    QVERIFY(load.ok);
    QVERIFY(load.temporal && load.temporal->playable());
    vtkNew<CountingWindow> window;
    auto view = viewer(window, std::move(load), ViewerOptions{});
    view->showFrame(1);

    for (const int position : {1, 0, 1}) {
      view->showField(0, position);
      auto* mapper = view->renderer_.context.actors.front()->GetMapper();
      QVERIFY(mapper->GetScalarVisibility());
      const char* name = position == 1 ? "voltage" : "regions";
      QCOMPARE(QString::fromUtf8(mapper->GetArrayName()), QString::fromLatin1(name));
      auto* values = view->load_.meshes.meshes.front()->GetPointData()->GetArray(name);
      QVERIFY(values);
      QCOMPARE(values->GetTuple1(0), position == 1 ? 100.0 : 0.0);
    }
    const auto& field = view->renderer_.fields()[fieldNamed(*view, "voltage")];
    QCOMPARE(field.globalRange[1], 1060.0);
#else
    QSKIP("The installed VTK has no VTKHDF writer.");
#endif
  }

  void colorPanelTypedValuesKeepPrecision() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.fixedRange = std::array<double, 2>{0.003, 999.875};
    auto load = testMeshes(1);
    load.meshes.meshes.front()->GetPointData()->RemoveArray("regions");
    auto view = viewer(window, std::move(load), options);
    view->updateColorRange();
    ColorBarWidget* legend = view->legends_.front();
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

    QCOMPARE(legend->clipMin_, 12.25);
    QCOMPARE(legend->valueText(legend->clipMin_), QStringLiteral("12.25"));
    QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("999.875"));

    const auto buttons = view->colorPanel_->findChildren<QToolButton*>();
    const auto reset = std::find_if(buttons.cbegin(), buttons.cend(), [](const auto* button) {
      return button->text() == QStringLiteral("Use data range");
    });
    QVERIFY(reset != buttons.cend());
    (*reset)->click();

    QCOMPARE(legend->clipMin_, 0.003);
    QCOMPARE(legend->valueText(legend->clipMin_), QStringLiteral("0.003"));
    QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("1000"));

    const int initialWidth = legend->width();
    enter(min, QStringLiteral("0.12345678901234567"));
    enter(max, QStringLiteral("876.5432109876543"));
    QVERIFY(legend->width() > initialWidth);
    QCOMPARE(legend->size(), legend->sizeHint());
    QVERIFY(view->vtkWidget_->rect().contains(legend->geometry()));
    for (const QRectF& value : legend->clipValueRects()) {
      QVERIFY(!value.isEmpty());
      QVERIFY(QRectF(legend->rect()).contains(value));
    }

    enter(max, QStringLiteral("99999"));
    QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("1000"));
    QCOMPARE(legend->width(), initialWidth);
  }

  void categoricalNaNColors_data() {
    QTest::addColumn<bool>("split");
    QTest::addColumn<bool>("cells");
    QTest::addColumn<bool>("common");
    for (const bool split : {false, true}) {
      for (const bool cells : {false, true}) {
        for (const bool common : {false, true}) {
          const auto name = QStringLiteral("%1-%2-%3").arg(split).arg(cells).arg(common);
          QTest::newRow(qPrintable(name)) << split << cells << common;
        }
      }
    }
  }

  void categoricalNaNColors() {
    QFETCH(bool, split);
    QFETCH(bool, cells);
    QFETCH(bool, common);
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2, 3);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 0; i < load.meshes.meshes.size(); ++i) {
      auto* mesh = load.meshes.meshes[i].Get();
      mesh->GetPointData()->Initialize();
      auto* attributes = cells ? static_cast<vtkDataSetAttributes*>(mesh->GetCellData())
                               : static_cast<vtkDataSetAttributes*>(mesh->GetPointData());
      const auto count = cells ? mesh->GetNumberOfCells() : mesh->GetNumberOfPoints();
      for (const char* name : {"af_sources", "finite"}) {
        vtkNew<vtkFloatArray> field;
        field->SetName(name);
        for (vtkIdType j = 0; j < count; ++j) {
          field->InsertNextValue(static_cast<float>(j % 2));
        }
        if (i == 1 && std::string_view(name) == "af_sources") {
          field->SetComponent(count - 1, 0, nan);
        }
        attributes->AddArray(field);
      }
    }
    ViewerOptions options;
    options.mode = split ? PaneMode::Split : PaneMode::Single;
    options.commonCatLut = common;
    auto view = viewer(window, std::move(load), options);
    // Both files show af_sources, beside a tile for finite when split.
    const std::vector<ColorBarWidget*> bars = legendsOf(*view, "af_sources");
    QCOMPARE(bars.size(), size_t(2));
    QVERIFY(view->renderer_.fields()[fieldNamed(*view, "af_sources")].analysis.hasNaN);
    QVERIFY(!view->renderer_.fields()[fieldNamed(*view, "finite")].analysis.hasNaN);
    const std::vector<ColorBarWidget*> finite = legendsOf(*view, "finite");
    QCOMPARE(finite.size(), split ? size_t(2) : size_t(0));
    for (auto* bar : finite) {
      QCOMPARE(bar->catEntries_.size(), size_t(2));
    }
    for (auto* bar : bars) {
      QVERIFY(bar);
      QCOMPARE(bar->catEntries_.size(), size_t(3));
      QCOMPARE(bar->catEntries_.back().first, QStringLiteral("NaN / No value"));
      QVERIFY(bar->subtitle_.contains(QStringLiteral("2 categories")));
    }
    const auto original = bars.front()->catEntries_;

    int before = window->renders;
    emit bars.front()->categoryColorChanged(2, QColor(Qt::magenta));
    QCOMPARE(window->renders - before, 1);
    for (auto* bar : bars) {
      QCOMPARE(bar->catEntries_[0], original[0]);
      QCOMPARE(bar->catEntries_[1], original[1]);
      QCOMPARE(bar->catEntries_[2].second, QColor(Qt::magenta));
    }
    if (!split) {
      QAbstractButton* missing = nullptr;
      for (auto* button : view->colorPanel_->findChildren<QAbstractButton*>()) {
        if (button->accessibleName() == QStringLiteral("Color of NaN / No value")) {
          missing = button;
          break;
        }
      }
      QVERIFY(missing);
      QCOMPARE(missing->accessibleName(), QStringLiteral("Color of NaN / No value"));
      view->showField(0, 1);
      QCOMPARE(bars.front()->catEntries_.size(), size_t(2));
      view->showField(0, 0);
      QCOMPARE(bars.front()->catEntries_.back().second, QColor(Qt::magenta));
      before = window->renders;
      emit view->colorPanel_->categoryColorChanged(2, QColor(Qt::cyan));
      QCOMPARE(window->renders - before, 1);
      QCOMPARE(bars.front()->catEntries_.back().second, QColor(Qt::cyan));
    }
    emit bars.front()->paletteChosen(QStringLiteral("okabe-ito"));
    const auto expected = split ? QColor(Qt::magenta) : QColor(Qt::cyan);
    for (auto* bar : bars) {
      QCOMPARE(bar->catEntries_.back().second, expected);
    }

    emit bars.front()->categoryColorChanged(2, std::nullopt);
    for (auto* bar : bars) {
      QCOMPARE(bar->catEntries_.back().second, original.back().second);
    }
  }

  // A field's range and colors change in every pane that shows it.
  void sharedFieldUpdates() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.mode = PaneMode::Split;
    auto load = testMeshes(3);
    for (auto& mesh : load.meshes.meshes) {
      auto* values = vtkFloatArray::SafeDownCast(mesh->GetPointData()->GetArray("voltage"));
      QVERIFY(values);
      values->SetValue(0, 0.006F);
      values->Modified();
    }
    auto view = viewer(window, std::move(load), options);
    const std::vector<ColorBarWidget*> voltage = legendsOf(*view, "voltage");
    const std::vector<ColorBarWidget*> regions = legendsOf(*view, "regions");
    QCOMPARE(voltage.size(), size_t(3));
    QCOMPARE(regions.size(), size_t(3));
    ColorBarWidget* bar = voltage.front();
    int before = window->renders;
    emit bar->clipRangeChanged(100.0, 500.0);
    QCOMPARE(window->renders - before, 1);
    const SceneField& field = view->renderer_.fields()[fieldNamed(*view, "voltage")];
    QCOMPARE(field.clipRange[0], 100.0);
    QCOMPARE(field.clipRange[1], 500.0);
    for (auto* legend : voltage) {
      QCOMPARE(legend->clipMin_, 100.0);
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
      // Not keyClick: its QPointer guard trips a clang-analyzer 18 false use-after-free.
      // The editor outlives Return, so press and release are equivalent here.
      QTest::keyPress(editor, Qt::Key_Return);
      QTest::keyRelease(editor, Qt::Key_Return);
      return true;
    };
    QVERIFY(enter(0, QStringLiteral("100.1234567890123")));
    QVERIFY(enter(1, QStringLiteral("500.1234567890123")));

    for (auto* legend : voltage) {
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
    view->layoutPanes();
    before = window->renders;
    emit bar->colormapChosen(QStringLiteral("viridis"), true);
    QCOMPARE(window->renders - before, 1);
    for (auto* legend : voltage) {
      QCOMPARE(legend->clipMin_, static_cast<double>(0.006F));
      QCOMPARE(legend->valueText(legend->clipMin_), QStringLiteral("0.006"));
      QCOMPARE(legend->valueText(legend->clipMax_), QStringLiteral("500.1234567890123"));
    }
    before = window->renders;
    emit regions.front()->paletteChosen(QStringLiteral("okabe-ito"));
    QCOMPARE(window->renders - before, 1);
    QCOMPARE(view->renderer_.fields()[fieldNamed(*view, "regions")].analysis.coloring.palette,
             findCategoryPalette("okabe-ito"));
    before = window->renders;
    emit regions.front()->categoryColorChanged(19, QColor(Qt::magenta));
    QCOMPARE(window->renders - before, 1);
    for (auto* legend : regions) {
      QCOMPARE(legend->catEntries_[19].second, QColor(Qt::magenta));
    }
    QVERIFY(!view->renderer_.setPartsVisible({{0, false}, {999, false}}));
    QVERIFY(view->renderer_.context.actors.front()->GetVisibility());
  }

  // A file without a field has no tile for it, and is not linked to the one that has.
  void fileWithoutAField() {
    vtkNew<CountingWindow> window;
    auto load = testMeshes(2);
    load.meshes.meshes.front()->GetPointData()->RemoveArray("voltage");
    ViewerOptions options;
    options.mode = PaneMode::Split;
    auto view = viewer(window, std::move(load), options);
    const size_t voltage = fieldNamed(*view, "voltage");
    const auto& panes = view->renderer_.panes();
    QCOMPARE(panes[0].fields.size(), size_t(1));
    QCOMPARE(panes[1].fields.size(), size_t(2));
    QVERIFY(!view->renderer_.fieldsLinked());
    const std::vector<ColorBarWidget*> legends = legendsOf(*view, "voltage");
    QCOMPARE(legends.size(), size_t(1));
    const int before = window->renders;
    emit legends.front()->colormapChosen(QStringLiteral("hue wheel"), false);
    QCOMPARE(window->renders - before, 1);
    QCOMPARE(view->renderer_.fields()[voltage].analysis.colormap.map, findColormap("hue wheel"));
    QVERIFY(legends.front()->subtitle_.contains(QStringLiteral("cyclic")));
  }

  void overflowCategoryEditing() {
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.mode = PaneMode::Split;
    auto view = viewer(window, testMeshes(2), options);
    const std::vector<ColorBarWidget*> regions = legendsOf(*view, "regions");
    QCOMPARE(regions.size(), size_t(2));
    ColorBarWidget* bar = regions.front();
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
    connect(&guard, &QTimer::timeout, &guard, []() {
      if (QWidget* popup = QApplication::activePopupWidget()) {
        popup->close();
      }
    });
    guard.start(5000);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, layout.more.center().toPoint());
    QVERIFY(selected);
    QCOMPARE(bar->catEntries_[static_cast<size_t>(entry)].second, QColor(Qt::magenta));
    QCOMPARE(regions.back()->catEntries_[static_cast<size_t>(entry)].second, QColor(Qt::magenta));
    QVERIFY(legendsOf(*view, "voltage").front()->catEntries_.empty());
  }
  void benchmarkSplitSetup_data() {
    QTest::addColumn<int>("files");
    QTest::addColumn<int>("side");
    QTest::newRow("one-file-16k-points") << 1 << 128;
    QTest::newRow("four-files-16k-points") << 4 << 128;
    QTest::newRow("eight-files-16k-points") << 8 << 128;
    QTest::newRow("four-files-263k-points") << 4 << 512;
  }

  void benchmarkSplitSetup() {
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
    options.mode = PaneMode::Split;
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

  void benchmarkSharedFieldInteraction() {
    if (qEnvironmentVariableIntValue("VV_RUN_BENCHMARKS") != 1) {
      QTest::qSkip("Set VV_RUN_BENCHMARKS=1 to run performance measurements.", __FILE__, __LINE__);
      return;
    }
    vtkNew<CountingWindow> window;
    ViewerOptions options;
    options.mode = PaneMode::Split;
    auto view = viewer(window, testMeshes(8), options);
    const auto& fields = view->renderer_.panes().front().fields;
    const auto voltage = std::find(fields.begin(), fields.end(), fieldNamed(*view, "voltage"));
    QVERIFY(voltage != fields.end());
    view->showField(0, static_cast<int>(std::distance(fields.begin(), voltage)));
    const auto legends = legendsOf(*view, "voltage");
    QCOMPARE(legends.size(), size_t(8));
    ColorBarWidget* legend = legends.front();
    int step = 0;
    QBENCHMARK {
      const double lower = 100.0 + (step++ % 100);
      emit legend->clipRangeChanged(lower, lower + 500.0);
      QApplication::processEvents();
    }
  }
};

QTEST_MAIN(ViewerTests)
#include "ViewerTests.moc"
