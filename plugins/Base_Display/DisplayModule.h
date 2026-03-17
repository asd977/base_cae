#ifndef DISPLAYMODULE_H
#define DISPLAYMODULE_H

#include <QObject>

class ProjectNode;

class DisplayModule : public QObject
{
    Q_OBJECT

  public:
    DisplayModule();

  private:
    QString m_rootNodeUuid;
    void registerProjectHooks();
    void ensureDisplayNode();
    ProjectNode* createDisplayNode() const;
};

#endif // DISPLAYMODULE_H
