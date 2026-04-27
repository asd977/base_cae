#include "SeepageBoundaryPage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTextStream>
#include <QVariant>
#include <algorithm>

#include "ui_SeepageBoundaryPage.h"

namespace
{
QString boundaryDisplayText(const MeshTagSet& set)
{
    return QStringLiteral("%1  (dim=%2, id=%3)")
    .arg(set.displayName())
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

SeepageBoundaryPage::SeepageBoundaryPage(const QString& pageType, const QString& boundaryTitle,
                                         const QString& exportFileName, QWidget* parent)
    : BasePage(pageType), ui(new Ui::SeepageBoundaryPage), m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName)
{
    ui->setupUi(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->labelFileNameValue->setText(exportFileName);
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &SeepageBoundaryPage::onClearClicked);
    ui->tableNodes->setVisible(false);
}

SeepageBoundaryPage::~SeepageBoundaryPage()
{
    delete ui;
}

void SeepageBoundaryPage::beforeLoad()
{
    setupBatchGroupCombo();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void SeepageBoundaryPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

QVector<int> SeepageBoundaryPage::selectedNodeIds()
{
    PickManager& pm = PickManager::instance();
    pm.updateStructure();

    auto collectPickedIds = [&](GridKind kind)
    {
        QVector<int> out;
        const QVector<vtkIdType> picked = pm.getIdList(getPageType(), m_uuid, PickMode::Node, kind);
        out.reserve(picked.size());
        for (vtkIdType pid : picked)
            out.push_back(static_cast<int>(pid));
        return out;
    };

    QSet<int> merged;
    for (int id : collectPickedIds(GridKind::Solid))
        merged.insert(id);
    for (int id : collectPickedIds(GridKind::Line))
        merged.insert(id);

    QVector<int> result;
    result.reserve(merged.size());
    for (int id : merged)
        result.push_back(id);

    std::sort(result.begin(), result.end());
    return result;
}

bool SeepageBoundaryPage::exportDatFile()
{
    const QVector<int> ids = selectedNodeIds();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");

    QFile file(wd.filePath("PREP/" + m_exportFileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << ids.size() << "\n";
    for (int i = 0; i < ids.size(); ++i)
        os << ids[i] << " " << (i + 1) << "\n";

    file.close();
    return true;
}

void SeepageBoundaryPage::onClearClicked()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
}

void SeepageBoundaryPage::setupBatchGroupCombo()
{
    if (!ui)
        return;

    qRegisterMetaType<MeshTagKey>("MeshTagKey");

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(tr("全部维度"), -1);
    ui->comboBoxDim->addItem(tr("dim0 点"), 0);
    ui->comboBoxDim->addItem(tr("dim1 线"), 1);
    ui->comboBoxDim->addItem(tr("dim2 面"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    disconnect(ui->btnBatchAdd, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SeepageBoundaryPage::onDimChanged, Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0 || !ui)
                        return;

                    auto* meshSvc =
                        ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
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

    connect(ui->btnBatchAdd, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);

    refreshGroupCombo();
}

void SeepageBoundaryPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || !ui)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();

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

void SeepageBoundaryPage::onDimChanged(int)
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    refreshGroupCombo();
}

void SeepageBoundaryPage::onGroupChanged(int index)
{
    if (index < 0 || !ui)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MeshTagKey key = comboBoundaryKey(ui->comboBoxGroup, index);
    const QVector<vtkIdType> pointIds = pointIdsOfBoundary(map, key);
    if (pointIds.isEmpty())
        return;

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pointIds,
                                                false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
}

QJsonObject SeepageBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj["dim_index"] = ui->comboBoxDim->currentIndex();

    const MeshTagKey key = comboBoundaryKey(ui->comboBoxGroup, ui->comboBoxGroup->currentIndex());
    obj["group_category"] = key.category;
    obj["group_dim"] = key.dim;
    obj["group_id"] = key.id;

    auto toJson = [](const QVector<vtkIdType>& ids)
    {
        QJsonArray arr;
        for (auto id : ids)
            arr.append(static_cast<qint64>(id));
        return arr;
    };

    obj["picked_solid"] =
        toJson(PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid));
    obj["picked_line"] =
        toJson(PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Line));
    return obj;
}

void SeepageBoundaryPage::loadJson(const QJsonObject& obj)
{
    const int dimIndex = obj.value("dim_index").toInt(0);
    ui->comboBoxDim->setCurrentIndex(dimIndex);

    MeshTagKey savedKey;
    savedKey.category = obj.value("group_category").toString();
    savedKey.dim = obj.value("group_dim").toInt(-1);
    savedKey.id = obj.value("group_id").toInt(0);

    for (int i = 0; i < ui->comboBoxGroup->count(); ++i)
    {
        if (comboBoundaryKey(ui->comboBoxGroup, i) == savedKey)
        {
            const QSignalBlocker blocker(ui->comboBoxGroup);
            ui->comboBoxGroup->setCurrentIndex(i);
            break;
        }
    }

    auto fromJson = [](const QJsonArray& arr)
    {
        QVector<vtkIdType> out;
        out.reserve(arr.size());
        for (const auto& v : arr)
            out.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        return out;
    };

    const auto solidIds = fromJson(obj.value("picked_solid").toArray());
    const auto lineIds = fromJson(obj.value("picked_line").toArray());

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, solidIds,
                                                true);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, lineIds,
                                                true);
}
