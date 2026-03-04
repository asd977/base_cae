#ifndef BOUNDARYCONDITIONSSERVICE_H
#define BOUNDARYCONDITIONSSERVICE_H

class BasePage;
struct BoundaryConditionsService {
  virtual ~BoundaryConditionsService() {};
  virtual void newProject() = 0;
  virtual bool saveProject() = 0;
  virtual bool loadProject() = 0;
  virtual void closeProject() = 0;

  virtual bool readMesh() = 0;
};

#endif  // BOUNDARYCONDITIONSSERVICE_H
