#include "MeshProcessor.h"

MeshProcessor::MeshProcessor()
{
    gmsh::initialize();
}

MeshProcessor::~MeshProcessor()
{
    // gmsh::finalize();
}

void MeshProcessor::clear()
{
    gmsh::finalize();
    gmsh::initialize();
    pointToNodes.clear();
    lineToElements.clear();
    surfaceToElements.clear();
    volumeToElements.clear();
    nodeCoordinates.clear();
    points.clear();
    edges.clear();
    faces.clear();
    volumes.clear();
    m_nodeSize = 0;
    m_pointEleSize = 0;
    m_lineEleSize = 0;
    m_faceEleSize = 0;
    m_solidEleSize = 0;
    m_isImportOrGener = false;
    m_nasPath = "";
    m_vtkPath = "";
    m_mshPath = "";
}

void MeshProcessor::loadOCCFile(const QString& filePath)
{
    try
    {
        clear();
        gmsh::model::add("model");
        std::vector<std::pair<int, int>> outDimTags;
        std::string stepName = filePath.toStdString();

        // 几何修复选项必须在 importShapes
        // 前设置；否则会在后续模型上生效，造成跨次串扰。
        gmsh::option::setNumber("Geometry.OCCFixDegenerated",
                                m_fixShape ? 1 : 0); // 修复退化边或面
        gmsh::option::setNumber("Geometry.OCCFixSmallEdges",
                                m_fixShape ? 1 : 0); // 修复小边
        gmsh::option::setNumber("Geometry.OCCFixSmallFaces",
                                m_fixShape ? 1 : 0); // 修复小面
        // OCCSewFaces 在部分 STEP 上会把实体缝成壳，导致 3D
        // 体网格为空；默认关闭以保证体网格稳定性。
        gmsh::option::setNumber("Geometry.OCCSewFaces", 0);  // 缝合面
        gmsh::option::setNumber("Geometry.Tolerance", 1e-6); // 设置几何容差

        gmsh::model::occ::importShapes(stepName, outDimTags, true);
        // Merge duplicated coincident OCC entities (e.g. doubled interface faces)
        // so multi-domain union keeps a shared interface instead of two copies.
        //        gmsh::model::occ::removeAllDuplicates();
        //        gmsh::model::occ::synchronize();

        qDebug() << "Successfully loaded occ file:" << QString::fromStdString(stepName);
        qDebug() << "Imported entities:";
        for (std::vector<std::pair<int, int>>::const_iterator it = outDimTags.begin();
             it != outDimTags.end(); ++it)
        {
            qDebug() << "Dimension:" << it->first << ", Tag:" << it->second;
        }
        bool isSuccess = createGridByDim();
        if (!isSuccess)
        {
            return;
        }
        // 为几何实体创建物理组
        std::map<int, int> geom_point_to_physical, geom_edge_to_physical, geom_volume_to_physical;
        geom_point_to_physical = createPointPhysicalGroup();
        geom_edge_to_physical = createLinePhysicalGroup();
        m_geom_face_to_physical = createFacePhysicalGroup();
        geom_volume_to_physical = createSolidPhysicalGroup();
        // 获取所有节点坐标
        std::vector<std::size_t> all_node_tags; // 所有的节点
        std::vector<double> all_node_coords;    // 所有的节点坐标（数量是all_node_tags的三倍）
        std::vector<double> all_node_params;
        gmsh::model::mesh::getNodes(all_node_tags, all_node_coords, all_node_params);
        for (std::size_t i = 0; i < all_node_tags.size(); ++i)
        {
            std::vector<double> coords(3);
            coords[0] = all_node_coords[3 * i];
            coords[1] = all_node_coords[3 * i + 1];
            coords[2] = all_node_coords[3 * i + 2];
            nodeCoordinates[all_node_tags[i]] = coords;
        }
        m_nodeSize = all_node_tags.size();
        // 获取所有网格元素
        std::vector<int> element_types0;
        std::vector<std::vector<std::size_t>> element_tags0;
        std::vector<std::vector<std::size_t>> node_tags0;
        gmsh::model::mesh::getElements(element_types0, element_tags0, node_tags0,
                                       0); // 点
        for (const auto& tags : element_tags0)
        {
            m_pointEleSize += tags.size();
        }

        std::vector<int> element_types1;
        std::vector<std::vector<std::size_t>> element_tags1;
        std::vector<std::vector<std::size_t>> node_tags1;
        gmsh::model::mesh::getElements(element_types1, element_tags1, node_tags1,
                                       1); // 线
        for (const auto& tags : element_tags1)
        {
            m_lineEleSize += tags.size();
        }

        std::vector<int> element_types2;
        std::vector<std::vector<std::size_t>> element_tags2;
        std::vector<std::vector<std::size_t>> node_tags2;
        gmsh::model::mesh::getElements(element_types2, element_tags2, node_tags2,
                                       2); // 面
        for (const auto& tags : element_tags2)
        {
            m_faceEleSize += tags.size();
        }

        std::vector<int> element_types3;
        std::vector<std::vector<std::size_t>> element_tags3;
        std::vector<std::vector<std::size_t>> node_tags3;
        gmsh::model::mesh::getElements(element_types3, element_tags3, node_tags3,
                                       3); // 体
        for (const auto& tags : element_tags3)
        {
            m_solidEleSize += tags.size();
        }

        // 节点到几何点的映射
        int pointIndex = 0;
        for (std::map<int, int>::const_iterator it = geom_point_to_physical.begin();
             it != geom_point_to_physical.end(); ++it)
        {
            int geom_tag = it->first;
            int phys_tag = it->second;
            std::vector<std::size_t> phys_node_tags;
            std::vector<double> phys_node_coords;
            gmsh::model::mesh::getNodesForPhysicalGroup(0, phys_tag, phys_node_tags,
                                                        phys_node_coords);
            std::set<std::size_t> phys_node_set(phys_node_tags.begin(), phys_node_tags.end());
            const std::vector<std::size_t>& elem_tags = element_tags0[0];
            const std::vector<std::size_t>& elem_nodes = node_tags0[0];
            int elem_index = elem_tags[pointIndex];
            int node_index = elem_nodes[pointIndex];
            std::vector<int> _vec1;
            _vec1.push_back(elem_index);
            std::vector<int> _vec2;
            _vec2.push_back(node_index);
            pointToNodes[geom_tag] = _vec1;
            pointToNodesIndex[elem_index] = _vec2;
            pointIndex++;
        }

        // 线单元到几何线的映射
        for (std::map<int, int>::const_iterator it = geom_edge_to_physical.begin();
             it != geom_edge_to_physical.end(); ++it)
        {
            int geom_tag = it->first;
            int phys_tag = it->second;
            std::vector<std::size_t> phys_node_tags;
            std::vector<double> phys_node_coords;
            gmsh::model::mesh::getNodesForPhysicalGroup(1, phys_tag, phys_node_tags,
                                                        phys_node_coords);
            std::set<std::size_t> phys_node_set(phys_node_tags.begin(), phys_node_tags.end());

            for (std::size_t i = 0; i < element_types1.size(); ++i)
            {
                if (element_types1[i] != 1)
                    continue; // 仅处理线单元
                const std::vector<std::size_t>& elem_tags = element_tags1[i];
                const std::vector<std::size_t>& elem_nodes = node_tags1[i];
                for (std::size_t j = 0; j < elem_tags.size(); ++j)
                {
                    std::size_t elem_tag = elem_tags[j];
                    std::size_t node1 = elem_nodes[2 * j];
                    std::size_t node2 = elem_nodes[2 * j + 1];
                    if (phys_node_set.find(node1) != phys_node_set.end() &&
                        phys_node_set.find(node2) != phys_node_set.end())
                    {
                        std::vector<int> _vec1;
                        _vec1.push_back(node1);
                        _vec1.push_back(node2);
                        lineToElements[geom_tag].push_back(elem_tag);
                        lineToElementsIndex[elem_tag] = _vec1;
                    }
                }
            }
        }

        // 面单元到几何面的映射
        for (std::map<int, int>::const_iterator it = m_geom_face_to_physical.begin();
             it != m_geom_face_to_physical.end(); ++it)
        {
            int geom_tag = it->first;
            int phys_tag = it->second;
            std::vector<std::size_t> phys_node_tags;
            std::vector<double> phys_node_coords;
            gmsh::model::mesh::getNodesForPhysicalGroup(2, phys_tag, phys_node_tags,
                                                        phys_node_coords);
            std::set<std::size_t> phys_node_set(phys_node_tags.begin(), phys_node_tags.end());

            for (std::size_t i = 0; i < element_types2.size(); ++i)
            {
                if (element_types2[i] != 2 && element_types2[i] != 3) // 三角形或四边形
                {
                    continue;
                }
                const std::vector<std::size_t>& elem_tags = element_tags2[i];
                const std::vector<std::size_t>& elem_nodes = node_tags2[i];
                int nodes_per_elem = (element_types2[i] == 2) ? 3 : 4;
                for (std::size_t j = 0; j < elem_tags.size(); ++j)
                {
                    std::size_t elem_tag = elem_tags[j];
                    bool all_nodes_in_face = true;
                    for (int k = 0; k < nodes_per_elem; ++k)
                    {
                        std::size_t node = elem_nodes[j * nodes_per_elem + k];
                        if (phys_node_set.find(node) == phys_node_set.end())
                        {
                            all_nodes_in_face = false;
                            break;
                        }
                    }
                    if (all_nodes_in_face)
                    {
                        surfaceToElements[geom_tag].push_back(elem_tag);
                        std::vector<int> _vec1;
                        for (int k = 0; k < nodes_per_elem; ++k)
                        {
                            _vec1.push_back(elem_nodes[j * nodes_per_elem + k]);
                        }
                        surfaceToElementsIndex[elem_tag] = _vec1;
                    }
                }
            }
        }

        // 体单元到几何体的映射
        for (std::map<int, int>::const_iterator it = geom_volume_to_physical.begin();
             it != geom_volume_to_physical.end(); ++it)
        {
            int geom_tag = it->first;
            int phys_tag = it->second;
            std::vector<std::size_t> phys_node_tags;
            std::vector<double> phys_node_coords;
            gmsh::model::mesh::getNodesForPhysicalGroup(3, phys_tag, phys_node_tags,
                                                        phys_node_coords);
            std::set<std::size_t> phys_node_set(phys_node_tags.begin(), phys_node_tags.end());

            for (std::size_t i = 0; i < element_types3.size(); ++i)
            {
                if (element_types3[i] != 4 && element_types3[i] != 5)
                    continue; // 四面体或六面体
                const std::vector<std::size_t>& elem_tags = element_tags3[i];
                const std::vector<std::size_t>& elem_nodes = node_tags3[i];
                int nodes_per_elem = (element_types3[i] == 4) ? 4 : 8;
                for (std::size_t j = 0; j < elem_tags.size(); ++j)
                {
                    std::size_t elem_tag = elem_tags[j];
                    bool all_nodes_in_volume = true;
                    for (int k = 0; k < nodes_per_elem; ++k)
                    {
                        std::size_t node = elem_nodes[j * nodes_per_elem + k];
                        if (phys_node_set.find(node) == phys_node_set.end())
                        {
                            all_nodes_in_volume = false;
                            break;
                        }
                    }
                    if (all_nodes_in_volume)
                    {
                        volumeToElements[geom_tag].push_back(elem_tag);
                        std::vector<int> _vec1;
                        for (int k = 0; k < nodes_per_elem; ++k)
                        {
                            _vec1.push_back(elem_nodes[j * nodes_per_elem + k]);
                        }
                        volumeToElementsIndex[elem_tag] = _vec1;
                    }
                }
            }
        }
        // 导出网格
        writeGmshFile();
    }
    catch (const std::string& error)
    {
        qDebug() << "Failed to load OCC file";
        throw std::runtime_error("Failed to load OCC file");
    }
}

