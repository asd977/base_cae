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
    void initializeTree();
    void clearTree();
    void setupNodeRole(ProjectNode* node, const QString& kind) const;
    ProjectNode* appendNode(ProjectNode* parent, const QString& title, const QString& kind,
                            const QStringList& details = {}) const;

  private:
    ProjectNode* m_root = nullptr;
};

#endif // DISPLAYMODULE_H
