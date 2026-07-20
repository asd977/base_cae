#include "ProbeSettingsWidget.h"

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAlgorithm.h>
#include <vtkAlgorithmOutput.h>
#include <vtkCallbackCommand.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarBarActor.h>

#include <QByteArray>
#include <QDebug>
#include <QHeaderView>
#include <QJsonObject>
#include <QTableWidget>
#include <cmath>
#include <limits>

#include "ResultDisplayNames.h"
#include "ui_ProbeSettingsWidget.h"

static inline QString sci(double v, int prec = 6)
{
    return QString::number(v, 'e', prec);
}

ProbeSettingsWidget::ProbeSettingsWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::ProbeSettingsWidget), BasePage("Probe"), m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    // UI 初始化
    if (ui->enableProbeCheckBox)
        ui->enableProbeCheckBox->setChecked(false);
    setupTableIfNeeded();

    // Picker
    m_picker = vtkSmartPointer<vtkCellPicker>::New();
    m_picker->SetTolerance(1e-3); // 可按需要调

    // 信号槽
    connect(ui->enableProbeCheckBox, &QCheckBox::toggled, this,
            &ProbeSettingsWidget::on_enableProbeCheckBox_toggled);
    connect(ui->clearPushButton, &QPushButton::clicked, this,
            &ProbeSettingsWidget::on_clearPushButton_clicked);

    // 清空按钮沿用全局 GEOCAE 主题，避免局部蓝色样式覆盖。

}

ProbeSettingsWidget::~ProbeSettingsWidget()
{
    unhookInteractor();
    delete ui;
}

void ProbeSettingsWidget::beforeLoad()
{
    ensureVisualService();
    if (m_hasPendingEnabledState)
    {
        m_hasPendingEnabledState = false;
        ui->enableProbeCheckBox->setChecked(m_pendingEnabledState);
    }

    // 回到页面时，如果 UI 上是开启状态，就确保已 hook
    if (ui->enableProbeCheckBox && ui->enableProbeCheckBox->isChecked() && !m_hooked)
        hookInteractor();
}

QJsonObject ProbeSettingsWidget::saveJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"),
                  ui->enableProbeCheckBox && ui->enableProbeCheckBox->isChecked());
    return object;
}

void ProbeSettingsWidget::loadJson(const QJsonObject& object)
{
    m_pendingEnabledState = object.value(QStringLiteral("enabled")).toBool(false);
    m_hasPendingEnabledState = true;
}

void ProbeSettingsWidget::beforeUnload()
{
    // 离开页面只解绑拾取，保留复选框与表格状态；返回页面后按原状态恢复。
    unhookInteractor();
}

bool ProbeSettingsWidget::ensureVisualService()
{
    if (!m_visualService)
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (!m_visualService)
        qWarning() << "[ProbeSettingsWidget] VisualWidgetService is not available.";

    return m_visualService != nullptr;
}

void ProbeSettingsWidget::hookInteractor()
{
    if (m_hooked)
        return;
    if (!ensureVisualService())
        return;
    auto rw = m_visualService ? m_visualService->renderWindow() : nullptr;
    if (!rw)
        return;
    auto iren = rw->GetInteractor();
    if (!iren)
        return;

    auto cb = vtkSmartPointer<vtkCallbackCommand>::New();
    cb->SetClientData(this);
    cb->SetCallback(
        [](vtkObject* caller, unsigned long eid, void* clientData, void* callData)
        {
            Q_UNUSED(callData);
            auto self = static_cast<ProbeSettingsWidget*>(clientData);
            if (!self)
                return;
            auto iren = static_cast<vtkRenderWindowInteractor*>(caller);
            if (!iren)
                return;
            int x = iren->GetEventPosition()[0];
            int y = iren->GetEventPosition()[1];
            // 左键按下触发
            if (eid == vtkCommand::LeftButtonPressEvent)
            {
                self->handlePick(x, y);
            }
        });

    iren->AddObserver(vtkCommand::LeftButtonPressEvent, cb, 1.0f);
    m_clickObserver = cb;
    m_hooked = true;
}

void ProbeSettingsWidget::unhookInteractor()
{
    if (!m_hooked)
        return;
    auto rw = m_visualService ? m_visualService->renderWindow() : nullptr;
    if (rw && m_clickObserver)
    {
        if (auto iren = rw->GetInteractor())
        {
            iren->RemoveObserver(m_clickObserver);
        }
    }
    m_clickObserver = nullptr;
    m_hooked = false;
}

