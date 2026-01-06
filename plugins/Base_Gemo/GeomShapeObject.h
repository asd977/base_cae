#pragma once
#include <qobject.h>
#include <qhash.h>
#include <qdebug.h>
// vtk
#include <vtkAppendPolyData.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkPolyData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkCleanPolyData.h>
#include <vtkKdTree.h>
#include <vtkCellData.h>
#include <vtkTriangle.h>
#include <vtkPolyDataNormals.h>
#include <vtkExtractCells.h>
#include <vtkDataSetMapper.h>
#include <vtkUnstructuredGrid.h>
// occ
#include <TopoDS_Shape.hxx>
#include <TopExp_Explorer.hxx>
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>

/**
 * @brief 几何形状类
 * @author tongle
 * @brief 包含当前几何解析后的所有的点线面体
 * @date 2025-07-08
 */

class GeomShapeObject :public QObject
{
public:
    /**
		* @brief 构造函数
		*/
    GeomShapeObject(QString uuid, TopoDS_Shape* shape);
    /**
		* @brief 析构函数
		*/
    ~GeomShapeObject();
    /**
		* @brief 获取形状的拓扑
		* @return TopoDS_Shape* 返回的拓扑
		*/
    TopoDS_Shape *getShape();

    QString getUUid() { return m_uuid; };

    void setActor(vtkSmartPointer<vtkActor>surfActor, vtkSmartPointer<vtkActor> edgeActor);

    vtkSmartPointer<vtkActor> getSurfActor();

    vtkSmartPointer<vtkActor>getEdgeActor();
    void initActor();
private:
    TopoDS_Shape* _shape;// 当前对象的TopoDS_Shape对象
    QString m_uuid;// 唯一索引
    vtkSmartPointer<vtkActor> m_surfActor;
    vtkSmartPointer<vtkActor> m_edgeActor;

};



