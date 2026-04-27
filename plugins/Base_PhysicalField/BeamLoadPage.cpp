#include "BeamLoadPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDebug>

#include "PickManager.h"
#include "ui_BeamLoadPage.h"

BeamLoadPage::BeamLoadPage(QWidget* parent) : BasePage("BeamLoadPage"), ui(new Ui::BeamLoadPage)
{
    ui->setupUi(this);

    // 一些默认值（你可按需要改）
    ui->spinBoxLDTYPE->setValue(0);
    ui->doubleSpinBoxA->setValue(0.0);
    ui->doubleSpinBoxP->setValue(0.0);
    ui->spinBoxIDIR->setValue(1);

    setupBatchGroupCombo();
}

BeamLoadPage::~BeamLoadPage()
{
    delete ui;
}

void BeamLoadPage::beforeLoad()
{
    PickManager::instance();

    // 2D 时禁用 ZP（写出时固定为 0）
    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);

    qDebug() << "[BeamLoadPage] beforeLoad node pick checkbox init, uuid=" << m_uuid;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();

    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     [this](const QString& domain, const QString& activeUuid, GridKind, vtkIdType,
                            int nodeId, double x, double y, double z, const QString& prevUuid)
                     {
                         if (domain != getPageType() || activeUuid != m_uuid)
                             return;
                         ui->labelNodeIdValue->setText(QString::number(nodeId));
                         ui->labelXValue->setText(QString::number(x, 'g', 12));
                         ui->labelYValue->setText(QString::number(y, 'g', 12));
                         ui->labelZValue->setText(QString::number(z, 'g', 12));
                         ui->labelPrevOwnerValue->setText(prevUuid);
                     },
                     Qt::UniqueConnection);
    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    QObject::connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
                     },
                     Qt::UniqueConnection);
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
}

void BeamLoadPage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    qDebug() << "[BeamLoadPage] beforeUnload deactivate node pick, uuid=" << m_uuid;
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

int BeamLoadPage::ldTypeValue() const
{
    return ui->spinBoxLDTYPE->value();
}
double BeamLoadPage::aValue() const
{
    return ui->doubleSpinBoxA->value();
}
double BeamLoadPage::pValue() const
{
    return ui->doubleSpinBoxP->value();
}
int BeamLoadPage::idirValue() const
{
    return ui->spinBoxIDIR->value();
}

void BeamLoadPage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    // 防止页面切换后重复连接导致槽函数被多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BeamLoadPage::onDimChanged, Qt::UniqueConnection);
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
                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = map.physicalDim(physId);
                    QVector<int> pointIdsInt;
                    if (dim == 0)
                        pointIdsInt = map.physicalPointIds(physId, 0);
                    else if (dim == 1)
                        pointIdsInt = map.physicalPointIds(physId, 1);
                    else if (dim == 2)
                        pointIdsInt = map.physicalPointIds(physId, 2);
                    QVector<vtkIdType> pids;
                    for (int pid : pointIdsInt)
                        pids.push_back(vtkIdType(pid));
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid, pids);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                },
                Qt::UniqueConnection);
    }
    refreshGroupCombo();
}

void BeamLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    const auto nameMap = map.physicalNameMap();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physicalDim(physId);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dimFilter != dim)
            continue;
        ui->comboBoxGroup->addItem(
            QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void BeamLoadPage::onDimChanged(int)
{
    refreshGroupCombo();
}

void BeamLoadPage::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physicalDim(physId);
    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.physicalPointIds(physId, 0);
    else if (dim == 1)
        pointIdsInt = map.physicalPointIds(physId, 1);
    else if (dim == 2)
        pointIdsInt = map.physicalPointIds(physId, 2);
    else
        return;

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
        pids.push_back(vtkIdType(pid));
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, false);
}
