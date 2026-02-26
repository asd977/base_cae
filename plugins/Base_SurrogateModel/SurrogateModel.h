#ifndef SURROGATEMODEL_H
#define SURROGATEMODEL_H

#include <services/SurrogateModelService.h>

#include <ProjectNode.h>

class SurrogateModel: public SurrogateModelService
{
public:
    SurrogateModel();
    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();

private slots:
    void newDatasetEvaluation();
    void newLatinHypercubeSamplingWidget();
    void newOrthogonalSamplingWidget();
    void newDNNModel();

private:
    ProjectNode *m_root = nullptr;
    void initAction();
    BasePage* createPageByType(const QString& type);
};

#endif // SURROGATEMODEL_H
