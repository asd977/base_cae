#include "BarElementPlacementWidget.h"

#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>

#include <vtkUnstructuredGrid.h>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>
#include <QStringList>

namespace
{
constexpr char kPickDomain[] = "BarElementPlacement";
}

BarElementPlacementWidget::BarElementPlacementWidget(QWidget* parent)
    : BasePage(QStringLiteral("BarElementPlacement"))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* hint = new QLabel(
        QStringLiteral("在现有实体网格上添加一维杆单元。建议直接点选两个实体网格节点；"
                       "手动输入时，起点和终点也必须与实体网格节点重合。阵列中的每根杆都会"
                       "检查端点连接和重复布置。杆单元生成与材料创建相互独立。"),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* geometryBox = new QGroupBox(QStringLiteral("布置参数"), this);
    auto* grid = new QGridLayout(geometryBox);
    grid->addWidget(new QLabel(QStringLiteral("坐标"), geometryBox), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("X"), geometryBox), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Y"), geometryBox), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("Z"), geometryBox), 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("起点"), geometryBox), 1, 0);
    grid->addWidget(new QLabel(QStringLiteral("终点"), geometryBox), 2, 0);
    grid->addWidget(new QLabel(QStringLiteral("阵列偏移"), geometryBox), 3, 0);
    for (int axis = 0; axis < 3; ++axis)
    {
        m_start[axis] = coordinateSpin();
        m_end[axis] = coordinateSpin(axis == 0 ? 1.0 : 0.0);
        m_offset[axis] = coordinateSpin();
        grid->addWidget(m_start[axis], 1, axis + 1);
        grid->addWidget(m_end[axis], 2, axis + 1);
        grid->addWidget(m_offset[axis], 3, axis + 1);
    }

    m_pickStatus = new QLabel(QStringLiteral("请在模型中依次点选起点和终点，坐标会自动填写。"),
                              geometryBox);
    m_pickStatus->setWordWrap(true);
    auto* clearPick = new QPushButton(QStringLiteral("清除点选"), geometryBox);
    grid->addWidget(m_pickStatus, 4, 0, 1, 3);
    grid->addWidget(clearPick, 4, 3);
    connect(clearPick, &QPushButton::clicked, this, [this]()
    {
        PickManager::instance().clearSelection(QString::fromLatin1(kPickDomain), m_uuid,
                                                PickMode::Node);
    });
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind kind)
            {
                if (domain == QString::fromLatin1(kPickDomain) && uuid == m_uuid &&
                    mode == PickMode::Node && kind == GridKind::Solid)
                {
                    updatePickedEndpointState();
                }
            });
    root->addWidget(geometryBox);

    auto* propertyBox = new QGroupBox(QStringLiteral("杆单元属性"), this);
    auto* form = new QFormLayout(propertyBox);
    m_segments = new QSpinBox(propertyBox);
    m_segments->setRange(1, 10000);
    m_segments->setValue(1);
    m_segments->setToolTip(QStringLiteral("沿起点到终点等距划分的一维杆单元数量。"));
    m_arrayCount = new QSpinBox(propertyBox);
    m_arrayCount->setRange(1, 10000);
    m_arrayCount->setValue(1);
    m_arrayCount->setToolTip(QStringLiteral("按阵列偏移向量复制的平行杆数量。"));
    for (auto* spin : m_offset)
        spin->setEnabled(false);
    connect(m_arrayCount, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int count)
            {
                for (auto* spin : m_offset)
                    spin->setEnabled(count > 1);
            });
    m_layer = new QSpinBox(propertyBox);
    m_layer->setRange(-999, 999);
    m_layer->setValue(-1);
    m_layer->setToolTip(QStringLiteral("杆件参加计算的安装级；洞室第一级通常使用 -1，第二级使用 -2。"));
    form->addRow(QStringLiteral("每根杆单元数"), m_segments);
    form->addRow(QStringLiteral("阵列数量"), m_arrayCount);
    form->addRow(QStringLiteral("安装层号"), m_layer);

    auto* create = new QPushButton(QStringLiteral("生成杆单元"), propertyBox);
    form->addRow(create);
    m_actionStatus = new QLabel(propertyBox);
    m_actionStatus->setWordWrap(true);
    form->addRow(QString(), m_actionStatus);
    connect(create, &QPushButton::clicked, this, &BarElementPlacementWidget::createBars);
    root->addWidget(propertyBox);

    auto* recordBox = new QGroupBox(QStringLiteral("已生成杆单元"), this);
    auto* recordLayout = new QVBoxLayout(recordBox);
    auto* recordHintRow = new QHBoxLayout();
    auto* recordHint = new QLabel(
        QStringLiteral("杆单元按普通材料区域管理。请在材料页面通过“关联MATYPE”和“施加到网格”设置材料。"),
        recordBox);
    recordHint->setWordWrap(true);
    auto* refreshMaterials = new QPushButton(QStringLiteral("刷新"), recordBox);
    recordHintRow->addWidget(recordHint, 1);
    recordHintRow->addWidget(refreshMaterials);
    recordLayout->addLayout(recordHintRow);
    m_records = new QTableWidget(0, 5, recordBox);
    m_records->setHorizontalHeaderLabels(
        {QStringLiteral("单元数"), QStringLiteral("材料区域"), QStringLiteral("材料"),
         QStringLiteral("安装层号"), QStringLiteral("操作")});
    m_records->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_records->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_records->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_records->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_records->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_records->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_records->setSelectionMode(QAbstractItemView::SingleSelection);
    m_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordLayout->addWidget(m_records);
    connect(refreshMaterials, &QPushButton::clicked, this,
            &BarElementPlacementWidget::refreshData);
    root->addWidget(recordBox, 1);

    refreshData();
}

