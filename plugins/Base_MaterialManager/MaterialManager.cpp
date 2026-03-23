#include "MaterialManager.h"

#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>

#include <QDebug>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QString>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>
#include <cmath>

#include "MaterialEntryWidget.h"
#include "MaterialModelWidget.h"

MaterialManager::MaterialManager()
    : m_materialManagerWidget(nullptr), m_rootItem(nullptr)
{
    m_projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    ServiceRegistry::instance().registerService<MaterialManagerService>(this);
    setupTreeWidget();
    loadDefaultLibrary();
    ensureEntryIds();
}

BasePage* MaterialManager::getMaterialManagerWidget()
{
    if (!m_materialManagerWidget)
    {
        m_materialManagerWidget = new MaterialManagerWidget(this);
    }
    return m_materialManagerWidget;
}

void MaterialManager::newProject()
{
    const QString workPath =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    newProjectForPath(workPath);
}

void MaterialManager::addMaterialFromMenu(const QString& modelKey)
{
    const QString entryId = addMaterialToModel(modelKey, true);
    if (!entryId.isEmpty())
    {
        focusOnMaterial(entryId);
    }
}

QString MaterialManager::addMaterialToModel(const QString& modelKey, bool addToProjectTree)
{
    MaterialModel* model = findModel(modelKey);
    if (!model)
    {
        return QString();
    }

    int maxIndex = 0;
    for (const auto& entry : model->materials)
    {
        if (!entry.name.startsWith(QStringLiteral("材料")))
        {
            continue;
        }
        bool ok = false;
        const int index = entry.name.mid(QStringLiteral("材料").size()).toInt(&ok);
        if (ok)
        {
            maxIndex = qMax(maxIndex, index);
        }
    }

    MaterialEntry entry;
    entry.id = QUuid::createUuid().toString();
    entry.name = QString("材料%1").arg(maxIndex + 1);
    for (const QString& column : model->columns)
    {
        entry.properties.insert(column, QStringLiteral("0"));
    }

    model->materials.append(entry);

    if (addToProjectTree)
    {
        m_addedEntryIds.insert(entry.id);
        rebuildProjectTree();
    }
    if (m_materialManagerWidget)
    {
        m_materialManagerWidget->refreshSummary(m_models);
    }

    return entry.id;
}

void MaterialManager::setupTreeWidget()
{
}

void MaterialManager::loadDefaultLibrary()
{
    m_selectedMatypes.clear();
    m_models = createDefaultMaterialModels();
    notifyLibraryChanged();
}

void MaterialManager::newProjectForPath(const QString& workPath)
{
    m_workPath = workPath;

    qDeleteAll(m_modelWidgets);
    m_modelWidgets.clear();
    qDeleteAll(m_entryWidgets);
    m_entryWidgets.clear();

    delete m_materialManagerWidget;
    m_materialManagerWidget = nullptr;

    m_rootItem = nullptr;
    m_modelItems.clear();
    m_entryItems.clear();
    m_addedEntryIds.clear();
    m_selectedMatypes.clear();
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();

    m_materialManagerWidget = new MaterialManagerWidget(this);
    loadDefaultLibrary();
    ensureEntryIds();

    if (!m_projectManagerService)
    {
        return;
    }

    m_rootItem = new ProjectNode(QString("材料"));
    m_rootItem->setData(0, ProjectNode::RoleUuid, m_rootItem->getUuid());
    m_rootItem->setData(0, ProjectNode::RoleModule, QString("Material"));
    m_rootItem->setPage(m_materialManagerWidget);
    m_projectManagerService->addTreeItem(m_rootItem);
}

const QVector<MaterialModel>& MaterialManager::models() const
{
    return m_models;
}

QVector<MaterialModel>& MaterialManager::models()
{
    return m_models;
}

MaterialModel* MaterialManager::findModel(const QString& key)
{
    for (auto& model : m_models)
    {
        if (model.key == key)
        {
            return &model;
        }
    }
    return nullptr;
}

MaterialEntry* MaterialManager::findEntry(const QString& id, MaterialModel** outModel)
{
    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (entry.id == id)
            {
                if (outModel)
                {
                    *outModel = &model;
                }
                return &entry;
            }
        }
    }
    if (outModel)
    {
        *outModel = nullptr;
    }
    return nullptr;
}