bool MeshProcessor::createGridByDim()
{
    try
    {
        qDebug() << "m_dimension " << m_dimension;

        if (m_dimension == 2)
        {
            if (m_gridType == 2)
            {
                gmsh::option::setNumber("Mesh.RecombineAll", 1); // 启用四边形重组
                gmsh::option::setNumber("Mesh.SubdivisionAlgorithm", 1);
            }
            // 最大单元尺寸
            gmsh::option::setNumber("Mesh.CharacteristicLengthMax", m_gridLengthMax);
            // 最小单元尺寸
            gmsh::option::setNumber("Mesh.CharacteristicLengthMin", m_gridLengthMin);
            // 尺寸因子
            gmsh::option::setNumber("Mesh.MeshSizeFactor", m_sizeFactor);
            // 阶数(1为线性2为二次)
            gmsh::option::setNumber("Mesh.ElementOrder", m_elementOrder);
            // 算法
            qDebug() << "算法：" << m_aigorithm;
            gmsh::option::setNumber("Mesh.Algorithm", m_aigorithm);
            // 光滑迭代次数
            gmsh::option::setNumber("Mesh.Smoothing", m_smoothing);

            //// 新增: 启用只网格可见实体
            // gmsh::option::setNumber("Mesh.MeshOnlyVisible", 1);

            //// 获取所有2D实体标签
            // std::vector<std::pair<int, int>> allSurfaces;
            // gmsh::model::getEntities(allSurfaces, 2);
            //// 隐藏非目标面
            // for (const auto& entity : allSurfaces)
            //{
            //	if (entity.second != 5)
            //	{  // surfaceTag 为目标面标签
            //		gmsh::model::setVisibility({ entity }, 0);
            //	}
            // }
            gmsh::model::mesh::generate(2);
            // gmsh::option::setNumber("Mesh.MeshOnlyVisible", 0);
        }
        else
        {
            if (m_gridType == 4)
            {
                gmsh::option::setNumber("Mesh.Recombine3DAll", 1);
                gmsh::option::setNumber("Mesh.SubdivisionAlgorithm",
                                        2); // 设置网格细分为六面体
            }
            gmsh::option::setNumber("Mesh.CharacteristicLengthMax", m_gridLengthMax);
            gmsh::option::setNumber("Mesh.CharacteristicLengthMin", m_gridLengthMin);
            gmsh::option::setNumber("Mesh.MeshSizeFactor", m_sizeFactor);
            gmsh::option::setNumber("Mesh.ElementOrder", m_elementOrder);
            qDebug() << "算法：" << m_aigorithm;
            gmsh::option::setNumber("Mesh.Algorithm3D", m_aigorithm);
            gmsh::model::mesh::generate(3);
        }
    }
    catch (const std::string& error)
    {
        return false;
    }
    catch (const char* error)
    {
        return false;
    }
    catch (...)
    {
        return false;
    }
    return true;
}

