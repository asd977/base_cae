#include "Visualization.h"

Visualization::Visualization()
{

    ServiceRegistry::instance().registerService<VisualWidgetService>(this);

//    m_scalarBarWidget = vtkSmartPointer<vtkScalarBarWidget >::New();

    //setupButton();

    UiFrameService *uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    m_vtkWidget = new vtkWidget;
    uiFrameService->addWidget(m_vtkWidget,("图形"));

    m_preProcessingWidget = new PreProcessingWidget;
    uiFrameService->addWidget(m_preProcessingWidget,"几何");

    m_postProcessingWidget = new PostProcessingWidget;
    uiFrameService->addWidget(m_postProcessingWidget,"后处理");
}

