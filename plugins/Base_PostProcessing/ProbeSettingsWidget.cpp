#include "ProbeSettingsWidget.h"

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

#include <QHeaderView>
#include <QTableWidget>
#include <cmath>

#include "ui_ProbeSettingsWidget.h"

// 小工具：把 VTK 回调转发到 this
namespace
{
struct ClickPayload
{
    ProbeSettingsWidget* self{nullptr};
};
} // namespace

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
}

ProbeSettingsWidget::~ProbeSettingsWidget()
{
    unhookInteractor();
    delete ui;
}

void ProbeSettingsWidget::beforeLoad()
{
    // 回到页面时，如果 UI 上是开启状态，就确保已 hook
    if (ui->enableProbeCheckBox && ui->enableProbeCheckBox->isChecked() && !m_hooked)
        hookInteractor();
}

void ProbeSettingsWidget::beforeUnload()
{
    // 离开页面：只解绑观察者，不清表，不销毁 picker
    unhookInteractor();
}

void ProbeSettingsWidget::hookInteractor()
{
    if (m_hooked)
        return;
    auto rw = m_visualService ? m_visualService->renderWindow() : nullptr;
    if (!rw)
        return;
    auto iren = rw->GetInteractor();
    if (!iren)
        return;

    auto cb = vtkSmartPointer<vtkCallbackCommand>::New();
    auto payload = new ClickPayload{this}; // 交由 callback 生命周期管理
    cb->SetClientData(payload);
    cb->SetCallback(
        [](vtkObject* caller, unsigned long eid, void* clientData, void* callData)
        {
            Q_UNUSED(callData);
            auto p = static_cast<ClickPayload*>(clientData);
            if (!p || !p->self)
                return;
            auto iren = static_cast<vtkRenderWindowInteractor*>(caller);
            if (!iren)
                return;
            int x = iren->GetEventPosition()[0];
            int y = iren->GetEventPosition()[1];
            // 左键按下触发
            if (eid == vtkCommand::LeftButtonPressEvent)
            {
                p->self->handlePick(x, y);
            }
        });

    iren->AddObserver(vtkCommand::LeftButtonPressEvent, cb);
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
    auto rw = m_visualService ? m_visualService->renderWindow() : nullptr;
    if (!rw)
        return;
    auto rc = rw->GetRenderers();
    if (!rc || rc->GetNumberOfItems() == 0)
        return;
    auto ren = rc->GetFirstRenderer();
    if (!ren)
        return;

    if (!m_picker->Pick(displayX, displayY, 0.0, ren))
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

    // fallback：取颜色条标题或 ActiveScalars
    if (arrayName.isEmpty())
    {
        arrayName = fallbackArrayNameFromScalarBar();
        if (arrayName.isEmpty())
        {
            if (auto sa = ds->GetPointData()->GetScalars())
                arrayName = sa->GetName();
            else if (auto ca = ds->GetCellData()->GetScalars())
                arrayName = ca->GetName();
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

    // 若没有 pointId，但需要点数据，使用最近点兜底
    if (assoc == 0 && pointId < 0)
    {
        vtkNew<vtkPointLocator> locator;
        locator->SetDataSet(ds);
        locator->BuildLocator();
        pointId = locator->FindClosestPoint(pos);
    }

    double value = std::numeric_limits<double>::quiet_NaN();
    if (!fetchScalarValue(ds, arrayName, assoc, pointId, cellId, value))
        return;

    appendRow(pos, arrayName, (assoc == 0 ? "Point" : "Cell"), value);

    // 立即刷新（可选）
    if (rw)
        rw->Render();
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
                                           long long pointId, long long cellId,
                                           double& outValue) const
{
    if (!ds)
        return false;
    vtkDataArray* arr = nullptr;
    if (assoc == 0)
    {
        arr = ds->GetPointData()->GetArray(arrayName.toUtf8().constData());
        if (!arr || pointId < 0 || pointId >= arr->GetNumberOfTuples())
            return false;
        outValue = arr->GetComponent(pointId, 0);
        return true;
    }
    else
    {
        arr = ds->GetCellData()->GetArray(arrayName.toUtf8().constData());
        if (!arr)
            return false;
        // 若 cellId 无效，尝试用 FindCell：但这里通常 pick 已返回
        if (cellId < 0 || cellId >= arr->GetNumberOfTuples())
            return false;
        outValue = arr->GetComponent(cellId, 0);
        return true;
    }
}

void ProbeSettingsWidget::setupTableIfNeeded()
{
    if (!ui->probeTable)
        return;
    if (ui->probeTable->columnCount() == 0)
    {
        ui->probeTable->setColumnCount(7);
        QStringList headers{"#", "X", "Y", "Z", "Array", "Assoc", "Value"};
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

    // 新记录插到最上面（最新在上）
    ui->probeTable->insertRow(0);

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
