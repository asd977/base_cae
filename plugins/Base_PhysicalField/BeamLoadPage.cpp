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
}

BeamLoadPage::BeamLoadPage(QWidget* parent) : BasePage("BeamLoadPage"), ui(new Ui::BeamLoadPage)
{
    ui->setupUi(this);
    ui->doubleSpinBoxA->setDecimals(6);ui->doubleSpinBoxA->setRange(0,1.e9);
    ui->doubleSpinBoxP->setDecimals(6);ui->doubleSpinBoxP->setRange(-1.e15,1.e15);

    ui->spinBoxLDTYPE->setRange(1, 4);
    ui->spinBoxIDIR->setRange(1, 3);
    ui->spinBoxLDTYPE->setValue(1);
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
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();

    // 2D 时禁用 ZP（写出时固定为 0）
    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);

    qInfo() << "[BeamLoadPage] beforeLoad node pick checkbox init, uuid=" << m_uuid;
    pickManager.activate(getPageType(), m_uuid, PickMode::Node);
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
                     });
    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    QObject::connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         PickManager& pickManager = PickManager::instance();
                         if (on)
                         {
                             pickManager.updateStructure();
                             pickManager.activate(getPageType(), m_uuid, PickMode::Node);
                         }
                         pickManager.setClickPickEnabled(getPageType(), m_uuid, on);
                     });
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
}

void BeamLoadPage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    qInfo() << "[BeamLoadPage] beforeUnload deactivate node pick, uuid=" << m_uuid;
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
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
    if (!ui)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

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
            });

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0)
                        return;
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Line,
                                                               toVtkIds(meshSvc->pointIdsOfTag(key)));
                });
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                });
    }

    refreshGroupCombo();
}



void BeamLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    const bool showDimPrefix = (dimFilter < 0);

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, showDimPrefix), QVariant::fromValue(set.key));
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
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Line,
                                                toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}

