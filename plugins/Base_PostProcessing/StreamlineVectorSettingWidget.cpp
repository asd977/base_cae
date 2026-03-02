#include "StreamlineVectorSettingWidget.h"
#include "ui_StreamlineVectorSettingWidget.h"

#include "PostVisualUtils.h"

#include <PublicConstants.h>

#include <vtkActor.h>
#include <vtkDataArray.h>
#include <vtkFloatArray.h>
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
#include <QSet>

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

    // 1) 直接使用已有 3 分量数组
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* arr = pd->GetArray(i);
        if (!arr || arr->GetNumberOfComponents() < 3) continue;
        pd->SetVectors(arr);
        return true;
    }

    // 2) 从三个标量数组拼接向量（优先 X/Y/Z 命名）
    struct ScalarArrayInfo {
        QString name;
        vtkDataArray* arr{nullptr};
    };

    QList<ScalarArrayInfo> scalars;
    for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        auto* arr = pd->GetArray(i);
        const char* n = pd->GetArrayName(i);
        if (!arr || arr->GetNumberOfComponents() != 1 || !n || !*n) continue;
        if (arr->GetNumberOfTuples() != grid->GetNumberOfPoints()) continue;
        scalars.push_back({QString::fromUtf8(n), arr});
    }
    if (scalars.size() < 3) return false;

    auto endsWithAxis = [](const QString& name, const QString& axis)->bool {
        const QString n = name.toLower();
        return n.endsWith("_" + axis) || n.endsWith("." + axis) || n.endsWith(axis);
    };

    auto commonBase = [](const QString& name)->QString {
        QString n = name;
        n = n.toLower();
        if (n.endsWith("_x") || n.endsWith("_y") || n.endsWith("_z") ||
            n.endsWith(".x") || n.endsWith(".y") || n.endsWith(".z")) {
            n.chop(2);
            return n;
        }
        if (n.endsWith("x") || n.endsWith("y") || n.endsWith("z")) {
            n.chop(1);
            return n;
        }
        return QString();
    };

    vtkDataArray* arrX = nullptr;
    vtkDataArray* arrY = nullptr;
    vtkDataArray* arrZ = nullptr;

    // 优先按公共前缀 + XYZ 配对
    QSet<QString> bases;
    for (const auto& s : scalars) {
        const QString b = commonBase(s.name);
        if (!b.isEmpty()) bases.insert(b);
    }
    for (const QString& b : bases) {
        for (const auto& s : scalars) {
            const QString lower = s.name.toLower();
            if (arrX == nullptr && commonBase(lower) == b && endsWithAxis(lower, "x")) arrX = s.arr;
            if (arrY == nullptr && commonBase(lower) == b && endsWithAxis(lower, "y")) arrY = s.arr;
            if (arrZ == nullptr && commonBase(lower) == b && endsWithAxis(lower, "z")) arrZ = s.arr;
        }
        if (arrX && arrY && arrZ) break;
        arrX = arrY = arrZ = nullptr;
    }

    // 回退：尝试名称正好是 x/y/z 或 u/v/w
    if (!(arrX && arrY && arrZ)) {
        for (const auto& s : scalars) {
            const QString n = s.name.toLower();
            if (!arrX && (n == "x" || n == "u" || n.endsWith("_u"))) arrX = s.arr;
            if (!arrY && (n == "y" || n == "v" || n.endsWith("_v"))) arrY = s.arr;
            if (!arrZ && (n == "z" || n == "w" || n.endsWith("_w"))) arrZ = s.arr;
        }
    }

    // 最后回退：取前三个标量数组
    if (!(arrX && arrY && arrZ)) {
        arrX = scalars[0].arr;
        arrY = scalars[1].arr;
        arrZ = scalars[2].arr;
    }

    if (!(arrX && arrY && arrZ)) return false;

    auto vec = vtkSmartPointer<vtkFloatArray>::New();
    vec->SetName("AutoVector");
    vec->SetNumberOfComponents(3);
    vec->SetNumberOfTuples(grid->GetNumberOfPoints());

    for (vtkIdType i = 0; i < grid->GetNumberOfPoints(); ++i) {
        const double x = arrX->GetComponent(i, 0);
        const double y = arrY->GetComponent(i, 0);
        const double z = arrZ->GetComponent(i, 0);
        vec->SetTuple3(i, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    }

    pd->AddArray(vec);
    pd->SetVectors(vec);
    return true;
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