std::map<int, int> MeshProcessor::createPointPhysicalGroup()
{
    // 获取所有几何实体
    gmsh::model::getEntities(points, 0); // 0维：点
    std::map<int, int> geom_point_to_physical;
    for (std::vector<std::pair<int, int>>::const_iterator it = points.begin(); it != points.end();
         ++it)
    {
        int tag = it->second;
        int physical_tag = gmsh::model::addPhysicalGroup(0, std::vector<int>(1, tag));
        gmsh::model::setPhysicalName(0, physical_tag, "point_" + std::to_string(tag));
        geom_point_to_physical[tag] = physical_tag;
    }
    return geom_point_to_physical;
}

std::map<int, int> MeshProcessor::createLinePhysicalGroup()
{
    gmsh::model::getEntities(edges, 1); // 1维：线
    std::map<int, int> geom_edge_to_physical;
    for (std::vector<std::pair<int, int>>::const_iterator it = edges.begin(); it != edges.end();
         ++it)
    {
        int tag = it->second;
        int physical_tag = gmsh::model::addPhysicalGroup(1, std::vector<int>(1, tag));
        gmsh::model::setPhysicalName(1, physical_tag, "edge_" + std::to_string(tag));
        geom_edge_to_physical[tag] = physical_tag;
    }
    return geom_edge_to_physical;
}