QDoubleSpinBox* BarElementPlacementWidget::coordinateSpin(double value)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(-1.0e12, 1.0e12);
    spin->setDecimals(8);
    spin->setValue(value);
    return spin;
}

void BarElementPlacementWidget::beforeLoad()
{
    PickManager::instance().updateStructure();
    PickManager::instance().activate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
    PickManager::instance().setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, true);
    refreshData();
    updatePickedEndpointState();
}

void BarElementPlacementWidget::beforeUnload()
{
    PickManager::instance().deactivate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
}

QJsonObject BarElementPlacementWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray start;
    QJsonArray end;
    QJsonArray offset;
    for (int axis = 0; axis < 3; ++axis)
    {
        start.append(m_start[axis]->value());
        end.append(m_end[axis]->value());
        offset.append(m_offset[axis]->value());
    }
    obj[QStringLiteral("start")] = start;
    obj[QStringLiteral("end")] = end;
    obj[QStringLiteral("array_offset")] = offset;
    obj[QStringLiteral("segments")] = m_segments->value();
    obj[QStringLiteral("array_count")] = m_arrayCount->value();
    obj[QStringLiteral("installation_layer")] = m_layer->value();
    return obj;
}

void BarElementPlacementWidget::loadJson(const QJsonObject& obj)
{
    const auto loadCoordinates = [](const QJsonArray& values, QDoubleSpinBox* spins[3])
    {
        for (int axis = 0; axis < 3 && axis < values.size(); ++axis)
            spins[axis]->setValue(values.at(axis).toDouble());
    };
    loadCoordinates(obj.value(QStringLiteral("start")).toArray(), m_start);
    loadCoordinates(obj.value(QStringLiteral("end")).toArray(), m_end);
    loadCoordinates(obj.value(QStringLiteral("array_offset")).toArray(), m_offset);
    m_segments->setValue(obj.value(QStringLiteral("segments")).toInt(1));
    m_arrayCount->setValue(obj.value(QStringLiteral("array_count")).toInt(1));
    m_layer->setValue(obj.value(QStringLiteral("installation_layer")).toInt(-1));
    refreshRecords();
}

void BarElementPlacementWidget::refreshData()
{
    refreshRecords();
}

void BarElementPlacementWidget::refreshRecords()
{
    m_records->setRowCount(0);
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
        return;
    QMap<int, QString> materialLabels;
    if (auto* materialService =
            ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        const QMap<int, QString> configured = materialService->assignedMaterialLabels();
        for (auto it = configured.constBegin(); it != configured.constEnd(); ++it)
            materialLabels.insert(it.key(), it.value());
    }
    const auto records = service->barElementRecords();
    for (const BarElementRecordInfo& record : records)
    {
        const int row = m_records->rowCount();
        m_records->insertRow(row);
        const QStringList values = {QString::number(record.cellCount),
                                    QString::number(record.matype), QString(),
                                    QString::number(record.installationLayer)};
        for (int column = 0; column < values.size(); ++column)
        {
            if (column != 2)
                m_records->setItem(row, column, new QTableWidgetItem(values.at(column)));
        }
        m_records->item(row, 0)->setData(Qt::UserRole, record.id);

        const QString materialName = materialLabels.value(
            record.matype, QStringLiteral("未关联材料"));
        auto* materialItem = new QTableWidgetItem(materialName);
        materialItem->setTextAlignment(Qt::AlignCenter);
        m_records->setItem(row, 2, materialItem);

        auto* removeButton = new QPushButton(QStringLiteral("删除"), m_records);
        removeButton->setToolTip(QStringLiteral("删除本次生成的全部杆单元。"));
        m_records->setCellWidget(row, 4, removeButton);
        connect(removeButton, &QPushButton::clicked, this, [this, row]()
        {
            m_records->setCurrentCell(row, 0);
            removeSelectedRecord();
        });
    }
}

