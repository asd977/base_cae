#include "LinearForcePage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QGridLayout>

#include "ui_LinearForcePage.h"

LinearForcePage::LinearForcePage(QWidget* parent)
    : BasePage("LinearForcePage"), ui(new Ui::LinearForcePage)
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

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);
    setupBatchGroupCombo();
}

void LinearForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Edge);
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
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("线", 1);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LinearForcePage::onDimChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LinearForcePage::onGroupChanged, Qt::UniqueConnection);

    if (auto* combo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(combo, &CustomComboBox::itemHovered, this,
                [this](int idx)
                {
                    onGroupChanged(idx);
                },
                Qt::UniqueConnection);
        connect(combo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    onGroupChanged(ui->comboBoxGroup->currentIndex());
                },
                Qt::UniqueConnection);
    }
    refreshGroupCombo();
}

void LinearForcePage::refreshGroupCombo()
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
        if (dim != 1)
            continue;
        if (dimFilter != 1)
            continue;
        ui->comboBoxGroup->addItem(QString("%1  (id=%2)").arg(it.value()).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void LinearForcePage::onDimChanged(int)
{
    refreshGroupCombo();
}

void LinearForcePage::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    const QVector<qint64> edgeKeys64 = map.bndEdgePhysToEdgeKeys.value(physId);
    QVector<vtkIdType> edgeKeys;
    edgeKeys.reserve(edgeKeys64.size());
    for (qint64 k : edgeKeys64)
        edgeKeys.push_back(vtkIdType(k));

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys,
                                                true);
}
