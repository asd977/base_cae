#include "GmshV2Reader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellData.h>
#include <vtkPointData.h>
#include <vtkIntArray.h>
#include <vtkStringArray.h>
#include <vtkIdTypeArray.h>

// 工具函数：trim
static inline std::string trim(const std::string& s)
{
    auto beg = s.begin();
    while (beg != s.end() && std::isspace(static_cast<unsigned char>(*beg)))
        ++beg;
    auto end = s.end();
    do
    {
        --end;
    } while (std::distance(beg, end) >= 0
             && std::isspace(static_cast<unsigned char>(*end)));
    return (std::distance(beg, end) < 0)
               ? std::string()
               : std::string(beg, end + 1);
}

// gmsh element type -> 几何维度
static int GmshElementDimension(int etype)
{
    switch (etype)
    {
    case 15: // point
        return 0;
    case 1:  // line
        return 1;
    case 2:  // triangle
    case 3:  // quad
        return 2;
    case 4:  // tet
    case 5:  // hex
    case 6:  // wedge
    case 7:  // pyramid
        return 3;
    default:
        return -1;
    }
}

// gmsh element type -> VTK cell type
static int GmshElementToVtkCellType(int etype)
{
    switch (etype)
    {
    case 15: // point
        return VTK_VERTEX;
    case 1:
        return VTK_LINE;
    case 2:
        return VTK_TRIANGLE;
    case 3:
        return VTK_QUAD;
    case 4:
        return VTK_TETRA;
    case 5:
        return VTK_HEXAHEDRON;
    case 6:
        return VTK_WEDGE;
    case 7:
        return VTK_PYRAMID;
    default:
        return -1;
    }
}

vtkSmartPointer<vtkUnstructuredGrid> ReadGmshV22ToVtk(
    const std::string& filename,
    GmshPhysicalInfo& info)
{
    std::ifstream fin(filename.c_str());
    if (!fin)
    {
        std::cerr << "Cannot open msh file: " << filename << std::endl;
        return nullptr;
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    grid->SetPoints(points);

    // point data: gmsh_node_id
    auto nodeIdArray = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArray->SetName("gmsh_node_id");
    grid->GetPointData()->AddArray(nodeIdArray);

    // cell data: element id, dimension, physical id, physical name
    auto elemIdArray = vtkSmartPointer<vtkIntArray>::New();
    elemIdArray->SetName("gmsh_elem_id");
    grid->GetCellData()->AddArray(elemIdArray);

    auto dimArray = vtkSmartPointer<vtkIntArray>::New();
    dimArray->SetName("gmsh_dim");
    grid->GetCellData()->AddArray(dimArray);

    auto physIdArray = vtkSmartPointer<vtkIntArray>::New();
    physIdArray->SetName("gmsh_physical_id");
    grid->GetCellData()->AddArray(physIdArray);

    auto physNameArray = vtkSmartPointer<vtkStringArray>::New();
    physNameArray->SetName("gmsh_physical_name");
    grid->GetCellData()->AddArray(physNameArray);

    // Gmsh nodeId -> vtkPointId 映射
    std::map<int, vtkIdType> nodeIdToVtkId;

    std::string line;
    while (std::getline(fin, line))
    {
        line = trim(line);
        if (line.empty())
            continue;

        if (line == "$PhysicalNames")
        {
            // 读取数量
            std::getline(fin, line);
            int nPhys = std::stoi(trim(line));
            for (int i = 0; i < nPhys; ++i)
            {
                std::getline(fin, line);
                if (line.empty()) { --i; continue; }

                std::istringstream iss(line);
                int dim = 0;
                int physTag = 0;
                std::string name;
                iss >> dim >> physTag;
                // 剩下的是带引号的 name，直接找第一个双引号
                auto pos = line.find('"');
                if (pos != std::string::npos)
                {
                    auto pos2 = line.find_last_of('"');
                    if (pos2 != std::string::npos && pos2 > pos)
                    {
                        name = line.substr(pos + 1, pos2 - pos - 1);
                    }
                }
                info.physicalNameMap[std::make_pair(dim, physTag)] = name;
            }
            // 读掉 $EndPhysicalNames
            std::getline(fin, line);
        }
        else if (line == "$Nodes")
        {
            std::getline(fin, line);
            int nNodes = std::stoi(trim(line));
            points->SetNumberOfPoints(nNodes);
            nodeIdArray->SetNumberOfValues(nNodes);

            for (int i = 0; i < nNodes; ++i)
            {
                std::getline(fin, line);
                if (line.empty()) { --i; continue; }

                std::istringstream iss(line);
                int nodeId;
                double x, y, z;
                iss >> nodeId >> x >> y >> z;
                vtkIdType pid = static_cast<vtkIdType>(i);
                points->SetPoint(pid, x, y, z);
                nodeIdToVtkId[nodeId] = pid;
                nodeIdArray->SetValue(pid, nodeId);
            }
            // $EndNodes
            std::getline(fin, line);
        }
        else if (line == "$Elements")
        {
            std::getline(fin, line);
            int nElem = std::stoi(trim(line));

            for (int i = 0; i < nElem; ++i)
            {
                std::getline(fin, line);
                if (line.empty()) { --i; continue; }

                std::istringstream iss(line);
                int elemId = 0;
                int elemType = 0;
                int numTags = 0;
                iss >> elemId >> elemType >> numTags;

                if (!iss)
                    continue;

                // 读取 tags
                std::vector<int> tags(numTags, 0);
                for (int t = 0; t < numTags; ++t)
                {
                    iss >> tags[t];
                }

                int physicalTag = (numTags >= 1) ? tags[0] : -1;
                // int elementaryTag = (numTags >= 2) ? tags[1] : -1; // 如需可用

                // 剩下的是节点号
                std::vector<int> nodeIds;
                int nid;
                while (iss >> nid)
                {
                    nodeIds.push_back(nid);
                }

                int vtkCellType = GmshElementToVtkCellType(elemType);
                int dim = GmshElementDimension(elemType);
                if (vtkCellType < 0 || dim < 0)
                {
                    // 不支持的单元类型，直接跳过
                    continue;
                }

                vtkIdType npts = static_cast<vtkIdType>(nodeIds.size());
                std::vector<vtkIdType> conn(npts);
                for (vtkIdType k = 0; k < npts; ++k)
                {
                    auto it = nodeIdToVtkId.find(nodeIds[k]);
                    if (it == nodeIdToVtkId.end())
                    {
                        std::cerr << "Node id " << nodeIds[k]
                                  << " not found in node map.\n";
                        conn[k] = 0;
                    }
                    else
                    {
                        conn[k] = it->second;
                    }
                }

                vtkIdType cellId = grid->InsertNextCell(
                    vtkCellType, npts, conn.data());

                // CellData 写入
                elemIdArray->InsertValue(cellId, elemId);
                dimArray->InsertValue(cellId, dim);
                physIdArray->InsertValue(cellId, physicalTag);

                // 找到物理名
                std::string physName;
                auto itName = info.physicalNameMap.find(
                    std::make_pair(dim, physicalTag));
                if (itName != info.physicalNameMap.end())
                    physName = itName->second;
                else
                    physName = "";

                physNameArray->InsertValue(cellId, physName);
            }
            // $EndElements
            std::getline(fin, line);
        }
        // 其他 section 直接忽略
    }

    return grid;
}
