#pragma once

#include <QString>

class ProjectNode;
struct ProjectManagerService
{
    virtual ~ProjectManagerService() {}

    virtual void addTreeItem(ProjectNode *node) = 0;

    /**
     * @brief 通过预定义 key 获取项目节点
     * @param key 节点 key（如 geometry、mesh、surrogate 等）
     */
    virtual ProjectNode* nodeByKey(const QString& key) const = 0;
};
