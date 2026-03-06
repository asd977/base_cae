#include "PointForcePage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "PickManager.h"
#include "ui_PointForcePage.h"

PointForcePage::PointForcePage(QWidget* parent)
    : BasePage("PointForcePage"), ui(new Ui::PointForcePage)
{
    ui->setupUi(this);
    setupBatchGroupCombo();
}

PointForcePage::~PointForcePage() = default;

void PointForcePage::beforeLoad()
{
    PickManager::instance();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is3D = (NDIM == 3);

    // 1. 对于 3D 特有的参数，2D 时置灰 (Enabled = false)
    ui->spinFz->setEnabled(is3D);
    ui->spinMx->setEnabled(is3D);
    ui->spinMy->setEnabled(is3D);

    // 建议：对应的 Label 也同步置灰，视觉上更统一
    ui->labelFz->setEnabled(is3D);
    ui->labelMx->setEnabled(is3D);
    ui->labelMy->setEnabled(is3D);

    // 2. 对于 Mz/M，始终可用，仅根据维度切换标签文字
    ui->spinMz->setEnabled(true);
    ui->labelMz->setText(is3D ? "MZ" : "M");
}

void PointForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

double PointForcePage::fxValue() const
{
    return ui->spinFx->value();
}
double PointForcePage::fyValue() const
{
    return ui->spinFy->value();
}
double PointForcePage::fzValue() const
{
    return ui->spinFz->value();
}

double PointForcePage::mxValue() const
{
    return ui->spinMx->value();
}
double PointForcePage::myValue() const
{
    return ui->spinMy->value();
}
double PointForcePage::mzValue() const
{
    return ui->spinMz->value();
}

void PointForcePage::setupBatchGroupCombo()
{
    if (!ui)
        return;
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PointForcePage::onDimChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PointForcePage::onGroupChanged, Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    onGroupChanged(row);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    onGroupChanged(ui->comboBoxGroup->currentIndex());
                },
                Qt::UniqueConnection);
    }

    refreshGroupCombo();
}

void PointForcePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dim != dimFilter)
            continue;
        ui->comboBoxGroup->addItem(
            QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void PointForcePage::onDimChanged(int)
{
    refreshGroupCombo();
}

void PointForcePage::onGroupChanged(int index)
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
