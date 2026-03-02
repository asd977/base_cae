#include "StreamlineVectorSettingWidget.h"
#include "ui_StreamlineVectorSettingWidget.h"

#include "PostVisualUtils.h"

#include <PublicConstants.h>

#include <vtkActor.h>
#include <vtkDataArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPointSource.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRungeKutta4.h>
#include <vtkScalarBarActor.h>
#include <vtkStreamTracer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <cmath>

namespace {
const QString kFixedVtuPath = QStringLiteral("C:/tool/123/test.vtu");
}

StreamlineVectorSettingWidget::StreamlineVectorSettingWidget(VisualWidgetService* visualService,
                                                             QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessVector)
    , ui(new Ui::StreamlineVectorSettingWidget)
    , m_visualService(visualService)
{
    ui->setupUi(this);
    ui->pathLineEdit->setText(kFixedVtuPath);

    connect(ui->buildPushButton, &QPushButton::clicked,
            this, &StreamlineVectorSettingWidget::on_buildPushButton_clicked);
}

StreamlineVectorSettingWidget::~StreamlineVectorSettingWidget()
{
    delete ui;
}

void StreamlineVectorSettingWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);

    if (m_visualService && m_streamlineActor) {
        addActorByService(m_visualService, m_streamlineActor);
        m_visualService->render();
    }
}

void StreamlineVectorSettingWidget::beforeUnload()
{
    if (m_visualService && m_streamlineActor) {
        removeActorByService(m_visualService, m_streamlineActor);
        m_visualService->render();
    }
}

void StreamlineVectorSettingWidget::on_buildPushButton_clicked()
{
    buildStreamline();
}

vtkSmartPointer<vtkUnstructuredGrid> StreamlineVectorSettingWidget::readFixedGrid() const
{
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(kFixedVtuPath.toUtf8().constData());
    reader->Update();

    auto* ug = reader->GetOutput();
    if (!ug || ug->GetNumberOfPoints() == 0) {
        qWarning() << "[Streamline] Failed to read fixed VTU file:" << kFixedVtuPath;
        return nullptr;
    }

    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    out->ShallowCopy(ug);
    return out;
}

bool StreamlineVectorSettingWidget::preparePointVectors(vtkUnstructuredGrid* grid) const
{
    if (!grid || !grid->GetPointData()) return false;

    auto* pd = grid->GetPointData();
    if (pd->GetVectors() && pd->GetVectors()->GetNumberOfComponents() >= 3) {
        return true;
    }

    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* arr = pd->GetArray(i);
        if (!arr || arr->GetNumberOfComponents() < 3) continue;
        pd->SetVectors(arr);
        return true;
    }

    return false;
}

void StreamlineVectorSettingWidget::buildStreamline()
{
    auto grid = readFixedGrid();
    if (!grid) return;

    if (!preparePointVectors(grid)) {
        qWarning() << "[Streamline] No valid 3-component point vector array found.";
        return;
    }

    double b[6];
    grid->GetBounds(b);
    const double dx = b[1] - b[0];
    const double dy = b[3] - b[2];
    const double dz = b[5] - b[4];
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);

    auto seeds = vtkSmartPointer<vtkPointSource>::New();
    seeds->SetCenter((b[0] + b[1]) * 0.5, (b[2] + b[3]) * 0.5, (b[4] + b[5]) * 0.5);
    seeds->SetRadius(diag > 0.0 ? diag * 0.08 : 1.0);
    seeds->SetNumberOfPoints(200);

    auto tracer = vtkSmartPointer<vtkStreamTracer>::New();
    tracer->SetInputData(grid);
    tracer->SetSourceConnection(seeds->GetOutputPort());
    tracer->SetIntegrator(vtkSmartPointer<vtkRungeKutta4>::New());
    tracer->SetIntegrationDirectionToBoth();
    tracer->SetMaximumPropagation(diag > 0.0 ? diag * 8.0 : 100.0);
    tracer->SetInitialIntegrationStep(0.2);
    tracer->SetComputeVorticity(false);
    tracer->Update();

    auto* outPd = tracer->GetOutput();
    if (!outPd || outPd->GetNumberOfPoints() == 0) {
        qWarning() << "[Streamline] Streamline result is empty.";
        return;
    }

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(tracer->GetOutputPort());
    mapper->ScalarVisibilityOn();

    const char* colorArray = "IntegrationTime";
    if (outPd->GetPointData() && outPd->GetPointData()->GetArray(colorArray)) {
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(colorArray);
    }

    mapper->Update();

    if (m_streamlineActor && m_visualService) {
        removeActorByService(m_visualService, m_streamlineActor);
    }

    m_streamlineActor = vtkSmartPointer<vtkActor>::New();
    m_streamlineActor->SetMapper(mapper);
    m_streamlineActor->GetProperty()->SetLineWidth(2.0);

    if (m_visualService) {
        addActorByService(m_visualService, m_streamlineActor, false);

        auto* sb = m_visualService->scalarBarActor();
        if (sb) {
            sb->SetVisibility(1);
            sb->SetTitle(colorArray);
            if (auto* lut = vtkLookupTable::SafeDownCast(sb->GetLookupTable())) {
                double range[2] = {0.0, 1.0};
                mapper->GetScalarRange(range);
                if (range[1] > range[0]) {
                    lut->SetTableRange(range);
                    lut->Build();
                }
                mapper->SetLookupTable(lut);
                mapper->UseLookupTableScalarRangeOn();
            }
        }

        m_visualService->render();
    }
}
