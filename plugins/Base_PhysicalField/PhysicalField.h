#ifndef PHYSICALFIELD_H
#define PHYSICALFIELD_H

#include <BasePage.h>
#include <BodyForcePage.h>
#include <ProjectNode.h>
#include <services/PhySicalFieldService.h>
#include <vtkIntArray.h>
#include <vtkUnstructuredGrid.h>

#include <QJsonObject>

class PhysicalField : public QObject, public PhysicalFieldService {
  Q_OBJECT
 public:
  PhysicalField();

  BasePage *getPhysicalFiledSettingWidget();
  void newProject();
  bool saveProject() override;
  bool loadProject() override;

  // 生成约束文件
  void generateIIIDFile();

  // 生成荷载文件
  void generateIIWLFile();
  QString pageTitleByUuid(const QString &uuid) const override;

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
  ProjectNode *m_node;
  void initAction();
  void intitTreeMenu();

  void collectDisplacementNodes(ProjectNode *root, QList<ProjectNode *> &out);
  ProjectNode *findNodeByUuid(ProjectNode *root, const QString &uuid) const;
  void writeIIWL_Part1_GeoStressPoint(QTextStream &os);
  void writeIIWL_Part2_GeoStressFitFace(QTextStream &os);
  void writeIIWL_Part3_BodyForce(QTextStream &os);
  void writeIIWL_Part4_PointForce(QTextStream &os);
  void writeIIWL_Part5_LinearForce(QTextStream &os);

  void writeIIWL_Part7_TemperatureLoad(QTextStream &os);
  void writeIIWL_Part8_BeamSpecialDistributedLoad(QTextStream &os);
  void writeIIWL_Part9_10_11_Seismic(QTextStream &os);
  void writeIIWL_Part6_FaceLoad(QTextStream &os);

  bool writeJsonFile(const QString &filePath, const QJsonObject &obj) const;
  bool readJsonFile(const QString &filePath, QJsonObject &out) const;
  ProjectNode *createNodeByKind(const QString &kind, const QString &title,
                                const QString &uuid);
  void restoreNodeSelection(ProjectNode *node, const QJsonObject &item);
};

#endif  // PHYSICALFIELD_H
