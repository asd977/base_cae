#ifndef SURROGATEMODELSERVICE_H
#define SURROGATEMODELSERVICE_H

class BasePage;
struct SurrogateModelService {
  virtual ~SurrogateModelService() {};
  virtual void newProject() = 0;
  virtual bool saveProject() = 0;
  virtual bool loadProject() = 0;
  virtual void closeProject() = 0;
};

#endif  // SURROGATEMODELSERVICE_H
