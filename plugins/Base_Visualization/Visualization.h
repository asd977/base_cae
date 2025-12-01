#ifndef VISUALIZATION_H
#define VISUALIZATION_H

#include "CurveWidget.h"
#include "PostProcessingWidget.h"
#include "PreProcessingWidget.h"
#include "VisualWidget.h"
#include <QObject>

class Visualization:public QObject, public VisualWidgetService
{
    Q_OBJECT
public:
    Visualization();
private:
    vtkWidget *m_vtkWidget;
    PreProcessingWidget *m_preProcessingWidget;
    PostProcessingWidget *m_postProcessingWidget;
    CurveWidget *curveWidget;
};

#endif // VISUALIZATION_H
