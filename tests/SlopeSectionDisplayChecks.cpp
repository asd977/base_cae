#include "../plugins/Base_Solver/SlopeSectionDisplay.h"
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <iostream>
#include <cmath>

static vtkSmartPointer<vtkPolyData> region(int width, int height, bool hole = false)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    const double x0 = 510261.375, y0 = 2300.96373625;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            if (hole && x == 1 && y == 1) continue;
            // Deliberately reproduce a cut with separate vertices per triangle.
            const int corners[2][3][2] = {
                {{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {1, 1}, {0, 1}}};
            for (const auto& triangle : corners)
            {
                vtkIdType ids[3];
                for (int i = 0; i < 3; ++i)
                    ids[i] = points->InsertNextPoint(
                        x0 + 0.007 * (x + triangle[i][0]),
                        y0 + 0.009 * (y + triangle[i][1]), 0.0);
                cells->InsertNextCell(3, ids);
            }
        }
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points); poly->SetPolys(cells);
    return poly;
}

int main()
{
    const auto input = region(12, 9);
    const auto display = makeSlopeRegionDisplay(input);
    if (display.fill->GetNumberOfCells() != input->GetNumberOfCells()
        || display.fill->GetNumberOfPoints() != 13 * 10
        || display.boundary->GetNumberOfCells() != 2 * (12 + 9)) return 1;
    if (display.fill->GetPoints()->GetDataType() != VTK_DOUBLE) return 2;
    double original[6], cleaned[6];
    input->GetBounds(original); display.fill->GetBounds(cleaned);
    for (int i = 0; i < 6; ++i)
        if (std::abs(original[i] - cleaned[i]) > 1.0e-10) return 3;
    const auto ring = makeSlopeRegionDisplay(region(3, 3, true));
    if (ring.boundary->GetNumberOfCells() != 16) return 4;
    // A welded input and an unwelded input must produce the same outline.
    const auto second = makeSlopeRegionDisplay(display.fill);
    if (second.boundary->GetNumberOfCells() != display.boundary->GetNumberOfCells()) return 5;
    if (input->GetNumberOfPoints() != 12 * 9 * 6) return 6;
    std::cout << "PASS: no interior triangle outlines; holes, survey precision and input preserved\n";
    return 0;
}
