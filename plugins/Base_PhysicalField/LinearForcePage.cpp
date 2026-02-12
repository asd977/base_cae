#include "LinearForcePage.h"
#include "ui_LinearForcePage.h"

#include <PickManager.h>
#include <QComboBox>
#include <QGridLayout>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

LinearForcePage::LinearForcePage(QWidget *parent)
    : BasePage("LinearForcePage")
    , ui(new Ui::LinearForcePage)
{
    ui->setupUi(this);
    setupBatchGroupCombo();
}

LinearForcePage::~LinearForcePage()
{
    delete ui;
}

void LinearForcePage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);
    setupBatchGroupCombo();

}

void LinearForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(),m_uuid,PickMode::Edge);
}

double LinearForcePage::fxValue()
{
    return ui->lineEditX->text().toDouble();
}

double LinearForcePage::fyValue()
{
    return ui->lineEditY->text().toDouble();
}

double LinearForcePage::fzValue()
{
    return ui->lineEditZ->text().toDouble();
}

void LinearForcePage::setupBatchGroupCombo()
{
    if (!m_groupCombo) {
        auto* layout = qobject_cast<QGridLayout*>(this->layout());
        if (!layout) return;
        m_groupCombo = new QComboBox(this);
        layout->addWidget(m_groupCombo, 4, 0, 1, 2);
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
        if (map.physIdToDim.value(physId, -1) != 1) continue;
        m_groupCombo->addItem(QString("%1  (dim=1, id=%2)").arg(it.value()).arg(physId), physId);
    }

    m_groupCombo->blockSignals(false);
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LinearForcePage::onGroupChanged, Qt::UniqueConnection);
}

void LinearForcePage::onGroupChanged(int index)
{
    if (!m_groupCombo || index < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = m_groupCombo->itemData(index).toInt();

    const QVector<qint64> edgeKeys64 = map.bndEdgePhysToEdgeKeys.value(physId);
    QVector<vtkIdType> edgeKeys;
    edgeKeys.reserve(edgeKeys64.size());
    for (qint64 k : edgeKeys64) edgeKeys.push_back(vtkIdType(k));

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys, true);
}
