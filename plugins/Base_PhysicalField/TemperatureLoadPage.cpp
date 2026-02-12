#include "TemperatureLoadPage.h"
#include "ui_TemperatureLoadPage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include "PickManager.h"
#include <QComboBox>
#include <QVBoxLayout>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

TemperatureLoadPage::TemperatureLoadPage(QWidget* parent)
    : BasePage("TemperatureLoadPage")
{
    ui = new Ui::TemperatureLoadPage;
    ui->setupUi(this);
    setupBatchGroupCombo();

}

TemperatureLoadPage::~TemperatureLoadPage() = default;

void TemperatureLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();
}

void TemperatureLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

double TemperatureLoadPage::alfaValue() const
{
    return ui->lineEditAlfa->text().toDouble();
}

double TemperatureLoadPage::dtValue() const
{
    return ui->lineEditDT->text().toDouble();
}

void TemperatureLoadPage::setupBatchGroupCombo()
{
    if (!ui) return;

    if (!m_groupCombo) {
        auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
        if (!layout) return;
        m_groupCombo = new QComboBox(this);
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
            this, &TemperatureLoadPage::onGroupChanged, Qt::UniqueConnection);
}

void TemperatureLoadPage::onGroupChanged(int index)
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
