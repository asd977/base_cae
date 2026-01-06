#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QSet>

class IiinGridModel;

struct MeshModuleControllerService
{
    virtual ~MeshModuleControllerService() {}
	virtual void newProject() = 0;
	virtual bool saveProject() = 0;
	virtual bool loadProject() = 0;
	virtual void closeProject() = 0;
    virtual bool currentModel(IiinGridModel &model) const = 0;
    virtual QSet<int> getAvailableMatypes() const = 0;
    virtual void highlightMatypes(const QSet<int> &matypes,
                                  int selectedMatype,
                                  int previewMatype,
                                  int preDeleteMatype) = 0;
};

#endif // MESH_MODULE_CONTROLLER_SERVICE_H
