#pragma once

class QString;
class ProjectNode;
struct ProjectManagerService {
  virtual ~ProjectManagerService() {}

  virtual void addTreeItem(ProjectNode* node) = 0;
  virtual void setCurrentItem(ProjectNode* node) = 0;
  virtual ProjectNode* findNodeByUuid(const QString& uuid) = 0;
  virtual void setNodeDimmed(const QString& uuid, bool dimmed) = 0;
};
