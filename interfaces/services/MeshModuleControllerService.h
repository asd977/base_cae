#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

struct MeshModuleControllerService
{
    virtual ~MeshModuleControllerService() {}
	virtual void newProject() = 0;
	virtual bool saveProject() = 0;
	virtual bool loadProject() = 0;
	virtual void closeProject() = 0;
};

#endif // MESH_MODULE_CONTROLLER_SERVICE_H
