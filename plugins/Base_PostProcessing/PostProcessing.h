#ifndef POSTPROCESSING_H
#define POSTPROCESSING_H

#include "BlockSettingsWidget.h"
#include "NephogramSettingWidget.h"
#include "PostProcessingWidget.h"
#include "SliceSettingsWidget.h"
#include "VectorSettingWidget.h"

#include <services/PostProcessingService.h>
#include <ProjectNode.h>
#include <qobject.h>

class PostProcessing:  public QObject,public PostProcessingService
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
    void newVectorSettingWidget();
    void newSliceSettingWidget();
    void newBlockSettingsWidget();
    void newIsosurfaceSettingsWidget();
    void newIsosurfaceSegmentationWidget();
    void newAnimationAviWidget();
    void newDerivedValueSettingWidget();

private:
    PostProcessingWidget *m_postProcessingWidget;
    NephogramSettingWidget *m_nephogramSettingWidget;

    VectorSettingWidget *m_vectorSettingWidget;

    SliceSettingsWidget *m_sliceSettingWidget;

    BlockSettingsWidget *m_blockSettingsWidget;
    ProjectNode *m_root;

    QMap<QString, std::function<BasePage*(PostProcessingWidget*)>> m_pageFactory;

    bool writeJsonFile(const QString &filePath, const QJsonObject &obj);
    bool readJsonFile(const QString &filePath, QJsonObject &out);
    void initPageFactory();
    BasePage *createPageByType(const QString &type);
};

#endif // POSTPROCESSING_H






















