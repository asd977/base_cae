#ifndef VTKSCALAR_H
#define VTKSCALAR_H

#include <QObject>

#include <vtkSmartPointer.h>
#include <vtkXMLUnstructuredGridReader.h>
#include <vtkUnstructuredGrid.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <QDebug>
#include <vtkAutoInit.h>
#include <vtkFloatArray.h>
#include <vtkDoubleArray.h>
#include <vtkContourFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkCutter.h>
#include <vtkPlane.h>
#include <vtkLineSource.h>
#include <vtkProbeFilter.h>
#include <vtkTable.h>
#include <vtkIntArray.h>
#include <vtkChartXY.h>
#include <vtkContextView.h>
#include <vtkContextScene.h>
#include <vtkPlot.h>
#include <vtkAxesActor.h>
#include <vtkArrowSource.h>
#include <vtkGlyph3DMapper.h>
#include <vtkPointSource.h>
#include <vtkStreamTracer.h>
#include <vtkRungeKutta4.h>
#include <vtkArrayCalculator.h>
#include <vtkStreamTracer.h>
//#include <QVTKOpenGLWidget.h>
#include <vtkProperty.h>
#include <vtkScalarBarActor.h>

#include "vtkTecplotReader.h"
#include "vtkMultiBlockDataSet.h"
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>
#include "vtkGlyph3DMapper.h"

#include <vector>


class VTKScalar : public QObject
{
    Q_OBJECT

public:
    VTKScalar();
    void change(int i);
    vtkSmartPointer<vtkDataSetMapper> mapper;

    vtkSmartPointer<vtkUnstructuredGrid> data_use;
    vtkSmartPointer<vtkActor> readScalarFile(std::vector<std::string>& vec_ps,vtkSmartPointer<vtkUnstructuredGrid> &grid, vtkSmartPointer<vtkScalarBarActor> &scalarBar);
    vtkSmartPointer<vtkActor> readScalarFile2(vtkSmartPointer<vtkUnstructuredGrid> &grid, vtkSmartPointer<vtkScalarBarActor> &scalarBar);
};

#endif // VTKSCALAR_H
