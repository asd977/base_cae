#include <QtWidgets>
#include <services/VisualWidgetService.h>
#include "../plugins/Base_PostProcessing/VectorSettingWidget.h"
#include "../plugins/Base_PostProcessing/PostVisualUtils.h"
#include <vtkActor.h>
#include <vtkArrowSource.h>
#include <vtkAlgorithmOutput.h>
#include <vtkCellData.h>
#include <vtkDoubleArray.h>
#include <vtkGlyph3D.h>
#include <vtkMapper.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkXMLUnstructuredGridWriter.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <iostream>
#include <stdexcept>
#include "PostTestVisual.h"

static void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

static vtkSmartPointer<vtkUnstructuredGrid> fixture()
{
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();points->SetDataTypeToDouble();
    auto vector = vtkSmartPointer<vtkDoubleArray>::New();
    vector->SetName("DISP");vector->SetNumberOfComponents(3);
    // Uniform region plus a strongly refined patch: sampling must depend on space.
    for (int patch = 0; patch < 2; ++patch) {
        const int nx = patch ? 70 : 50, ny = patch ? 70 : 30;
        const vtkIdType first = points->GetNumberOfPoints();
        for (int y = 0; y <= ny; ++y)for (int x = 0; x <= nx; ++x) {
            const double px = patch ? 20.0 + x * .02 : x * 2.0;
            const double py = patch ? 20.0 + y * .02 : y * 2.0;
            points->InsertNextPoint(1870.97 + px, -3483.33 + py, 2110);
            const double length = 1 + px / 100;
            vector->InsertNextTuple3(length, .3 * std::sin(py / 10), 0);
        }
        for (int y = 0; y < ny; ++y)for (int x = 0; x < nx; ++x) {
            vtkIdType p = first + y * (nx + 1) + x;
            vtkIdType a[3] = {p, p + 1, p + nx + 1}, b[3] = {p + 1, p + nx + 2, p + nx + 1};
            grid->InsertNextCell(VTK_TRIANGLE, 3, a);grid->InsertNextCell(VTK_TRIANGLE, 3, b);
        }
    }
    for (int i = 0; i < 20; ++i)vector->SetTuple3(i, 0, 0, 0);
    grid->SetPoints(points);grid->GetPointData()->AddArray(vector);
    // Same-named cell data must not replace the actual nodal displacement.
    auto cell = vtkSmartPointer<vtkDoubleArray>::New();cell->SetName("DISP");
    cell->SetNumberOfComponents(3);cell->SetNumberOfTuples(grid->GetNumberOfCells());
    cell->FillComponent(0, 0);cell->FillComponent(1, 0);cell->FillComponent(2, 0);
    grid->GetCellData()->AddArray(cell);
    return grid;
}

static void load(VectorSettingWidget& page, vtkUnstructuredGrid* grid, const QString& path)
{
    auto writer = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    writer->SetFileName(path.toUtf8());writer->SetInputData(grid);check(writer->Write(), "write fixture");
    auto* steps = page.findChild<QComboBox*>("stepComboBox");check(steps, "result selector");
    {QSignalBlocker blocker(steps);steps->clear();steps->addItem("vector", path);}
    check(QMetaObject::invokeMethod(&page, "on_stepComboBox_currentIndexChanged", Q_ARG(int, 0)), "load result");
}