void ProbeSettingsWidget::handlePick(int displayX, int displayY)
{
    if (!ensureVisualService())
        return;

    auto rw = m_visualService ? m_visualService->renderWindow() : nullptr;
    if (!rw)
        return;

    vtkRenderer* ren = m_visualService ? m_visualService->renderer() : nullptr;

    bool picked = false;
    if (ren)
        picked = m_picker->Pick(displayX, displayY, 0.0, ren) != 0;

    if (!picked)
    {
        auto rc = rw->GetRenderers();
        if (!rc || rc->GetNumberOfItems() == 0)
            return;

        rc->InitTraversal();
        while (auto* candidate = rc->GetNextItem())
        {
            if (candidate == ren)
                continue;
            if (m_picker->Pick(displayX, displayY, 0.0, candidate))
            {
                ren = candidate;
                picked = true;
                break;
            }
        }
    }

    if (!picked)
        return;

    double pos[3];
    m_picker->GetPickPosition(pos);
    vtkActor* actor = m_picker->GetActor();
    if (!actor)
        return;

    vtkDataSet* ds = datasetFromActor(actor);
    if (!ds)
        return;

    // 先从 mapper 获取数组名 & 关联
    auto mapper = actor->GetMapper();
    QString arrayName = activeArrayNameFromMapper(mapper);
    int assoc = arrayAssociationFromMapper(mapper); // 0 points, 1 cells, -1 unknown

    // 颜色条标题现在是简短显示名，不能再当作内部数组名使用。
    // Mapper 没有显式数组名时，直接读取数据集的活动标量。
    if (arrayName.isEmpty())
    {
        if (auto sa = ds->GetPointData()->GetScalars())
        {
            arrayName = QString::fromUtf8(sa->GetName() ? sa->GetName() : "");
            assoc = 0;
        }
        else if (auto ca = ds->GetCellData()->GetScalars())
        {
            arrayName = QString::fromUtf8(ca->GetName() ? ca->GetName() : "");
            assoc = 1;
        }
    }
    if (assoc < 0)
    {
        // 根据数据是否有点/元数组来推断
        assoc = ds->GetPointData()->HasArray(arrayName.toUtf8().constData())
                    ? 0
                    : (ds->GetCellData()->HasArray(arrayName.toUtf8().constData()) ? 1 : 0);
    }

    // 优先用 picker 返回的 ID
    long long pointId = m_picker->GetPointId();
    long long cellId = m_picker->GetCellId();

    // 若没有 pointId，但需要点数据，复用最近点定位器，避免每次点击都重建。
    if (assoc == 0 && pointId < 0)
    {
        if (!m_pointLocator || m_locatorDataSet != ds)
        {
            m_pointLocator = vtkSmartPointer<vtkPointLocator>::New();
            m_pointLocator->SetDataSet(ds);
            m_pointLocator->BuildLocator();
            m_locatorDataSet = ds;
        }
        pointId = m_pointLocator->FindClosestPoint(pos);
    }

    double value = std::numeric_limits<double>::quiet_NaN();
    int components = 0;
    if (!fetchScalarValue(ds, arrayName, assoc, pointId, cellId, value, components))
    {
        qWarning() << "[Probe] Unable to read array" << arrayName << "from picked data.";
        return;
    }

    QString shownName = ResultDisplayNames::displayName(arrayName);
    if (components > 1)
        shownName += QStringLiteral("（模长）");
    appendRow(pos, shownName, (assoc == 0 ? QStringLiteral("节点") : QStringLiteral("单元")),
              value);
}

vtkDataSet* ProbeSettingsWidget::datasetFromActor(vtkActor* actor) const
{
    if (!actor)
        return nullptr;
    auto mapper = actor->GetMapper();
    if (!mapper)
        return nullptr;

    // 优先从 pipeline 末端拿数据对象
    mapper->Update();
    if (auto ds = vtkDataSet::SafeDownCast(mapper->GetInput()))
        return ds;

    // 退化：若是从连接拿
    if (auto alg = mapper->GetInputConnection(0, 0)
                       ? mapper->GetInputConnection(0, 0)->GetProducer()
                       : nullptr)
    {
        alg->Update();
        if (auto out = alg->GetOutputDataObject(0))
            return vtkDataSet::SafeDownCast(out);
    }
    return nullptr;
}

QString ProbeSettingsWidget::activeArrayNameFromMapper(vtkMapper* mapper) const
{
    if (!mapper)
        return {};
    const char* n = mapper->GetArrayName();
    if (n && *n)
        return QString::fromUtf8(n);
    return {};
}

int ProbeSettingsWidget::arrayAssociationFromMapper(vtkMapper* mapper) const
{
    if (!mapper)
        return -1;
    switch (mapper->GetScalarMode())
    {
    case VTK_SCALAR_MODE_USE_POINT_FIELD_DATA:
        return 0;
    case VTK_SCALAR_MODE_USE_CELL_FIELD_DATA:
        return 1;
    default:
        return -1;
    }
}

