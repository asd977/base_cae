#include "BeamLoadPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
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

    qDebug() << "[BeamLoadPage] beforeLoad activate Edge pick, uuid=" << m_uuid;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();
}

void BeamLoadPage::beforeUnload()
{
    qDebug() << "[BeamLoadPage] beforeUnload deactivate Edge pick, uuid=" << m_uuid;
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

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BeamLoadPage::onDimChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BeamLoadPage::onGroupChanged, Qt::UniqueConnection);
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
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
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
    const int dim = map.physIdToDim.value(physId, -1);
    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1)
        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2)
        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else
        return;

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
        pids.push_back(vtkIdType(pid));
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, true);
}
