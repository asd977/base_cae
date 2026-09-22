#ifndef GEOMETRYSELECTIONVISUALS_H
#define GEOMETRYSELECTIONVISUALS_H

#include <GeometryPickManager.h>

namespace GeometrySelectionVisuals
{
vtkSmartPointer<vtkProp> buildPickActor(const QVector<GeometryPickItem>& items, bool preview);

void clearTreePickSelection(bool render = false);
bool applyTreeSelectionHighlight(const QString& uuid);
void clearTreeSelectionHighlight(bool render = true);

bool applySourcePreviewHighlight(const QString& uuid);
void clearSourcePreviewHighlight(bool render = true);

// Reapply active highlights after an actor was rebuilt or visibility changed.
void refreshHighlights(bool render = true);
} // namespace GeometrySelectionVisuals

#endif // GEOMETRYSELECTIONVISUALS_H