QString ProbeSettingsWidget::fallbackArrayNameFromScalarBar() const
{
    if (!m_visualService)
        return {};
    auto sb = m_visualService->scalarBarActor();
    if (!sb)
        return {};
    const char* title = sb->GetTitle();
    if (title && *title)
        return QString::fromUtf8(title);
    return {};
}

bool ProbeSettingsWidget::fetchScalarValue(vtkDataSet* ds, const QString& arrayName, int assoc,
                                           long long pointId, long long cellId, double& outValue,
                                           int& outComponents) const
{
    outComponents = 0;
    if (!ds || arrayName.isEmpty())
        return false;

    vtkDataArray* arr = nullptr;
    vtkIdType tupleId = -1;
    const QByteArray encodedName = arrayName.toUtf8();
    if (assoc == 0)
    {
        arr = ds->GetPointData() ? ds->GetPointData()->GetArray(encodedName.constData()) : nullptr;
        tupleId = static_cast<vtkIdType>(pointId);
    }
    else
    {
        arr = ds->GetCellData() ? ds->GetCellData()->GetArray(encodedName.constData()) : nullptr;
        tupleId = static_cast<vtkIdType>(cellId);
    }

    if (!arr || tupleId < 0 || tupleId >= arr->GetNumberOfTuples())
        return false;

    outComponents = arr->GetNumberOfComponents();
    if (outComponents <= 0)
        return false;

    if (outComponents == 1)
    {
        outValue = arr->GetComponent(tupleId, 0);
    }
    else
    {
        double sumSquares = 0.0;
        for (int component = 0; component < outComponents; ++component)
        {
            const double value = arr->GetComponent(tupleId, component);
            sumSquares += value * value;
        }
        outValue = std::sqrt(sumSquares);
    }
    return std::isfinite(outValue);
}

void ProbeSettingsWidget::setupTableIfNeeded()
{
    if (!ui->probeTable)
        return;
    if (ui->probeTable->columnCount() == 0)
    {
        ui->probeTable->setColumnCount(7);
        QStringList headers{QStringLiteral("序号"), QStringLiteral("X"), QStringLiteral("Y"),
                            QStringLiteral("Z"), QStringLiteral("物理量"),
                            QStringLiteral("类型"), QStringLiteral("数值")};
        ui->probeTable->setHorizontalHeaderLabels(headers);
        ui->probeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->probeTable->verticalHeader()->setVisible(false);
        ui->probeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->probeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->probeTable->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->probeTable->setSortingEnabled(false); // 关键：避免用户排序打乱“最新在上”
    }
}

void ProbeSettingsWidget::appendRow(const double pos[3], const QString& arrayName,
                                    const QString& assocStr, double value)
{
    setupTableIfNeeded();
    if (!ui->probeTable)
        return;

    // 新记录插到最上面（最新在上），限制记录数量避免长时间拾取导致表格持续膨胀。
    constexpr int kMaximumProbeRows = 500;
    ui->probeTable->insertRow(0);
    while (ui->probeTable->rowCount() > kMaximumProbeRows)
        ui->probeTable->removeRow(ui->probeTable->rowCount() - 1);

    auto makeItem = [](const QString& s)
    {
        auto* it = new QTableWidgetItem(s);
        it->setTextAlignment(Qt::AlignCenter);
        return it;
    };

    // # 仍用递增编号，便于查看顺序
    ui->probeTable->setItem(0, 0, makeItem(QString::number(++m_rowCounter)));
    // X/Y/Z 与 Value 全部科学计数法
    ui->probeTable->setItem(0, 1, makeItem(sci(pos[0], 2)));
    ui->probeTable->setItem(0, 2, makeItem(sci(pos[1], 2)));
    ui->probeTable->setItem(0, 3, makeItem(sci(pos[2], 2)));
    ui->probeTable->setItem(0, 4, makeItem(arrayName));
    ui->probeTable->setItem(0, 5, makeItem(assocStr));
    ui->probeTable->setItem(0, 6, makeItem(sci(value, 2))); // 值可以给高一点精度

    ui->probeTable->clearSelection();
    ui->probeTable->selectRow(0);
    ui->probeTable->scrollToTop();
}

void ProbeSettingsWidget::on_enableProbeCheckBox_toggled(bool checked)
{
    if (checked)
        hookInteractor();
    else
        unhookInteractor();
}

void ProbeSettingsWidget::on_clearPushButton_clicked()
{
    if (!ui->probeTable)
        return;
    ui->probeTable->setRowCount(0);
    m_rowCounter = 0;
}
