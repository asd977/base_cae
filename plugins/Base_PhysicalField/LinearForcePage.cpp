#include "LinearForcePage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGridLayout>

#include "ui_LinearForcePage.h"


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    case 0: return QStringLiteral("点边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkKeys(const QVector<MeshIdType>& ids)
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

static MeshTagSet findBoundarySet(MeshModuleControllerService* meshSvc, const MeshTagKey& key)
{
    if (!meshSvc || !key.isValid())
        return {};
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(key.dim);
    for (const MeshTagSet& set : sets)
        if (set.key == key)
            return set;
    return {};
}
}

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
    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    QObject::connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
                     });
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    setupBatchGroupCombo();
}

void LinearForcePage::beforeUnload()
{
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
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
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LinearForcePage::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this](){ onGroupChanged(ui->comboBoxGroup->currentIndex()); });
    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0) return;
                    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, row));
                    if (set.key.dim == 1)
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.edgeKeys));
                    
                });
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this](){ const int dim = ui->comboBoxDim->currentData().toInt(); PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge); });
    }
    refreshGroupCombo();
}



void LinearForcePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(1);
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
    {
        if (set.edgeKeys.isEmpty()) continue;
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, false), QVariant::fromValue(set.key));
    }
    ui->comboBoxGroup->blockSignals(false);
}


void LinearForcePage::onDimChanged(int)
{
    refreshGroupCombo();
}


void LinearForcePage::onGroupChanged(int index)
{
    if (index < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, index));
    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.edgeKeys), false);
}

