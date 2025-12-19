#include "ProjectNode.h"
#include <qdebug.h>

ProjectNode::ProjectNode(const QString &n, const QString &id, BasePage *p)
    : name(n), uuid(id), page(p)
{
    setText(0, n);
}

void ProjectNode::addChild(ProjectNode *child)
{
    if (!child)
    {
        return;
    }
    children.append(child);
    QTreeWidgetItem::addChild(child);
}

bool ProjectNode::removeChild(ProjectNode *child)
{
    if (!child)
    {
        return false;
    }
    children.removeOne(child);
    QTreeWidgetItem::removeChild(child);
    return true;
}
