#ifndef POSTPROCESSINGSERVICE_H
#define POSTPROCESSINGSERVICE_H

class BasePage;
struct PostProcessingService
{

    virtual ~PostProcessingService() {};

    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void closeProject() = 0;

};

#endif // POSTPROCESSINGSERVICE_H
