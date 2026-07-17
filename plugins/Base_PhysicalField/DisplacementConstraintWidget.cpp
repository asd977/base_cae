#include "DisplacementConstraintWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhySicalFieldService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>

#include <algorithm>

#include "ui_DisplacementConstraintWidget.h"

// VTK
#include <PickManager.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 0: return QStringLiteral("点边界");
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}

static MeshTagKey comboTagKey(QComboBox* combo, int row)
{
    if (!combo || row < 0)
        return {};
    return combo->itemData(row).value<MeshTagKey>();
}

static QString boundaryDisplayText(MeshModuleControllerService* meshSvc, const MeshTagSet& set, bool showDimPrefix)
{
    QString text = meshSvc ? meshSvc->displayTextOfTag(set.key) : set.name.trimmed();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("%1%2").arg(boundaryDimText(set.key.dim)).arg(set.key.id);
    return showDimPrefix ? QStringLiteral("%1：%2").arg(boundaryDimText(set.key.dim), text) : text;
}

static void initConstraintCombo(QComboBox* combo)
{
    combo->clear();
    combo->addItem(QStringLiteral("固定"), QStringLiteral("0"));
    combo->addItem(QStringLiteral("自由"), QStringLiteral("1"));
    combo->addItem(QStringLiteral("悬空杆件节点（专用）"), QStringLiteral("-10000"));
    combo->setCurrentIndex(0);
}

static void setConstraintValue(QComboBox* combo, const QString& value)
{
    const int index = combo->findData(value.trimmed());
    combo->setCurrentIndex(index >= 0 ? index : 0);
}
}

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget* parent)
    : BasePage("DisplacementConstraintWidget"), ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
    initConstraintCombo(ui->UXLineEdit);
    initConstraintCombo(ui->UYLineEdit);
    initConstraintCombo(ui->UZLineEdit);

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);
    ui->UZLineEdit->setEnabled(!is2D);
    ui->UZLineEdit->setToolTip(is2D ? QStringLiteral("二维模型不使用 Z 方向") : QString());

    connect(ui->btnSetAllBarsSuspended, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onSetAllBarsSuspended);
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
}

void DisplacementConstraintWidget::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &DisplacementConstraintWidget::onNodePicked, Qt::UniqueConnection);

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());

    // 防止页面来回切换后重复连接，造成批量选逻辑多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    // 维度变化时仅刷新候选组
    QObject::connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DisplacementConstraintWidget::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0)
                        return;
                    const MeshTagKey key = ui->comboBoxGroup->itemData(row).value<MeshTagKey>();
                    const QVector<MeshIdType> tagPids = meshSvc->pointIdsOfTag(key);
                    QVector<vtkIdType> pids;
                    pids.reserve(tagPids.size());
                    for (MeshIdType pid : tagPids)
                        pids.push_back(static_cast<vtkIdType>(pid));
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                               pids);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                },
                Qt::UniqueConnection);
    }

    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
            },
            Qt::UniqueConnection);

    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
}


void DisplacementConstraintWidget::refreshGroupCombo(int dimFilter)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    const bool showDimPrefix = (dimFilter < 0);
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, showDimPrefix), QVariant::fromValue(set.key));
    ui->comboBoxGroup->blockSignals(false);
}


void DisplacementConstraintWidget::onDimChanged(int)
{
    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());
}


void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}



QJsonObject DisplacementConstraintWidget::saveJson()
{
    QJsonObject obj;
    obj["ux"] = ui->UXLineEdit->currentData().toString();
    obj["uy"] = ui->UYLineEdit->currentData().toString();
    obj["uz"] = ui->UZLineEdit->currentData().toString();
    return obj;
}

void DisplacementConstraintWidget::onSetAllBarsSuspended()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    vtkUnstructuredGrid* lineGrid = pickManager.lineGrid();
    if (!lineGrid || lineGrid->GetNumberOfCells() == 0)
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QSet<vtkIdType> usedPointIds;
    for (vtkIdType cellId = 0; cellId < lineGrid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = lineGrid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 1)
            continue;
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            usedPointIds.insert(cell->GetPointId(i));
    }

    if (usedPointIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QVector<vtkIdType> pointIds;
    pointIds.reserve(usedPointIds.size());
    for (vtkIdType pointId : usedPointIds)
        pointIds.push_back(pointId);
    std::sort(pointIds.begin(), pointIds.end());

    setConstraintValue(ui->UXLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UYLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UZLineEdit, QStringLiteral("-10000"));

    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, {}, true);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, pointIds, true);

    qInfo().noquote()
        << QStringLiteral("__RAW__已自动选择 %1 个杆件节点并设置为悬空。")
               .arg(pointIds.size());
}

void DisplacementConstraintWidget::loadJson(const QJsonObject& obj)
{
    setConstraintValue(ui->UXLineEdit,
                       obj.value("ux").toString(ui->UXLineEdit->currentData().toString()));
    setConstraintValue(ui->UYLineEdit,
                       obj.value("uy").toString(ui->UYLineEdit->currentData().toString()));
    setConstraintValue(ui->UZLineEdit,
                       obj.value("uz").toString(ui->UZLineEdit->currentData().toString()));
}

void DisplacementConstraintWidget::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
                        &DisplacementConstraintWidget::onNodePicked);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

int DisplacementConstraintWidget::uxFlag()
{
    return ui->UXLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uyFlag()
{
    return ui->UYLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uzFlag()
{
    return ui->UZLineEdit->currentData().toInt();
}

void DisplacementConstraintWidget::onNodePicked(const QString& domain, const QString& activeUuid,
                                                GridKind, vtkIdType, int nodeId, double x, double y,
                                                double z, const QString& prevUuid)
{
    if (domain != getPageType())
        return;
    if (activeUuid != m_uuid)
        return;

    // 更新 UI：node_id + 坐标
    ui->IDLabel->setText(QString::number(nodeId));
    ui->XLabel->setText(QString::number(x, 'g', 12));
    ui->YLabel->setText(QString::number(y, 'g', 12));
    ui->ZLabel->setText(QString::number(z, 'g', 12));

    if (!prevUuid.isEmpty())
    {
        auto* pf = ServiceRegistry::instance().getService<PhysicalFieldService>();
        const QString prevName = pf ? pf->pageTitleByUuid(prevUuid) : QString();
        ui->PrevUuidLabel->setText(prevName.isEmpty() ? prevUuid : prevName);
    }
    else
    {
        ui->PrevUuidLabel->clear();
    }
}
