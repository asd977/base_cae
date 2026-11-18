#ifndef PHYSICALFIELD_H
#define PHYSICALFIELD_H

#include <BasePage.h>
#include <BodyForcePage.h>
#include <ProjectNode.h>
#include <services/PhySicalFieldService.h>
#include <vtkIntArray.h>
#include <vtkUnstructuredGrid.h>

#include <QJsonObject>

class PhysicalField : public QObject, public PhysicalFieldService
{
    Q_OBJECT
  public:
    PhysicalField();

    BasePage* getPhysicalFiledSettingWidget();
    void newProject();
    void closeProject() override;
    bool saveProject() override;
    bool loadProject() override;

    // 生成约束文件
    bool generateIIIDFile() override;
    bool hasPrescribedDisplacements() const override;

    // 生成荷载文件
    bool generateIIWLFile() override;
    bool importIIIDFile(const QString& filePath, QString* errorMessage = nullptr) override;
    bool validateSeismicAcceleration(QString* errorMessage = nullptr) const override;
    bool openSeismicAccelerationPage(QString* errorMessage = nullptr) override;
    QString pageTitleByUuid(const QString& uuid) const override;

  private slots:
    void addDisplacementConstraint();
    void addGeoStressPointPage();
    void addGeoStressFitFacePage();
    void addBodyForcePage();
    void addConcentratedForcePage();
    void addSurfaceForcePage();
    void addTemperatureLoadPage();
    void addSeismicAndAddedMassPage();
    void addLinearForcePage();

    void addBeamLoadPage();

  private:
    ProjectNode* m_node = nullptr;
    void initAction();
    void intitTreeMenu();

    void collectDisplacementNodes(ProjectNode* root, QList<ProjectNode*>& out);
    ProjectNode* findNodeByUuid(ProjectNode* root, const QString& uuid) const;
    void renameNodeByUuid(const QString& uuid);
    void removeNodeByUuid(const QString& uuid);
    void writeIIWL_Part1_GeoStressPoint(QTextStream& os);
    void writeIIWL_Part2_GeoStressFitFace(QTextStream& os);
    void writeIIWL_Part3_BodyForce(QTextStream& os);
    void writeIIWL_Part4_PointForce(QTextStream& os);
    void writeIIWL_Part5_LinearForce(QTextStream& os);

    bool writeIIWL_Part7_TemperatureLoad(QTextStream& os);
    void writeIIWL_Part8_BeamSpecialDistributedLoad(QTextStream& os);
    void writeIIWL_Part6_FaceLoad(QTextStream& os);
    void generateWaveFiles();

    bool writeJsonFile(const QString& filePath, const QJsonObject& obj) const;
    bool readJsonFile(const QString& filePath, QJsonObject& out) const;
    ProjectNode* createNodeByKind(const QString& kind, const QString& title, const QString& uuid);
    void restoreNodeSelection(ProjectNode* node, const QJsonObject& item);

    bool m_loadingProject = false;
    QVector<double> m_prescribedValues;
    bool m_constraintsValid=false;
    int m_exportedDofs=3;
};

#endif // PHYSICALFIELD_H
