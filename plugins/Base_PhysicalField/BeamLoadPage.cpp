#include "BeamLoadPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QPushButton>
#include <QVariant>

#include "PickManager.h"
#include "ui_BeamLoadPage.h"

namespace
{
QString boundaryDisplayText(const MeshTagSet& set)
{
    const QString name = set.displayName();
    return QStringLiteral("%1  (dim=%2, id=%3)")
        .arg(name)
        .arg(set.key.dim)
        .arg(set.key.id);
}

MeshTagKey comboBoundaryKey(QComboBox* combo, int index)
{
    if (!combo || index < 0 || index >= combo->count())
        return MeshTagKey{};
    return combo->itemData(index).value<MeshTagKey>();
}

QVector<vtkIdType> pointIdsOfBoundary(const MshPickMap& map, const MeshTagKey& key)
{
    QVector<vtkIdType> out;
    if (!key.isValid())
        return out;

    const MeshTagSet set = map.tagSets.value(key);
    out.reserve(set.pointIds.size());
    for (MeshIdType pid : set.pointIds)
        out.push_back(static_cast<vtkIdType>(pid));
    return out;
}
} // namespace

BeamLoadPage::BeamLoadPage(QWidget* parent) : BasePage("BeamLoadPage"), ui(new Ui::BeamLoadPage)
{
    ui->setupUi(this);

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
    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    Q_UNUSED(NDIM);

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
    qRegisterMetaType<MeshTagKey>("MeshTagKey");

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    QObject::disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BeamLoadPage::onDimChanged, Qt::UniqueConnection);

    connect(ui->btnBatchAdd, &QPushButton::clicked, this,
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
                    const MeshTagKey key = comboBoundaryKey(ui->comboBoxGroup, row);
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid,
                                                               GridKind::Solid,
                                                               pointIdsOfBoundary(map, key));
                },
                Qt::UniqueConnection);

        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  PickMode::Node);
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

    const QVector<MeshTagSet> sets = map.boundaryTagSets(dimFilter);
    for (const MeshTagSet& set : sets)
    {
        if (set.pointIds.isEmpty())
            continue;
        ui->comboBoxGroup->addItem(boundaryDisplayText(set), QVariant::fromValue(set.key));
    }

    ui->comboBoxGroup->blockSignals(false);
}

void BeamLoadPage::onDimChanged(int)
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
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
    const MeshTagKey key = comboBoundaryKey(ui->comboBoxGroup, index);
    const QVector<vtkIdType> pids = pointIdsOfBoundary(map, key);
    if (pids.isEmpty())
        return;

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids,
                                                false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
}