std::map<int, int> MeshProcessor::createFacePhysicalGroup()
{
    std::map<int, int> geom_to_phys;

    // 获取几何中的所有“面”实体（OCC surface），不是网格单元
    gmsh::model::getEntities(faces, 2); // 2 = surface

    qDebug() << "createFacePhysicalGroup: surfaces count =" << faces.size();

    for (const auto& s : faces)
    {
        int surface_tag = s.second;

        // 不手动指定 phys_id，让 gmsh 自己分配（和你之前的行为一致）
        int phys_tag = gmsh::model::addPhysicalGroup(2, std::vector<int>(1, surface_tag));

        // 名字随便起一个，主要是方便检查
        std::string name = "face_" + std::to_string(surface_tag);
        gmsh::model::setPhysicalName(2, phys_tag, name);

        // 记录：几何面 tag -> physical tag
        geom_to_phys[surface_tag] = phys_tag;

        qDebug() << "  surface_tag =" << surface_tag << " -> phys_tag =" << phys_tag
                 << " name =" << QString::fromStdString(name);
    }

    return geom_to_phys;
}

std::map<int, int> MeshProcessor::createSolidPhysicalGroup()
{
    gmsh::model::getEntities(volumes, 3); // 3维：体
    std::map<int, int> geom_volume_to_physical;
    for (std::vector<std::pair<int, int>>::const_iterator it = volumes.begin(); it != volumes.end();
         ++it)
    {
        int tag = it->second;
        int physical_tag = gmsh::model::addPhysicalGroup(3, std::vector<int>(1, tag));
        gmsh::model::setPhysicalName(3, physical_tag, "volume_" + std::to_string(tag));
        geom_volume_to_physical[tag] = physical_tag;
    }
    return geom_volume_to_physical;
}