void MaterialManager::notifyLibraryChanged(bool refreshProjectTree)
{
    if (refreshProjectTree)
    {
        rebuildProjectTree();
    }

    if (m_materialManagerWidget)
    {
        m_materialManagerWidget->refreshSummary(m_models);
    }
}

void MaterialManager::notifyEntryUpdated(const QString& entryId, bool nameChanged)
{
    if (nameChanged)
    {
        ProjectNode* item = m_entryItems.value(entryId, nullptr);
        MaterialEntry* entry = findEntry(entryId);
        if (item && entry)
        {
            item->setText(0, entry->name);
        }
    }
}

QVector<QStringList> MaterialManager::getValueStrings()
{
    QVector<QStringList> results;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            QStringList row;
            row << model.key << model.displayName << entry.name;
            QStringList encoded;
            for (const QString& column : model.columns)
            {
                encoded << (column + QLatin1Char(':') + entry.properties.value(column));
            }
            row << encoded.join(QString("|;|"));
            QList<int> appliedList = entry.appliedMatypes.values();
            std::sort(appliedList.begin(), appliedList.end());
            QStringList appliedStrings;
            for (int value : appliedList)
            {
                appliedStrings << QString::number(value);
            }
            row << appliedStrings.join(QLatin1Char(','));
            results.append(row);
        }
    }
    return results;
}

void MaterialManager::setValueStrings(QVector<QStringList> values)
{
    m_selectedMatypes.clear();
    if (values.isEmpty())
    {
        loadDefaultLibrary();
        return;
    }

    QVector<MaterialModel> defaults = createDefaultMaterialModels();
    m_models.clear();
    m_models.reserve(defaults.size());
    for (const auto& model : defaults)
    {
        MaterialModel cleared = model;
        cleared.materials.clear();
        m_models.append(cleared);
    }

    auto findIndex = [this](const QString& key) -> int
    {
        for (int i = 0; i < m_models.size(); ++i)
        {
            if (m_models.at(i).key == key)
            {
                return i;
            }
        }
        return -1;
    };

    for (const auto& list : values)
    {
        if (list.size() < 4)
        {
            continue;
        }
        const QString key = list.at(0);
        const QString displayName = list.at(1);
        const QString name = list.at(2);
        const QString encoded = list.at(3);
        const QString appliedMatypes = (list.size() > 4) ? list.at(4) : QString();

        int index = findIndex(key);
        if (index < 0)
        {
            MaterialModel extra;
            extra.key = key;
            extra.displayName = displayName;
            index = m_models.size();
            m_models.append(extra);
        }

        MaterialEntry entry;
        entry.name = name;
        const QStringList pairs = encoded.split(QString("|;|"), QString::SkipEmptyParts);
        for (const QString& pair : pairs)
        {
            int sep = pair.indexOf(QLatin1Char(':'));
            if (sep <= 0)
            {
                continue;
            }
            QString columnName = pair.left(sep);
            QString value = pair.mid(sep + 1);
            entry.properties.insert(columnName, value);
            if (!m_models[index].columns.contains(columnName))
            {
                m_models[index].columns.append(columnName);
            }
        }

        m_models[index].materials.append(entry);
        const QStringList matypeList =
            appliedMatypes.split(QLatin1Char(','), QString::SkipEmptyParts);
        for (const QString& matypeStr : matypeList)
        {
            bool ok = false;
            int value = matypeStr.toInt(&ok);
            if (ok)
            {
                m_models[index].materials.last().appliedMatypes.insert(value);
            }
        }
    }

    ensureEntryIds();
    notifyLibraryChanged();
}

