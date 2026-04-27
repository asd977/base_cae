#include "PointForcePage.h"

#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>

#include "PickManager.h"
#include "ui_PointForcePage.h"

namespace
{
QString boundaryTagText(const MeshTagSet& set)
{
    QString name = set.name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("%1_%2").arg(set.key.category).arg(set.key.id);

    QString dimName;
    switch (set.key.dim)
    {
    case 0:
        dimName = QStringLiteral("点");
        break;
    case 1:
        dimName = QStringLiteral("线");
        break;
    case 2:
        dimName = QStringLiteral("面");
        break;
    default:
        dimName = QStringLiteral("dim%1").arg(set.key.dim);
        break;
    }

    return QStringLiteral("%1  (%2, id=%3)").arg(name, dimName).arg(set.key.id);
}

MeshTagKey comboTagKey(QComboBox* combo, int index)
{
    if (!combo || index < 0 || index >= combo->count())
        return MeshTagKey{};

    const QVariant data = combo->itemData(index);
    if (!data.isValid())
        return MeshTagKey{};

    return data.value<MeshTagKey>();
}

QVector<vtkIdType> vtkPointIdsOfBoundaryTag(const MshPickMap& map, const MeshTagKey& key)
{
    QVector<vtkIdType> out;
    if (!key.isValid())
        return out;

    const MeshTagSet set = map.tagSets.value(key);
    out.reserve(set.pointIds.size());
    for (MeshIdType pid : set.pointIds)
    {
        if (pid >= 0)
            out.push_back(static_cast<vtkIdType>(pid));
    }
    return out;
}
} // namespace

PointForcePage::PointForcePage(QWidget* parent)
    : BasePage("PointForcePage"), ui(new Ui::PointForcePage)
{
    Q_UNUSED(parent);

    ui->setupUi(this);
    setupBatchGroupCombo();
}

PointForcePage::~PointForcePage() = default;

void PointForcePage::beforeLoad()
{
    PickManager::instance();
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

    const int ndim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is3D = (ndim == 3);

    ui->spinFz->setEnabled(is3D);
    ui->spinMx->setEnabled(is3D);
    ui->spinMy->setEnabled(is3D);

    ui->labelFz->setEnabled(is3D);
    ui->labelMx->setEnabled(is3D);
    ui->labelMy->setEnabled(is3D);

    ui->spinMz->setEnabled(true);
    ui->labelMz->setText(is3D ? QStringLiteral("MZ") : QStringLiteral("M"));
}

void PointForcePage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
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
    ui->comboBoxDim->addItem(QStringLiteral("全部维度"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("dim0 点"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("dim1 线"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("dim2 面"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    QObject::disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PointForcePage::onDimChanged, Qt::UniqueConnection);

    connect(ui->btnBatchAdd, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0 || !ui)
                        return;

                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;

                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    const QVector<vtkIdType> pids = vtkPointIdsOfBoundaryTag(map, key);
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid, pids);
                },
                Qt::UniqueConnection);

        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                },
                Qt::UniqueConnection);
    }

    refreshGroupCombo();
}

void PointForcePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || !ui)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();

    QVector<MeshTagSet> sets = map.boundaryTagSets(dimFilter);
    std::sort(sets.begin(), sets.end(), [](const MeshTagSet& a, const MeshTagSet& b)
              {
                  if (a.key.dim != b.key.dim)
                      return a.key.dim < b.key.dim;
                  if (a.key.category != b.key.category)
                      return a.key.category < b.key.category;
                  return a.key.id < b.key.id;
              });

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (const MeshTagSet& set : sets)
    {
        if (!set.key.isValid() || set.key.dim < 0 || set.key.dim > 2)
            continue;
        if (set.pointIds.isEmpty())
            continue;
        ui->comboBoxGroup->addItem(boundaryTagText(set), QVariant::fromValue(set.key));
    }

    ui->comboBoxGroup->blockSignals(false);
}

void PointForcePage::onDimChanged(int)
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    refreshGroupCombo();
}

void PointForcePage::onGroupChanged(int index)
{
    if (index < 0 || !ui)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    const QVector<vtkIdType> pids = vtkPointIdsOfBoundaryTag(map, key);
    if (pids.isEmpty())
        return;

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
}
