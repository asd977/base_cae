#include "StreamlineVectorSettingWidget.h"
#include "ui_StreamlineVectorSettingWidget.h"

#include "PostVisualUtils.h"

#include <PublicConstants.h>

#include <vtkActor.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkScalarBarActor.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>

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

    if (m_visualService && m_nephogramActor) {
        addActorByService(m_visualService, m_nephogramActor);
        m_visualService->render();
    }
}

void StreamlineVectorSettingWidget::beforeUnload()
{
    if (m_visualService && m_nephogramActor) {
        removeActorByService(m_visualService, m_nephogramActor);
        m_visualService->render();
    }
}

void StreamlineVectorSettingWidget::on_buildPushButton_clicked()
{
    buildNephogram();
}

vtkSmartPointer<vtkUnstructuredGrid> StreamlineVectorSettingWidget::readFixedGrid() const
{
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(kFixedVtuPath.toUtf8().constData());
    reader->Update();

    auto* ug = reader->GetOutput();
    if (!ug || ug->GetNumberOfPoints() == 0) {
        qWarning() << "[Nephogram] Failed to read fixed VTU file:" << kFixedVtuPath;
        return nullptr;
    }

    auto out = vtkSmartPointer<vtkUnstructuredGrid>::New();
    out->ShallowCopy(ug);
    return out;
}

QString StreamlineVectorSettingWidget::chooseScalarArray(vtkUnstructuredGrid* grid) const
{
    if (!grid || !grid->GetPointData()) return QString();
    auto* pd = grid->GetPointData();

    if (pd->GetScalars() && pd->GetScalars()->GetName()) {
        return QString::fromUtf8(pd->GetScalars()->GetName());
    }

    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* arr = pd->GetArray(i);
        const char* n = pd->GetArrayName(i);
        if (!arr || arr->GetNumberOfComponents() != 1 || !n || !*n) continue;
        return QString::fromUtf8(n);
    }
    return QString();
}

void StreamlineVectorSettingWidget::buildNephogram()
{
    auto grid = readFixedGrid();
    if (!grid) return;

    const QString scalarName = chooseScalarArray(grid);
    if (scalarName.isEmpty()) {
        qWarning() << "[Nephogram] No point scalar array found in fixed VTU.";
        return;
    }

    auto* arr = grid->GetPointData()->GetArray(scalarName.toUtf8().constData());
    if (!arr) {
        qWarning() << "[Nephogram] Scalar array missing:" << scalarName;
        return;
    }

    double range[2] = {0.0, 1.0};
    arr->GetRange(range);

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray(scalarName.toUtf8().constData());
    mapper->SetScalarRange(range);
    mapper->SetUseLookupTableScalarRange(false);

    if (m_nephogramActor && m_visualService) {
        removeActorByService(m_visualService, m_nephogramActor);
    }

    m_nephogramActor = vtkSmartPointer<vtkActor>::New();
    m_nephogramActor->SetMapper(mapper);
    m_nephogramActor->GetProperty()->SetRepresentationToSurface();

    if (m_visualService) {
        auto* sb = m_visualService->scalarBarActor();
        if (sb) {
            sb->SetVisibility(1);
            sb->SetTitle(scalarName.toUtf8().constData());
            if (auto* lut = vtkLookupTable::SafeDownCast(sb->GetLookupTable())) {
                lut->SetTableRange(range);
                lut->Build();
                mapper->SetLookupTable(lut);
            }
        }

        addActorByService(m_visualService, m_nephogramActor, false);
        m_visualService->render();
    }
}
