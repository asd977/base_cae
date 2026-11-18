#pragma once
#include <QString>
#include <vtkClipDataSet.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <cmath>
#include <limits>

// Display copies only: source arrays, numbering and result files are untouched.
struct PostThresholdGrids
{
    vtkSmartPointer<vtkUnstructuredGrid> retained;
    vtkSmartPointer<vtkUnstructuredGrid> below;
};

inline PostThresholdGrids splitPostThreshold(vtkUnstructuredGrid* source,
                                            const QString& field, bool pointData,
                                            double threshold)
{
    PostThresholdGrids result;
    if (!source) return result;
    auto* array = pointData ? source->GetPointData()->GetArray(field.toUtf8().constData())
                            : source->GetCellData()->GetArray(field.toUtf8().constData());
    if (!array) { result.retained = source; return result; }
    double range[2]; array->GetRange(range);
    result.retained = vtkSmartPointer<vtkUnstructuredGrid>::New();
    result.below = vtkSmartPointer<vtkUnstructuredGrid>::New();
    if (range[0] >= threshold) { result.retained->ShallowCopy(source); return result; }
    if (range[1] < threshold) { result.below->ShallowCopy(source); return result; }
    if (pointData)
    {
        // VTK 9.2's secondary clipped output omits point/cell attributes.
        // Two primary outputs preserve all result arrays on both sides.
        for (bool below : {false, true})
        {
            auto clip = vtkSmartPointer<vtkClipDataSet>::New();
            clip->SetInputData(source);
            clip->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                        field.toUtf8().constData());
            // VTK keeps values strictly greater than its clipping value. Move
            // by one representable value so equality belongs to the cloud.
            clip->SetValue(std::nextafter(threshold, -std::numeric_limits<double>::infinity()));
            clip->SetInsideOut(below);
            clip->Update();
            (below ? result.below : result.retained)->ShallowCopy(clip->GetOutput());
        }
    }
    else
    {
        auto highIds = vtkSmartPointer<vtkIdList>::New();
        auto lowIds = vtkSmartPointer<vtkIdList>::New();
        for (vtkIdType i = 0; i < source->GetNumberOfCells(); ++i)
            (array->GetComponent(i, 0) < threshold ? lowIds : highIds)->InsertNextId(i);
        auto extract = vtkSmartPointer<vtkExtractCells>::New();
        extract->SetInputData(source);
        extract->SetCellList(highIds); extract->Update();
        result.retained->ShallowCopy(extract->GetOutput());
        extract->SetCellList(lowIds); extract->Update();
        result.below->ShallowCopy(extract->GetOutput());
    }
    return result;
}
