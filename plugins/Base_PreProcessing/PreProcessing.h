#ifndef PREPROCESSING_H
#define PREPROCESSING_H

#include "PreProcessingWidget.h"
#include <services/PreProcessingService.h>
#include <QObject>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "ProjectNode.h"
#include <PublicVariables.h>
#include <PublicConstants.h>
#include <functional>

class PreProcessing:public QObject,public PreProcessingService
{
    Q_OBJECT
public:
    PreProcessing();
	void newProject() override;
	bool saveProject() override;
	bool loadProject() override;
	void closeProject() override;
	bool getGridIsDraw() override;
    void render() override;
private:
	bool writeJsonFile(const QString &filePath, const QJsonObject &obj);
	bool readJsonFile(const QString &filePath, QJsonObject &out);
	bool loadChildProject(ProjectNode * node,const QString & name);
	void initPageFactory();
	BasePage* createPageByType(const QString &type ,const QString& title);

private:
    PreProcessingWidget *m_preProcessingWidget;
	QMap<QString, std::function<BasePage* (QString title)>> m_pageFactory;
	ProjectNode *m_geomNode;
	ProjectNode *m_gridNode;

};

#endif // PREPROCESSING_H