void MaterialManager::focusOnMaterial(const QString& entryId)
{
    if (!m_projectManagerService)
    {
        return;
    }
    ProjectNode* item = m_entryItems.value(entryId, nullptr);
    if (item)
    {
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::focusOnModel(const QString& modelKey)
{
    if (!m_projectManagerService)
    {
        return;
    }
    ProjectNode* item = m_modelItems.value(modelKey, nullptr);
    if (item)
    {
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::ensureEntryIds()
{
    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (entry.id.isEmpty())
            {
                entry.id = QUuid::createUuid().toString();
            }
        }
    }
}

void MaterialManager::rebuildProjectTree()
{
    if (!m_rootItem)
    {
        return;
    }

    QSet<QString> activeModelKeys;
    QSet<QString> validEntries;

    for (const QString& entryId : std::as_const(m_addedEntryIds))
    {
        MaterialModel* model = nullptr;
        MaterialEntry* entry = findEntry(entryId, &model);
        if (!entry || !model)
        {
            continue;
        }
        validEntries.insert(entryId);
        activeModelKeys.insert(model->key);

        ProjectNode* modelItem = m_modelItems.value(model->key, nullptr);
        if (!modelItem)
        {
            auto* modelWidget = new MaterialModelWidget(this);
            modelItem =
                new ProjectNode(model->displayName, QUuid::createUuid().toString(), modelWidget);
            modelItem->setData(0, ProjectNode::RoleUuid, modelItem->getUuid());
            modelItem->setData(0, ProjectNode::RoleModule, QString("Material"));
            modelItem->setData(0, ProjectNode::RoleKind, QString("MaterialModel"));
            m_modelWidgets.insert(model->key, modelWidget);
            m_modelItems.insert(model->key, modelItem);
            m_rootItem->addChild(modelItem);
        }
        modelItem->setText(0, model->displayName);
        modelItem->setExpanded(true);

        if (m_modelWidgets.contains(model->key))
        {
            m_modelWidgets[model->key]->setModel(*model);
        }

        ProjectNode* entryItem = m_entryItems.value(entryId, nullptr);
        if (!entryItem)
        {
            auto* entryWidget = new MaterialEntryWidget(this);
            entryWidget->setEntry(model->key, entryId);
            entryItem = new ProjectNode(entry->name, entryId, entryWidget);
            entryItem->setData(0, ProjectNode::RoleUuid, entryId);
            entryItem->setData(0, ProjectNode::RoleModule, QString("Material"));
            entryItem->setData(0, ProjectNode::RoleKind, QString("MaterialEntry"));
            m_entryWidgets.insert(entryId, entryWidget);
            m_entryItems.insert(entryId, entryItem);
            modelItem->addChild(entryItem);
        }
        else if (entryItem->parent() != modelItem)
        {
            auto* oldParent = dynamic_cast<ProjectNode*>(entryItem->parent());
            if (oldParent)
            {
                oldParent->removeChild(entryItem);
            }
            modelItem->addChild(entryItem);
        }
        entryItem->setText(0, entry->name);
        if (m_entryWidgets.contains(entryId))
        {
            m_entryWidgets[entryId]->setEntry(model->key, entryId);
        }
    }

    // remove stale entry items
    QList<QString> staleEntries;
    for (auto it = m_entryItems.cbegin(); it != m_entryItems.cend(); ++it)
    {
        if (!validEntries.contains(it.key()))
        {
            staleEntries.append(it.key());
        }
    }
    for (const QString& id : staleEntries)
    {
        ProjectNode* item = m_entryItems.take(id);
        if (item)
        {
            if (auto* parent = dynamic_cast<ProjectNode*>(item->parent()))
            {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_entryWidgets.contains(id))
        {
            delete m_entryWidgets.take(id);
        }
    }
    m_addedEntryIds = validEntries;

    // remove stale models
    QList<QString> staleModels;
    for (auto it = m_modelItems.cbegin(); it != m_modelItems.cend(); ++it)
    {
        if (!activeModelKeys.contains(it.key()))
        {
            staleModels.append(it.key());
        }
    }
    for (const QString& modelKey : staleModels)
    {
        ProjectNode* item = m_modelItems.take(modelKey);
        if (item)
        {
            QList<QString> orphanEntries;
            for (auto it = m_entryItems.begin(); it != m_entryItems.end();)
            {
                if (it.value()->parent() == item)
                {
                    orphanEntries.append(it.key());
                    ProjectNode* entryItem = it.value();
                    item->removeChild(entryItem);
                    delete entryItem;
                    it = m_entryItems.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (const QString& id : orphanEntries)
            {
                if (m_entryWidgets.contains(id))
                {
                    delete m_entryWidgets.take(id);
                }
            }
            if (auto* parent = dynamic_cast<ProjectNode*>(item->parent()))
            {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_modelWidgets.contains(modelKey))
        {
            delete m_modelWidgets.take(modelKey);
        }
    }

    m_rootItem->setExpanded(true);
}

void MaterialManager::clearProjectTree()
{
    if (!m_rootItem)
    {
        return;
    }
    const auto children = m_rootItem->getChildren();
    for (ProjectNode* child : children)
    {
        m_rootItem->removeChild(child);
        delete child;
    }
}

void MaterialManager::addEntryToProjectTree(const QString& entryId)
{
    ensureEntryIds();
    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (!entry || !model)
    {
        return;
    }

    m_addedEntryIds.insert(entryId);
    rebuildProjectTree();
}

void MaterialManager::removeEntryFromProjectTree(const QString& entryId)
{
    if (!m_addedEntryIds.contains(entryId))
    {
        return;
    }
    m_addedEntryIds.remove(entryId);
    rebuildProjectTree();
}

QSet<int> MaterialManager::availableMatypes() const
{
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
    {
        return {};
    }
    return service->getAvailableMatypes();
}

void MaterialManager::highlightMatypes(const QSet<int>& matypes, int selectedMatype,
                                       int previewMatype, int preDeleteMatype)
{
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
    {
        return;
    }
    service->highlightMatypes(matypes, selectedMatype, previewMatype, preDeleteMatype);
}

void MaterialManager::refreshMatypeHighlight(const QString& entryId)
{
    MaterialEntry* entry = findEntry(entryId);
    if (!entry)
        return;

    const int preview = m_previewMatypes.value(entryId, -1);
    const int preDelete = m_preDeleteMatypes.value(entryId, -1);
    // 不再使用 Selected 高亮，仅保留 Applied / Preview / PreDelete。
    highlightMatypes(entry->appliedMatypes, -1, preview, preDelete);
}

int MaterialManager::selectedMatypeForEntry(const QString& entryId) const
{
    return m_selectedMatypes.value(entryId, -1);
}

void MaterialManager::setSelectedMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
    {
        return;
    }
    if (matype < 0)
    {
        m_selectedMatypes.remove(entryId);
        return;
    }
    m_selectedMatypes.insert(entryId, matype);
}

void MaterialManager::applyMatypeToEntry(const QString& entryId, int matype)
{
    if (matype < 0)
        return;

    MaterialEntry* entry = findEntry(entryId);
    if (!entry)
        return;

    // 移除其他材料上的相同 MATYPE（你原逻辑不变）
    for (auto& model : m_models)
    {
        for (auto& otherEntry : model.materials)
        {
            if (otherEntry.id == entryId)
                continue;

            if (otherEntry.appliedMatypes.remove(matype))
            {
                if (m_selectedMatypes.value(otherEntry.id, -1) == matype)
                {
                    m_selectedMatypes.remove(otherEntry.id);
                }
                if (m_previewMatypes.value(otherEntry.id, -1) == matype)
                {
                    m_previewMatypes.remove(otherEntry.id);
                }
                if (m_preDeleteMatypes.value(otherEntry.id, -1) == matype)
                {
                    m_preDeleteMatypes.remove(otherEntry.id);
                }

                refreshEntryWidgetMatypes(otherEntry.id, otherEntry.appliedMatypes,
                                          m_selectedMatypes.value(otherEntry.id, -1));
            }
        }
    }

    setSelectedMatypeForEntry(entryId, matype);
    entry->appliedMatypes.insert(matype);

    // 施加后通常清掉 predelete/preview
    setPreviewMatypeForEntry(entryId, -1);
    setPreDeleteMatypeForEntry(entryId, -1);

    refreshEntryWidgetMatypes(entryId, entry->appliedMatypes, matype);
    notifyEntryUpdated(entryId, false);
    refreshMatypeHighlight(entryId);
}

void MaterialManager::removeMatypeFromEntry(const QString& entryId, int matype)
{
    if (matype < 0)
        return;

    MaterialEntry* entry = findEntry(entryId);
    if (!entry)
        return;

    if (entry->appliedMatypes.remove(matype))
    {
        if (m_selectedMatypes.value(entryId, -1) == matype)
        {
            m_selectedMatypes.remove(entryId);
        }
        if (m_previewMatypes.value(entryId, -1) == matype)
        {
            m_previewMatypes.remove(entryId);
        }
        if (m_preDeleteMatypes.value(entryId, -1) == matype)
        {
            m_preDeleteMatypes.remove(entryId);
        }

        refreshEntryWidgetMatypes(entryId, entry->appliedMatypes,
                                  m_selectedMatypes.value(entryId, -1));
        notifyEntryUpdated(entryId, false);
        refreshMatypeHighlight(entryId);
    }
}

void MaterialManager::refreshEntryWidgetMatypes(const QString& entryId, const QSet<int>& applied,
                                                int selectedMatype)
{
    auto* widget = m_entryWidgets.value(entryId, nullptr);
    if (!widget)
    {
        return;
    }
    widget->refreshAppliedMatypes(applied, selectedMatype);
}

bool MaterialManager::generateCaculateFile()
{
    // ===== 1) 收集：只要“已添加到工程 + 关联了 MATYPE”的材料，并建立 matype ->
    // (model, entry) 映射 =====
    struct MatInfo
    {
        MaterialModel* model = nullptr;
        MaterialEntry* entry = nullptr;
    };

    QMap<int, MatInfo> matypeMap; // matype -> info
    int maxMatype = -1;

    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (entry.appliedMatypes.isEmpty())
                continue;
            if (!m_addedEntryIds.contains(entry.id))
                continue;

            for (int matype : entry.appliedMatypes)
            {
                if (matype < 0)
                    continue;
                maxMatype = qMax(maxMatype, matype);

                // 如果同一个 MATYPE 被多个材料占用，这里默认“后者覆盖前者”
                matypeMap[matype] = MatInfo{&model, &entry};
            }
        }
    }

    if (maxMatype < 0 || m_workPath.isEmpty())
        return false;

    // ===== 2) NMAT / NCON =====
    const int ncon = 200;
    const int nmatReal = maxMatype;                        // 1..maxMatype
    const int nmat = qMax(10, ((nmatReal + 9) / 10) * 10); // 补齐到 10 的倍数（每组 10 列）

    // ===== 3) 输出文件 =====
    QDir workDir(m_workPath);
    if (!workDir.exists("input"))
        workDir.mkpath("input");
    const QString filePath = workDir.filePath(QString("input/Imat.dat"));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);

    // ===== 4) 预先构造数值表：values[row(0..199)][matIndex(0..nmat-1)] =====
    QVector<QVector<double>> values(ncon, QVector<double>(nmat, 0.0));

    auto valueOf = [](const MaterialEntry* entry, const QString& key) -> double
    {
        if (!entry)
        {
            return 0.0;
        }
        bool ok = false;
        const double value = entry->properties.value(key, QStringLiteral("0")).toDouble(&ok);
        return ok ? value : 0.0;
    };

    // 填值：黄金坪地下厂房模型按 CONMAT 行号写入，其他模型仍走 columnOutputRows
    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        const int matype = it.key();
        const MatInfo& info = it.value();
        if (!info.model || !info.entry)
            continue;
        // IMAT.DAT 的编号从 1 开始，0 号无效
        if (matype <= 0 || matype > nmat)
            continue;

        const int matIndex = matype - 1;

        if (info.model->key == QStringLiteral("huangjinping_underground_plant"))
        {
            const double matypeOri = valueOf(info.entry, QStringLiteral("编号"));
            const double itagEx = valueOf(info.entry, QStringLiteral("岩类分区"));
            const double alfaBa = valueOf(info.entry, QStringLiteral("半开挖区荷载释放系数"));
            const double laReborn = valueOf(info.entry, QStringLiteral("再生时对应开挖级"));
            const double eeGpa = valueOf(info.entry, QStringLiteral("变形模量E（GPa）"));
            const double pu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rouTm3 = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double ff = valueOf(info.entry, QStringLiteral("抗剪断强度内摩擦系数"));
            const double coMpa = valueOf(info.entry, QStringLiteral("凝聚力C‘（MPa）"));
            const double alfa0 = valueOf(info.entry, QStringLiteral("洞室边缘松弛系数α0"));
            const double d0 = valueOf(info.entry, QStringLiteral("松弛距离d0"));

            const double ibuck = 2.0;
            const double alfa1 = 1.0;

            const double ee = eeGpa * 1.0e9;
            const double rou = rouTm3 * 1.0e3;
            const double fai = std::atan(ff) * 180.0 / 3.141592653589793;
            const double co = coMpa * 1.0e6;

            // 默认替代：用 Mohr-Coulomb 近似计算 RC/RT（原 DEFAULT_RTRC 未给出）
            static bool loggedApproximation = false;
            if (!loggedApproximation)
            {
                qWarning() << "MaterialManager: DEFAULT_RTRC 未提供，RC/RT 当前使用 Mohr-Coulomb 近似公式。";
                loggedApproximation = true;
            }
            const double faiRad = fai * 3.141592653589793 / 180.0;
            const double sinF = std::sin(faiRad);
            const double cosF = std::cos(faiRad);
            const double denRc = 1.0 - sinF;
            const double denRt = 1.0 + sinF;
            const double rc = (std::fabs(denRc) > 1.0e-12) ? (2.0 * co * cosF / denRc) : 0.0;
            const double rt = (std::fabs(denRt) > 1.0e-12) ? (2.0 * co * cosF / denRt) : 0.0;
            const double gg = (std::fabs(1.0 + pu) > 1.0e-12) ? (ee / 2.0 / (1.0 + pu)) : 0.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, ibuck);
            setRow(2, ee);
            setRow(3, ee);
            setRow(4, pu);
            setRow(5, pu);
            setRow(6, gg);
            setRow(7, rou);
            setRow(8, 1.0);
            setRow(9, rc);
            setRow(10, rt);
            setRow(11, fai);
            setRow(12, co);
            setRow(13, 0.0);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            setRow(23, 0.0);
            setRow(35, 0.0);
            setRow(64, 0.0);
            setRow(65, 1.3);
            setRow(66, 1.0);
            setRow(67, 0.05);
            setRow(77, alfa0);
            setRow(78, d0);
            setRow(79, alfa1);
            setRow(84, matypeOri);
            setRow(91, rc);
            setRow(92, rt);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, fai);
            setRow(100, itagEx);

            if (qRound64(itagEx) == -1)
            {
                setRow(58, alfaBa);
            }
            if (qRound64(itagEx) == 1)
            {
                setRow(59, laReborn);
            }
            continue;
        }

        if (info.model->key == QStringLiteral("soil_eb"))
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0(°)"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryKb = valueOf(info.entry, QStringLiteral("Kb"));
            const double dryM = valueOf(info.entry, QStringLiteral("m"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -1.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryN);
            setRow(74, dryKb);
            setRow(75, dryM);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, fai);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_emu"))
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0(°)"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double g = valueOf(info.entry, QStringLiteral("G"));
            const double d = valueOf(info.entry, QStringLiteral("D"));
            const double f = valueOf(info.entry, QStringLiteral("F"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -3.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(76, dFai);
            setRow(77, g);
            setRow(78, d);
            setRow(79, f);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, fai);
            continue;
        }

        if (info.model->key == QStringLiteral("dam_dynamic"))
        {
            const double maori = valueOf(info.entry, QStringLiteral("静力计算编号"));
            const double ifliqd = valueOf(info.entry, QStringLiteral("是否液化"));
            const double dryk1 = valueOf(info.entry, QStringLiteral("DRYK1(KPa)"));
            const double dryn1 = valueOf(info.entry, QStringLiteral("DRYN1"));
            const double fai = valueOf(info.entry, QStringLiteral("FAI(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("CO(KPa)"));
            const double pu = valueOf(info.entry, QStringLiteral("PU"));
            const double drydam = valueOf(info.entry, QStringLiteral("DRYDAM"));
            const double ratiao1 = valueOf(info.entry, QStringLiteral("RATIAO1(0.1)"));
            const double ratiao2 = valueOf(info.entry, QStringLiteral("RATIAO2(0.3)"));
            const double ratiao3 = valueOf(info.entry, QStringLiteral("RATIAO3(0.5)"));

            double ckv = valueOf(info.entry, QStringLiteral("Kv"));
            double ckp = valueOf(info.entry, QStringLiteral("Kp"));
            double cnv = valueOf(info.entry, QStringLiteral("nv"));
            double cnp = valueOf(info.entry, QStringLiteral("np"));

            if (ckv < 0.0001)
                ckv = dryk1 * 1.1;
            if (ckp < 0.0001)
                ckp = ckv * 0.8;
            if (cnv < 0.0001)
                cnv = dryn1;
            if (cnp < 0.0001)
                cnp = dryn1 * 1.5;
            if (cnp > 0.9)
                cnp = 0.9;

            const double co = coKpa * 1.0e3;

            const int maoriInt = qRound64(maori);
            if (maoriInt > 0 && maoriInt <= nmat)
            {
                const int maoriIndex = maoriInt - 1;
                for (int row = 0; row < ncon; ++row)
                {
                    values[row][matIndex] = values[row][maoriIndex];
                }
            }

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -4.0);
            setRow(13, ifliqd);
            setRow(35, 1.0);
            setRow(4, pu);
            setRow(5, pu);
            setRow(58, 0.1);
            setRow(59, 0.3);
            setRow(60, 0.5);
            setRow(61, ratiao1);
            setRow(62, ratiao2);
            setRow(63, ratiao3);
            setRow(80, dryk1);
            setRow(81, dryn1);
            setRow(82, drydam);
            setRow(83, co);
            setRow(84, maori);
            setRow(85, ratiao1);
            setRow(86, ratiao2);
            setRow(87, ratiao3);
            setRow(88, fai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(105, ckv);
            setRow(106, ckp);
            setRow(107, cnv);
            setRow(108, cnp);
            continue;
        }

        if (info.model->key == QStringLiteral("jk_corewall_steady_seepage"))
        {
            const double tol = 1.0e-3;
            const double akLu = valueOf(info.entry, QStringLiteral("渗透系数K(Lu)"));
            const double rip = valueOf(info.entry, QStringLiteral("允许比降"));
            const double ak = akLu * 1.0e-7;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(96, ak);
            setRow(97, ak);
            setRow(98, ak);
            setRow(60, rip);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);

            if (values[7 - 1][matIndex] < tol)
                setRow(7, 2.0e3);
            if (values[89 - 1][matIndex] < tol)
                setRow(89, 2.0e3);
            if (values[90 - 1][matIndex] < tol)
                setRow(90, 2.0e3);
            if (values[99 - 1][matIndex] < tol)
                setRow(99, 1.0);
            continue;
        }

        if (info.model->key == QStringLiteral("huangjinping_bar_element"))
        {
            const double ee1 = valueOf(info.entry, QStringLiteral("变模E1,E2（GPa）"));
            const double dryRou = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double area = valueOf(info.entry, QStringLiteral("杆面积（m2）"));
            const double dryRt = valueOf(info.entry, QStringLiteral("抗拉（压）强度ft（MPa）"));
            const double ba = valueOf(info.entry, QStringLiteral("间距(m)"));
            const double bb = valueOf(info.entry, QStringLiteral("排距(m)"));
            const double fbar = valueOf(info.entry, QStringLiteral("预应力T（KN）"));
            const double alfaFbar = valueOf(info.entry, QStringLiteral("预应系数"));

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, 1.0);
            setRow(13, 2.0);
            setRow(12, dryRt * 1.0e6 * 0.512);
            setRow(2, ee1 * 1.0e9);
            setRow(3, ee1 * 1.0e9);
            setRow(7, dryRou * 1.0e3);
            setRow(8, area);
            setRow(10, dryRt * 1.0e6);
            setRow(92, dryRt * 1.0e6);
            setRow(9, dryRt * 1.0e6);
            setRow(91, dryRt * 1.0e6);
            setRow(93, fbar * 1.0e3);
            setRow(38, ba);
            setRow(37, bb);
            setRow(39, 1.0);
            setRow(89, alfaFbar);
            continue;
        }

        const auto& cols = info.model->columns;
        const auto& lineMap = info.model->columnOutputRows;
        const int count = qMin(cols.size(), lineMap.size());

        for (int c = 0; c < count; ++c)
        {
            const int targetRow = lineMap.at(c); // 1-based
            if (targetRow <= 0 || targetRow > ncon)
                continue;

            const QString key = cols.at(c);
            const QString strVal = info.entry->properties.value(key, QString("0"));

            bool ok = false;
            const double v = strVal.toDouble(&ok);
            values[targetRow - 1][matIndex] = ok ? v : 0.0;
        }
    }

    // ===== 5) 按你示例的 1P 科学计数格式输出：0.2000E+01 这种 =====
    auto fmt1P = [](double v) -> QString
    {
        double mant = 0.0;
        int exp = 0;

        const double av = std::fabs(v);
        if (av > 0.0)
        {
            exp = static_cast<int>(std::floor(std::log10(av))) + 1; // 1P: exp 比常规多 1
            mant = v / std::pow(10.0, exp);

            // 处理极端情况：由于浮点舍入导致 mant 变成 1.0000
            if (std::fabs(mant) >= 1.0)
            {
                mant /= 10.0;
                exp += 1;
            }
        }

        const QString mantStr = QString::asprintf("% .4f", mant); // 正数前带空格，负数带 -
        const QString expStr = QString::asprintf("E%+03d", exp);  // E+00 / E+01 / E-01

        // 每格固定宽度，方便列对齐
        return QString("%1").arg(mantStr + expStr, 13);
    };

    auto imatLabel = [](int matype) -> QString
    {
        // 尽量贴近你示例：IMAT= 10 这种（两位数前空一格）
        if (matype < 10)
            return QString("IMAT=%1").arg(matype);
        if (matype < 100)
            return QString("IMAT= %1").arg(matype);
        return QString("IMAT=%1").arg(matype);
    };

    // ===== 6) 文件头 =====
    // 示例：        300         200    NMAT  NCON
    out << QString("%1%2    NMAT  NCON\n").arg(nmat, 12).arg(ncon, 12);

    // ===== 7) 分组输出：每组 10 列 =====
    const int groupCount = nmat / 10;

    for (int g = 0; g < groupCount; ++g)
    {
        const int startMat = g * 10 + 1;

        out << QString(" CONMAT(NMAT,NCON)   GROUP NUM=%1\n").arg(g + 1, 4);

        // 组头 IMAT=...
        QStringList head;
        head.reserve(10);
        for (int k = 0; k < 10; ++k)
        {
            head << imatLabel(startMat + k);
        }
        out << head.join(QString("       ")) << "\n";

        // 200 行
        for (int r = 0; r < ncon; ++r)
        {
            QString line;
            line.reserve(10 * 13 + 8);

            for (int k = 0; k < 10; ++k)
            {
                const int matype = startMat + k;
                const int matIndex = matype - 1;
                const double v = (matIndex >= 0 && matIndex < nmat) ? values[r][matIndex] : 0.0;
                line += fmt1P(v);
            }

            // 行号（1..200）
            line += QString("%1").arg(r + 1, 5);
            out << line << "\n";
        }
    }

    file.close();
    return true;
}

