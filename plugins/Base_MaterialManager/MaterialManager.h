#pragma once

#include <QObject>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>

#include <struct/MeshMap.h>
#include <services/MaterialManagerService.h>
#include <services/ProjectManagerService.h>

#include "MaterialLibraryData.h"

class BasePage;
class ProjectNode;
class MaterialManagerWidget;
class MaterialModelWidget;
class MaterialEntryWidget;
class MaterialTransitionPage;
class ProjectManagerService;

class MaterialManager : public QObject, public MaterialManagerService
{
    Q_OBJECT

  public:
    explicit MaterialManager();

    BasePage* getMaterialManagerWidget();

    void newProject();
    bool saveProject();
    bool loadProject();
    void newProjectForPath(const QString& workPath);

    const QVector<MaterialModel>& models() const;
    QVector<MaterialModel>& models();

    MaterialModel* findModel(const QString& key);
    MaterialEntry* findEntry(const QString& id, MaterialModel** outModel = nullptr);

    void notifyLibraryChanged(bool refreshProjectTree = true);
    void notifyEntryUpdated(const QString& entryId, bool nameChanged);

    QVector<QStringList> getValueStrings();
    void setValueStrings(QVector<QStringList> values);

    static bool isAutoNumberColumn(const QString& columnName);

    bool isSeepageModelKey(const QString& modelKey) const;
    const QVector<MaterialModel>& seepageModels() const;

    QString addMaterialToModel(const QString& modelKey, bool addToProjectTree = true);
    void addMaterialFromMenu(const QString& modelKey);

    void showBuiltinMaterialListDialog();
    void showBuiltinSeepageListDialog();
    void showMaterialTransitionDialog();

    QVector<QPair<int, int>> materialTransitions() const;
    void setMaterialTransitions(const QVector<QPair<int, int>>& transitions);

    bool validateTransitionMatype(const QString& entryId, int matype, QString* error = nullptr) const;
    QMap<int, QString> transitionSourceLabels() const;
    QMap<int, QString> transitionTargetLabels() const;

    void focusOnMaterial(const QString& entryId);
    void focusOnModel(const QString& modelKey);

    void addEntryToProjectTree(const QString& entryId);
    void removeEntryFromProjectTree(const QString& entryId);

    QVector<MeshTagSet> availableDomainTagSets() const;

    void highlightDomainTags(const QString& entryId,
                             const QSet<MeshTagKey>& tags,
                             const MeshTagKey& selected,
                             const MeshTagKey& preview,
                             const MeshTagKey& preDelete);
    void refreshDomainTagHighlight(const QString& entryId);

    MeshTagKey selectedDomainTagForEntry(const QString& entryId) const;
    void setSelectedDomainTagForEntry(const QString& entryId, const MeshTagKey& key);

    MeshTagKey previewDomainTagForEntry(const QString& entryId) const;
    void setPreviewDomainTagForEntry(const QString& entryId, const MeshTagKey& key);

    MeshTagKey preDeleteDomainTagForEntry(const QString& entryId) const;
    void setPreDeleteDomainTagForEntry(const QString& entryId, const MeshTagKey& key);

    void applyDomainTagToEntry(const QString& entryId, const MeshTagKey& key);
    void removeDomainTagFromEntry(const QString& entryId, const MeshTagKey& key);

    void refreshEntryWidgetDomainTags(const QString& entryId,
                                      const QSet<MeshTagKey>& applied,
                                      const MeshTagKey& selected);

    bool generateCaculateFile();
    bool generateIMATFile();

    void clearDomainTagHighlight(const QString& entryId);
    void clearAllDomainTagHighlight();

    void setPickContext(const QString& domain, const QString& uuid);
    void clearPickContext();

  private:
    void setupTreeWidget();
    void loadDefaultLibrary();
    void ensureTransitionNode();
    void ensureEntryIds();
    void rebuildProjectTree();
    void clearProjectTree();
    void rebuildDomainTagOwners();

  private:
    ProjectManagerService* m_projectManagerService = nullptr;

    MaterialManagerWidget* m_materialManagerWidget = nullptr;
    MaterialTransitionPage* m_transitionPage = nullptr;

    ProjectNode* m_rootItem = nullptr;
    ProjectNode* m_transitionNode = nullptr;

    QHash<QString, ProjectNode*> m_modelItems;
    QHash<QString, ProjectNode*> m_entryItems;

    QHash<QString, MaterialModelWidget*> m_modelWidgets;
    QHash<QString, MaterialEntryWidget*> m_entryWidgets;

    QVector<MaterialModel> m_models;
    QVector<MaterialModel> m_seepageModels;
    QVector<QPair<int, int>> m_materialTransitions;

    QSet<QString> m_addedEntryIds;

    QHash<QString, MeshTagKey> m_selectedDomainTags;
    QHash<QString, MeshTagKey> m_previewDomainTags;
    QHash<QString, MeshTagKey> m_preDeleteDomainTags;
    QHash<MeshTagKey, QString> m_domainTagOwnerByKey;

    QString m_workPath;
    QString m_pickDomain;
    QString m_pickUuid;
};
