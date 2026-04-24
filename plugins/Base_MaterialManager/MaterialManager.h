#ifndef MATERIALMANAGER_H
#define MATERIALMANAGER_H

#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/ProjectManagerService.h>

#include <QHash>
#include <QMap>
#include <QSet>
#include <QSqlDatabase>

#include "MaterialLibraryData.h"
#include "MaterialManagerWidget.h"

class MaterialManagerWidget;
class MaterialEntryWidget;
class MaterialModelWidget;
class MaterialTransitionPage;
class ProjectNode;

class MaterialManager : public QObject, public MaterialManagerService
{
    Q_OBJECT
  public:
    MaterialManager();

    BasePage* getMaterialManagerWidget() override;
    void newProject() override;
    bool saveProject() override;
    bool loadProject() override;
    void addMaterialFromMenu(const QString& modelKey) override;
    void showBuiltinMaterialListDialog() override;
    void showBuiltinSeepageListDialog() override;
    void showMaterialTransitionDialog() override;
    QVector<QPair<int, int>> materialTransitions() const override;
    void setMaterialTransitions(const QVector<QPair<int, int>>& transitions) override;
    bool generateIMATFile() override;

    QString addMaterialToModel(const QString& modelKey, bool addToProjectTree = true);

    const QVector<MaterialModel>& models() const;
    QVector<MaterialModel>& models();

    MaterialModel* findModel(const QString& key);
    MaterialEntry* findEntry(const QString& id, MaterialModel** outModel = nullptr);

    void notifyLibraryChanged(bool refreshProjectTree = true);
    void notifyEntryUpdated(const QString& entryId, bool nameChanged);

    void focusOnMaterial(const QString& entryId);
    void focusOnModel(const QString& modelKey);

    void addEntryToProjectTree(const QString& entryId);
    void removeEntryFromProjectTree(const QString& entryId);

    QSet<int> availableDomainGroups() const;
    QSet<int> availableMatypes() const; // deprecated

    // ===== MATYPE 高亮核心接口 =====
    void refreshDomainGroupHighlight(const QString& entryId);
    void clearDomainGroupHighlight(const QString& entryId);
    void clearAllDomainGroupHighlight();

    int selectedDomainGroupForEntry(const QString& entryId) const;
    void setSelectedDomainGroupForEntry(const QString& entryId, int groupId);

    int previewDomainGroupForEntry(const QString& entryId) const;
    void setPreviewDomainGroupForEntry(const QString& entryId, int groupId);

    int preDeleteDomainGroupForEntry(const QString& entryId) const;
    void setPreDeleteDomainGroupForEntry(const QString& entryId, int groupId);

    void applyDomainGroupToEntry(const QString& entryId, int groupId);
    void removeDomainGroupFromEntry(const QString& entryId, int groupId);

    void refreshEntryWidgetDomainGroups(const QString& entryId, const QSet<int>& applied,
                                        int selectedGroupId);

    void refreshMatypeHighlight(const QString& entryId); // deprecated
    void clearMatypeHighlight(const QString& entryId);   // deprecated
    void clearAllMatypeHighlight();                      // deprecated
    int selectedMatypeForEntry(const QString& entryId) const; // deprecated
    void setSelectedMatypeForEntry(const QString& entryId, int matype); // deprecated
    int previewMatypeForEntry(const QString& entryId) const; // deprecated
    void setPreviewMatypeForEntry(const QString& entryId, int matype); // deprecated
    int preDeleteMatypeForEntry(const QString& entryId) const; // deprecated
    void setPreDeleteMatypeForEntry(const QString& entryId, int matype); // deprecated
    void applyMatypeToEntry(const QString& entryId, int matype); // deprecated
    void removeMatypeFromEntry(const QString& entryId, int matype); // deprecated
    void refreshEntryWidgetMatypes(const QString& entryId, const QSet<int>& applied,
                                   int selectedMatype); // deprecated

    bool generateCaculateFile();

    QVector<QStringList> getValueStrings();
    void setValueStrings(QVector<QStringList> values);
    static bool isAutoNumberColumn(const QString& columnName);
    bool isSeepageModelKey(const QString& modelKey) const;
    const QVector<MaterialModel>& seepageModels() const;
    bool validateTransitionMatype(const QString& entryId, int matype, QString* error) const;
    QMap<int, QString> transitionSourceLabels() const;
    QMap<int, QString> transitionTargetLabels() const;
    void setPickContext(const QString& domain, const QString& uuid);
    void clearPickContext();

  private:
    void ensureTransitionNode();
    void newProjectForPath(const QString& workPath);
    void setupTreeWidget();
    void loadDefaultLibrary();
    void ensureEntryIds();
    void rebuildProjectTree();
    void clearProjectTree();
    // 原 highlightMatypes 扩展为 3 态输入（Selected / Preview / PreDelete）
    void highlightDomainGroups(const QString& entryId, const QSet<int>& groupIds, int selectedGroupId,
                               int previewGroupId, int preDeleteGroupId);
    void highlightMatypes(const QString& entryId, const QSet<int>& matypes, int selectedMatype,
                          int previewMatype, int preDeleteMatype); // deprecated

  private:
    MaterialManagerWidget* m_materialManagerWidget;

    // project tree
    ProjectNode* m_rootItem;
    QHash<QString, ProjectNode*> m_modelItems;
    QHash<QString, ProjectNode*> m_entryItems;

    QHash<QString, MaterialModelWidget*> m_modelWidgets;
    QHash<QString, MaterialEntryWidget*> m_entryWidgets;
    MaterialTransitionPage* m_transitionPage = nullptr;
    ProjectNode* m_transitionNode = nullptr;

    QVector<MaterialModel> m_models;
    QSet<QString> m_addedEntryIds;
    QVector<MaterialModel> m_seepageModels;
    QVector<QPair<int, int>> m_materialTransitions;

    // ===== 3态：Selected / Preview / PreDelete =====
    QHash<QString, int> m_selectedDomainGroups;
    QHash<QString, int> m_previewDomainGroups;
    QHash<QString, int> m_preDeleteDomainGroups;

    // services
    ProjectManagerService* m_projectManagerService = nullptr;
    QString m_workPath;
    QString m_pickDomain;
    QString m_pickUuid;
};

#endif // MATERIALMANAGER_H
