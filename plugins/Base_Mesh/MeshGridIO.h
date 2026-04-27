#pragma once

#include <struct/MeshMap.h>
#include <vtkSmartPointer.h>

#include <QString>

class vtkUnstructuredGrid;

class MeshGridIO
{
  public:
    static MeshGridIO& instance();

    MeshGridIO(const MeshGridIO&) = delete;
    MeshGridIO& operator=(const MeshGridIO&) = delete;
    MeshGridIO(MeshGridIO&&) = delete;
    MeshGridIO& operator=(MeshGridIO&&) = delete;

    vtkSmartPointer<vtkUnstructuredGrid> loadIiinFile(const QString& fileName,
                                                      QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> importFlac3D(const QString& fileName,
                                                      QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> importInp(const QString& fileName,
                                                   QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> loadMshFile(const QString& fileName,
                                                     QString* errMsg = nullptr);
    vtkSmartPointer<vtkUnstructuredGrid> loadCurrentGmshMesh(QString* errMsg = nullptr);

    bool exportIiinFile(const QString& fileName,
                        vtkUnstructuredGrid* grid,
                        QString* errMsg = nullptr) const;

    static QString defaultIiinOutputPath(const QString& sourceFile);

    MshPickMap getMeshMap();
    const MshPickMap& getMeshMapRef() const;
    void setMeshMap(const MshPickMap& pickMap);

  private:
    MeshGridIO() = default;
    ~MeshGridIO() = default;

    vtkSmartPointer<vtkUnstructuredGrid> loadFlac3dGrid(const QString& fileName) const;
    vtkSmartPointer<vtkUnstructuredGrid> loadAbaqusInpGrid(const QString& fileName);

    void ensureDefaultArrays(vtkUnstructuredGrid* grid) const;
    void clearPickMap();

  private:
    MshPickMap m_mshPickMap;
};