static vtkGlyph3D* glyph(TestVisual& visual)
{
    for (const auto& prop : visual.props)
        if (auto* actor = vtkActor::SafeDownCast(prop))
            if (auto* mapper = actor->GetMapper())
                if (auto* glyph = vtkGlyph3D::SafeDownCast(mapper->GetInputAlgorithm()))return glyph;
    return nullptr;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try {
        QTemporaryDir project;check(project.isValid(), "temporary project");
        PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, project.path(), "vector-test");
        TestVisual visual;VectorSettingWidget page(&visual);
        const auto source = fixture();
        load(page, source, project.filePath("vector.vtu"));
        auto* arrows = glyph(visual);check(arrows, "vector glyph exists");
        auto* sample = vtkPolyData::SafeDownCast(arrows->GetInput());check(sample, "sampled points");
        const auto count = sample->GetNumberOfPoints();
        check(count > 100 && count < source->GetNumberOfPoints() / 2, "spatial sampling reduces refined patch");
        int refined = 0;
        for (vtkIdType i = 0; i < count; ++i) {
            double p[3];sample->GetPoint(i, p);
            if (p[0] >= 1890.97 && p[0] <= 1892.37 && p[1] >= -3463.33 && p[1] <= -3461.93)++refined;
            check(sample->GetPointData()->GetArray("VectorMag")->GetComponent(i, 0) > 0, "zero vectors excluded");
        }
        check(refined <= 4, "refined mesh does not dominate arrow density");
        check(sample->GetPoints()->GetDataType() == VTK_DOUBLE, "survey-coordinate precision");
        auto* arrowSource = vtkArrowSource::SafeDownCast(arrows->GetInputConnection(1, 0)->GetProducer());
        check(arrowSource && arrowSource->GetTipResolution() == 12 && arrowSource->GetShaftResolution() == 12,
              "TMSR smooth arrow geometry");
        auto* field = page.findChild<QComboBox*>("vectorFieldComboBox");
        auto* scaleByMag = page.findChild<QCheckBox*>("scaleByMagnitudeCheckBox");
        auto* length = page.findChild<QDoubleSpinBox*>("scaleSpinBox");
        auto* density = page.findChild<QSpinBox*>("maskSizeSpinBox");
        check(field && scaleByMag && length && density, "TMSR vector controls");
        check(field->currentData().toString() == "DISP" && page.findChild<QComboBox*>("xComboBox")->isHidden(),
              "packed vector hides redundant components");
        scaleByMag->setChecked(true);arrows = glyph(visual);
        check(arrows->GetScaleMode() == VTK_SCALE_BY_VECTOR, "magnitude-scaled arrows");
        const double originalScale = arrows->GetScaleFactor();length->setValue(2);
        check(std::abs(arrows->GetScaleFactor() - originalScale * 2) < 1e-10, "length control preserves normalization");
        page.beforeLoad();
        auto retained = vtkSmartPointer<vtkGlyph3D>(glyph(visual));
        auto* retainedMapper = vtkActor::SafeDownCast(visual.getProp(postActorId(page.getUuid(),"vector")))->GetMapper();
        const auto retainedLut = vtkSmartPointer<vtkScalarsToColors>(retainedMapper->GetLookupTable());
        page.beforeUnload();
        auto otherLut = vtkSmartPointer<vtkLookupTable>::New();otherLut->SetRange(100, 200);otherLut->Build();
        visual.legend->SetLookupTable(otherLut);page.beforeLoad();
        check(glyph(visual) == retained, "reentry reuses current pipeline");
        check(visual.legend->GetLookupTable() == retainedLut, "reentry restores independent vector colors");
        check(postDisplayFieldContext().valid && postDisplayFieldContext().fieldName == "VectorMag",
              "reentry restores vector display controls context");
        page.beforeUnload();
        EventManager::instance().sendEvent(postDisplaySettingsChangedEvent());page.beforeLoad();
        check(glyph(visual) != retained, "inactive filter change invalidates cached arrows");
        auto appearanceGlyph=vtkSmartPointer<vtkGlyph3D>(glyph(visual));
        auto appearanceActor=vtkSmartPointer<vtkActor>(vtkActor::SafeDownCast(visual.getProp(postActorId(page.getUuid(),"vector"))));
        auto appearanceInput=vtkSmartPointer<vtkPolyData>(vtkPolyData::SafeDownCast(appearanceGlyph->GetInput()));
        const auto appearanceTime=appearanceInput->GetMTime();
        for (bool model : {false, true}) for (bool lines : {false, true}) {
            postSceneStyle().showContextOutline=model;postSceneStyle().showContextLines=lines;
            EventManager::instance().sendEvent(postDisplaySettingsChangedEvent(),{{"reason","context-outline"}});
            check(bool(visual.getProp(postActorId(page.getUuid(),"context-model")))==model,"vector reference model has independent visibility");
            check(bool(visual.getProp(postActorId(page.getUuid(),"context-outline")))==lines,"vector outline lines have independent visibility");
            check(glyph(visual)==appearanceGlyph && appearanceInput->GetMTime()==appearanceTime,"context toggles preserve arrow geometry");
        }
        postSceneStyle().showContextLines=false;
        EventManager::instance().sendEvent(postDisplaySettingsChangedEvent(),{{"reason","context-outline"}});
        auto* contextModel=vtkActor::SafeDownCast(visual.getProp(postActorId(page.getUuid(),"context-model")));
        check(contextModel && !contextModel->GetProperty()->GetEdgeVisibility(),"reference model contains no edge overlay");
        const int oldRepresentation=postSceneStyle().representationMode;
        postSceneStyle().representationMode=1;applyPostSceneRepresentation(&visual,false);
        check(contextModel->GetProperty()->GetRepresentation()==VTK_SURFACE && !contextModel->GetProperty()->GetEdgeVisibility(),"result representation does not enable reference model lines");
        postSceneStyle().representationMode=oldRepresentation;
        auto& displayRange=postFieldRangeStyle("VectorMag");displayRange.automatic=false;
        displayRange.initialized=true;displayRange.minimum=0;displayRange.maximum=5;
        const auto beforeAppearance=visual.totalRenderRequests();
        EventManager::instance().sendEvent(postDisplaySettingsChangedEvent(),{{"reason","range"}});
        check(glyph(visual)==appearanceGlyph && visual.getProp(postActorId(page.getUuid(),"vector"))==appearanceActor
              && appearanceInput->GetMTime()==appearanceTime,"range changes reuse arrow geometry and sampling");
        check(visual.totalRenderRequests()==beforeAppearance+1,"range change renders once");
        check(appearanceActor->GetMapper()->GetLookupTable()->GetRange()[1]==5,"range update reaches retained mapper");
        page.beforeUnload();
        EventManager::instance().sendEvent(postDisplaySettingsChangedEvent(),{{"reason","color-map"}});
        page.beforeLoad();
        check(glyph(visual)==appearanceGlyph,"inactive appearance changes reuse arrows on reentry");
        const auto state = page.saveJson();check(state["scaleByMagnitude"].toBool(), "magnitude mode saved");
        scaleByMag->setChecked(false);page.loadJson(state);
        check(scaleByMag->isChecked() && field->currentData().toString() == "DISP", "saved vector controls restored");
        density->setValue(5);check(vtkPolyData::SafeDownCast(glyph(visual)->GetInput())->GetNumberOfPoints() > count, "manual sampling works");
        density->setValue(0);
        if (argc > 1) {
            QDir().mkpath(QString::fromLocal8Bit(argv[1]));
            visual.scene->ResetCamera();visual.window->Render();
            auto capture = vtkSmartPointer<vtkWindowToImageFilter>::New();capture->SetInput(visual.window);capture->Update();
            auto png = vtkSmartPointer<vtkPNGWriter>::New();
            const auto path = QDir(QString::fromLocal8Bit(argv[1])).filePath("vectors.png").toUtf8();
            png->SetFileName(path);png->SetInputConnection(capture->GetOutputPort());png->Write();
        }
        // Cell-only vectors remain usable, and unrelated scalars never become fake arrows.
        auto cellOnly = fixture();cellOnly->GetPointData()->RemoveArray("DISP");
        auto* cell = cellOnly->GetCellData()->GetArray("DISP");cell->FillComponent(0, 1);
        load(page, cellOnly, project.filePath("cell.vtu"));
        check(glyph(visual) && vtkPolyData::SafeDownCast(glyph(visual)->GetInput())->GetNumberOfPoints() > 0, "cell vector interpolation");
        auto scalar = fixture();scalar->GetPointData()->RemoveArray("DISP");scalar->GetCellData()->RemoveArray("DISP");
        auto temperature = vtkSmartPointer<vtkDoubleArray>::New();temperature->SetName("T");
        temperature->SetNumberOfTuples(scalar->GetNumberOfPoints());temperature->FillComponent(0, 300);
        scalar->GetPointData()->AddArray(temperature);load(page, scalar, project.filePath("scalar.vtu"));
        check(!glyph(visual), "scalar-only results do not produce fake vectors");
        std::cout << "PASS: TMSR vector appearance, spatial sampling (" << count << " arrows), controls, persistence, cache invalidation and data compatibility\n";
        return 0;
    } catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}


