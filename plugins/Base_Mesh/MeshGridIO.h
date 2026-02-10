#pragma once

#include <QString>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;

class MeshGridIO
{
public:
    // 直接返回 vtk 网格
    vtkSmartPointer<vtkUnstructuredGrid> loadIiinFile(const QString &fileName, QString *errMsg = nullptr) const;
    vtkSmartPointer<vtkUnstructuredGrid> importFlac3D(const QString &fileName, QString *errMsg = nullptr) const;
    vtkSmartPointer<vtkUnstructuredGrid> importInp(const QString &fileName, QString *errMsg = nullptr) const;

    // 从 vtk 网格导出 IIIN.DAT
    bool exportIiinFile(const QString &fileName, vtkUnstructuredGrid *grid, QString *errMsg = nullptr) const;

    static QString defaultIiinOutputPath(const QString &sourceFile);

private:
    vtkSmartPointer<vtkUnstructuredGrid> loadFlac3dGrid(const QString &fileName) const;
    vtkSmartPointer<vtkUnstructuredGrid> loadAbaqusInpGrid(const QString &fileName) const;

    // 确保网格上有这些数组：point: node_id；cell: MATYPE/LAYER/KETYPE/elem_id/src_part/TAIL1/TAIL2
    void ensureDefaultArrays(vtkUnstructuredGrid *grid) const;
};
