#ifndef DISPLAYMODULE_H
#define DISPLAYMODULE_H

#include <QObject>
#include <QStringList>
#include <QVector>

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
                            const QString& intro = QString(), const QStringList& details = {},
                            const QStringList& tableHeaders = {},
                            const QVector<QStringList>& tableRows = {}) const;

  private:
    ProjectNode* m_root = nullptr;
};

#endif // DISPLAYMODULE_H
