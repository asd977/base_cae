#ifndef MATERIALMANAGERSERVICE_H
#define MATERIALMANAGERSERVICE_H
class BasePage;

struct MaterialManagerService {
  virtual BasePage *getMaterialManagerWidget() = 0;

  virtual void newProject() = 0;
};

#endif  // MATERIALMANAGERSERVICE_H
