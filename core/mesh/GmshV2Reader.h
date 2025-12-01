#pragma once

#include <string>
#include <map>
#include <utility>

#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <mesh/BoundaryConditions.h>

// 读取 Gmsh 2.2 ASCII .msh，返回 vtkUnstructuredGrid
// 同时把 PhysicalNames 信息放在 info 里，方便外部使用。
vtkSmartPointer<vtkUnstructuredGrid> ReadGmshV22ToVtk(
    const std::string& filename,
    GmshPhysicalInfo& info);
