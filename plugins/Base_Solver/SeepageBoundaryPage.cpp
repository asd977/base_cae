#include "SeepageBoundaryPage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
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
#include <algorithm>

#include "ui_SeepageBoundaryPage.h"

#include "SolverUiLayoutHelper.h"
SeepageBoundaryPage::SeepageBoundaryPage(const QString& pageType, const QString& boundaryTitle,
                                         const QString& exportFileName, QWidget* parent,
                                         bool removeFileWhenEmpty)
    : BasePage(pageType), ui(new Ui::SeepageBoundaryPage), m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName), m_removeFileWhenEmpty(removeFileWhenEmpty)
{
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->label2->hide();
    ui->labelFileNameValue->hide();
    ui->boxSelectHintLabel->setText(
        QStringLiteral("选择对象：网格节点。Shift+拖拽框选，Ctrl+拖拽取消选择；也可按边界分组批量添加节点。"));
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &SeepageBoundaryPage::onClearClicked);
    ui->tableNodes->setVisible(false);
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind)
            {
                if (domain == getPageType() && uuid == m_uuid && mode == PickMode::Node)
                    updateSelectionSummary();
            });
    updateSelectionSummary();
}

SeepageBoundaryPage::~SeepageBoundaryPage()
{
    delete ui;
}

void SeepageBoundaryPage::beforeLoad()
{
    setupBatchGroupCombo();
    updateSelectionSummary();
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
        // PickManager::getIdList(Node) 返回的是业务点号（point/node id），
        // 不是 vtk point id。这里直接使用该结果导出 JD_*.DAT。
        const QVector<vtkIdType> picked = pm.getIdList(getPageType(), m_uuid, PickMode::Node, kind);
        if (picked.isEmpty())
            return out;
        out.reserve(picked.size());
        for (vtkIdType pid : picked)
        {
            out.push_back(static_cast<int>(pid));
        }
        return out;
    };

    QSet<int> merged;
    const QVector<int> solidNodes = collectPickedIds(GridKind::Solid);
    const QVector<int> lineNodes = collectPickedIds(GridKind::Line);
    for (int id : solidNodes)
        merged.insert(id);
    for (int id : lineNodes)
        merged.insert(id);

    QVector<int> result;
    result.reserve(merged.size());
    for (int id : merged)
        result.push_back(id);

    std::sort(result.begin(), result.end());
    return result;
}

int SeepageBoundaryPage::selectionCount()
{
    return selectedNodeIds().size();
}

void SeepageBoundaryPage::updateSelectionSummary()
{
    const int count = selectionCount();
    ui->labelBoundaryTitle->setText(
        count > 0 ? QStringLiteral("%1（已选择 %2 个节点）").arg(m_boundaryTitle).arg(count)
                  : QStringLiteral("%1（未设置）").arg(m_boundaryTitle));
    emit selectionCountChanged(count);
}

bool SeepageBoundaryPage::exportDatFile()
{
    const QVector<int> ids = selectedNodeIds();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    const QString outputPath = inputDir.filePath(m_exportFileName);
    if (ids.isEmpty() && m_removeFileWhenEmpty)
    {
        return !QFile::exists(outputPath) || QFile::remove(outputPath);
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream os(&file);
    os << ids.size() << "\n";
    for (int i = 0; i < ids.size(); ++i)
    {
        os << ids[i] << " " << (i + 1) << "\n";
    }

    file.close();
    return true;
}

void SeepageBoundaryPage::onClearClicked()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
    updateSelectionSummary();
}

void SeepageBoundaryPage::setupBatchGroupCombo()
{
    if (!ui)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(tr("全部边界分组"), -1);
    ui->comboBoxDim->addItem(tr("点分组"), 0);
    ui->comboBoxDim->addItem(tr("线分组"), 1);
    ui->comboBoxDim->addItem(tr("面分组"), 2);
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
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = map.physicalDim(physId);

                    QVector<int> pointIdsInt;
                    if (dim == 0)
                        pointIdsInt = map.physicalPointIds(physId, 0);
                    else if (dim == 1)
                        pointIdsInt = map.physicalPointIds(physId, 1);
                    else if (dim == 2)
                        pointIdsInt = map.physicalPointIds(physId, 2);
                    else
                        return;

                    QVector<vtkIdType> pointIds;
                    pointIds.reserve(pointIdsInt.size());
                    for (int pid : pointIdsInt)
                    {
                        pointIds.push_back(vtkIdType(pid));
                    }

                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid,
                                                               GridKind::Solid, pointIds);
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

    const auto nameMap = map.physicalNameMap();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physicalDim(physId);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dim != dimFilter)
            continue;

        ui->comboBoxGroup->addItem(it.value(), physId);
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
    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physicalDim(physId);

    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.physicalPointIds(physId, 0);
    else if (dim == 1)
        pointIdsInt = map.physicalPointIds(physId, 1);
    else if (dim == 2)
        pointIdsInt = map.physicalPointIds(physId, 2);
    else
        return;

    QVector<vtkIdType> pointIds;
    pointIds.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
    {
        pointIds.push_back(vtkIdType(pid));
    }

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pointIds,
                                                false);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    updateSelectionSummary();
}

QJsonObject SeepageBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj["dim_index"] = ui->comboBoxDim->currentIndex();
    obj["group_phys_id"] = ui->comboBoxGroup->currentData().toInt();

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

    const int physId = obj.value("group_phys_id").toInt(-1);
    for (int i = 0; i < ui->comboBoxGroup->count(); ++i)
    {
        if (ui->comboBoxGroup->itemData(i).toInt() == physId)
        {
            // 只恢复 UI 选项，不触发批量分组选点覆盖已保存的 picked_*。
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
    updateSelectionSummary();
}
