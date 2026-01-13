#include "DisplacementConstraintSelectionManager.h"
#include "DisplacementConstraintWidget.h"

DisplacementConstraintSelectionManager& DisplacementConstraintSelectionManager::instance()
{
    static DisplacementConstraintSelectionManager manager;
    return manager;
}

void DisplacementConstraintSelectionManager::registerWidget(DisplacementConstraintWidget* widget)
{
    if (!widget) return;
    m_widgetNodes.insert(widget, QSet<int>{});
}

void DisplacementConstraintSelectionManager::unregisterWidget(DisplacementConstraintWidget* widget)
{
    if (!widget) return;

    const auto nodes = m_widgetNodes.value(widget);
    for (int nodeId : nodes) {
        auto ownerIt = m_nodeOwners.find(nodeId);
        if (ownerIt != m_nodeOwners.end() && ownerIt.value() == widget) {
            m_nodeOwners.erase(ownerIt);
        }
    }
    m_widgetNodes.remove(widget);
}

void DisplacementConstraintSelectionManager::requestSelection(DisplacementConstraintWidget* widget, int nodeId)
{
    if (!widget) return;

    auto owner = m_nodeOwners.value(nodeId);
    if (owner && owner != widget) {
        owner->removeSelectedByManager(nodeId);
        m_widgetNodes[owner].remove(nodeId);
    }

    m_nodeOwners.insert(nodeId, widget);
    m_widgetNodes[widget].insert(nodeId);
}

void DisplacementConstraintSelectionManager::releaseSelection(DisplacementConstraintWidget* widget, int nodeId)
{
    if (!widget) return;

    auto ownerIt = m_nodeOwners.find(nodeId);
    if (ownerIt != m_nodeOwners.end() && ownerIt.value() == widget) {
        m_nodeOwners.erase(ownerIt);
    }
    m_widgetNodes[widget].remove(nodeId);
}
