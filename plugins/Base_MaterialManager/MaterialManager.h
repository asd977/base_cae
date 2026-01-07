#ifndef MATERIALMANAGER_H
#define MATERIALMANAGER_H


#include "MaterialLibraryData.h"

#include "MaterialManagerWidget.h"

#include <public/services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <public/services/ProjectManagerService.h>
#include <public/services/UiFrameService.h>

#include <QSqlDatabase>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTreeWidgetItem>

class MaterialManagerWidget;
class MaterialLibraryWidget;
class MaterialEntryWidget;
class MaterialModelWidget;

class MaterialManager:public QObject,public MaterialManagerService
{
    Q_OBJECT
public:
    MaterialManager();

    const QVector<MaterialModel> &models() const;
    QVector<MaterialModel> &models();

    MaterialModel *findModel(const QString &key);
    MaterialEntry *findEntry(const QString &id, MaterialModel **outModel = nullptr);

    void newProject(QString workPath);

    void notifyLibraryChanged(bool refreshProjectTree = true);
    void notifyEntryUpdated(const QString &entryId, bool nameChanged);

    void openLibrary(const QString &key = QString(), bool addNew = false);

    void focusOnMaterial(const QString &entryId);
    void focusOnModel(const QString &modelKey);

    void addEntryToProjectTree(const QString &entryId);
    void removeEntryFromProjectTree(const QString &entryId);

    QSet<int> availableMatypes() const;

    // ===== MATYPE 高亮核心接口 =====
    void refreshMatypeHighlight(const QString &entryId);          // 按当前 entry 状态刷新网格
    void clearMatypeHighlight(const QString &entryId);            // 清空该 entry 的所有高亮（含 preview/predelete）
    void clearAllMatypeHighlight();                               // 清空全局高亮（推荐在离开模块时用）

    int  selectedMatypeForEntry(const QString &entryId) const;
    void setSelectedMatypeForEntry(const QString &entryId, int matype);

    int  previewMatypeForEntry(const QString &entryId) const;
    void setPreviewMatypeForEntry(const QString &entryId, int matype);

    int  preDeleteMatypeForEntry(const QString &entryId) const;
    void setPreDeleteMatypeForEntry(const QString &entryId, int matype);

    void applyMatypeToEntry(const QString &entryId, int matype);
    void removeMatypeFromEntry(const QString &entryId, int matype);

    void refreshEntryWidgetMatypes(const QString &entryId, const QSet<int> &applied, int selectedMatype);

    bool generateCaculateFile();

    QVector<QStringList> getValueStrings();
    void setValueStrings(QVector<QStringList> values);

private:
    void setupTreeWidget();
    void loadDefaultLibrary();
    void ensureEntryIds();
    void rebuildProjectTree();
    void clearProjectTree();

    // 原 highlightMatypes 扩展为 3 态输入（Selected / Preview / PreDelete）
    void highlightMatypes(const QSet<int> &matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype);

private:
    MaterialLibraryWidget *m_materialLibraryWidget;
    MaterialManagerWidget *m_materialManagerWidget;

    // project tree
    QTreeWidgetItem *m_rootItem;
    QHash<QString, QTreeWidgetItem*> m_modelItems;
    QHash<QString, QTreeWidgetItem*> m_entryItems;

    QHash<QString, MaterialModelWidget*> m_modelWidgets;
    QHash<QString, MaterialEntryWidget*> m_entryWidgets;

    QVector<MaterialModel> m_models;
    QSet<QString> m_addedEntryIds;

    // ===== 3态：Selected / Preview / PreDelete =====
    QHash<QString, int> m_selectedMatypes;
    QHash<QString, int> m_previewMatypes;
    QHash<QString, int> m_preDeleteMatypes;

    // services
    ProjectManagerService *m_projectManagerService = nullptr;
    UiFrameService *m_uiFrameService = nullptr;

    QString m_workPath;
};

#endif // MATERIALMANAGER_H
