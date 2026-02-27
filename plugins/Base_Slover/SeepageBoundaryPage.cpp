#include "SeepageBoundaryPage.h"
#include "ui_SeepageBoundaryPage.h"

#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QDir>
#include <QFile>
#include <QComboBox>
#include <QSet>
#include <QTextStream>
#include <QJsonArray>
#include <PublicConstants.h>
#include <PublicVariables.h>

#include <algorithm>

SeepageBoundaryPage::SeepageBoundaryPage(const QString& pageType,
                                         const QString& boundaryTitle,
                                         const QString& exportFileName,
                                         QWidget* parent)
    : BasePage(pageType),
      ui(new Ui::SeepageBoundaryPage),
      m_boundaryTitle(boundaryTitle),
      m_exportFileName(exportFileName)
{
    ui->setupUi(this);
    ui->labelBoundaryTitle->setText(boundaryTitle);
    ui->labelFileNameValue->setText(exportFileName);
    setupBatchGroupCombo();

    connect(ui->btnClear, &QPushButton::clicked, this, &SeepageBoundaryPage::onClearClicked);
    ui->tableNodes->setVisible(false);
    ui->labelCountValue->setText(tr("暂不显示"));
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
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

QVector<int> SeepageBoundaryPage::selectedNodeIds()
{
    PickManager& pm = PickManager::instance();
    pm.updateStructure();

    auto collectPickedIds = [&](GridKind kind) {
        QVector<int> out;
        // PickManager::getIdList(Node) 返回的是业务点号（point/node id），
        // 不是 vtk point id。这里直接使用该结果导出 JD_*.DAT。
        const QVector<vtkIdType> picked = pm.getIdList(getPageType(), m_uuid, PickMode::Node, kind);
        if (picked.isEmpty()) return out;
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
    for (int id : solidNodes) merged.insert(id);
    for (int id : lineNodes) merged.insert(id);

    QVector<int> result;
    result.reserve(merged.size());
    for (int id : merged) result.push_back(id);

    std::sort(result.begin(), result.end());
    return result;
}

bool SeepageBoundaryPage::exportDatFile()
{
    const QVector<int> ids = selectedNodeIds();

    const QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP")) wd.mkpath("PREP");

    QFile file(wd.filePath("PREP/" + m_exportFileName));
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
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
}

void SeepageBoundaryPage::setupBatchGroupCombo()
{
    if (!ui) return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(tr("全部维度"), -1);
    ui->comboBoxDim->addItem(tr("dim0 点"), 0);
    ui->comboBoxDim->addItem(tr("dim1 线"), 1);
    ui->comboBoxDim->addItem(tr("dim2 面"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    connect(ui->comboBoxDim,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &SeepageBoundaryPage::onDimChanged,
            Qt::UniqueConnection);
    connect(ui->comboBoxGroup,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &SeepageBoundaryPage::onGroupChanged,
            Qt::UniqueConnection);

    refreshGroupCombo();
}

void SeepageBoundaryPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc || !ui) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2) continue;
        if (dimFilter >= 0 && dim != dimFilter) continue;

        ui->comboBoxGroup->addItem(QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId),
                                   physId);
    }

    ui->comboBoxGroup->blockSignals(false);
}

void SeepageBoundaryPage::onDimChanged(int)
{
    refreshGroupCombo();
}

void SeepageBoundaryPage::onGroupChanged(int index)
{
    if (index < 0 || !ui) return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);

    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1)
        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2)
        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else
        return;

    QVector<vtkIdType> pointIds;
    pointIds.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
    {
        pointIds.push_back(vtkIdType(pid));
    }

    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pointIds, true);
}


QJsonObject SeepageBoundaryPage::saveJson()
{
    QJsonObject obj;
    obj["dim_index"] = ui->comboBoxDim->currentIndex();
    obj["group_phys_id"] = ui->comboBoxGroup->currentData().toInt();

    auto toJson = [](const QVector<vtkIdType>& ids) {
        QJsonArray arr;
        for (auto id : ids) arr.append(static_cast<qint64>(id));
        return arr;
    };
    obj["picked_solid"] = toJson(PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid));
    obj["picked_line"] = toJson(PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Line));
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
            ui->comboBoxGroup->setCurrentIndex(i);
            onGroupChanged(i);
            break;
        }
    }

    auto fromJson = [](const QJsonArray& arr) {
        QVector<vtkIdType> out;
        out.reserve(arr.size());
        for (const auto& v : arr) out.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
        return out;
    };
    const auto solidIds = fromJson(obj.value("picked_solid").toArray());
    const auto lineIds = fromJson(obj.value("picked_line").toArray());
    if (!solidIds.isEmpty() || !lineIds.isEmpty())
    {
        PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, solidIds, true);
        PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, lineIds, true);
    }
}
