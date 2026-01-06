#include "MaterialManager.h"
#include "MaterialLibraryWidget.h"
#include "MaterialEntryWidget.h"
#include "MaterialModelWidget.h"

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QPluginLoader>
#include <QSqlDatabase>
#include <QSqlDriverPlugin>
#include <QSqlError>
#include <QSqlQuery>
#include <QPair>
#include <QSet>
#include <QToolButton>
#include <QUuid>
#include <QString>
#include <QTextStream>
#include <QtGlobal>

#include <tuple>

#include <services/PhysicalFieldService.h>
#include <services/MeshModuleControllerService.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>

MaterialManager::MaterialManager()
    : m_materialLibraryWidget(nullptr)
    , m_materialManagerWidget(nullptr)
    , m_rootItem(nullptr)
{
    m_projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    ServiceRegistry::instance().registerService<MaterialManagerService>(this);
    setupTreeWidget();
    loadDefaultLibrary();
    ensureEntryIds();
}

BasePage *MaterialManager::getMaterialManagerWidget()
{
    if (!m_materialManagerWidget) {
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

void MaterialManager::setupTreeWidget()
{
}

void MaterialManager::loadDefaultLibrary()
{
    m_selectedMatypes.clear();
    m_models = createDefaultMaterialModels();
    notifyLibraryChanged();
}

void MaterialManager::newProjectForPath(const QString &workPath)
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

    if (!m_projectManagerService) {
        return;
    }

    m_rootItem = new ProjectNode(QStringLiteral("材料"));
    m_rootItem->setData(0, ProjectNode::RoleUuid, m_rootItem->getUuid());
    m_rootItem->setData(0, ProjectNode::RoleModule, QStringLiteral("Material"));
    m_rootItem->setPage(m_materialManagerWidget);
    m_projectManagerService->addTreeItem(m_rootItem);
}

const QVector<MaterialModel> &MaterialManager::models() const
{
    return m_models;
}

QVector<MaterialModel> &MaterialManager::models()
{
    return m_models;
}

MaterialModel *MaterialManager::findModel(const QString &key)
{
    for (auto &model : m_models) {
        if (model.key == key) {
            return &model;
        }
    }
    return nullptr;
}

MaterialEntry *MaterialManager::findEntry(const QString &id, MaterialModel **outModel)
{
    for (auto &model : m_models) {
        for (auto &entry : model.materials) {
            if (entry.id == id) {
                if (outModel) {
                    *outModel = &model;
                }
                return &entry;
            }
        }
    }
    if (outModel) {
        *outModel = nullptr;
    }
    return nullptr;
}

void MaterialManager::notifyLibraryChanged(bool refreshProjectTree)
{
    if (refreshProjectTree) {
        rebuildProjectTree();
    }

    if (m_materialManagerWidget) {
        m_materialManagerWidget->refreshSummary(m_models);
    }
    if (m_materialLibraryWidget) {
        m_materialLibraryWidget->refreshModels();
    }
}

void MaterialManager::notifyEntryUpdated(const QString &entryId, bool nameChanged)
{
    if (nameChanged) {
        ProjectNode *item = m_entryItems.value(entryId, nullptr);
        MaterialEntry *entry = findEntry(entryId);
        if (item && entry) {
            item->setText(0, entry->name);
        }
    }
}

void MaterialManager::openLibrary(const QString &key, bool addNew)
{
    if (!m_materialLibraryWidget) {
        m_materialLibraryWidget = new MaterialLibraryWidget(this);
    }

    if (!key.isEmpty()) {
        m_materialLibraryWidget->setCurrentModelKey(key);
    }
    m_materialLibraryWidget->refreshModels();
    m_materialLibraryWidget->show();
    m_materialLibraryWidget->raise();
    m_materialLibraryWidget->activateWindow();

    if (addNew) {
        m_materialLibraryWidget->addMaterialAndFocus();
    }
}

QVector<QStringList> MaterialManager::getValueStrings()
{
    QVector<QStringList> results;
    for (const auto &model : m_models) {
        for (const auto &entry : model.materials) {
            QStringList row;
            row << model.key << model.displayName << entry.name;
            QStringList encoded;
            for (const QString &column : model.columns) {
                encoded << (column + QLatin1Char(':') + entry.properties.value(column));
            }
            row << encoded.join(QStringLiteral("|;|"));
            QList<int> appliedList = entry.appliedMatypes.values();
            std::sort(appliedList.begin(), appliedList.end());
            QStringList appliedStrings;
            for (int value : appliedList) {
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
    if (values.isEmpty()) {
        loadDefaultLibrary();
        return;
    }

    QVector<MaterialModel> defaults = createDefaultMaterialModels();
    m_models.clear();
    m_models.reserve(defaults.size());
    for (const auto &model : defaults) {
        MaterialModel cleared = model;
        cleared.materials.clear();
        m_models.append(cleared);
    }

    auto findIndex = [this](const QString &key) -> int {
        for (int i = 0; i < m_models.size(); ++i) {
            if (m_models.at(i).key == key) {
                return i;
            }
        }
        return -1;
    };

    for (const auto &list : values) {
        if (list.size() < 4) {
            continue;
        }
        const QString key = list.at(0);
        const QString displayName = list.at(1);
        const QString name = list.at(2);
        const QString encoded = list.at(3);
        const QString appliedMatypes = (list.size() > 4) ? list.at(4) : QString();

        int index = findIndex(key);
        if (index < 0) {
            MaterialModel extra;
            extra.key = key;
            extra.displayName = displayName;
            index = m_models.size();
            m_models.append(extra);
        }

        MaterialEntry entry;
        entry.name = name;
        const QStringList pairs = encoded.split(QStringLiteral("|;|"), QString::SkipEmptyParts);
        for (const QString &pair : pairs) {
            int sep = pair.indexOf(QLatin1Char(':'));
            if (sep <= 0) {
                continue;
            }
            QString columnName = pair.left(sep);
            QString value = pair.mid(sep + 1);
            entry.properties.insert(columnName, value);
            if (!m_models[index].columns.contains(columnName)) {
                m_models[index].columns.append(columnName);
            }
        }

        m_models[index].materials.append(entry);
        const QStringList matypeList = appliedMatypes.split(QLatin1Char(','), QString::SkipEmptyParts);
        for (const QString &matypeStr : matypeList) {
            bool ok = false;
            int value = matypeStr.toInt(&ok);
            if (ok) {
                m_models[index].materials.last().appliedMatypes.insert(value);
            }
        }
    }

    ensureEntryIds();
    notifyLibraryChanged();
}

void MaterialManager::focusOnMaterial(const QString &entryId)
{
    if (!m_projectManagerService) {
        return;
    }
    ProjectNode *item = m_entryItems.value(entryId, nullptr);
    if (item) {
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::focusOnModel(const QString &modelKey)
{
    if (!m_projectManagerService) {
        return;
    }
    ProjectNode *item = m_modelItems.value(modelKey, nullptr);
    if (item) {
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::ensureEntryIds()
{
    for (auto &model : m_models) {
        for (auto &entry : model.materials) {
            if (entry.id.isEmpty()) {
                entry.id = QUuid::createUuid().toString();
            }
        }
    }
}

void MaterialManager::rebuildProjectTree()
{
    if (!m_rootItem) {
        return;
    }

    QSet<QString> activeModelKeys;
    QSet<QString> validEntries;

    for (const QString &entryId : std::as_const(m_addedEntryIds)) {
        MaterialModel *model = nullptr;
        MaterialEntry *entry = findEntry(entryId, &model);
        if (!entry || !model) {
            continue;
        }
        validEntries.insert(entryId);
        activeModelKeys.insert(model->key);

        ProjectNode *modelItem = m_modelItems.value(model->key, nullptr);
        if (!modelItem) {
            auto *modelWidget = new MaterialModelWidget(this);
            modelItem = new ProjectNode(model->displayName, QUuid::createUuid().toString(), modelWidget);
            modelItem->setData(0, ProjectNode::RoleUuid, modelItem->getUuid());
            modelItem->setData(0, ProjectNode::RoleModule, QStringLiteral("Material"));
            modelItem->setData(0, ProjectNode::RoleKind, QStringLiteral("MaterialModel"));
            m_modelWidgets.insert(model->key, modelWidget);
            m_modelItems.insert(model->key, modelItem);
            m_rootItem->addChild(modelItem);
        }
        modelItem->setText(0, model->displayName);
        modelItem->setExpanded(true);

        if (m_modelWidgets.contains(model->key)) {
            m_modelWidgets[model->key]->setModel(*model);
        }

        ProjectNode *entryItem = m_entryItems.value(entryId, nullptr);
        if (!entryItem) {
            auto *entryWidget = new MaterialEntryWidget(this);
            entryWidget->setEntry(model->key, entryId);
            entryItem = new ProjectNode(entry->name, entryId, entryWidget);
            entryItem->setData(0, ProjectNode::RoleUuid, entryId);
            entryItem->setData(0, ProjectNode::RoleModule, QStringLiteral("Material"));
            entryItem->setData(0, ProjectNode::RoleKind, QStringLiteral("MaterialEntry"));
            m_entryWidgets.insert(entryId, entryWidget);
            m_entryItems.insert(entryId, entryItem);
            modelItem->addChild(entryItem);
        } else if (entryItem->parent() != modelItem) {
            auto *oldParent = dynamic_cast<ProjectNode *>(entryItem->parent());
            if (oldParent) {
                oldParent->removeChild(entryItem);
            }
            modelItem->addChild(entryItem);
        }
        entryItem->setText(0, entry->name);
        if (m_entryWidgets.contains(entryId)) {
            m_entryWidgets[entryId]->setEntry(model->key, entryId);
        }
    }

    // remove stale entry items
    QList<QString> staleEntries;
    for (auto it = m_entryItems.cbegin(); it != m_entryItems.cend(); ++it) {
        if (!validEntries.contains(it.key())) {
            staleEntries.append(it.key());
        }
    }
    for (const QString &id : staleEntries) {
        ProjectNode *item = m_entryItems.take(id);
        if (item) {
            if (auto *parent = dynamic_cast<ProjectNode *>(item->parent())) {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_entryWidgets.contains(id)) {
            delete m_entryWidgets.take(id);
        }
    }
    m_addedEntryIds = validEntries;

    // remove stale models
    QList<QString> staleModels;
    for (auto it = m_modelItems.cbegin(); it != m_modelItems.cend(); ++it) {
        if (!activeModelKeys.contains(it.key())) {
            staleModels.append(it.key());
        }
    }
    for (const QString &modelKey : staleModels) {
        ProjectNode *item = m_modelItems.take(modelKey);
        if (item) {
            QList<QString> orphanEntries;
            for (auto it = m_entryItems.begin(); it != m_entryItems.end();) {
                if (it.value()->parent() == item) {
                    orphanEntries.append(it.key());
                    ProjectNode *entryItem = it.value();
                    item->removeChild(entryItem);
                    delete entryItem;
                    it = m_entryItems.erase(it);
                } else {
                    ++it;
                }
            }
            for (const QString &id : orphanEntries) {
                if (m_entryWidgets.contains(id)) {
                    delete m_entryWidgets.take(id);
                }
            }
            if (auto *parent = dynamic_cast<ProjectNode *>(item->parent())) {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_modelWidgets.contains(modelKey)) {
            delete m_modelWidgets.take(modelKey);
        }
    }

    m_rootItem->setExpanded(true);
}

void MaterialManager::clearProjectTree()
{
    if (!m_rootItem) {
        return;
    }
    const auto children = m_rootItem->getChildren();
    for (ProjectNode *child : children) {
        m_rootItem->removeChild(child);
        delete child;
    }
}

void MaterialManager::addEntryToProjectTree(const QString &entryId)
{
    ensureEntryIds();
    MaterialModel *model = nullptr;
    MaterialEntry *entry = findEntry(entryId, &model);
    if (!entry || !model) {
        return;
    }

    m_addedEntryIds.insert(entryId);
    rebuildProjectTree();
}

void MaterialManager::removeEntryFromProjectTree(const QString &entryId)
{
    if (!m_addedEntryIds.contains(entryId)) {
        return;
    }
    m_addedEntryIds.remove(entryId);
    rebuildProjectTree();
}

QSet<int> MaterialManager::availableMatypes() const
{
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        return {};
    }
    return service->getAvailableMatypes();
}


void MaterialManager::highlightMatypes(const QSet<int> &matypes,
                                       int selectedMatype,
                                       int previewMatype,
                                       int preDeleteMatype)
{
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        return;
    }
    service->highlightMatypes(matypes, selectedMatype, previewMatype, preDeleteMatype);
}

void MaterialManager::refreshMatypeHighlight(const QString &entryId)
{
    MaterialEntry *entry = findEntry(entryId);
    if (!entry) return;

    const int preview   = m_previewMatypes.value(entryId, -1);
    const int preDelete = m_preDeleteMatypes.value(entryId, -1);

    if (preview >= 0) {
        highlightMatypes(QSet<int>{}, -1, preview, -1);
        return;
    }
    if (preDelete >= 0) {
        highlightMatypes(QSet<int>{}, -1, -1, preDelete);
        return;
    }

    highlightMatypes(entry->appliedMatypes, -1, -1, -1);
}


int MaterialManager::selectedMatypeForEntry(const QString &entryId) const
{
    return m_selectedMatypes.value(entryId, -1);
}

void MaterialManager::setSelectedMatypeForEntry(const QString &entryId, int matype)
{
    if (entryId.isEmpty()) {
        return;
    }
    if (matype < 0) {
        m_selectedMatypes.remove(entryId);
        return;
    }
    m_selectedMatypes.insert(entryId, matype);
}

void MaterialManager::applyMatypeToEntry(const QString &entryId, int matype)
{
    if (matype < 0) return;

    MaterialEntry *entry = findEntry(entryId);
    if (!entry) return;

    // 移除其他材料上的相同 MATYPE（你原逻辑不变）
    for (auto &model : m_models) {
        for (auto &otherEntry : model.materials) {
            if (otherEntry.id == entryId) continue;

            if (otherEntry.appliedMatypes.remove(matype)) {
                if (m_selectedMatypes.value(otherEntry.id, -1) == matype) {
                    m_selectedMatypes.remove(otherEntry.id);
                }
                if (m_previewMatypes.value(otherEntry.id, -1) == matype) {
                    m_previewMatypes.remove(otherEntry.id);
                }
                if (m_preDeleteMatypes.value(otherEntry.id, -1) == matype) {
                    m_preDeleteMatypes.remove(otherEntry.id);
                }

                refreshEntryWidgetMatypes(otherEntry.id,
                                          otherEntry.appliedMatypes,
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

void MaterialManager::removeMatypeFromEntry(const QString &entryId, int matype)
{
    if (matype < 0) return;

    MaterialEntry *entry = findEntry(entryId);
    if (!entry) return;

    if (entry->appliedMatypes.remove(matype)) {
        if (m_selectedMatypes.value(entryId, -1) == matype) {
            m_selectedMatypes.remove(entryId);
        }
        if (m_previewMatypes.value(entryId, -1) == matype) {
            m_previewMatypes.remove(entryId);
        }
        if (m_preDeleteMatypes.value(entryId, -1) == matype) {
            m_preDeleteMatypes.remove(entryId);
        }

        refreshEntryWidgetMatypes(entryId, entry->appliedMatypes,
                                  m_selectedMatypes.value(entryId, -1));
        notifyEntryUpdated(entryId, false);
        refreshMatypeHighlight(entryId);
    }
}


void MaterialManager::refreshEntryWidgetMatypes(const QString &entryId, const QSet<int> &applied, int selectedMatype)
{
    auto *widget = m_entryWidgets.value(entryId, nullptr);
    if (!widget) {
        return;
    }
    widget->refreshAppliedMatypes(applied, selectedMatype);
}

bool MaterialManager::generateCaculateFile()
{
    // ===== 1) 收集：只要“已添加到工程 + 关联了 MATYPE”的材料，并建立 matype -> (model, entry) 映射 =====
    struct MatInfo {
        MaterialModel* model = nullptr;
        MaterialEntry* entry = nullptr;
    };

    QMap<int, MatInfo> matypeMap; // matype -> info
    int maxMatype = -1;

    for (auto &model : m_models)
    {
        for (auto &entry : model.materials)
        {
            if (entry.appliedMatypes.isEmpty()) continue;
            if (!m_addedEntryIds.contains(entry.id)) continue;

            for (int matype : entry.appliedMatypes)
            {
                if (matype < 0) continue;
                maxMatype = qMax(maxMatype, matype);

                // 如果同一个 MATYPE 被多个材料占用，这里默认“后者覆盖前者”
                matypeMap[matype] = MatInfo{ &model, &entry };
            }
        }
    }

    if (maxMatype < 0 || m_workPath.isEmpty())
        return false;

    // ===== 2) NMAT / NCON =====
    const int ncon = 200;
    const int nmatReal = maxMatype + 1;                         // 0..maxMatype
    const int nmat = qMax(10, ((nmatReal + 9) / 10) * 10);      // 补齐到 10 的倍数（每组 10 列）

    // ===== 3) 输出文件 =====
    QDir workDir(m_workPath);
    if (!workDir.exists("input")) workDir.mkpath("input");
    const QString filePath = workDir.filePath(QStringLiteral("input/Imat.dat"));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setCodec("UTF-8");

    // ===== 4) 预先构造数值表：values[row(0..199)][matype(0..nmat-1)] =====
    QVector<QVector<double>> values(ncon, QVector<double>(nmat, 0.0));

    // 填值：按 model->columnOutputRows 写入对应行
    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        const int matype = it.key();
        const MatInfo &info = it.value();
        if (!info.model || !info.entry) continue;
        if (matype < 0 || matype >= nmat) continue;

        const auto &cols = info.model->columns;
        const auto &lineMap = info.model->columnOutputRows;
        const int count = qMin(cols.size(), lineMap.size());

        for (int c = 0; c < count; ++c)
        {
            const int targetRow = lineMap.at(c); // 1-based
            if (targetRow <= 0 || targetRow > ncon) continue;

            const QString key = cols.at(c);
            const QString strVal = info.entry->properties.value(key, QStringLiteral("0"));

            bool ok = false;
            const double v = strVal.toDouble(&ok);
            values[targetRow - 1][matype] = ok ? v : 0.0;
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

        const QString mantStr = QString::asprintf("% .4f", mant);  // 正数前带空格，负数带 -
        const QString expStr  = QString::asprintf("E%+03d", exp);  // E+00 / E+01 / E-01

        // 每格固定宽度，方便列对齐
        return QString("%1").arg(mantStr + expStr, 13);
    };

    auto imatLabel = [](int matype) -> QString
    {
        // 尽量贴近你示例：IMAT= 10 这种（两位数前空一格）
        if (matype < 10)  return QStringLiteral("IMAT=%1").arg(matype);
        if (matype < 100) return QStringLiteral("IMAT= %1").arg(matype);
        return QStringLiteral("IMAT=%1").arg(matype);
    };

    // ===== 6) 文件头 =====
    // 示例：        300         200    NMAT  NCON
    out << QString("%1%2    NMAT  NCON\n").arg(nmat, 12).arg(ncon, 12);

    // ===== 7) 分组输出：每组 10 列 =====
    const int groupCount = nmat / 10;

    for (int g = 0; g < groupCount; ++g)
    {
        const int startMat = g * 10;

        out << QStringLiteral(" CONMAT(NMAT,NCON)   GROUP NUM=%1\n").arg(g + 1, 4);

        // 组头 IMAT=...
        QStringList head;
        head.reserve(10);
        for (int k = 0; k < 10; ++k)
        {
            head << imatLabel(startMat + k);
        }
        out << head.join(QStringLiteral("       ")) << "\n";

        // 200 行
        for (int r = 0; r < ncon; ++r)
        {
            QString line;
            line.reserve(10 * 13 + 8);

            for (int k = 0; k < 10; ++k)
            {
                const int matype = startMat + k;
                const double v = (matype >= 0 && matype < nmat) ? values[r][matype] : 0.0;
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

void MaterialManager::clearMatypeHighlight(const QString &entryId)
{
    if (entryId.isEmpty()) {
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

int MaterialManager::previewMatypeForEntry(const QString &entryId) const
{
    return m_previewMatypes.value(entryId, -1);
}

void MaterialManager::setPreviewMatypeForEntry(const QString &entryId, int matype)
{
    if (entryId.isEmpty()) return;
    if (matype < 0) {
        m_previewMatypes.remove(entryId);
    } else {
        m_previewMatypes.insert(entryId, matype);
    }
}

int MaterialManager::preDeleteMatypeForEntry(const QString &entryId) const
{
    return m_preDeleteMatypes.value(entryId, -1);
}

void MaterialManager::setPreDeleteMatypeForEntry(const QString &entryId, int matype)
{
    if (entryId.isEmpty()) return;
    if (matype < 0) {
        m_preDeleteMatypes.remove(entryId);
    } else {
        m_preDeleteMatypes.insert(entryId, matype);
    }
}
