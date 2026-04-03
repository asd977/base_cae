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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTableWidget>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>
#include <cmath>
#include <QDialog>
#include <QHeaderView>
#include <QVBoxLayout>

#include "MaterialEntryWidget.h"
#include "MaterialModelWidget.h"

namespace
{
void showBuiltInModelDialog(const QString& title, const QVector<MaterialModel>& models, QWidget* parent)
{
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(900, 520);

    auto* layout = new QVBoxLayout(dialog);
    auto* table = new QTableWidget(dialog);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("模型名称"), QStringLiteral("模型Key"),
         QStringLiteral("IB"), QStringLiteral("字段数")});
    table->setRowCount(models.size());
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);

    for (int row = 0; row < models.size(); ++row)
    {
        const auto& model = models[row];
        table->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        table->setItem(row, 1, new QTableWidgetItem(model.displayName));
        table->setItem(row, 2, new QTableWidgetItem(model.key));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(model.ib)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(model.columns.size())));
    }

    table->resizeColumnsToContents();
    layout->addWidget(table);
    dialog->show();
}

void syncAutoNumberProperties(MaterialModel* model, MaterialEntry* entry)
{
    if (!model || !entry)
    {
        return;
    }

    QList<int> matypes = entry->appliedMatypes.values();
    std::sort(matypes.begin(), matypes.end());

    QStringList tokens;
    for (int matype : matypes)
    {
        if (matype >= 0)
        {
            tokens.append(QString::number(matype));
        }
    }
    const QString value = tokens.join(QLatin1Char(','));

    for (const QString& column : model->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
        {
            entry->properties.insert(column, value);
        }
    }
}
} // namespace

