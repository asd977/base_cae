#ifndef PHYSICALFIELD_H
#define PHYSICALFIELD_H

#include <BasePage.h>
#include <ProjectNode.h>
#include <services/PhySicalFieldService.h>

class PhysicalField :public QObject, public PhysicalFieldService
{
    Q_OBJECT
public:
    PhysicalField();

    BasePage *getPhysicalFiledSettingWidget();
    void newProject();

private slots:
    void addDisplacementConstraint();
    void addGeoStressPointPage();
    void addGeoStressFitFacePage();
    void addBodyForcePage();
    void addConcentratedForcePage();
    void addSurfaceForcePage();
    void addTemperatureLoadPage();
    void addSeismicAndAddedMassPage();


private:
    ProjectNode *m_node;
    void intitTreeMenu();

};

#endif // PHYSICALFIELD_H
