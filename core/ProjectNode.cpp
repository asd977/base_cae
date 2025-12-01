#include "ProjectNode.h"
#include <qdebug.h>

ProjectNode::ProjectNode(const QString &n, const QString &id, BasePage *p)
    : name(n), uuid(id), page(p)
{
    setText(0, n);
}

