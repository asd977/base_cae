#pragma once

#include <QHash>
#include <QPointer>
#include <QSet>

class DisplacementConstraintWidget;

class DisplacementConstraintSelectionManager
{
public:
    static DisplacementConstraintSelectionManager& instance();

    void registerWidget(DisplacementConstraintWidget* widget);
    void unregisterWidget(DisplacementConstraintWidget* widget);

    void requestSelection(DisplacementConstraintWidget* widget, int nodeId);
    void releaseSelection(DisplacementConstraintWidget* widget, int nodeId);

private:
    DisplacementConstraintSelectionManager() = default;

    QHash<int, QPointer<DisplacementConstraintWidget>> m_nodeOwners;
    QHash<DisplacementConstraintWidget*, QSet<int>> m_widgetNodes;
};
