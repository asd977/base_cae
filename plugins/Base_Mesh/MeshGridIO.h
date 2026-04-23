#pragma once

#include <struct/MeshMap.h>
#include <vtkSmartPointer.h>

#include <QString>

class vtkUnstructuredGrid;

class MeshGridIO
{
  public:
    // ===== 单例入口 =====
    static MeshGridIO& instance();

    // 禁止拷贝/移动
    MeshGridIO(const MeshGridIO&) = delete;
    MeshGridIO& operator=(const MeshGridIO&) = delete;
    MeshGridIO(MeshGridIO&&) = delete;
    MeshGridIO& operator=(MeshGridIO&&) = delete;

  private:
    // ===== 构造/析构私有化 =====
    MeshGridIO() = default;
    ~MeshGridIO() = default;

  public:
    // 直接返回 vtk 网格
    vtkSmartPointer<vtkUnstructuredGrid> loadIiinFile(const QString& fileName,
                                                      QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> importFlac3D(const QString& fileName,
                                                      QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> importInp(const QString& fileName,
                                                   QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> loadMshFile(const QString& fileName,
                                                     QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> loadCurrentGmshMesh(QString* errMsg = nullptr);

    // 从 vtk 网格导出 IIIN.DAT
    bool exportIiinFile(const QString& fileName, vtkUnstructuredGrid* grid,
                        QString* errMsg = nullptr) const;

    static QString defaultIiinOutputPath(const QString& sourceFile);

    MshPickMap getMeshMap();
    void setMeshMap(const MshPickMap& pickMap);

  private:
    vtkSmartPointer<vtkUnstructuredGrid> loadFlac3dGrid(const QString& fileName) const;
    vtkSmartPointer<vtkUnstructuredGrid> loadAbaqusInpGrid(const QString& fileName);

    // 确保网格上有这些数组：point: node_id；cell:
    // MATYPE/LAYER/KETYPE/elem_id/src_part/TAIL1/TAIL2
    void ensureDefaultArrays(vtkUnstructuredGrid* grid) const;

    void clearPickMap();

    MshPickMap m_mshPickMap;
};