void MeshProcessor::writeGmshFile()
{
    m_isImportOrGener = true;
    QString exelPath = QCoreApplication::applicationDirPath();
    const QString tempPath = exelPath + "/../tempGrid/";
    removeDir(tempPath);
    QDir dir = QDir(tempPath);
    if (!dir.exists())
    {
        bool flag = dir.mkpath(tempPath);
    }
    m_nasPath = tempPath + "mesh.nas";
    m_vtkPath = tempPath + "mesh.vtk";
    m_mshPath = tempPath + "mesh.msh";
    gmsh::write(m_nasPath.toStdString());       // Nastran
    gmsh::option::setNumber("Mesh.SaveAll", 0); // 保存仅定义物理组的元素
    gmsh::write(m_vtkPath.toStdString());       // VTK
    gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
    gmsh::write(m_mshPath.toStdString()); // MSH
}

bool MeshProcessor::writeGridFile(const QString& path)
{
    if (!m_isImportOrGener)
        return false;
    std::string gridPath = path.toStdString();
    gmsh::write(gridPath);
    return true;
}

bool MeshProcessor::removeDir(QString dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return true;
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    QFileInfoList fileList = dir.entryInfoList();
    for (QFileInfo file : fileList)
    {
        if (file.isFile())
        {
            file.dir().remove(file.fileName());
        }
        else
        {
            removeDir(file.absoluteFilePath());
        }
    }
    return dir.rmpath(dir.absolutePath());
}

QList<int> MeshProcessor::getGridInfo()
{
    QList<int> _list;
    _list << m_nodeSize;
    _list << m_pointEleSize;
    _list << m_lineEleSize;
    _list << m_faceEleSize;
    _list << m_solidEleSize;
    return _list;
}

bool MeshProcessor::loadImportGridFile(const QString& filePath)
{
    clear();
    try
    {
        std::string path = filePath.toStdString();
        std::string modelName = "msh_model";
        removeExistingModel(modelName);
        gmsh::option::setNumber("General.Verbosity", 5);
        gmsh::model::add(modelName);
        gmsh::merge(path);
    }
    catch (const std::exception& e)
    {
        qDebug() << "Error importing .msh file: ";
        return false;
    }
    std::vector<std::size_t> nodeTags;
    std::vector<double> nodeCoords, parametricCoords;
    gmsh::model::mesh::getNodes(nodeTags, nodeCoords, parametricCoords);
    m_nodeSize = nodeTags.size();

    std::vector<int> element_types0;
    std::vector<std::vector<std::size_t>> element_tags0;
    std::vector<std::vector<std::size_t>> node_tags0;
    gmsh::model::mesh::getElements(element_types0, element_tags0, node_tags0,
                                   0); // 点
    for (const auto& tags : element_tags0)
    {
        m_pointEleSize += tags.size();
    }
    std::vector<int> element_types1;
    std::vector<std::vector<std::size_t>> element_tags1;
    std::vector<std::vector<std::size_t>> node_tags1;
    gmsh::model::mesh::getElements(element_types1, element_tags1, node_tags1,
                                   1); // 线
    for (const auto& tags : element_tags1)
    {
        m_lineEleSize += tags.size();
    }
    std::vector<int> element_types2;
    std::vector<std::vector<std::size_t>> element_tags2;
    std::vector<std::vector<std::size_t>> node_tags2;
    gmsh::model::mesh::getElements(element_types2, element_tags2, node_tags2,
                                   2); // 面
    for (const auto& tags : element_tags2)
    {
        m_faceEleSize += tags.size();
    }
    std::vector<int> element_types3;
    std::vector<std::vector<std::size_t>> element_tags3;
    std::vector<std::vector<std::size_t>> node_tags3;
    gmsh::model::mesh::getElements(element_types3, element_tags3, node_tags3,
                                   3); // 体
    for (const auto& tags : element_tags3)
    {
        m_solidEleSize += tags.size();
    }
    writeGmshFile();
    return true;
}

void MeshProcessor::removeExistingModel(const std::string& modelName)
{
    std::vector<std::string> modelNames;
    gmsh::model::list(modelNames);
    for (const auto& name : modelNames)
    {
        if (name == modelName)
        {
            gmsh::model::setCurrent(modelName); // 设置为当前模型
            gmsh::model::remove();              // 移除模型
            qDebug() << "Removed existing model: " << QString::fromStdString(modelName);
            break;
        }
    }
}

int MeshProcessor::getGMeshFaceIndex(int surfaceTag)
{
    return m_geom_face_to_physical[surfaceTag];
}
