#pragma once

#include <QString>
#include <vtkSmartPointer.h>

#include "IiinGridModel.h"

class vtkUnstructuredGrid;

class MeshGridIO
{
public:
    bool loadIiinFile(const QString &fileName, IiinGridModel &model, QString *errMsg = nullptr) const;
    bool importFlac3D(const QString &fileName, IiinGridModel &model, QString *errMsg = nullptr) const;
    bool importInp(const QString &fileName, IiinGridModel &model, QString *errMsg = nullptr) const;
    bool exportIiinFile(const QString &fileName, const IiinGridModel &model, QString *errMsg = nullptr) const;

    static QString defaultIiinOutputPath(const QString &sourceFile);

private:
    vtkSmartPointer<vtkUnstructuredGrid> loadFlac3dGrid(const QString &fileName) const;
    vtkSmartPointer<vtkUnstructuredGrid> loadAbaqusInpGrid(const QString &fileName) const;

    bool populateModelFromGrid(vtkUnstructuredGrid *grid,
                               const QString &outputFileName,
                               IiinGridModel &model,
                               QString *errMsg) const;
};
