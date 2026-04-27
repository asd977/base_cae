#include "FaceLoadPage.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDebug>
#include <QPushButton>
#include <QVariant>
#include <algorithm>

#include "PickManager.h"
#include "ui_FaceLoadPage.h"

namespace
{
QString boundaryTagDisplayText(const MeshTagSet& set)
{
    QString name = set.name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("%1_%2").arg(set.key.category).arg(set.key.id);

    const QString dimName = (set.key.dim == 1) ? QStringLiteral("线") : QStringLiteral("面");
    return QStringLiteral("%1  (%2, id=%3)").arg(name, dimName).arg(set.key.id);
}

MeshTagKey comboTagKey(QComboBox* combo, int index)
{
    if (!combo || index < 0 || index >= combo->count())
        return MeshTagKey{};

    const QVariant v = combo->itemData(index);
    if (!v.isValid())
        return MeshTagKey{};

    return v.value<MeshTagKey>();
}

QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}
} // namespace

FaceLoadPage::FaceLoadPage(QWidget* parent) : BasePage("FaceLoadPage"), ui(new Ui::FaceLoadPage)
{
    Q_UNUSED(parent);
    qRegisterMetaType<MeshTagKey>("MeshTagKey");
    ui->setupUi(this);
    setupBatchGroupCombo();
}

FaceLoadPage::~FaceLoadPage()
{
    delete ui;
}

void FaceLoadPage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
    setupBatchGroupCombo();
}

void FaceLoadPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

double FaceLoadPage::fxValue()
{
    return ui->lineEditX->text().toDouble();
}

double FaceLoadPage::fyValue()
{
    return ui->lineEditY->text().toDouble();
}

double FaceLoadPage::fzValue()
{
    return ui->lineEditZ->text().toDouble();
}

void FaceLoadPage::setupBatchGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    const MshPickMap map = meshSvc ? meshSvc->mshPickMapSolid() : MshPickMap{};

    const bool hasEdges = !map.boundaryTagSets(1).isEmpty();
    const bool hasFaces = !map.boundaryTagSets(2).isEmpty();

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    if (hasEdges)
        ui->comboBoxDim->addItem(QStringLiteral("线"), 1);
    if (hasFaces)
        ui->comboBoxDim->addItem(QStringLiteral("面"), 2);
    if (ui->comboBoxDim->count() == 0)
        ui->comboBoxDim->addItem(QStringLiteral("面"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    QObject::disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &FaceLoadPage::onDimChanged, Qt::UniqueConnection);

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
                [this](int row)
                {
                    if (row < 0)
                        return;

                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    const MeshTagSet set = map.tagSets.value(key);

                    if (!set.key.isValid())
                        return;

                    if (set.key.dim == 1)
                    {
                        PickManager::instance().setPreviewEdgeKeys(
                            getPageType(), m_uuid, GridKind::Solid, toVtkIds(set.edgeKeys));
                    }
                    else if (set.key.dim == 2)
                    {
                        PickManager::instance().setPreviewFaceKeys(
                            getPageType(), m_uuid, GridKind::Solid, toVtkIds(set.faceKeys));
                    }
                },
                Qt::UniqueConnection);

        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
                },
                Qt::UniqueConnection);
    }

    refreshGroupCombo();
}

void FaceLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    QVector<MeshTagSet> sets = map.boundaryTagSets(dimFilter);
    std::sort(sets.begin(), sets.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });

    for (const MeshTagSet& set : sets)
    {
        if (dimFilter == 1 && set.edgeKeys.isEmpty())
            continue;
        if (dimFilter == 2 && set.faceKeys.isEmpty())
            continue;

        ui->comboBoxGroup->addItem(boundaryTagDisplayText(set), QVariant::fromValue(set.key));
    }

    ui->comboBoxGroup->blockSignals(false);
}

void FaceLoadPage::onDimChanged(int)
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    refreshGroupCombo();
}

void FaceLoadPage::onGroupChanged(int index)
{
    if (index < 0)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    const MeshTagSet set = map.tagSets.value(key);

    if (!set.key.isValid())
        return;

    if (set.key.dim == 1)
    {
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    toVtkIds(set.edgeKeys), false);
        PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
    }
    else if (set.key.dim == 2)
    {
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    toVtkIds(set.faceKeys), false);
        PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    }
}
