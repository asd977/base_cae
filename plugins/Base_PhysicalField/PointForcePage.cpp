#include "PointForcePage.h"
#include "ui_PointForcePage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <ServiceRegistry.h>
#include <PublicVariables.h>

#include "PickManager.h"
#include <PublicConstants.h>
#include <services/MeshModuleControllerService.h>
#include <QComboBox>
#include <QVBoxLayout>
#include <struct/MeshMap.h>

PointForcePage::PointForcePage(QWidget* parent)
    : BasePage("PointForcePage")
    , ui(new Ui::PointForcePage)
{
    ui->setupUi(this);
    setupBatchGroupCombo();

}

PointForcePage::~PointForcePage() = default;

void PointForcePage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

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

double PointForcePage::fxValue() const { return ui->spinFx->value(); }
double PointForcePage::fyValue() const { return ui->spinFy->value(); }
double PointForcePage::fzValue() const { return ui->spinFz->value(); }

double PointForcePage::mxValue() const { return ui->spinMx->value(); }
double PointForcePage::myValue() const { return ui->spinMy->value(); }
double PointForcePage::mzValue() const { return ui->spinMz->value(); }

void PointForcePage::setupBatchGroupCombo()
{
    if (!ui) return;

    if (!m_groupCombo) {
        auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
        if (!layout) return;
        m_groupCombo = new QComboBox(this);
        m_groupCombo->setToolTip("批量选择分组");
        layout->insertWidget(0, m_groupCombo);
    }

    m_groupCombo->blockSignals(true);
    m_groupCombo->clear();

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        m_groupCombo->blockSignals(false);
        return;
    }

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2) continue;
        m_groupCombo->addItem(QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }

    m_groupCombo->blockSignals(false);
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PointForcePage::onGroupChanged, Qt::UniqueConnection);
}

void PointForcePage::onGroupChanged(int index)
{
    if (!m_groupCombo || index < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    const int physId = m_groupCombo->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);
    QVector<int> pointIdsInt;
    if (dim == 0) pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1) pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2) pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else return;

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt) pids.push_back(vtkIdType(pid));
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, true);
}