bool BarElementPlacementWidget::syncPickedEndpoints(bool showWarning)
{
    auto& picker = PickManager::instance();
    const QVector<vtkIdType> ids = picker.getSelectedPointIds(
        QString::fromLatin1(kPickDomain), m_uuid, GridKind::Solid);
    vtkUnstructuredGrid* grid = picker.solidGrid();
    if (!grid || ids.size() != 2 || ids.at(0) < 0 || ids.at(1) < 0 ||
        ids.at(0) >= grid->GetNumberOfPoints() || ids.at(1) >= grid->GetNumberOfPoints())
    {
        if (showWarning)
        {
            QMessageBox::warning(this, QStringLiteral("杆单元端点"),
                                 QStringLiteral("请在模型中恰好点选两个实体网格节点。"));
        }
        return false;
    }

    double xyz[3]{};
    grid->GetPoint(ids.at(0), xyz);
    for (int axis = 0; axis < 3; ++axis)
        m_start[axis]->setValue(xyz[axis]);
    grid->GetPoint(ids.at(1), xyz);
    for (int axis = 0; axis < 3; ++axis)
        m_end[axis]->setValue(xyz[axis]);
    return true;
}

void BarElementPlacementWidget::updatePickedEndpointState()
{
    const QVector<vtkIdType> ids = PickManager::instance().getSelectedPointIds(
        QString::fromLatin1(kPickDomain), m_uuid, GridKind::Solid);
    if (ids.size() == 2 && syncPickedEndpoints(false))
    {
        m_pickStatus->setText(QStringLiteral("已选择 2 个节点，起点和终点坐标已自动填写。"));
    }
    else if (ids.isEmpty())
    {
        m_pickStatus->setText(
            QStringLiteral("未点选节点：可依次点选两个实体节点；手动坐标必须落在实体节点上。"));
    }
    else
    {
        m_pickStatus->setText(
            QStringLiteral("当前已选择 %1 个节点，需要恰好 2 个。可点击“清除点选”重新选择。")
                .arg(ids.size()));
    }
}

void BarElementPlacementWidget::createBars()
{
    const QVector<vtkIdType> selectedPoints = PickManager::instance().getSelectedPointIds(
        QString::fromLatin1(kPickDomain), m_uuid, GridKind::Solid);
    if (!selectedPoints.isEmpty() && !syncPickedEndpoints(true))
        return;

    BarElementPlacement placement;
    for (int axis = 0; axis < 3; ++axis)
    {
        placement.start[axis] = m_start[axis]->value();
        placement.end[axis] = m_end[axis]->value();
        placement.arrayOffset[axis] = m_offset[axis]->value();
    }
    placement.segmentCount = m_segments->value();
    placement.arrayCount = m_arrayCount->value();
    placement.installationLayer = m_layer->value();

    const double dx = placement.end[0] - placement.start[0];
    const double dy = placement.end[1] - placement.start[1];
    const double dz = placement.end[2] - placement.start[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 1.0e-12)
    {
        QMessageBox::warning(this, QStringLiteral("杆单元参数"),
                             QStringLiteral("起点和终点不能重合。"));
        return;
    }
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    QString error;
    if (!service || !service->appendBarElements(placement, &error))
    {
        QMessageBox::warning(this, QStringLiteral("杆单元生成失败"),
                             error.isEmpty() ? QStringLiteral("无法修改当前网格。") : error);
        return;
    }
    PickManager::instance().clearSelection(QString::fromLatin1(kPickDomain), m_uuid,
                                            PickMode::Node);
    refreshRecords();
    if (m_actionStatus)
    {
        m_actionStatus->setText(
            QStringLiteral("已生成 %1 个杆单元。请在材料页面按普通材料流程关联对应的材料区域。")
                .arg(placement.segmentCount * placement.arrayCount));
    }
}

void BarElementPlacementWidget::removeSelectedRecord()
{
    const int row = m_records->currentRow();
    if (row < 0 || !m_records->item(row, 0))
        return;
    const int recordId = m_records->item(row, 0)->data(Qt::UserRole).toInt();
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    QString error;
    if (!service || !service->removeBarElementRecord(recordId, &error))
    {
        QMessageBox::warning(this, QStringLiteral("删除杆单元失败"),
                             error.isEmpty() ? QStringLiteral("无法修改当前网格。") : error);
        return;
    }
    refreshRecords();
    if (m_actionStatus)
    {
        m_actionStatus->setText(
            QStringLiteral("选中的杆单元已删除，未被其他单元使用的节点已同步清理。"));
    }
}