MaterialManager::MaterialManager()
    : m_materialManagerWidget(nullptr), m_rootItem(nullptr)
{
    m_projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    ServiceRegistry::instance().registerService<MaterialManagerService>(this);
    setupTreeWidget();
    m_seepageModels = createSeepageSubModels();
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

bool MaterialManager::saveProject()
{
    if (m_workPath.isEmpty())
    {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("module"), QStringLiteral("Material"));

    QJsonArray modelArray;
    for (const auto& model : m_models)
    {
        QJsonObject modelObj;
        modelObj.insert(QStringLiteral("key"), model.key);
        modelObj.insert(QStringLiteral("display_name"), model.displayName);

        QJsonArray entryArray;
        for (const auto& entry : model.materials)
        {
            QJsonObject entryObj;
            entryObj.insert(QStringLiteral("id"), entry.id);
            entryObj.insert(QStringLiteral("name"), entry.name);

            QJsonObject propertiesObj;
            for (auto it = entry.properties.constBegin(); it != entry.properties.constEnd(); ++it)
            {
                propertiesObj.insert(it.key(), it.value());
            }
            entryObj.insert(QStringLiteral("properties"), propertiesObj);

            QList<int> matypes = entry.appliedMatypes.values();
            std::sort(matypes.begin(), matypes.end());
            QJsonArray appliedArray;
            for (int matype : matypes)
            {
                appliedArray.append(matype);
            }
            entryObj.insert(QStringLiteral("applied_matypes"), appliedArray);

            entryObj.insert(QStringLiteral("seepage_model_key"), entry.seepageModelKey);
            QJsonObject seepagePropertiesObj;
            for (auto it = entry.seepageProperties.constBegin();
                 it != entry.seepageProperties.constEnd(); ++it)
            {
                seepagePropertiesObj.insert(it.key(), it.value());
            }
            entryObj.insert(QStringLiteral("seepage_properties"), seepagePropertiesObj);
            entryArray.append(entryObj);
        }

        modelObj.insert(QStringLiteral("entries"), entryArray);
        modelArray.append(modelObj);
    }
    root.insert(QStringLiteral("models"), modelArray);

    QJsonArray addedEntryArray;
    const QList<QString> addedIds = m_addedEntryIds.values();
    for (const QString& id : addedIds)
    {
        addedEntryArray.append(id);
    }
    root.insert(QStringLiteral("added_entry_ids"), addedEntryArray);

    QFile file(QDir(m_workPath).filePath(PublicConstants::kMaterialProjectFileName));
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool MaterialManager::loadProject()
{
    const QString workPath =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    newProjectForPath(workPath);

    if (m_workPath.isEmpty())
    {
        return false;
    }

    QFile file(QDir(m_workPath).filePath(PublicConstants::kMaterialProjectFileName));
    if (!file.open(QIODevice::ReadOnly))
    {
        return true;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (!root.contains(QStringLiteral("models")))
    {
        return true;
    }

    const QVector<MaterialModel> defaults = createDefaultMaterialModels();
    m_models.clear();
    m_models.reserve(defaults.size());
    for (const auto& model : defaults)
    {
        MaterialModel copy = model;
        copy.materials.clear();
        m_models.append(copy);
    }

    auto findModelIndex = [this](const QString& key) -> int
    {
        for (int i = 0; i < m_models.size(); ++i)
        {
            if (m_models[i].key == key)
            {
                return i;
            }
        }
        return -1;
    };

    const QJsonArray modelArray = root.value(QStringLiteral("models")).toArray();
    for (const auto& modelValue : modelArray)
    {
        const QJsonObject modelObj = modelValue.toObject();
        const QString key = modelObj.value(QStringLiteral("key")).toString();
        const QString displayName = modelObj.value(QStringLiteral("display_name")).toString();

        int modelIndex = findModelIndex(key);
        if (modelIndex < 0)
        {
            MaterialModel extra;
            extra.key = key;
            extra.displayName = displayName;
            modelIndex = m_models.size();
            m_models.append(extra);
        }

        const QJsonArray entries = modelObj.value(QStringLiteral("entries")).toArray();
        for (const auto& entryValue : entries)
        {
            const QJsonObject entryObj = entryValue.toObject();
            MaterialEntry entry;
            entry.id = entryObj.value(QStringLiteral("id")).toString();
            entry.name = entryObj.value(QStringLiteral("name")).toString();

            const QJsonObject properties = entryObj.value(QStringLiteral("properties")).toObject();
            for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
            {
                entry.properties.insert(it.key(), it.value().toString());
                if (!m_models[modelIndex].columns.contains(it.key()))
                {
                    m_models[modelIndex].columns.append(it.key());
                }
            }

            const QJsonArray applied = entryObj.value(QStringLiteral("applied_matypes")).toArray();
            for (const auto& matypeValue : applied)
            {
                entry.appliedMatypes.insert(matypeValue.toInt(-1));
            }

            entry.seepageModelKey = entryObj.value(QStringLiteral("seepage_model_key")).toString();
            const QJsonObject seepageProperties =
                entryObj.value(QStringLiteral("seepage_properties")).toObject();
            for (auto it = seepageProperties.constBegin(); it != seepageProperties.constEnd(); ++it)
            {
                entry.seepageProperties.insert(it.key(), it.value().toString());
            }

            syncAutoNumberProperties(&m_models[modelIndex], &entry);

            m_models[modelIndex].materials.append(entry);
        }
    }

    m_addedEntryIds.clear();
    const QJsonArray addedArray = root.value(QStringLiteral("added_entry_ids")).toArray();
    for (const auto& idValue : addedArray)
    {
        const QString id = idValue.toString();
        if (!id.isEmpty())
        {
            m_addedEntryIds.insert(id);
        }
    }

    ensureEntryIds();
    notifyLibraryChanged();
    // 工程打开时会在后台重建条目页面，避免把条目高亮残留到初始状态。
    clearAllMatypeHighlight();
    return true;
}

void MaterialManager::addMaterialFromMenu(const QString& modelKey)
{
    const QString entryId = addMaterialToModel(modelKey, true);
    if (!entryId.isEmpty())
    {
        focusOnMaterial(entryId);
    }
}

void MaterialManager::showBuiltinMaterialListDialog()
{
    showBuiltInModelDialog(QStringLiteral("内置材料模型列表（9种）"), createDefaultMaterialModels(),
                           m_materialManagerWidget);
}

void MaterialManager::showBuiltinSeepageListDialog()
{
    showBuiltInModelDialog(QStringLiteral("内置渗流参数模型列表（5种）"), createSeepageSubModels(),
                           m_materialManagerWidget);
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
        entry.properties.insert(column, isAutoNumberColumn(column) ? QString() : QStringLiteral("0"));
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
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
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
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
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
        syncAutoNumberProperties(&m_models[index], &m_models[index].materials.last());
    }

    ensureEntryIds();
    notifyLibraryChanged();
}

bool MaterialManager::isAutoNumberColumn(const QString& columnName)
{
    return columnName == QStringLiteral("序号") || columnName == QStringLiteral("编号") ||
           columnName == QStringLiteral("材料编号");
}

bool MaterialManager::isSeepageModelKey(const QString& modelKey) const
{
    for (const auto& model : m_seepageModels)
    {
        if (model.key == modelKey)
        {
            return true;
        }
    }
    return false;
}

const QVector<MaterialModel>& MaterialManager::seepageModels() const
{
    return m_seepageModels;
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

                syncAutoNumberProperties(&model, &otherEntry);
                refreshEntryWidgetMatypes(otherEntry.id, otherEntry.appliedMatypes,
                                          m_selectedMatypes.value(otherEntry.id, -1));
            }
        }
    }

    setSelectedMatypeForEntry(entryId, matype);
    entry->appliedMatypes.insert(matype);
    MaterialModel* model = nullptr;
    findEntry(entryId, &model);
    syncAutoNumberProperties(model, entry);

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

    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
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

        syncAutoNumberProperties(model, entry);
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
    if (!workDir.exists("PREP"))
        workDir.mkpath("PREP");
    const QString filePath = workDir.filePath(QString("PREP/IMAT.DAT"));

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

    auto seepageValueOf = [](const MaterialEntry* entry, const QString& key) -> double
    {
        if (!entry)
        {
            return 0.0;
        }
        bool ok = false;
        const double value = entry->seepageProperties.value(key, QStringLiteral("0")).toDouble(&ok);
        return ok ? value : 0.0;
    };

    auto applySeepageModel = [&](const QString& seepageModelKey, const MaterialEntry* entry,
                                 int matIndex)
    {
        auto setRow = [&](int row, double value)
        {
            if (row >= 1 && row <= ncon)
            {
                values[row - 1][matIndex] = value;
            }
        };

        if (seepageModelKey == QStringLiteral("jk_corewall_steady_seepage"))
        {
            const double tol = 1.0e-3;
            const double akLu = seepageValueOf(entry, QStringLiteral("渗透系数K(Lu)"));
            const double rip = seepageValueOf(entry, QStringLiteral("允许比降"));
            const double ak = akLu * 1.0e-7;
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
            return;
        }
        if (seepageModelKey == QStringLiteral("shenzhujiang_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数α")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数b")));
            setRow(16, seepageValueOf(entry, QStringLiteral("剪切流变系数d")));
            setRow(102, 1.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("liguoying_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数α")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数b")));
            setRow(16, seepageValueOf(entry, QStringLiteral("体积流变系数c")));
            setRow(17, seepageValueOf(entry, QStringLiteral("剪切流变系数d")));
            setRow(18, seepageValueOf(entry, QStringLiteral("体积流变指数m1")));
            setRow(19, seepageValueOf(entry, QStringLiteral("体积流变指数m2")));
            setRow(20, seepageValueOf(entry, QStringLiteral("剪切流变指数m3")));
            setRow(102, 2.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("nine_param_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数λv")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数Cα")));
            setRow(16, seepageValueOf(entry, QStringLiteral("体积流变系数Cβ")));
            setRow(17, seepageValueOf(entry, QStringLiteral("体积流变指数dα")));
            setRow(18, seepageValueOf(entry, QStringLiteral("体积流变指数dβ")));
            setRow(19, seepageValueOf(entry, QStringLiteral("主应变系数c")));
            setRow(20, seepageValueOf(entry, QStringLiteral("主应变系数d")));
            setRow(21, seepageValueOf(entry, QStringLiteral("主应变流变系数η")));
            setRow(22, seepageValueOf(entry, QStringLiteral("主应变流变系数m")));
            setRow(102, 3.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("xiyuan_creep"))
        {
            setRow(2, seepageValueOf(entry, QStringLiteral("瞬时弹性模量E1（GPa）")));
            setRow(3, seepageValueOf(entry, QStringLiteral("粘塑性模量E2（GPa）")));
            setRow(4, seepageValueOf(entry, QStringLiteral("泊松比μ1")));
            setRow(5, seepageValueOf(entry, QStringLiteral("泊松比μ2")));
            setRow(103, seepageValueOf(entry, QStringLiteral("粘弹性系数η1")));
            setRow(104, seepageValueOf(entry, QStringLiteral("粘塑性系数η2")));
            setRow(102, 4.0);
        }
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

        if (info.model->key == QStringLiteral("huangjinping_underground_plant")) // 黄金坪厂房(样表1 2)
        {
            const double matypeOri = static_cast<double>(matype);
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
            double rt = 0.0;
            double rc = 0.0;

            if (co > 1.0e-1)
            {
                const double faiRad = fai * 3.141592653589793 / 180.0;
                const double sinF = std::sin(faiRad);
                const double cosF = std::cos(faiRad);

                rt = 2.0 * co * cosF / (1.0 + sinF);
                rc = 2.0 * co * cosF / (1.0 - sinF);

                if (rt < co)
                    rt = co;
            }
            else
            {
                rt = 1.0;
                rc = 2.0;
            }

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
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("dagangshan_isotropic")) // 大岗山各向同性
        {
            const double eeGpa = valueOf(info.entry, QStringLiteral("变形模量E（GPa）"));
            const double pu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rouTm3 = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double ff = valueOf(info.entry, QStringLiteral("抗剪断强度内摩擦系数"));
            const double coMpa = valueOf(info.entry, QStringLiteral("凝聚力C‘（MPa）"));

            const double ibuck = 2.0;
            const double matypeOri = static_cast<double>(matype);
            const double itagEx = 0.0;
            const double alfaBa = 0.0;
            const double laReborn = 0.0;

            const double ee = eeGpa * 1.0e9;
            const double rou = rouTm3 * 1.0e3;
            const double co = coMpa * 1.0e6;
            const double fai = std::atan(ff) * 180.0 / 3.141592653589793;
            const double gg = (std::fabs(1.0 + pu) > 1.0e-12) ? (ee / 2.0 / (1.0 + pu)) : 0.0;

            // 默认替代：用 Mohr-Coulomb 近似计算 RC/RT（原 DEFAULT_RTRC 未给出）
            double rt = 0.0;
            double rc = 0.0;

            if (co > 1.0e-1)
            {
                const double faiRad = fai * 3.141592653589793 / 180.0;
                const double sinF = std::sin(faiRad);
                const double cosF = std::cos(faiRad);

                rt = 2.0 * co * cosF / (1.0 + sinF);
                rc = 2.0 * co * cosF / (1.0 - sinF);

                if (rt < co)
                    rt = co;
            }
            else
            {
                rt = 1.0;
                rc = 2.0;
            }

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
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_eb")) // 土体E-B模型
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
            setRow(75, dryN);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, fai);
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_emu")) // 土体E-μ模型
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
            setRow(72, dryK * 1.5);   // 对应 DRYK * 1.5 (公式中提到的 KUR)
            setRow(73, dryN);          // 对应 DRYN (公式中提到的 NUR)
            setRow(77, g);             // 对应 DRYG
            setRow(78, d);             // 对应 DRYD
            setRow(79, f);             // 对应 DRYF
            setRow(75, g);             // 找不到
            setRow(76, dFai);
            setRow(78, d);
            setRow(79, f);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_emu_crush")) // 土体E-μ破碎模型
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
            const double cbk1 = valueOf(info.entry, QStringLiteral("C1"));
            const double cn1 = valueOf(info.entry, QStringLiteral("n1"));
            const double cbk2 = valueOf(info.entry, QStringLiteral("C2"));
            const double cn2 = valueOf(info.entry, QStringLiteral("n2"));
            const double cbk3 = valueOf(info.entry, QStringLiteral("C3"));
            const double cn3 = valueOf(info.entry, QStringLiteral("n3"));

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

            setRow(1, -6.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryK * 1.5);
            setRow(73, dryN);
            setRow(75, dryN);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(109, cbk1);
            setRow(110, cn1);
            setRow(111, cbk2);
            setRow(112, cn2);
            setRow(113, cbk3);
            setRow(114, cn3);
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_nanshui")) // 南水模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryCd = valueOf(info.entry, QStringLiteral("cd"));
            const double dryNd = valueOf(info.entry, QStringLiteral("nd"));
            const double dryRd = valueOf(info.entry, QStringLiteral("Rd"));

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

            setRow(1, -5.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryN);
            setRow(77, dryCd);
            setRow(78, dryNd);
            setRow(79, dryRd);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_kg")) // 土体K-G模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double aki = valueOf(info.entry, QStringLiteral("Ki,Kie(10^5Pa)"));
            const double alfak = valueOf(info.entry, QStringLiteral("αk,αke"));
            const double agi = valueOf(info.entry, QStringLiteral("Gi,Gie(10^5Pa)"));
            const double alfag = valueOf(info.entry, QStringLiteral("αG,αGe"));
            const double betag = valueOf(info.entry, QStringLiteral("βG"));
            const double akg = valueOf(info.entry, QStringLiteral("a(10^5Pa)"));
            const double bkg = valueOf(info.entry, QStringLiteral("b"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -2.0);
            setRow(8, 1.0);
            setRow(7, dryRou);
            setRow(90, pacRou);
            setRow(89, wetRou);
            setRow(69, aki);
            setRow(70, alfak);
            setRow(74, agi);
            setRow(71, alfag);
            setRow(75, betag);
            setRow(72, akg);
            setRow(73, bkg);
            setRow(76, aki);
            setRow(78, alfak);
            setRow(77, agi);
            setRow(79, alfag);
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("shenzhujiang_creep")) // 沈珠江三参数流变
        {
            const double a1 = valueOf(info.entry, QStringLiteral("应变速率系数α"));
            const double b1 = valueOf(info.entry, QStringLiteral("体积流变系数b"));
            const double d1 = valueOf(info.entry, QStringLiteral("剪切流变系数d"));
            const double icreep = 1.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, a1);
            setRow(15, b1);
            setRow(16, d1);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("liguoying_creep")) // 李国英七参数流变
        {
            const double a1 = valueOf(info.entry, QStringLiteral("应变速率系数α"));
            const double b1 = valueOf(info.entry, QStringLiteral("体积流变系数b"));
            const double c1 = valueOf(info.entry, QStringLiteral("体积流变系数c"));
            const double d1 = valueOf(info.entry, QStringLiteral("剪切流变系数d"));
            const double fm1 = valueOf(info.entry, QStringLiteral("体积流变指数m1"));
            const double fm2 = valueOf(info.entry, QStringLiteral("体积流变指数m2"));
            const double fm3 = valueOf(info.entry, QStringLiteral("剪切流变指数m3"));
            const double icreep = 2.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, a1);
            setRow(15, b1);
            setRow(16, c1);
            setRow(17, d1);
            setRow(18, fm1);
            setRow(19, fm2);
            setRow(20, fm3);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("nine_param_creep")) // 九参数流变
        {
            const double almdV = valueOf(info.entry, QStringLiteral("应变速率系数λv"));
            const double cAlfa = valueOf(info.entry, QStringLiteral("体积流变系数Cα"));
            const double cBeta = valueOf(info.entry, QStringLiteral("体积流变系数Cβ"));
            const double dAlfa = valueOf(info.entry, QStringLiteral("体积流变指数dα"));
            const double dBeta = valueOf(info.entry, QStringLiteral("体积流变指数dβ"));
            const double c1 = valueOf(info.entry, QStringLiteral("主应变系数c"));
            const double d1 = valueOf(info.entry, QStringLiteral("主应变系数d"));
            const double ata1 = valueOf(info.entry, QStringLiteral("主应变流变系数η"));
            const double fm1 = valueOf(info.entry, QStringLiteral("主应变流变系数m"));
            const double icreep = 3.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, almdV);
            setRow(15, cAlfa);
            setRow(16, cBeta);
            setRow(17, dAlfa);
            setRow(18, dBeta);
            setRow(19, c1);
            setRow(20, d1);
            setRow(21, ata1);
            setRow(22, fm1);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("xiyuan_creep")) // 西源流变
        {
            const double ee1 = valueOf(info.entry, QStringLiteral("瞬时弹性模量E1（GPa）"));
            const double ee2 = valueOf(info.entry, QStringLiteral("粘塑性模量E2（GPa）"));
            const double pu1 = valueOf(info.entry, QStringLiteral("泊松比μ1"));
            const double pu2 = valueOf(info.entry, QStringLiteral("泊松比μ2"));
            const double vc1 = valueOf(info.entry, QStringLiteral("粘弹性系数η1"));
            const double vc2 = valueOf(info.entry, QStringLiteral("粘塑性系数η2"));
            const double icreep = 4.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(2, ee1);
            setRow(3, ee2);
            setRow(4, pu1);
            setRow(5, pu2);
            setRow(103, vc1);
            setRow(104, vc2);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("dam_dynamic")) // 坝料动力
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
            setRow(61, 0.1);
            setRow(62, 0.3);
            setRow(63, 0.5);
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
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("jk_corewall_steady_seepage")) // 各向同性稳定渗流场参数
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

        if (info.model->key == QStringLiteral("huangjinping_bar_element")) // 黄金坪杆单元
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
            applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
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

        applySeepageModel(info.entry->seepageModelKey, info.entry, matIndex);
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

    // ===== 8) 同步生成 MACB.DAT / MACHAN.DAT =====
    const QList<int> macbMatypes = matypeMap.keys(); // QMap keys 默认升序

    auto writeNumDat = [&](const QString& fileName, const QList<int>& ids) -> bool
    {
        QFile outFile(workDir.filePath(QString("PREP/%1").arg(fileName)));
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return false;
        }

        QTextStream ts(&outFile);
        ts << QString("%1   NUM\n").arg(ids.size(), 12);
        for (int id : ids)
        {
            ts << QString("%1\n").arg(id, 8);
        }
        outFile.close();
        return true;
    };

    if (!writeNumDat(QStringLiteral("MACB.DAT"), macbMatypes))
    {
        return false;
    }
    if (!writeNumDat(QStringLiteral("MACHAN.DAT"), QList<int>{}))
    {
        return false;
    }

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
