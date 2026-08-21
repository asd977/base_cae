#ifndef POSTPROCESSING_H
#define POSTPROCESSING_H

#include <ProjectNode.h>
#include <qobject.h>
#include <services/PostProcessingService.h>
#include <services/VisualWidgetService.h>

#include <QSet>

#include "BlockSettingsWidget.h"
#include "NephogramSettingWidget.h"
#include "SliceSettingsWidget.h"
#include "VectorSettingWidget.h"

class PostDisplayControlWidget;

class PostProcessing : public QObject, public PostProcessingService
{
    Q_OBJECT
  public:
    PostProcessing();

  public:
    void newProject() override;

    bool saveProject() override;
    bool loadProject() override;
    void closeProject() override;

  private:
    void newNephogramSettingWidget();
    void initAction();
    void initTreeMenu();
    void newVectorSettingWidget();
    void newSliceSettingWidget();
    void newBlockSettingsWidget();
    void newIsosurfaceSettingsWidget();
    void newIsosurfaceSegmentationWidget();
    void newAnimationAviWidget();
    void newDerivedValueSettingWidget();
    void newCurveSettingWidget();
    void newPostContourReaderWidget();
    void prepareResultRootAsync(const QString& projectDir, const QString& resultRoot);
    void prewarmResultFileAsync(const QString& filePath);
    VisualWidgetService* visualService();

  private:
    VisualWidgetService* m_visualService{nullptr};
    NephogramSettingWidget* m_nephogramSettingWidget{nullptr};

    VectorSettingWidget* m_vectorSettingWidget{nullptr};

    SliceSettingsWidget* m_sliceSettingWidget{nullptr};

    BlockSettingsWidget* m_blockSettingsWidget{nullptr};
    PostDisplayControlWidget* m_displayControlWidget{nullptr};
    ProjectNode* m_root{nullptr};
    QSet<QString> m_preparingResultRoots;
    QSet<QString> m_pendingResultRoots;
    QSet<QString> m_prewarmingResultFiles;

    QMap<QString, std::function<BasePage*(VisualWidgetService*)>> m_pageFactory;

    bool writeJsonFile(const QString& filePath, const QJsonObject& obj);
    bool readJsonFile(const QString& filePath, QJsonObject& out);
    void initPageFactory();
    BasePage* createPageByType(const QString& type);
    ProjectNode* createPostNode(const QString& type, const QString& title,
                                const QString& uuid = QString(), bool select = true);
    ProjectNode* findChildByUuid(const QString& uuid) const;
    ProjectNode* findChildByType(const QString& type) const;
    void configureNode(ProjectNode* node, const QString& type);
    void renameNode(const QString& uuid);
    void deleteNode(const QString& uuid);
    void removeRootFromTree();
};

#endif // POSTPROCESSING_H