void MaterialManager::clearMatypeHighlight(const QString& entryId)
{
    if (entryId.isEmpty())
    {
        return;
    }

    m_previewMatypes.remove(entryId);
    m_preDeleteMatypes.remove(entryId);

    // 注意：selected 不一定清掉（否则回到页面无法恢复“选中颜色”）
    // 只清显示即可：
    highlightMatypes(QSet<int>{}, -1, -1, -1);
}

void MaterialManager::clearAllMatypeHighlight()
{
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
    highlightMatypes(QSet<int>{}, -1, -1, -1);
}

int MaterialManager::previewMatypeForEntry(const QString& entryId) const
{
    return m_previewMatypes.value(entryId, -1);
}

void MaterialManager::setPreviewMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
        return;
    if (matype < 0)
    {
        m_previewMatypes.remove(entryId);
    }
    else
    {
        m_previewMatypes.insert(entryId, matype);
    }
}

int MaterialManager::preDeleteMatypeForEntry(const QString& entryId) const
{
    return m_preDeleteMatypes.value(entryId, -1);
}

void MaterialManager::setPreDeleteMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
        return;
    if (matype < 0)
    {
        m_preDeleteMatypes.remove(entryId);
    }
    else
    {
        m_preDeleteMatypes.insert(entryId, matype);
    }
}
