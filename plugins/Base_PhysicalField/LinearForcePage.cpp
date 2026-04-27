#include "LinearForcePage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QPushButton>
#include <QGridLayout>

#include "ui_LinearForcePage.h"

namespace
{
QString boundaryTagText(const MeshTagSet& set)
{
    const QString name = set.displayName();
    return QStringLiteral("%1  (dim=%2, id=%3)")
        .arg(name)
        .arg(set.key.dim)
        .arg(set.key.id);
}

MeshTagKey comboTagKey(QComboBox* combo, int index = -1)
{
    if (!combo)
        return MeshTagKey{};
    const QVariant v = (index >= 0) ? combo->itemData(index) : combo->currentData();
    if (!v.isValid())
        return MeshTagKey{};
    return v.value<MeshTagKey>();
}

QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& values)
{
    QVector<vtkIdType> out;
    out.reserve(values.size());
    for (MeshIdType v : values)
        out.push_back(static_cast<vtkIdType>(v));
    return out;
}

QVector<vtkIdType> pointIdsOfTag(const MshPickMap& map, const MeshTagKey& key)
{
    const MeshTagSet set = map.tagSets.value(key);
    return toVtkIds(set.pointIds);
}

QVector<vtkIdType> edgeKeysOfTag(const MshPickMap& map, const MeshTagKey& key)
{
    const MeshTagSet set = map.tagSets.value(key);
    return toVtkIds(set.edgeKeys);
}

QVector<vtkIdType> faceKeysOfTag(const MshPickMap& map, const MeshTagKey& key)
{
    const MeshTagSet set = map.tagSets.value(key);
    return toVtkIds(set.faceKeys);
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

    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    QObject::disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LinearForcePage::onDimChanged, Qt::UniqueConnection);

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
                    if (row < 0)
                        return;
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;
                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid,
                                                               GridKind::Solid,
                                                               edgeKeysOfTag(map, key));
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
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

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    const QVector<MeshTagSet> sets = map.boundaryTagSets(1);
    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryTagText(set), QVariant::fromValue(set.key));

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
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    if (!key.isValid())
        return;

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                edgeKeysOfTag(map, key), false);
}
