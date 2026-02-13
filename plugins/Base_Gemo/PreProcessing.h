#ifndef PREPROCESSING_H
#define PREPROCESSING_H

#include <services/PreProcessingService.h>
#include <services/UiFrameService.h>
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

    void generateStepFile() override;
    QVector<qulonglong> occShapePointers() override;
    QStringList listEntityIds(int dim) override;
    bool highlightEntity(int dim, int id) override;
    void clearEntityHighlight() override;



    BasePage *newGeometryImportWidget(QString title = QObject::tr("导入几何体"));
    void newGeometryBoxWidget(QString title = QObject::tr("长方体"));
    void newGeometrySphereWidget(QString title = QObject::tr("球体"));
    void newGeometryConeWidget(QString title = QObject::tr("圆锥体"));
    void newGeometryTorusWidget(QString title = QObject::tr("圆环体"));
    void newGeometryCylinderWidget(QString title = QObject::tr("圆柱体"));
    BasePage *newGeometryCircleWidget(QString title = QObject::tr("圆形"));
    BasePage *newGeometryEllipseWidget(QString title = QObject::tr("椭圆"));
    BasePage *newGeometryPolygonWidget(QString title = QObject::tr("多边形"));
    BasePage *newGeometryRectangleWidget(QString title = QObject::tr("矩形"));
    void newOccBooleanFuseWidget(QString title = QObject::tr("布尔融合"));
    void newOccBooleanUnionWidget(QString title = QObject::tr("联合体"));
    void newOccBooleanCutWidget(QString title = QObject::tr("布尔切割"));
    void newOccBooleanCommonWidget(QString title = QObject::tr("布尔求交"));
    bool exportGeom();
private:
	bool writeJsonFile(const QString &filePath, const QJsonObject &obj);
	bool readJsonFile(const QString &filePath, QJsonObject &out);
	bool loadChildProject(ProjectNode * node,const QString & name);
	void initPageFactory();
	BasePage* createPageByType(const QString &type ,const QString& title);

    void initRibbonActionConn();

private:
	QMap<QString, std::function<BasePage* (QString title)>> m_pageFactory;
	ProjectNode *m_geomNode;
	ProjectNode *m_gridNode;
    UiFrameService *m_uiFrameService;

    void toggleVisible(const QString& uuid);
    void removeNode(const QString& uuid);
    void renameNode(const QString& uuid);

    void initTreeMenu();
    bool writeMultipleShapesStepFile(const QString &filePath);
    QString makeUniqueName(const QString &baseName, const QString &typeKey);

    QHash<QString, int> m_nameCounters;
};

#endif // PREPROCESSING_H
