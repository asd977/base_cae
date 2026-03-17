#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <QObject>

class ProjectNode;

class DisplayManager : public QObject
{
    Q_OBJECT

  public:
    DisplayManager();

  private:
    void initializeTree();
    void clearTree();
    void setupNodeRole(ProjectNode* node, const QString& kind) const;

  private:
    ProjectNode* m_root = nullptr;
};

#endif // DISPLAYMANAGER_H
