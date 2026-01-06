#pragma once

class ProjectNode;
struct ProjectManagerService
{
    virtual ~ProjectManagerService() {}

    virtual void addTreeItem(ProjectNode *node) = 0;
    virtual void setCurrentItem(ProjectNode *node) = 0;
};
