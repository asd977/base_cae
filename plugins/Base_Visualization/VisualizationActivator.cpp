#include "VisualizationActivator.h"

#include <vtkAutoInit.h>

VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingContextOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle)

VisualizationActivator::VisualizationActivator() = default;
