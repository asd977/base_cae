#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkIdTypeArray.h>
#include <vtkPointData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkWeakPointer.h>
#include <algorithm>

// Reuse only a snapshot written in this session for the same unmodified grid.
// Save As, replacement grids and externally changed/deleted files force a write.
class MeshSnapshotState
{
public:
    bool matches(vtkUnstructuredGrid* grid, const QString& path) const
    {
        const QFileInfo file(path);
        return grid && m_grid.GetPointer() == grid && m_time == modifiedTime(grid) &&
            file.isFile() && file.absoluteFilePath() == m_path &&
            file.size() == m_size && file.lastModified() == m_fileTime;
    }

    void remember(vtkUnstructuredGrid* grid, const QString& path)
    {
        const QFileInfo file(path);
        m_grid = grid;
        m_time = grid ? modifiedTime(grid) : 0;
        m_path = file.absoluteFilePath();
        m_size = file.size();
        m_fileTime = file.lastModified();
    }

    void reset() { m_grid = nullptr; m_path.clear(); }

private:
    static vtkMTimeType modifiedTime(vtkUnstructuredGrid* grid)
    {
        vtkMTimeType time = grid->GetMTime();
        // Connectivity and polyhedron faces are not all covered by PointSet's
        // GetMTime. Include them explicitly so topology edits cannot be skipped.
        auto include = [&time](vtkObject* object) {
            if (object) time = std::max(time, object->GetMTime());
        };
        include(grid->GetCells());
        if (auto* cells = grid->GetCells())
        {
            include(cells->GetOffsetsArray());
            include(cells->GetConnectivityArray());
        }
        include(grid->GetCellTypesArray());
        include(grid->GetFaces());
        include(grid->GetFaceLocations());
        include(grid->GetCellData());
        include(grid->GetPointData());
        include(grid->GetFieldData());
        return time;
    }

    vtkWeakPointer<vtkUnstructuredGrid> m_grid;
    vtkMTimeType m_time = 0;
    QString m_path;
    qint64 m_size = -1;
    QDateTime m_fileTime;
};
