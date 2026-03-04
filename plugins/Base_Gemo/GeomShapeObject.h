#pragma once
#include <qdebug.h>
#include <qhash.h>
#include <qobject.h>
// vtk
#include <vtkActor.h>
#include <vtkAppendPolyData.h>
#include <vtkCellData.h>
#include <vtkCleanPolyData.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkKdTree.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkTriangle.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>
// occ
#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

/**
 * @brief 几何形状类
 * @author tongle
 * @brief 包含当前几何解析后的所有的点线面体
 * @date 2025-07-08
 */

class GeomShapeObject : public QObject
{
  public:
    /**
     * @brief 构造函数
     */
    GeomShapeObject(QString uuid, TopoDS_Shape* shape, QString name = QString());
    /**
     * @brief 析构函数
     */
    ~GeomShapeObject();
    /**
     * @brief 获取形状的拓扑
     * @return TopoDS_Shape* 返回的拓扑
     */
    TopoDS_Shape* getShape();

    QString getUUid()
    {
        return m_uuid;
    };
    QString getName() const
    {
        return m_name;
    }
    void setName(const QString& name)
    {
        m_name = name;
    }

    void setActor(vtkSmartPointer<vtkActor> surfActor, vtkSmartPointer<vtkActor> edgeActor);

    vtkSmartPointer<vtkActor> getSurfActor();

    vtkSmartPointer<vtkActor> getEdgeActor();
    void initActor();

  private:
    TopoDS_Shape* _shape; // 当前对象的TopoDS_Shape对象
    QString m_uuid;       // 唯一索引
    QString m_name;
    vtkSmartPointer<vtkActor> m_surfActor;
    vtkSmartPointer<vtkActor> m_edgeActor;
};
