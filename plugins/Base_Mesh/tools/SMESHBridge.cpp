#include "SMESHBridgeApi.h"
#include "PreparedPartitionManifest.h"
#include "MeshNativeDomainPolicy.h"
#include "MeshNativeComputePolicy.h"

#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BOPAlgo_Splitter.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <gp_Vec.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>

#include <SMDS_Mesh.hxx>
#include <SMDS_MeshElement.hxx>
#include <SMDS_MeshNode.hxx>
#include <SMDS_VolumeTool.hxx>
#include <SMESH_subMesh.hxx>
#include <SMESH_subMeshEventListener.hxx>
#include <SMESHDS_SubMesh.hxx>
#include <SMESH_ComputeError.hxx>
#include <SMESH_Gen.hxx>
#include <SMESH_Algo.hxx>
#include <SMESH_Hypothesis.hxx>
#include <SMESH_Mesh.hxx>
#include <SMESH_MeshEditor.hxx>
#include <SMESH_HypoFilter.hxx>
#include <SMESHDS_Mesh.hxx>
#include <StdMeshers_Cartesian_3D.hxx>
#include <StdMeshers_CartesianParameters3D.hxx>
#include <StdMeshers_LocalLength.hxx>
#include <StdMeshers_MEFISTO_2D.hxx>
#include <StdMeshers_NumberOfSegments.hxx>
#include <StdMeshers_Prism_3D.hxx>
#include <StdMeshers_Projection_1D2D.hxx>
#include <StdMeshers_ProjectionSource2D.hxx>
#include <StdMeshers_Quadrangle_2D.hxx>
#include <StdMeshers_Regular_1D.hxx>
#include <StdMeshers_ViscousLayers.hxx>
#include <StdMeshers_ViscousLayers2D.hxx>
#include <NETGENPlugin_NETGEN_2D.hxx>
#include <NETGENPlugin_NETGEN_2D_ONLY.hxx>
#include <NETGENPlugin_NETGEN_2D3D.hxx>
#include <NETGENPlugin_NETGEN_3D.hxx>
#include <NETGENPlugin_Hypothesis.hxx>
#include <NETGENPlugin_Mesher.hxx>
#include <NETGENPlugin_Hypothesis_2D.hxx>

#include <vtkCellData.h>
#include <vtkFieldData.h>
#include <vtkIdTypeArray.h>
#include <vtkDoubleArray.h>
#include <vtkCellType.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkPoints.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridWriter.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <tuple>
#include <type_traits>
#include <thread>
#include <utility>
#include <set>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
// Some newer NETGENPlugin builds expose NETGENPlugin_Hypothesis::SetNbThreads,
// while the Windows standalone SMESH 9.9.x packages often do not. Select
// this at compile time against the actually installed header. Do NOT run
// SMESH_Gen::Compute() concurrently on the same parent mesh: shared interface
// submeshes and NETGEN global state are not independent.
template <typename Hypothesis, typename = void>
struct HasNativeNetgenThreads : std::false_type {};

template <typename Hypothesis>
struct HasNativeNetgenThreads<Hypothesis, std::void_t<decltype(
    std::declval<Hypothesis&>().SetNbThreads(1))>> : std::true_type {};

int requestedNetgenThreads()
{
    const unsigned int detected = std::thread::hardware_concurrency();
    const int suggested = std::clamp(static_cast<int>(detected ? detected : 1u), 1, 8);
    const char* raw = std::getenv("CAE_SMESH_NETGEN_THREADS");
    if (!raw || !*raw) return suggested;
    char* end = nullptr;
    const long count = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || count < 1 || count > 64)
        throw std::invalid_argument("CAE_SMESH_NETGEN_THREADS must be an integer from 1 to 64");
    return static_cast<int>(count);
}

template <typename Hypothesis>
bool configureNativeNetgenThreads(Hypothesis* hyp, const int count)
{
    if constexpr (HasNativeNetgenThreads<Hypothesis>::value)
    {
        hyp->SetNbThreads(count);
        return true;
    }
    else
    {
        (void)hyp;
        (void)count;
        return false;
    }
}

class ComputeLock
{
public:
    ComputeLock()
    {
#ifdef _WIN32
        AcquireSRWLockExclusive(&mutex());
#else
        mutex().lock();
#endif
    }
    ~ComputeLock()
    {
#ifdef _WIN32
        ReleaseSRWLockExclusive(&mutex());
#else
        mutex().unlock();
#endif
    }
    ComputeLock(const ComputeLock&) = delete;
    ComputeLock& operator=(const ComputeLock&) = delete;
private:
#ifdef _WIN32
    static SRWLOCK& mutex()
    {
        static SRWLOCK value = SRWLOCK_INIT;
        return value;
    }
#else
    static std::mutex& mutex()
    {
        static std::mutex value;
        return value;
    }
#endif
};

template <class T>
T* makeHypothesisCompat(SMESH_Gen* gen)
{
    const int id = gen->GetANewId();
    if constexpr (std::is_constructible_v<T, int, SMESH_Gen*>)
        return new T(id, gen);
    else
        return new T(id, 0, gen);
}

void setError(char* buffer, std::size_t capacity, const std::string& message)
{
    if (!buffer || capacity == 0) return;
    const std::size_t count = std::min(capacity - 1, message.size());
    std::memcpy(buffer, message.data(), count);
    buffer[count] = '\0';
}

std::filesystem::path utf8Path(const char* value)
{
    if (!value || !*value) return {};
#ifdef _WIN32
    return std::filesystem::u8path(value);
#else
    return std::filesystem::path(value);
#endif
}

// SMESH operates directly on OCC topology. A shape index refers to the exact
// BREP snapshot passed into the bridge, not a stable ID across CAD edits.
TopoDS_Shape indexedShape(const TopoDS_Shape& root, TopAbs_ShapeEnum type,
                          int shapeIndex, const char* label)
{
    if (shapeIndex <= 0)
        throw std::runtime_error(std::string("Missing topology index for ") + label);
    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(root, type, shapes);
    if (shapeIndex > shapes.Extent())
        throw std::runtime_error(std::string("Invalid BREP topology index for ") + label);
    return shapes(shapeIndex);
}

void requireHypothesis(SMESH_Mesh& mesh,
                       const TopoDS_Shape& shape,
                       SMESH_Hypothesis& hypothesis,
                       const char* label)
{
    std::string detail;
    const auto status = mesh.AddHypothesis(shape, hypothesis.GetID(), &detail);
    if (SMESH_Hypothesis::IsStatusFatal(status))
    {
        std::ostringstream message;
        message << "SMESH rejected " << label << " (status=" << int(status) << ')';
        if (!detail.empty()) message << ": " << detail;
        throw std::runtime_error(message.str());
    }
}

bool belongsTo(const TopoDS_Shape& edge, const TopTools_IndexedMapOfShape& edges)
{
    return edges.Contains(edge);
}

template<class PointIndex>
void cadBoundaryMetadata(const SMESH_Mesh& mesh, const PointIndex& pointIds,
                         vtkIdTypeArray* connectivity, vtkDoubleArray* descriptors)
{
    connectivity->SetName("CadBoundaryConnectivity");
    descriptors->SetName("CadBoundaryDescriptors");
    descriptors->SetNumberOfComponents(6);
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(mesh.GetShapeToMesh(), TopAbs_FACE, faces);
    const auto* data = mesh.GetMeshDS();
    for (int i = 1; i <= faces.Extent(); ++i) {
        const int shapeId = data->ShapeToIndex(faces(i));
        const double row[]{double(shapeId), 0.0, 0.0, 0.0, 0.0, double(i)};
        descriptors->InsertNextTuple(row);
    }
    auto iterator = data->facesIterator();
    while (iterator->more()) {
        const auto* face = iterator->next();
        if (!face || face->GetShapeID() <= 0 || face->NbNodes() < 3) continue;
        std::vector<vtkIdType> nodes;
        for (int n = 0; n < face->NbNodes(); ++n) {
            const auto found = pointIds.find(face->GetNode(n));
            if (found == pointIds.end()) break;
            nodes.push_back(vtkIdType(found->second));
        }
        if (nodes.size() != std::size_t(face->NbNodes())) continue;
        connectivity->InsertNextValue(face->GetShapeID());
        connectivity->InsertNextValue(vtkIdType(nodes.size()));
        for (auto id : nodes) connectivity->InsertNextValue(id);
    }
}

template<class PointIndex>
vtkSmartPointer<vtkIntArray> caeBoundaryFacets(const SMESH_Mesh& mesh, const PointIndex& pointIds, int dimension)
{
    auto facets = vtkSmartPointer<vtkIntArray>::New();
    facets->SetName("SmeshBoundaryFacets"); facets->SetNumberOfComponents(23);
    auto append = [&](const SMDS_MeshElement* element, int dim, int region) {
        if (!element || region <= 0 || element->NbNodes() > 20) return;
        int row[23]; std::fill(std::begin(row), std::end(row), -1);
        row[0] = dim; row[1] = region; row[2] = element->NbNodes();
        for (int n = 0; n < element->NbNodes(); ++n) {
            auto found = pointIds.find(element->GetNode(n)); if (found == pointIds.end()) return;
            row[n+3] = int(found->second);
        }
        facets->InsertNextTypedTuple(row);
    };
    const auto* data = mesh.GetMeshDS();
    for (int dim = 0; dim < dimension; ++dim) {
        TopTools_IndexedMapOfShape shapes;
        TopExp::MapShapes(mesh.GetShapeToMesh(), dim == 0 ? TopAbs_VERTEX : dim == 1 ? TopAbs_EDGE : TopAbs_FACE, shapes);
        std::map<int, int> indices;
        for (int i = 1; i <= shapes.Extent(); ++i) indices[data->ShapeToIndex(shapes(i))] = i;
        if (dim == 0) {
            auto it = data->nodesIterator();
            while (it->more()) {
                const auto* node = it->next(); const auto found = indices.find(node->GetShapeID());
                if (found == indices.end()) continue;
                const auto id = pointIds.find(node); if (id == pointIds.end()) continue;
                int row[23]; std::fill(std::begin(row), std::end(row), -1);
                row[0]=0; row[1]=found->second; row[2]=1; row[3]=int(id->second); facets->InsertNextTypedTuple(row);
            }
        } else if (dim == 1) {
            auto it = data->edgesIterator(); while (it->more()) { const auto* e=it->next(); append(e, dim, indices[e->GetShapeID()]); }
        } else {
            auto it = data->facesIterator(); while (it->more()) { const auto* e=it->next(); append(e, dim, indices[e->GetShapeID()]); }
        }
    }
    return facets;
}

void writeLegacyVtk(const SMESH_Mesh& mesh, const std::filesystem::path& path,
                    const std::map<int, int>& regions = {},
                    bool markPrismsAsBoundaryLayer = false,
                    const std::vector<std::pair<int,int>>& uiFaceMap = {})
{
    if (path.empty()) return;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create VTK output file");

    const SMDS_Mesh* data = mesh.GetMeshDS();
    if (!data) throw std::runtime_error("SMESH returned no mesh data");

    std::vector<const SMDS_MeshNode*> nodes;
    std::unordered_map<const SMDS_MeshNode*, std::size_t> nodeIds;
    SMDS_NodeIteratorPtr nodeIt = data->nodesIterator();
    while (nodeIt->more())
    {
        const SMDS_MeshNode* node = nodeIt->next();
        nodeIds.emplace(node, nodes.size());
        nodes.push_back(node);
    }

    std::vector<const SMDS_MeshElement*> cells;
    std::size_t cellStorage = 0;
    SMDS_VolumeIteratorPtr volumeIt = data->volumesIterator();
    while (volumeIt->more())
    {
        const SMDS_MeshElement* cell = volumeIt->next();
        if (!cell) continue;
        if (cell->IsPoly()) throw std::runtime_error("Polyhedral VTK export is not supported by this sweep bridge");
        cells.push_back(cell);
        cellStorage += 1 + static_cast<std::size_t>(cell->NbNodes());
    }

    out << "# vtk DataFile Version 3.0\nTMSR standalone SMESH sweep\nASCII\n"
           "DATASET UNSTRUCTURED_GRID\nPOINTS " << nodes.size() << " double\n";
    out << std::setprecision(17);
    for (const SMDS_MeshNode* node : nodes)
        out << node->X() << ' ' << node->Y() << ' ' << node->Z() << '\n';

    out << "CELLS " << cells.size() << ' ' << cellStorage << '\n';
    for (const SMDS_MeshElement* cell : cells)
    {
        const int count = cell->NbNodes();
        const auto& vtkOrder = SMDS_MeshCell::toVtkOrder(cell->GetVtkType());
        out << count;
        for (int i = 0; i < count; ++i)
        {
            const int smdsIndex = vtkOrder.empty() ? i : vtkOrder.at(i);
            const auto found = nodeIds.find(cell->GetNode(smdsIndex));
            if (found == nodeIds.end()) throw std::runtime_error("Invalid SMESH cell connectivity");
            out << ' ' << found->second;
        }
        out << '\n';
    }
    out << "CELL_TYPES " << cells.size() << '\n';
    for (const SMDS_MeshElement* cell : cells) out << int(cell->GetVtkType()) << '\n';
    auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New();
    auto descriptors = vtkSmartPointer<vtkDoubleArray>::New();
    cadBoundaryMetadata(mesh, nodeIds, connectivity, descriptors);
    out << "FIELD FieldData " << (uiFaceMap.empty() ? 3 : 4)
        << "\nCadBoundaryConnectivity 1 " << connectivity->GetNumberOfValues() << " int\n";
    for (vtkIdType i = 0; i < connectivity->GetNumberOfValues(); ++i) out << connectivity->GetValue(i) << '\n';
    out << "CadBoundaryDescriptors 6 " << descriptors->GetNumberOfTuples() << " double\n";
    for (vtkIdType i = 0; i < descriptors->GetNumberOfTuples(); ++i) {
        for (int component = 0; component < 6; ++component) out << descriptors->GetComponent(i, component) << ' ';
        out << '\n';
    }

    if (!uiFaceMap.empty())
    {
        out << "CadFaceUiMap 2 " << uiFaceMap.size() << " int\n";
        for (const auto& [shapeId, uiId] : uiFaceMap)
            out << shapeId << ' ' << uiId << '\n';
    }

    auto nativeFacets=caeBoundaryFacets(mesh,nodeIds,3);
    out << "SmeshBoundaryFacets 23 " << nativeFacets->GetNumberOfTuples() << " int\n";
    for(vtkIdType i=0;i<nativeFacets->GetNumberOfValues();++i)out<<nativeFacets->GetValue(i)<<'\n';

    if (!regions.empty() || markPrismsAsBoundaryLayer)
    {
        out << "CELL_DATA " << cells.size() << '\n';
        if (!regions.empty())
        {
            out << "SCALARS matype int 1\nLOOKUP_TABLE default\n";
            for (const SMDS_MeshElement* cell : cells)
            {
                const auto it = regions.find(cell->GetShapeID());
                out << (it == regions.end() ? 0 : it->second) << '\n';
            }
        }
        if (markPrismsAsBoundaryLayer)
        {
            out << "SCALARS BoundaryLayerAddedCell int 1\nLOOKUP_TABLE default\n";
            for (const SMDS_MeshElement* cell : cells)
                out << (cell->GetEntityType() == SMDSEntity_Penta ? 1 : 0) << '\n';
        }
    }
    out.flush();
    if (!out) throw std::runtime_error("Failed while writing VTK output");
}

// Fast, streaming legacy VTK writer for large native NETGEN meshes. Keep the
// old ASCII writer for the sweep/mixed paths; binary is enabled for NETGEN only.
// Legacy VTK requires big-endian binary values on all host architectures.
class BinaryVtkBuffer
{
public:
    explicit BinaryVtkBuffer(std::ofstream& output) : out_(output) {}
    ~BinaryVtkBuffer() { flush(); }

    void int32(std::int32_t value) { appendBigEndian(value); }
    void float64(double value) { appendBigEndian(value); }
    void flush()
    {
        if (size_)
        {
            out_.write(buffer_.data(), static_cast<std::streamsize>(size_));
            size_ = 0;
        }
    }
private:
    template<typename T> void appendBigEndian(T value)
    {
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        const std::uint16_t one = 1;
        if (*reinterpret_cast<const unsigned char*>(&one))
            std::reverse(bytes, bytes + sizeof(T));
        if (size_ + sizeof(T) > buffer_.size()) flush();
        std::memcpy(buffer_.data() + size_, bytes, sizeof(T));
        size_ += sizeof(T);
    }
    std::ofstream& out_;
    std::array<char, 1 << 16> buffer_{};
    std::size_t size_ = 0;
};

void writeNetgenBinaryVtk(const SMESH_Mesh& mesh, const std::filesystem::path& path,
                          const std::map<int, int>& regions, bool markPrismsAsBoundaryLayer,
                          const std::vector<std::pair<int,int>>& faceBrepMap = {})
{
    if (path.empty()) return;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const SMDS_Mesh* data = mesh.GetMeshDS();
    if (!data) throw std::runtime_error("SMESH returned no mesh data");

    std::vector<const SMDS_MeshNode*> nodes;
    std::unordered_map<const SMDS_MeshNode*, std::int32_t> nodeIds;
    nodes.reserve(static_cast<std::size_t>(mesh.NbNodes()));
    nodeIds.reserve(static_cast<std::size_t>(mesh.NbNodes()));
    auto nodeIt = data->nodesIterator();
    while (nodeIt->more())
    {
        const auto* node = nodeIt->next();
        if (nodes.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::runtime_error("Legacy VTK 32-bit point index limit exceeded");
        nodeIds.emplace(node, static_cast<std::int32_t>(nodes.size()));
        nodes.push_back(node);
    }

    std::vector<const SMDS_MeshElement*> cells;
    cells.reserve(static_cast<std::size_t>(mesh.NbVolumes()));
    std::uint64_t cellStorage = 0;
    auto volumeIt = data->volumesIterator();
    while (volumeIt->more())
    {
        const auto* cell = volumeIt->next();
        if (!cell) continue;
        if (cell->IsPoly()) throw std::runtime_error("Polyhedral NETGEN VTK export is not supported");
        cells.push_back(cell);
        cellStorage += 1 + static_cast<std::uint64_t>(cell->NbNodes());
    }
    const auto maxInt = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (cellStorage > maxInt || cells.size() > maxInt || nodes.size() > maxInt)
        throw std::runtime_error("Legacy VTK 32-bit cell storage limit exceeded");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create binary NETGEN VTK file");
    out << "# vtk DataFile Version 3.0\nTMSR SMESH NETGEN\nBINARY\n"
           "DATASET UNSTRUCTURED_GRID\nPOINTS " << nodes.size() << " double\n";
    {
        BinaryVtkBuffer bin(out);
        for (const auto* node : nodes)
        {
            bin.float64(node->X());
            bin.float64(node->Y());
            bin.float64(node->Z());
        }
        bin.flush();
    }
    out << "\nCELLS " << cells.size() << ' ' << cellStorage << '\n';
    {
        BinaryVtkBuffer bin(out);
        for (const auto* cell : cells)
        {
            const int count = cell->NbNodes();
            bin.int32(count);
            const auto& vtkOrder = SMDS_MeshCell::toVtkOrder(cell->GetVtkType());
            for (int i = 0; i < count; ++i)
            {
                const int smdsIndex = vtkOrder.empty() ? i : vtkOrder.at(i);
                const auto it = nodeIds.find(cell->GetNode(smdsIndex));
                if (it == nodeIds.end()) throw std::runtime_error("Invalid SMESH cell connectivity");
                bin.int32(it->second);
            }
        }
        bin.flush();
    }
    out << "\nCELL_TYPES " << cells.size() << '\n';
    {
        BinaryVtkBuffer bin(out);
        for (const auto* cell : cells) bin.int32(static_cast<std::int32_t>(cell->GetVtkType()));
        bin.flush();
    }

    // Preserve the original CAD face information consumed by MeshCadBoundary.
    auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New();
    auto descriptors = vtkSmartPointer<vtkDoubleArray>::New();
    cadBoundaryMetadata(mesh, nodeIds, connectivity, descriptors);
    if (connectivity->GetNumberOfValues() > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("Legacy VTK CAD boundary field is too large");
    out << "\nFIELD FieldData " << (faceBrepMap.empty()?3:4) << "\nCadBoundaryConnectivity 1 "
        << connectivity->GetNumberOfValues() << " int\n";
    {
        BinaryVtkBuffer bin(out);
        for (vtkIdType i = 0; i < connectivity->GetNumberOfValues(); ++i)
            bin.int32(static_cast<std::int32_t>(connectivity->GetValue(i)));
        bin.flush();
    }
    out << "\nCadBoundaryDescriptors 6 " << descriptors->GetNumberOfTuples() << " double\n";
    {
        BinaryVtkBuffer bin(out);
        for (vtkIdType i = 0; i < descriptors->GetNumberOfTuples(); ++i)
            for (int c = 0; c < 6; ++c)
                bin.float64(descriptors->GetComponent(i, c));
        bin.flush();
    }
    if(!faceBrepMap.empty()) {
        out << "\nCadFaceBrepMap 2 " << faceBrepMap.size() << " int\n";
        BinaryVtkBuffer bin(out);for(const auto& row:faceBrepMap){bin.int32(row.first);bin.int32(row.second);}bin.flush();
    }
    auto nativeFacets = caeBoundaryFacets(mesh, nodeIds, 3);
    out << "\nSmeshBoundaryFacets 23 " << nativeFacets->GetNumberOfTuples() << " int\n";
    { BinaryVtkBuffer bin(out); for (vtkIdType i=0; i<nativeFacets->GetNumberOfValues(); ++i) bin.int32(nativeFacets->GetValue(i)); bin.flush(); }
    if (!regions.empty() || markPrismsAsBoundaryLayer)
    {
        out << "\nCELL_DATA " << cells.size() << '\n';
        if (!regions.empty())
        {
            out << "SCALARS matype int 1\nLOOKUP_TABLE default\n";
            BinaryVtkBuffer bin(out);
            for (const auto* cell : cells)
            {
                const auto it = regions.find(cell->GetShapeID());
                bin.int32(it == regions.end() ? 0 : it->second);
            }
            bin.flush();
            out << '\n';
        }
        if (markPrismsAsBoundaryLayer)
        {
            out << "SCALARS BoundaryLayerAddedCell int 1\nLOOKUP_TABLE default\n";
            BinaryVtkBuffer bin(out);
            for (const auto* cell : cells)
                bin.int32(cell->GetEntityType() == SMDSEntity_Penta ? 1 : 0);
            bin.flush();
            out << '\n';
        }
    }
    out.flush();
    if (!out) throw std::runtime_error("Failed while writing binary NETGEN VTK output");
}

// Opt-in read-only diagnostic; never publish a failed surface as a result.
void dumpFailedSurface(SMESH_Mesh& mesh, const TopoDS_Shape& solid,
                       const std::filesystem::path& path)
{
    if (!std::getenv("CAE_SMESH_DUMP_FAILED_SURFACE")) return;
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
    auto nodeIds = vtkSmartPointer<vtkIdTypeArray>::New(); nodeIds->SetName("smds_node_id");
    auto shapeIds = vtkSmartPointer<vtkIntArray>::New(); shapeIds->SetName("smesh_face_id");
    auto elementIds = vtkSmartPointer<vtkIdTypeArray>::New(); elementIds->SetName("smds_element_id");
    std::map<const SMDS_MeshNode*, vtkIdType> indices;
    for (TopExp_Explorer ex(solid, TopAbs_FACE); ex.More(); ex.Next()) {
        auto* sub = mesh.GetSubMesh(ex.Current())->GetSubMeshDS();
        if (!sub) continue;
        auto elements = sub->GetElements();
        while (elements->more()) {
            const auto* element = elements->next();
            if (element->GetType() != SMDSAbs_Face || element->NbCornerNodes() != 3) continue;
            vtkIdType ids[3];
            for (int n = 0; n < 3; ++n) {
                const auto* node = element->GetNode(n);
                auto found = indices.find(node);
                if (found == indices.end()) {
                    const auto id = points->InsertNextPoint(node->X(), node->Y(), node->Z());
                    nodeIds->InsertNextValue(node->GetID());
                    found = indices.emplace(node, id).first;
                }
                ids[n] = found->second;
            }
            grid->InsertNextCell(VTK_TRIANGLE, 3, ids);
            shapeIds->InsertNextValue(mesh.GetMeshDS()->ShapeToIndex(ex.Current()));
            elementIds->InsertNextValue(element->GetID());
        }
    }
    grid->SetPoints(points);
    grid->GetPointData()->AddArray(nodeIds);
    grid->GetCellData()->AddArray(shapeIds);
    grid->GetCellData()->AddArray(elementIds);
    auto writer = vtkSmartPointer<vtkUnstructuredGridWriter>::New();
    writer->SetFileName(path.u8string().c_str()); writer->SetInputData(grid);
    writer->SetFileTypeToBinary();
    if (!writer->Write()) throw std::runtime_error("Cannot save diagnostic surface");
}

// Body Fitting can create true polyhedral cut cells at the CAD boundary.
// Use VTK itself for this path so VTK_POLYHEDRON face streams are exported
// correctly. The existing sweep writer above intentionally stays unchanged.
void writeBodyFittedVtk(const SMESH_Mesh& mesh, const std::filesystem::path& path,
                        const std::map<int, int>& regions)
{
    if (path.empty()) return;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const SMDS_Mesh* data = mesh.GetMeshDS();
    if (!data) throw std::runtime_error("SMESH returned no mesh data");

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToDouble();
    std::unordered_map<const SMDS_MeshNode*, vtkIdType> pointIds;
    SMDS_NodeIteratorPtr nodeIt = data->nodesIterator();
    while (nodeIt->more())
    {
        const SMDS_MeshNode* node = nodeIt->next();
        const vtkIdType id = points->InsertNextPoint(node->X(), node->Y(), node->Z());
        pointIds.emplace(node, id);
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(points);
    grid->Allocate(static_cast<vtkIdType>(mesh.NbVolumes()));

    auto matype = vtkSmartPointer<vtkIntArray>::New();
    matype->SetName("matype");
    matype->SetNumberOfComponents(1);

    SMDS_VolumeIteratorPtr volumeIt = data->volumesIterator();
    while (volumeIt->more())
    {
        const SMDS_MeshElement* cell = volumeIt->next();
        if (!cell) continue;

        if (!cell->IsPoly())
        {
            const int count = cell->NbNodes();
            std::vector<vtkIdType> ids(static_cast<std::size_t>(count));
            const auto& vtkOrder = SMDS_MeshCell::toVtkOrder(cell->GetVtkType());
            for (int i = 0; i < count; ++i)
            {
                const int smdsIndex = vtkOrder.empty() ? i : vtkOrder.at(i);
                const auto found = pointIds.find(cell->GetNode(smdsIndex));
                if (found == pointIds.end()) throw std::runtime_error("Invalid SMESH cell connectivity");
                ids[static_cast<std::size_t>(i)] = found->second;
            }
            grid->InsertNextCell(static_cast<int>(cell->GetVtkType()), count, ids.data());
        }
        else
        {
            SMDS_VolumeTool tool(cell, /*ignoreCentralNodes=*/true);
            tool.SetExternalNormal();

            std::vector<vtkIdType> uniqueIds;
            uniqueIds.reserve(static_cast<std::size_t>(tool.NbNodes()));
            for (int i = 0; i < tool.NbNodes(); ++i)
            {
                const auto found = pointIds.find(tool.GetNodes()[i]);
                if (found == pointIds.end()) throw std::runtime_error("Invalid SMESH polyhedron node");
                uniqueIds.push_back(found->second);
            }

            std::vector<vtkIdType> faces;
            for (int faceIndex = 0; faceIndex < tool.NbFaces(); ++faceIndex)
            {
                const int nbFaceNodes = tool.NbFaceNodes(faceIndex);
                const SMDS_MeshNode** faceNodes = tool.GetFaceNodes(faceIndex);
                faces.push_back(nbFaceNodes);
                for (int n = 0; n < nbFaceNodes; ++n)
                {
                    const auto found = pointIds.find(faceNodes[n]);
                    if (found == pointIds.end()) throw std::runtime_error("Invalid SMESH polyhedron face");
                    faces.push_back(found->second);
                }
            }

            grid->InsertNextCell(VTK_POLYHEDRON,
                                 static_cast<vtkIdType>(uniqueIds.size()), uniqueIds.data(),
                                 static_cast<vtkIdType>(tool.NbFaces()), faces.data());
        }

        const auto region = regions.find(cell->GetShapeID());
        matype->InsertNextValue(region == regions.end() ? 0 : region->second);
    }

    auto connectivity = vtkSmartPointer<vtkIdTypeArray>::New();
    auto descriptors = vtkSmartPointer<vtkDoubleArray>::New();
    cadBoundaryMetadata(mesh, pointIds, connectivity, descriptors);
    grid->GetFieldData()->AddArray(connectivity);
    grid->GetFieldData()->AddArray(descriptors);
    grid->GetCellData()->AddArray(matype);
    grid->GetCellData()->SetActiveScalars("matype");

    auto writer = vtkSmartPointer<vtkUnstructuredGridWriter>::New();
#ifdef _WIN32
    const std::string fileName = path.u8string();
#else
    const std::string fileName = path.string();
#endif
    writer->SetFileName(fileName.c_str());
    writer->SetInputData(grid);
    writer->SetFileTypeToBinary();
    if (!writer->Write()) throw std::runtime_error("Failed while writing Body Fitting VTK output");
}

SMESH_Gen* generator()
{
    // Upstream projection singletons keep the first generator forever.
    static SMESH_Gen* gen = new SMESH_Gen();
    auto& registered = gen->GetStudyContext()->mapHypothesis;
    for (auto it = registered.begin(); it != registered.end(); )
        if (!it->second) it = registered.erase(it); else ++it;
    return gen;
}

void countElements(const SMESH_Mesh& mesh, CaeSMESHSweepResult& result)
{
    result = {mesh.NbNodes(), mesh.NbTriangles(), mesh.NbQuadrangles(),
              mesh.NbTetras(), mesh.NbPyramids(), mesh.NbPrisms(), mesh.NbHexas()};
}

// Read-only observer of native completion events. It neither forwards events
// nor requests Compute, and does not alter hypotheses or dependency traversal.
class NativeDomainProgress final : public SMESH_subMeshEventListener
{
public:
    explicit NativeDomainProgress(std::function<void()> complete)
        : SMESH_subMeshEventListener(false, "CAE_NativeDomainProgress"), complete_(std::move(complete)) {}
    void ProcessEvent(int, int eventType, SMESH_subMesh* sub,
        SMESH_subMeshEventListenerData*, const SMESH_Hypothesis*) override
    {
        if (reported_ || eventType != SMESH_subMesh::COMPUTE_EVENT ||
            sub->GetComputeState() != SMESH_subMesh::COMPUTE_OK || !sub->IsMeshComputed()) return;
        reported_ = true;
        try { complete_(); } catch (...) { /* presentation must not fail Compute */ }
    }
private:
    std::function<void()> complete_;
    bool reported_ = false;
};

std::string computeError(SMESH_Mesh& mesh, const TopoDS_Shape& shape)
{
    std::ostringstream out;
    int count = 0;
    auto it = mesh.GetSubMesh(shape)->getDependsOnIterator(true, false);
    while (it->more())
    {
        SMESH_subMesh* sub = it->next();
        auto err = sub->GetComputeError();
        if (err && !err->IsOK())
        {
            out << " [shape=" << sub->GetId() << ": " << err->myComment << ']';
            if (++count >= 8) { out << " (additional submesh errors omitted)"; break; }
        }
    }
    if (!count)
        out << " [shape=" << mesh.GetSubMesh(shape)->GetId()
            << ", algoState=" << int(mesh.GetSubMesh(shape)->GetAlgoState())
            << ", computeState=" << int(mesh.GetSubMesh(shape)->GetComputeState()) << ']';
    return out.str();
}
}

namespace {
std::atomic<bool> g_netgenCancelRequested{false};
// Do not take ComputeLock from the UI thread: the worker owns it for the
// entire native Compute. Guard only the lifetime of active algorithm pointers.
std::mutex g_activeNetgenMutex;
std::vector<SMESH_Algo*> g_activeNetgenAlgos;

// SALOME's NETGENPlugin CancelCompute() forwards the stop request to the
// actual NETGEN engine (netgen::multithread.terminate). The former bridge
// checked cancellation only AFTER the blocking SMESH_Gen::Compute returned.
void cancelActiveNetgenAlgorithms()
{
    std::lock_guard<std::mutex> guard(g_activeNetgenMutex);
    for (SMESH_Algo* algorithm : g_activeNetgenAlgos)
        if (algorithm) algorithm->CancelCompute();
}

class NativeNetgenCancellationScope
{
public:
    explicit NativeNetgenCancellationScope(const std::vector<std::unique_ptr<SMESH_Hypothesis>>& hypotheses)
    {
        std::lock_guard<std::mutex> guard(g_activeNetgenMutex);
        for (const auto& hypothesis : hypotheses) {
            SMESH_Algo* algorithm = dynamic_cast<SMESH_Algo*>(hypothesis.get());
            if (algorithm &&
                (dynamic_cast<NETGENPlugin_NETGEN_2D*>(algorithm) ||
                 dynamic_cast<NETGENPlugin_NETGEN_2D_ONLY*>(algorithm) ||
                 dynamic_cast<NETGENPlugin_NETGEN_2D3D*>(algorithm) ||
                 dynamic_cast<NETGENPlugin_NETGEN_3D*>(algorithm)))
                g_activeNetgenAlgos.push_back(algorithm);
        }
        // A cancel arriving during geometry setup must also reach the engine.
        if (g_netgenCancelRequested.load(std::memory_order_acquire))
            for (SMESH_Algo* algorithm : g_activeNetgenAlgos)
                algorithm->CancelCompute();
    }
    ~NativeNetgenCancellationScope()
    {
        std::lock_guard<std::mutex> guard(g_activeNetgenMutex);
        g_activeNetgenAlgos.clear();
    }
    NativeNetgenCancellationScope(const NativeNetgenCancellationScope&) = delete;
    NativeNetgenCancellationScope& operator=(const NativeNetgenCancellationScope&) = delete;
};
}
extern "C" CAE_SMESH_API void cae_smesh_reset_cancel()
{
    g_netgenCancelRequested.store(false, std::memory_order_release);
}
extern "C" CAE_SMESH_API void cae_smesh_request_cancel()
{
    g_netgenCancelRequested.store(true, std::memory_order_release);
    cancelActiveNetgenAlgorithms();
}

extern "C" CAE_SMESH_API std::uint32_t cae_smesh_abi_version()
{
    return 16;
}

extern "C" CAE_SMESH_API int cae_smesh_sweep(const CaeSMESHSweepRequest* request,
                                               CaeSMESHSweepResult* result,
                                               char* error,
                                               std::size_t errorCapacity)
{
    if (result) *result = {};
    setError(error, errorCapacity, {});
    try
    {
        // Upstream SMESH has process-wide caches and non-thread-safe SMDS
        // accessors. Serialize entry while allowing callers to use a worker.
        // SRWLOCK avoids depending on the version-sensitive MSVC std::mutex
        // constructor ABI in applications that ship an older MSVC runtime.
        ComputeLock lock;
        if (!request || !result) throw std::invalid_argument("Null SMESH bridge request/result");
        if (!request->brepPathUtf8 || !*request->brepPathUtf8)
            throw std::invalid_argument("BREP path is empty");
        if (request->layers < 1) throw std::invalid_argument("Layer count must be positive");
        if (!(request->sourceSize > 0.0)) throw std::invalid_argument("Source size must be positive");

        const std::filesystem::path brepPath = utf8Path(request->brepPathUtf8);
        TopoDS_Shape imported;
        BRep_Builder builder;
        std::ifstream brepStream(brepPath, std::ios::binary);
        if (!brepStream) throw std::runtime_error("Cannot open BREP geometry");
        BRepTools::Read(imported, brepStream, builder);
        if (imported.IsNull())
            throw std::runtime_error("Cannot read BREP geometry");

        const TopoDS_Shape selectedShape = indexedShape(imported, TopAbs_SOLID, request->volumeShapeIndex, "Sweep Solid");
        if (selectedShape.IsNull()) throw std::runtime_error("Cannot uniquely map the requested CAD Volume");
        const TopoDS_Solid solid = TopoDS::Solid(selectedShape);

        const TopoDS_Shape sourceShape = indexedShape(imported, TopAbs_FACE, request->sourceShapeIndex, "Sweep Source");
        const TopoDS_Shape targetShape = indexedShape(imported, TopAbs_FACE, request->targetShapeIndex, "Sweep Target");
        if (sourceShape.IsNull()) throw std::runtime_error("Cannot map Source Face inside the selected Volume");
        if (targetShape.IsNull()) throw std::runtime_error("Cannot map Target Face inside the selected Volume");
        TopTools_IndexedMapOfShape selectedSolidFaces;
        TopExp::MapShapes(solid, TopAbs_FACE, selectedSolidFaces);
        if (!selectedSolidFaces.Contains(sourceShape) || !selectedSolidFaces.Contains(targetShape))
            throw std::runtime_error("Sweep Source/Target does not belong to the selected Solid");
        if (sourceShape.IsSame(targetShape)) throw std::runtime_error("Source and Target map to the same face");

        // Prism_3D's upstream projection/quadrangle singletons retain the first
        // generator passed to them. Keep that generator alive for the pinned
        // DLL's process lifetime; only per-request meshes/hypotheses are freed.
        SMESH_Gen* gen = generator();
        std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
        std::unique_ptr<SMESH_Mesh> mesh(gen->CreateMesh(true));
        mesh->ShapeToMesh(solid);
        auto make = [&](auto* object) {
            hypotheses.emplace_back(object);
            return object;
        };

        auto* localLength = make(new StdMeshers_LocalLength(gen->GetANewId(), gen));
        localLength->SetLength(request->sourceSize);
        auto* regular1d = make(new StdMeshers_Regular_1D(gen->GetANewId(), gen));
        requireHypothesis(*mesh, solid, *localLength, "LocalLength");
        requireHypothesis(*mesh, solid, *regular1d, "Regular_1D");

        TopTools_IndexedMapOfShape sourceEdges;
        TopTools_IndexedMapOfShape targetEdges;
        TopTools_IndexedMapOfShape solidEdges;
        TopExp::MapShapes(sourceShape, TopAbs_EDGE, sourceEdges);
        TopExp::MapShapes(targetShape, TopAbs_EDGE, targetEdges);
        TopExp::MapShapes(solid, TopAbs_EDGE, solidEdges);
        for (int edgeIndex = 1; edgeIndex <= solidEdges.Extent(); ++edgeIndex)
        {
            const TopoDS_Shape edge = solidEdges(edgeIndex);
            if (belongsTo(edge, sourceEdges) || belongsTo(edge, targetEdges)) continue;
            auto* layers = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
            layers->SetNumberOfSegments(request->layers);
            requireHypothesis(*mesh, edge, *layers, "NumberOfSegments on sweep edge");
        }

        if (request->quadrangleSource)
        {
            auto* source2d = make(new StdMeshers_Quadrangle_2D(gen->GetANewId(), gen));
            requireHypothesis(*mesh, sourceShape, *source2d, "Quadrangle_2D on Source");
        }
        else
        {
            auto* source2d = make(new StdMeshers_MEFISTO_2D(gen->GetANewId(), gen));
            requireHypothesis(*mesh, sourceShape, *source2d, "MEFISTO_2D on Source");
        }

        // SALOME Extrusion 3D pattern for meshes with sub-meshes:
        // explicitly project the source cap onto the target cap. This avoids
        // relying on Prism_3D's implicit cap projection order.
        auto* targetProjection = make(new StdMeshers_Projection_1D2D(gen->GetANewId(), gen));
        requireHypothesis(*mesh, targetShape, *targetProjection, "Projection_1D2D on Target");
        auto* projectionSource = make(new StdMeshers_ProjectionSource2D(gen->GetANewId(), gen));
        projectionSource->SetSourceFace(sourceShape);
        requireHypothesis(*mesh, targetShape, *projectionSource, "ProjectionSource2D on Target");

        auto* prism3d = make(new StdMeshers_Prism_3D(gen->GetANewId(), gen));
        requireHypothesis(*mesh, solid, *prism3d, "Prism_3D");

        if (!gen->CheckAlgoState(*mesh, solid))
            throw std::runtime_error("SMESH algorithm/hypothesis state is invalid for this Volume");
        if (!gen->Compute(*mesh, solid))
            throw std::runtime_error("StdMeshers_Prism_3D failed to compute the selected existing Volume");

        result->nodes = mesh->NbNodes();
        result->triangles = mesh->NbTriangles();
        result->quadrangles = mesh->NbQuadrangles();
        result->tetrahedra = mesh->NbTetras();
        result->pyramids = mesh->NbPyramids();
        result->prisms = mesh->NbPrisms();
        result->hexahedra = mesh->NbHexas();
        if (result->prisms + result->hexahedra <= 0)
            throw std::runtime_error("SMESH produced no swept 3D elements");

        writeLegacyVtk(*mesh, utf8Path(request->vtkPathUtf8));
        return 0;
    }
    catch (const std::exception& ex)
    {
        setError(error, errorCapacity, ex.what());
        return 1;
    }
    catch (...)
    {
        setError(error, errorCapacity, "Unknown exception in standalone SMESH bridge");
        return 2;
    }
}

extern "C" CAE_SMESH_API int cae_smesh_body_fitted(const CaeSMESHBodyFittedRequest* request,
                                                     CaeSMESHBodyFittedResult* result,
                                                     char* error,
                                                     std::size_t errorCapacity)
{
    if (result) *result = {};
    setError(error, errorCapacity, {});
    try
    {
        ComputeLock lock;
        if (!request || !result) throw std::invalid_argument("Null Body Fitting request/result");
        if (!request->brepPathUtf8 || !*request->brepPathUtf8)
            throw std::invalid_argument("BREP path is empty");
        if (!std::isfinite(request->cellSize) || request->cellSize <= 0.0)
            throw std::invalid_argument("Body Fitting cell size must be positive");
        if (!std::isfinite(request->sizeThreshold) || request->sizeThreshold <= 1.0)
            throw std::invalid_argument("Body Fitting size threshold must be > 1");

        TopoDS_Shape shape;
        BRep_Builder builder;
        std::ifstream input(utf8Path(request->brepPathUtf8), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open BREP geometry");
        BRepTools::Read(shape, input, builder);
        if (shape.IsNull()) throw std::runtime_error("Cannot read BREP geometry");
        if (!BRepCheck_Analyzer(shape).IsValid())
            throw std::runtime_error("BREP geometry is invalid; Body Fitting was not started");

        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() == 0) throw std::runtime_error("BREP contains no solid");

        SMESH_Gen* gen = generator();
        std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
        std::unique_ptr<SMESH_Mesh> mesh(gen->CreateMesh(true));
        mesh->ShapeToMesh(shape);
        auto make = [&](auto* object) {
            hypotheses.emplace_back(object);
            return object;
        };

        auto* cartesian = make(new StdMeshers_Cartesian_3D(gen->GetANewId(), gen));
        auto* params = make(new StdMeshers_CartesianParameters3D(gen->GetANewId(), gen));

        const std::string spacingText = std::to_string(request->cellSize);
        for (int axis = 0; axis < 3; ++axis)
        {
            std::vector<std::string> spacing{spacingText};
            std::vector<double> internalPoints;
            params->SetGridSpacing(spacing, internalPoints, axis);
        }
        params->SetSizeThreshold(request->sizeThreshold);
        params->SetToAddEdges(request->addGeometryEdges != 0);
        params->SetToConsiderInternalFaces(request->considerInternalFaces != 0);
        params->SetToUseThresholdForInternalFaces(false);
        params->SetToCreateFaces(request->createFaces != 0);

        if (request->optimalAxes)
        {
            double axes[9] = {1., 0., 0., 0., 1., 0., 0., 0., 1.};
            StdMeshers_CartesianParameters3D::ComputeOptimalAxesDirs(shape, true, axes);
            params->SetAxisDirs(axes);
        }

        requireHypothesis(*mesh, shape, *cartesian, "Cartesian_3D Body Fitting");
        requireHypothesis(*mesh, shape, *params, "CartesianParameters3D");
        if (!gen->CheckAlgoState(*mesh, shape))
            throw std::runtime_error("SMESH Body Fitting algorithm/hypothesis state is invalid");
        if (!gen->Compute(*mesh, shape))
            throw std::runtime_error("SMESH Body Fitting failed" + computeError(*mesh, shape));

        result->nodes = mesh->NbNodes();
        result->solids = solids.Extent();
        result->volumes = mesh->NbVolumes();
        result->hexahedra = mesh->NbHexas();
        result->polyhedra = mesh->NbPolyhedrons();
        result->tetrahedra = mesh->NbTetras();
        result->pyramids = mesh->NbPyramids();
        result->prisms = mesh->NbPrisms();
        const std::int64_t known = result->hexahedra + result->polyhedra + result->tetrahedra +
                                   result->pyramids + result->prisms;
        result->otherVolumes = std::max<std::int64_t>(0, result->volumes - known);
        if (result->volumes <= 0)
            throw std::runtime_error("SMESH Body Fitting produced no 3D cells");

        // Preserve original solid ordering as a lightweight domain id in VTK.
        // No CAD Partition is performed by this one-click path.
        std::map<int, int> regions;
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            const int shapeId = mesh->GetMeshDS()->ShapeToIndex(solids(i));
            if (shapeId > 0) regions[shapeId] = i;
        }
        writeBodyFittedVtk(*mesh, utf8Path(request->vtkPathUtf8), regions);
        return 0;
    }
    catch (const std::exception& ex)
    {
        setError(error, errorCapacity, ex.what());
        return 1;
    }
    catch (...)
    {
        setError(error, errorCapacity, "Unknown exception in SMESH Body Fitting");
        return 2;
    }
}


extern "C" CAE_SMESH_API int cae_smesh_viscous_layers(const CaeSMESHViscousRequest* request,
                                                        CaeSMESHViscousResult* result,
                                                        char* error,
                                                        std::size_t errorCapacity)
{
    if (result) *result = {};
    setError(error, errorCapacity, {});
    try
    {
        ComputeLock lock;
        if (!request || !result) throw std::invalid_argument("Null ViscousLayers request/result");
        if (!request->brepPathUtf8 || !*request->brepPathUtf8)
            throw std::invalid_argument("BREP path is empty");
        if (!std::isfinite(request->maxSize) || request->maxSize <= 0.0 ||
            !std::isfinite(request->minSize) || request->minSize < 0.0 ||
            request->minSize > request->maxSize)
            throw std::invalid_argument("Invalid NETGEN min/max mesh size");
        if (!std::isfinite(request->totalThickness) || request->totalThickness <= 0.0 ||
            !std::isfinite(request->firstHeight) || request->firstHeight <= 0.0 ||
            request->layers < 1 || !std::isfinite(request->stretchFactor) || request->stretchFactor < 1.0)
            throw std::invalid_argument("Invalid ViscousLayers thickness/layers/stretch factor");
        if (request->extrusionMethod < 0 || request->extrusionMethod > 2)
            throw std::invalid_argument("Invalid ViscousLayers extrusion method");
        if (!request->automaticWall && (request->wallCount < 1 || !request->walls))
            throw std::invalid_argument("Manual ViscousLayers mode has no Wall faces");

        TopoDS_Shape shape;
        BRep_Builder builder;
        std::ifstream input(utf8Path(request->brepPathUtf8), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open BREP geometry");
        BRepTools::Read(shape, input, builder);
        if (shape.IsNull()) throw std::runtime_error("Cannot read BREP geometry");

        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() == 0) throw std::runtime_error("BREP contains no solid");

        // Topological exterior = a face owned by exactly one solid. This avoids
        // growing ViscousLayers on true shared interfaces in multi-domain CAD.
        TopTools_IndexedDataMapOfShapeListOfShape faceOwners;
        TopExp::MapShapesAndAncestors(shape, TopAbs_FACE, TopAbs_SOLID, faceOwners);

        TopTools_IndexedMapOfShape selectedWalls;
        if (!request->automaticWall)
        {
            for (int i = 0; i < request->wallCount; ++i)
            {
                const CaeSMESHViscousFace& wall = request->walls[i];
                const TopoDS_Shape face = indexedShape(shape, TopAbs_FACE, wall.shapeIndex, "Viscous Wall");
                if (face.IsNull())
                    throw std::runtime_error("Cannot map a selected Wall face to the BREP");
                const int ownerIndex = faceOwners.FindIndex(face);
                if (ownerIndex <= 0 || faceOwners.FindFromIndex(ownerIndex).Extent() != 1)
                    throw std::runtime_error("Selected Wall is not a topological external face");
                if (selectedWalls.Contains(face))
                    throw std::runtime_error("Two selected Wall indices map to the same BREP face");
                selectedWalls.Add(face);
            }
        }

        SMESH_Gen* gen = generator();
        std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
        std::unique_ptr<SMESH_Mesh> mesh(gen->CreateMesh(true));
        mesh->ShapeToMesh(shape);
        auto make = [&](auto* object) {
            hypotheses.emplace_back(object);
            return object;
        };

        // This is the native SALOME combination used by the Python/GUI API:
        // NETGEN_2D3D is the 3D algorithm and ViscousLayers is an auxiliary
        // hypothesis consumed directly by NETGENPlugin_Mesher.
        auto* netgen = make(new NETGENPlugin_NETGEN_2D3D(gen->GetANewId(), gen));
        auto* netgenHyp = make(new NETGENPlugin_Hypothesis(gen->GetANewId(), gen));
        netgenHyp->SetMaxSize(request->maxSize);
        netgenHyp->SetMinSize(request->minSize);
        netgenHyp->SetOptimize(true);
        netgenHyp->SetSurfaceCurvature(true);
        netgenHyp->SetQuadAllowed(false);

        std::map<int, int> regions;
        std::int64_t wallFaceCount = 0;
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            const TopoDS_Shape solid = solids(i);
            requireHypothesis(*mesh, solid, *netgen, "NETGEN 1D-2D-3D");
            requireHypothesis(*mesh, solid, *netgenHyp, "NETGEN parameters");

            const int solidShapeId = mesh->GetMeshDS()->ShapeToIndex(solid);
            if (solidShapeId > 0) regions[solidShapeId] = i;

            std::vector<int> wallIds;
            TopTools_IndexedMapOfShape solidFaces;
            TopExp::MapShapes(solid, TopAbs_FACE, solidFaces);
            for (int f = 1; f <= solidFaces.Extent(); ++f)
            {
                const TopoDS_Shape face = solidFaces(f);
                const int ownerIndex = faceOwners.FindIndex(face);
                const bool isExternal = ownerIndex > 0 && faceOwners.FindFromIndex(ownerIndex).Extent() == 1;
                if (!isExternal) continue;
                if (!request->automaticWall && !selectedWalls.Contains(face)) continue;

                const int faceShapeId = mesh->GetMeshDS()->ShapeToIndex(face);
                if (faceShapeId > 0) wallIds.push_back(faceShapeId);
            }

            if (!wallIds.empty())
            {
                auto* viscous = make(new StdMeshers_ViscousLayers(gen->GetANewId(), gen));
                viscous->SetTotalThickness(request->totalThickness);
                viscous->SetNumberLayers(request->layers);
                viscous->SetStretchFactor(request->stretchFactor);
                viscous->SetBndShapes(wallIds, /*toIgnore=*/false);
                viscous->SetMethod(static_cast<StdMeshers_ViscousLayers::ExtrusionMethod>(request->extrusionMethod));
                requireHypothesis(*mesh, solid, *viscous, "ViscousLayers");
                wallFaceCount += static_cast<std::int64_t>(wallIds.size());
            }
        }

        if (wallFaceCount <= 0)
            throw std::runtime_error("No topological external Wall face is available for ViscousLayers");

        // All algorithms/hypotheses are configured before the first Compute,
        // matching the stable SMESH SubMesh lifecycle used by the Sweep path.
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            const TopoDS_Shape solid = solids(i);
            if (!gen->CheckAlgoState(*mesh, solid))
                throw std::runtime_error("SMESH NETGEN/ViscousLayers hypothesis state is invalid on solid " +
                                         std::to_string(i));
            if (!gen->Compute(*mesh, solid, SMESH_Gen::SHAPE_ONLY))
                throw std::runtime_error("SMESH NETGEN + ViscousLayers failed on solid " +
                                         std::to_string(i) + computeError(*mesh, solid));
        }

        result->nodes = mesh->NbNodes();
        result->solids = solids.Extent();
        result->wallFaces = wallFaceCount;
        result->volumes = mesh->NbVolumes();
        result->tetrahedra = mesh->NbTetras();
        result->prisms = mesh->NbPrisms();
        result->pyramids = mesh->NbPyramids();
        result->hexahedra = mesh->NbHexas();
        // The standalone SMESH revision used by TMSR does not expose the newer
        // StdMeshers_ViscousLayers::Get1stLayerThickness() helper.  The UI already
        // computes totalThickness from firstHeight/layers/stretchFactor, so report
        // the requested first-layer height directly.  This avoids depending on a
        // newer SALOME SMESH API while keeping the ViscousLayers parameters identical.
        result->actualFirstHeight = request->firstHeight;
        const std::int64_t known = result->tetrahedra + result->prisms +
                                   result->pyramids + result->hexahedra;
        result->otherVolumes = std::max<std::int64_t>(0, result->volumes - known);

        if (result->volumes <= 0)
            throw std::runtime_error("SMESH NETGEN + ViscousLayers produced no 3D cells");
        if (result->prisms <= 0)
            throw std::runtime_error("SMESH ViscousLayers produced no Prism cells");

        writeLegacyVtk(*mesh, utf8Path(request->vtkPathUtf8), regions,
                       /*markPrismsAsBoundaryLayer=*/true);
        return 0;
    }
    catch (const std::exception& ex)
    {
        setError(error, errorCapacity, ex.what());
        return 1;
    }
    catch (...)
    {
        setError(error, errorCapacity, "Unknown exception in SMESH ViscousLayers");
        return 2;
    }
}

// A live parent Mesh shared by successive scoped NETGEN Tet operations.
// All access is serialized by ComputeLock. Unlike merging exported VTK files,
// this preserves native shared Face/Edge submeshes and their node identities.
struct NativeTetSession
{
    std::string key;
    std::string geometryKey; // CAD content, parent scope and OCC mode, excluding sizing.
    TopoDS_Shape inputShape;
    TopoDS_Shape mainShape;
    TopTools_IndexedMapOfShape solids;
    // Mesh must be destroyed before hypotheses (same lifetime as runNetgen v11).
    std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
    std::unique_ptr<SMESH_Mesh> mesh;
    std::map<int, std::string> solidConfiguration;
    std::map<int, TopoDS_Shape> surfaceScopes;
    std::map<int, std::string> faceConfiguration;
    // Root surface algorithm is installed once per parent Mesh session.
    // New batches must not replace an already discretized interface.
    bool surfaceAlgorithmInstalled = false;
    // Installed only for a homogeneous full-parent NETGEN job. Heterogeneous,
    // partial and boundary-layer tasks retain explicit per-Solid SubMeshes.
    bool volumeAlgorithmInstalled = false;
    std::map<int,std::vector<TopoDS_Shape>> faceImages;
    std::map<int,std::vector<TopoDS_Shape>> edgeImages;
    std::map<int, std::string> edgeConfiguration;
    std::set<int> completed;
};

static std::unique_ptr<NativeTetSession> g_nativeTetSession;

struct NativeCadPreparation
{
    std::string key;
    TopoDS_Shape inputShape;
    TopoDS_Shape mainShape;
    TopTools_IndexedMapOfShape solids;
    std::map<int,std::vector<TopoDS_Shape>> faceImages;
    std::map<int,std::vector<TopoDS_Shape>> edgeImages;
};
// Root remeshing changes hypotheses, not geometry. Keep one verified native
// OCC preparation independently of the disposable SMESH_Mesh session.
static std::unique_ptr<NativeCadPreparation> g_nativeCadPreparation;

// A session is scoped to a Geometry-owned export and its file generation.
// Use O(1) file status to invalidate an in-process Mesh/Partition cache instead
// of scanning/hashing entire BREP files on each incremental NETGEN request.
static std::string brepFileGeneration(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || !size) throw std::runtime_error("Cannot stat BREP geometry");
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) throw std::runtime_error("Cannot stat BREP modification time");
    return path.u8string() + ":" + std::to_string(size) + ":" +
        std::to_string(modified.time_since_epoch().count());
}

static int runNetgen(const CaeSMESHNetgenRequest* request,
                                                CaeSMESHNetgenResult* result,
                                                char* error,
                                                std::size_t errorCapacity,
                                                CaeSMESHProgress progress,
                                                void* progressContext,
                                                const CaeSMESHFaceCurvatureControl* faceCurvatures = nullptr,
                                                int faceCurvatureCount = 0, bool automaticMinSize = false,
                                                const std::int32_t* selectedSolidIndices = nullptr,
                                                int selectedSolidCount = 0,
                                                const CaeSMESHScopedNetgenControl* scopedNetgen = nullptr,
                                                int scopedNetgenCount = 0,
                                                const std::int32_t* mainSolidIndices = nullptr,
                                                int mainSolidCount = 0,
                                                const char* sessionKeyUtf8 = nullptr,
                                                bool resetSession = false,
                                                bool autoPartition = false,
                                                const CaeSMESHFaceMinimumControl* faceMinimums = nullptr,
                                                int faceMinimumCount = 0,
                                                bool chordalErrorEnabled = false,
                                                double chordalError = 0.0,
                                                const std::int32_t* boundarySolidIndices = nullptr,
                                                int boundarySolidCount = 0,
                                                const std::int32_t* surfaceSolidIndices = nullptr,
                                                int surfaceSolidCount = 0,
                                                bool surfaceOnly = false,
                                                std::int64_t surfaceElementLimit = 0,
                                                std::int64_t* surfaceElements = nullptr,
                                                const CaeSMESHScopedSurfacePrecision* scopedPrecision = nullptr,
                                                int scopedPrecisionCount = 0,
                                                int elementOrder = 1)
{
    if (result) *result = {};
    if (surfaceElements) *surfaceElements = 0;
    setError(error, errorCapacity, {});
    // Keep the last validated native session while a policy-changing rebuild
    // is under way. A failed attempt must not destroy the old successful state.
    std::unique_ptr<NativeTetSession> previousValidSession;
    try
    {
        ComputeLock lock;
        const auto bridgeStarted = std::chrono::steady_clock::now();
        const auto notify = [&](const std::string& message) {
            if (progress) { try { progress(progressContext, message.c_str()); } catch (...) {} }
        };
        if (!request || !result) throw std::invalid_argument("Null NETGEN request/result");
        if (surfaceElementLimit < 0) throw std::invalid_argument("Invalid surface element limit");
        if (boundarySolidCount < 0 || (boundarySolidCount > 0 && !boundarySolidIndices))
            throw std::invalid_argument("Invalid boundary-layer Solid scope");
        if (chordalErrorEnabled && (!std::isfinite(chordalError) || chordalError <= 0.0))
            throw std::invalid_argument("Invalid NETGEN surface chordal error");
        std::map<int, CaeSMESHScopedSurfacePrecision> perSolidPrecision;
        if (scopedPrecisionCount < 0 || (scopedPrecisionCount > 0 && !scopedPrecision))
            throw std::invalid_argument("Invalid scoped surface precision array");
        for (int n = 0; n < scopedPrecisionCount; ++n) {
            const auto& c = scopedPrecision[n];
            if (c.solidShapeIndex <= 0 || c.chordalErrorEnabled < 0 || c.chordalErrorEnabled > 1 ||
                (c.chordalErrorEnabled && (!std::isfinite(c.chordalError) || c.chordalError <= 0.0)) ||
                !perSolidPrecision.emplace(c.solidShapeIndex, c).second)
                throw std::invalid_argument("Invalid or duplicate Solid chordal precision");
        }
        const auto precisionForSolid = [&](int index) {
            const auto found = perSolidPrecision.find(index);
            return found == perSolidPrecision.end()
                ? CaeSMESHScopedSurfacePrecision{index, chordalErrorEnabled ? 1 : 0, chordalError}
                : found->second;
        };
        if (!request->brepPathUtf8 || !*request->brepPathUtf8)
            throw std::invalid_argument("BREP path is empty");
        if (!std::isfinite(request->maxSize) || request->maxSize <= 0.0 ||
            !std::isfinite(request->minSize) || request->minSize < 0.0 ||
            request->minSize > request->maxSize)
            throw std::invalid_argument("Invalid NETGEN min/max mesh size");
        if (!std::isfinite(request->tolerance) || request->tolerance <= 0.0)
            throw std::invalid_argument("Invalid geometry mapping tolerance");
        if (request->fineness < 0 || request->fineness > 5 ||
            !std::isfinite(request->growthRate) || request->growthRate <= 0.0 ||
            !std::isfinite(request->nbSegPerEdge) || request->nbSegPerEdge <= 0.0 ||
            !std::isfinite(request->nbSegPerRadius) || request->nbSegPerRadius <= 0.0)
            throw std::invalid_argument("Invalid NETGEN fineness parameters");
        if (request->localSizeCount < 0 || (request->localSizeCount > 0 && !request->localSizes))
            throw std::invalid_argument("Invalid local size controls");
        if (request->edgeSegmentCount < 0 || (request->edgeSegmentCount > 0 && !request->edgeSegments))
            throw std::invalid_argument("Invalid edge segment controls");
        if (faceCurvatureCount < 0 || (faceCurvatureCount > 0 && !faceCurvatures))
            throw std::invalid_argument("Invalid face curvature controls");
        if (faceMinimumCount < 0 || (faceMinimumCount > 0 && !faceMinimums))
            throw std::invalid_argument("Invalid face minimum controls");
        if (selectedSolidCount < 0 || (selectedSolidCount > 0 && !selectedSolidIndices))
            throw std::invalid_argument("Invalid selected NETGEN solid indices");
        if (scopedNetgenCount < 0 || (scopedNetgenCount > 0 && !scopedNetgen))
            throw std::invalid_argument("Invalid scoped NETGEN controls");
        if (mainSolidCount < 0 || (mainSolidCount > 0 && !mainSolidIndices))
            throw std::invalid_argument("Invalid Mesh Main Shape selection");
        if (request->boundaryLayerEnabled)
        {
            if (!std::isfinite(request->totalThickness) || request->totalThickness <= 0.0 ||
                !std::isfinite(request->firstHeight) || request->firstHeight <= 0.0 ||
                request->layers < 1 || !std::isfinite(request->stretchFactor) || request->stretchFactor < 1.0)
                throw std::invalid_argument("Invalid ViscousLayers thickness/layers/stretch factor");
            if (request->extrusionMethod < 0 || request->extrusionMethod > 2)
                throw std::invalid_argument("Invalid ViscousLayers extrusion method");
            if (!request->automaticWall && (request->wallCount < 1 || !request->walls))
                throw std::invalid_argument("Manual ViscousLayers mode has no Wall faces");
        }

        // Never reuse a native Mesh across projects, geometry changes, or a
        // different parent Main Shape. The session ID is the Mesh root UUID.
        const std::string sourceGeneration = request->geometryGenerationUtf8 && *request->geometryGenerationUtf8
            ? request->geometryGenerationUtf8 : brepFileGeneration(utf8Path(request->brepPathUtf8));
        // Explicit Geometry finalization is independent of permission to run a
        // NEW Partition here. Include both files in the session identity so a
        // finalized/re-finalized CAD cannot reuse an older unprepared Mesh.
        auto finalizedPath = utf8Path(request->brepPathUtf8);
        finalizedPath += ".finalized.brep";
        auto finalizedMapPath = finalizedPath;
        finalizedMapPath += ".topology-map.json";
        const std::string finalizedGeneration =
            (std::filesystem::exists(finalizedPath) && std::filesystem::exists(finalizedMapPath))
            ? brepFileGeneration(finalizedPath) + ":" + brepFileGeneration(finalizedMapPath)
            : "absent";
        std::string persistentKey;
        if (sessionKeyUtf8 && *sessionKeyUtf8 && !request->boundaryLayerEnabled)
            persistentKey = std::string(sessionKeyUtf8) + ":" +
                sourceGeneration +
                (autoPartition ? ":partition" : ":native");
        std::vector<int> sortedMain;
        if (mainSolidCount > 0 && mainSolidIndices)
            sortedMain.assign(mainSolidIndices, mainSolidIndices + mainSolidCount);
        std::sort(sortedMain.begin(), sortedMain.end());
        std::ostringstream preparationKey;
        bool usesSubshapeControls = request->edgeSegmentCount > 0 || faceCurvatureCount > 0 || faceMinimumCount > 0;
        for (int i = 0; i < request->localSizeCount; ++i)
            usesSubshapeControls |= request->localSizes[i].dimension == 2;
        preparationKey << sourceGeneration << ':' << finalizedGeneration << ':' << autoPartition << ':'
            << usesSubshapeControls << ':' << std::setprecision(17)
            << request->tolerance;
        for (int index : sortedMain) preparationKey << ':' << index;
        const bool reusePreparation = g_nativeCadPreparation &&
            g_nativeCadPreparation->key == preparationKey.str();
        for (int index : sortedMain)
            if (!persistentKey.empty()) persistentKey += ":" + std::to_string(index);
        // Geometry preparation also depends on topology-mapping tolerance
        // and whether explicit face/edge controls participate in Partition.
        // A change here must never reuse an old SMESH_Mesh with different OCC
        // TShape identities, even if the BREP file itself is unchanged.
        if (!persistentKey.empty()) persistentKey += ":prep:" + preparationKey.str();
        const std::string geometryKey = persistentKey;
        // A shared 1D-2D parent hypothesis is immutable once meshing starts.
        // Include the effective surface baseline in session identity, otherwise
        // another incremental batch may quietly reuse edges/faces computed with
        // old root sizing after a parent edit or first-node configuration change.
        if (!persistentKey.empty()) {
            std::ostringstream baseline;
            baseline << std::setprecision(17) << ":surface:"
                << request->maxSize << ':' << request->minSize << ':'
                << request->fineness << ':' << request->surfaceCurvature << ':'
                << request->optimize << ':' << request->growthRate << ':'
                << request->nbSegPerEdge << ':' << request->nbSegPerRadius;
            persistentKey += baseline.str();
        }
        // The session must also track *all* configured SubMesh hypotheses,
        // including siblings not being computed in this preview.  Otherwise
        // changing a previously configured local size, priority or curvature
        // rule may silently reuse a meshed shared face from the old policy.
        // Include exact double representations; this is a session identity,
        // not a fuzzy "same size" comparison.
        if (!persistentKey.empty()) {
            std::ostringstream policy;
            policy << std::setprecision(17) << ":scoped:" << scopedNetgenCount;
            // Preserve caller order: SMESH priority is order-sensitive.
            for (int i = 0; i < scopedNetgenCount; ++i) {
                const auto& c = scopedNetgen[i];
                policy << ':' << c.solidShapeIndex << ':' << c.fineness
                    << ':' << c.surfaceCurvature << ':' << c.optimize
                    << ':' << c.automaticMinSize << ':' << c.manualMinSize
                    << ':' << c.growthRate << ':' << c.nbSegPerEdge
                    << ':' << c.nbSegPerRadius;
            }
            policy << ":sizes:" << request->localSizeCount;
            for (int i = 0; i < request->localSizeCount; ++i) {
                const auto& c = request->localSizes[i];
                policy << ':' << c.dimension << ':' << c.shapeIndex
                    << ':' << c.targetSize;
            }
            policy << ":edges:" << request->edgeSegmentCount;
            for (int i = 0; i < request->edgeSegmentCount; ++i) {
                const auto& c = request->edgeSegments[i];
                policy << ':' << c.shapeIndex << ':' << c.divisions
                    << ':' << c.bias;
            }
            policy << ":facecurvature:" << faceCurvatureCount;
            for (int i = 0; i < faceCurvatureCount; ++i) {
                const auto& c = faceCurvatures[i];
                policy << ':' << c.shapeIndex << ':' << c.elementsPer2Pi;
            }
            // The policy string is not an external identifier. Keeping it
            policy << ":faceminimum:" << faceMinimumCount;
            for (int i=0;i<faceMinimumCount;++i)
                policy << ':' << faceMinimums[i].shapeIndex << ':' << faceMinimums[i].mode << ':' << faceMinimums[i].manualSize;
            policy << ":chordal:" << chordalErrorEnabled << ':' << (chordalErrorEnabled ? chordalError : 0.0);
            for (const auto& entry : perSolidPrecision) {
                const auto& c = entry.second;
                policy << ":solidChordal:" << c.solidShapeIndex << ':' << c.chordalErrorEnabled
                    << ':' << (c.chordalErrorEnabled ? c.chordalError : 0.0);
            }
            if(surfaceSolidCount<0 || (surfaceSolidCount>0 && !surfaceSolidIndices))
                throw std::invalid_argument("Invalid Solid surface sizing scope");
            policy << ":solid-surface:" << surfaceSolidCount;
            for(int i=0;i<surfaceSolidCount;++i) policy << ':' << surfaceSolidIndices[i];
            // lossless avoids hash collisions causing unsafe mesh reuse.
            persistentKey += policy.str();
        }
        // In a persistent Mesh, a change of sibling hypotheses or their order
        // can change already meshed interfaces. Never mutate those faces in
        // place. Rebuild a fresh native parent and REPLAY only domains that
        // were completed in this session (plus the newly requested domains),
        // not all 163 CAD solids. This is a conservative safe fallback until
        // native dependency-aware ClearSubMesh is proven by integration tests.
        // If geometry/topology changed, the old domain indices are not trusted:
        // do NOT replay them across a changed CAD Main Shape.
        std::set<int> replayCompleted;
        const bool scopedPreview = selectedSolidCount > 0 && !resetSession;
        if (sessionKeyUtf8 && *sessionKeyUtf8 && g_nativeTetSession &&
            !persistentKey.empty() && g_nativeTetSession->key != persistentKey &&
            !resetSession && g_nativeTetSession->key.rfind(
                std::string(sessionKeyUtf8) + ":", 0) == 0) {
            if (!scopedPreview) {
                // A full parent request should not require the UI to issue a
                // second special "reset" command when the policy changed.
                // The old session stays available until this fresh run succeeds.
                notify(u8"[SMESH会话重建] 父 Mesh 参数发生变化；自动新建原生 Mesh，并完整重算主几何。");
            } else if (g_nativeTetSession->geometryKey == geometryKey) {
                replayCompleted = g_nativeTetSession->completed;
                notify(std::string(u8"[SMESH依赖重算] 分域参数/优先级已改变；旧共享面不得直接复用。将新建原生 Mesh 并重新计算已完成的 ")+
                    std::to_string(replayCompleted.size())+
                    u8" 个域及本次选中域；未计算的其余域不会划分。此为安全重算，不是原位局部清除。");
            } else {
                notify(u8"[SMESH依赖重算] 几何或主几何范围已变化；不复用旧域索引或网格，只计算本次选中域。正式结果保持不变。");
            }
            previousValidSession = std::move(g_nativeTetSession);
        }
        if (resetSession && !persistentKey.empty() && g_nativeTetSession)
            previousValidSession = std::move(g_nativeTetSession);
        const bool reuse = !persistentKey.empty() && g_nativeTetSession &&
            g_nativeTetSession->key == persistentKey;
        TopoDS_Shape shape;
        BRep_Builder builder;
        if (reusePreparation) shape = g_nativeCadPreparation->inputShape;
        else {
            std::ifstream input(utf8Path(request->brepPathUtf8), std::ios::binary);
            if (!input) throw std::runtime_error("Cannot open BREP geometry");
            BRepTools::Read(shape, input, builder);
        }
        if (shape.IsNull()) throw std::runtime_error("Cannot read BREP geometry");
        // Reuse the original OCC TShapes as well as the SMESH_Mesh: a new
        // BREP import has different topology object identities.
        if (reuse) shape = g_nativeTetSession->inputShape;

        TopTools_IndexedMapOfShape solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() == 0) throw std::runtime_error("BREP contains no solid");
        // Preserve imported indices when one-to-one CAD Partition replaces
        // TopoDS_Solid identities with the canonical shared-face versions.
        const TopTools_IndexedMapOfShape originalSolids = solids;
        std::set<int> boundarySolids;
        for(int i=0;i<boundarySolidCount;++i) {
            const int id=boundarySolidIndices[i];
            if(id<1 || id>solids.Extent() || !boundarySolids.insert(id).second)
                throw std::invalid_argument("Boundary-layer Solid index invalid or duplicated");
        }
        TopTools_IndexedMapOfShape originalFaces;TopExp::MapShapes(shape,TopAbs_FACE,originalFaces);
        TopTools_IndexedMapOfShape originalEdges;TopExp::MapShapes(shape,TopAbs_EDGE,originalEdges);
        std::map<int,std::vector<TopoDS_Shape>> faceImages;
        std::map<int,std::vector<TopoDS_Shape>> edgeImages;
        for(int i=1;i<=originalFaces.Extent();++i)faceImages[i]={originalFaces(i)};
        for(int i=1;i<=originalEdges.Extent();++i)edgeImages[i]={originalEdges(i)};
        std::set<int> selectedSolids;
        for (int i = 0; i < selectedSolidCount; ++i)
        {
            const int index = selectedSolidIndices[i];
            if (index < 1 || index > solids.Extent() || !selectedSolids.insert(index).second)
                throw std::invalid_argument("Selected NETGEN SOLID index invalid or duplicated");
        }
        // A deleted/reassigned SubMesh is not a completed region of the
        // *new* configuration. Do not accidentally recreate its old volumes
        // using inherited root sizing. Every replayed domain must still have
        // a configured scoped hypothesis in the new request.
        if (!replayCompleted.empty()) {
            std::set<int> configuredNow;
            for (int n = 0; n < scopedNetgenCount; ++n)
                configuredNow.insert(scopedNetgen[n].solidShapeIndex);
            for (auto it = replayCompleted.begin(); it != replayCompleted.end();) {
                if (!configuredNow.count(*it)) it = replayCompleted.erase(it);
                else ++it;
            }
            notify(std::string(u8"[SMESH依赖重算] 仍属于已配置 SubMesh 的历史完成域数量=") +
                std::to_string(replayCompleted.size()));
        }
        // Replaying in the SAME geometry re-establishes all previously
        // computed shared interfaces under the new complete hypothesis set.
        // A shape-index mismatch is a hard error, not a reason to guess.
        for (int index : replayCompleted) {
            if (index < 1 || index > solids.Extent())
                throw std::runtime_error("Native Mesh replay has invalid CAD solid index");
            selectedSolids.insert(index);
        }
        std::set<int> mainSolids;
        for (int i = 0; i < mainSolidCount; ++i) {
            const int index = mainSolidIndices[i];
            if (index < 1 || index > solids.Extent() || !mainSolids.insert(index).second)
                throw std::invalid_argument("Invalid/duplicate Main Shape CAD solid index");
        }
        for (int index : selectedSolids)
            if (!mainSolids.empty() && !mainSolids.count(index))
                throw std::invalid_argument("SubMesh solid is not contained in parent Mesh Main Shape");
        // The native Mesh must be built on topology shared by the parent
        // domains. Compound(OriginalSolids) does NOT glue coincident faces.
        // A Partition may be used here only when it leaves a one-to-one
        // correspondence between CAD Solid indices and material domains.
        TopoDS_Shape mainShape = shape;
        for(int id:boundarySolids)if(!mainSolids.empty() && !mainSolids.count(id))
            throw std::invalid_argument("Boundary-layer target Solid is outside Mesh Main Shape");
        if (!mainSolids.empty() && int(mainSolids.size()) < solids.Extent()) {
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            for (int index : mainSolids) builder.Add(compound, solids(index));
            mainShape = compound;
        }
        // Source subshape controls can belong to solids outside this parent
        // Main Shape. Preserve the previous behaviour of ignoring those rules,
        // but never silently drop a control *inside* the parent after Partition.
        TopTools_IndexedMapOfShape sourceMainFaces;
        TopTools_IndexedMapOfShape sourceMainEdges;
        TopExp::MapShapes(mainShape, TopAbs_FACE, sourceMainFaces);
        TopExp::MapShapes(mainShape, TopAbs_EDGE, sourceMainEdges);
        if (reuse) {
            mainShape = g_nativeTetSession->mainShape;
            solids = g_nativeTetSession->solids;
            faceImages=g_nativeTetSession->faceImages;
            edgeImages=g_nativeTetSession->edgeImages;
        } else if (reusePreparation) {
            mainShape = g_nativeCadPreparation->mainShape;
            solids = g_nativeCadPreparation->solids;
            faceImages=g_nativeCadPreparation->faceImages;
            edgeImages=g_nativeCadPreparation->edgeImages;
            notify(u8"[SMESH几何缓存] 复用已核对的主几何与共享拓扑；尺寸变化不重复执行 OCC Partition。");
        } else {
            std::vector<int> domainIndices;
            for (int i = 1; i <= solids.Extent(); ++i)
                if (mainSolids.empty() || mainSolids.count(i)) domainIndices.push_back(i);
            if (!domainIndices.empty()) {
                // Identify actual geometric contact, NOT mere bounding-box
                // proximity, and distinguish touching from overlapping solids.
                struct DomainBounds { Bnd_Box box; double volume = 0; };
                std::map<int,DomainBounds> bounds;
                if (autoPartition) for (int index : domainIndices) {
                    auto& data = bounds[index];
                    BRepBndLib::Add(solids(index), data.box, false);
                    GProp_GProps volume;
                    BRepGProp::VolumeProperties(solids(index), volume);
                    data.volume = std::abs(volume.Mass());
                    if (!std::isfinite(data.volume) || data.volume <= 0)
                        throw std::runtime_error("Invalid original CAD solid volume during conformal audit");
                }
                // With explicit geometry preparation enabled, let OCC perform
                // one batch Boolean with its native broad phase. Repeating
                // Common(Solid,Solid) and Common(Face,Face) for every possible
                // pair can take minutes before NETGEN has even started.
                // No pairwise Common(Solid,Solid)/Common(Face,Face) probing:
                // OCC is the only authority for creating shared topology.
                // If Partition is disabled, we do NOT claim that geometrically
                // coincident but topologically independent faces are conformal.
                // Fuzzy tolerance has the same length unit as the CAD. Honour
                // the explicit request; an absolute 1e-6 cap changes its meaning
                // when the geometry/parameters are converted between mm and m.
                const double fuzzy = request->tolerance;
                {
                    // A geometry edit that changes solid/face indices cannot
                    // silently carry over face/edge sizing and BC assignments.
                    // Geometry can prepare/finalize the SAME source CAD once.
                    // The persistent pair remains a cache: old shape indices in
                    // the UI stay in the source BREP domain. We reconstruct
                    // their OCC history as prepared shapes, NOT as bare IDs.
                    // Boundary-layer Walls use the same verified OCC history.
                    // Subset Main Shapes reuse mapped Solids from the validated full-model snapshot.
                    bool usedFinalizedGeometry = false;
                    std::string finalizedFailure;
                    // The finalized snapshot covers the complete source BREP.
                    // A parent Main Shape can contain only a subset of those CAD
                    // solids: validate the WHOLE snapshot, then form its subset
                    // from the verified mapped Solid objects. A fresh Partition
                    // of the subset would lose the original Wall face history.
                    const bool subsetMainShape =
                        domainIndices.size() != static_cast<std::size_t>(originalSolids.Extent());
                    {
                        const auto sourcePath = utf8Path(request->brepPathUtf8);
                        auto preparedPath = sourcePath;
                        preparedPath += ".finalized.brep";
                        auto mappingPath = preparedPath;
                        mappingPath += ".topology-map.json";
                        if (std::filesystem::exists(mappingPath) && std::filesystem::exists(preparedPath)) {
                            try {
                                const auto map = CaePreparedPartition::load(mappingPath);
                                if (std::filesystem::last_write_time(preparedPath) < std::filesystem::last_write_time(sourcePath)
                                    || std::filesystem::last_write_time(mappingPath) < std::filesystem::last_write_time(sourcePath))
                                    throw std::runtime_error("Prepared topology predates the source BREP export");
                                if (std::abs(map.fuzzyTolerance - fuzzy) >
                                        std::max(1e-14, std::abs(fuzzy) * 1e-12) ||
                                    map.sourceSolidCount != originalSolids.Extent() ||
                                    map.sourceFaceCount != originalFaces.Extent() ||
                                    map.sourceEdgeCount != originalEdges.Extent())
                                    throw std::runtime_error("CAD, OCC tolerance or source topology changed");
                                BRep_Builder cachedBuilder;
                                TopoDS_Shape preparedShape;
                                std::ifstream preparedStream(preparedPath, std::ios::binary);
                                if (!preparedStream)
                                    throw std::runtime_error("Prepared CAD cannot be opened");
                                // The std::istream overload returns void in this OCCT version.
                                // Read first, then validate the resulting CAD shape.
                                BRepTools::Read(preparedShape, preparedStream, cachedBuilder);
                                if (preparedShape.IsNull() ||
                                    !BRepCheck_Analyzer(preparedShape).IsValid())
                                    throw std::runtime_error("Prepared CAD is missing or invalid");
                                TopTools_IndexedMapOfShape preparedSolids, preparedFaces, preparedEdges;
                                TopExp::MapShapes(preparedShape, TopAbs_SOLID, preparedSolids);
                                TopExp::MapShapes(preparedShape, TopAbs_FACE, preparedFaces);
                                TopExp::MapShapes(preparedShape, TopAbs_EDGE, preparedEdges);
                                if (preparedSolids.Extent() != map.preparedSolidCount ||
                                    preparedFaces.Extent() != map.preparedFaceCount ||
                                    preparedEdges.Extent() != map.preparedEdgeCount)
                                    throw std::runtime_error("Prepared BREP topology does not match mapping report");
                                TopTools_IndexedMapOfShape restoredSolids;
                                for (int i=1; i<=originalSolids.Extent(); ++i) {
                                    const TopoDS_Shape& restored = preparedSolids(map.solidImage.at(i));
                                    GProp_GProps before, after;
                                    BRepGProp::VolumeProperties(originalSolids(i), before);
                                    BRepGProp::VolumeProperties(restored, after);
                                    const double volume = std::abs(before.Mass());
                                    if (volume <= 0.0 || !std::isfinite(volume) ||
                                        !std::isfinite(after.Mass()) ||
                                        std::abs(std::abs(after.Mass()) - volume) >
                                            std::max(volume * 1e-6, 1e-12))
                                        throw std::runtime_error("Prepared CAD Solid volume/identity changed");
                                    restoredSolids.Add(restored);
                                }
                                std::map<int,std::vector<TopoDS_Shape>> restoredFaces, restoredEdges;
                                for (int i=1; i<=originalFaces.Extent(); ++i)
                                    for (int id : map.faceImages.at(i))
                                        restoredFaces[i].push_back(preparedFaces(id));
                                for (int i=1; i<=originalEdges.Extent(); ++i)
                                    for (int id : map.edgeImages.at(i))
                                        restoredEdges[i].push_back(preparedEdges(id));
                                // Finish every fallible validation before
                                // replacing the source-geometry working set.
                                TopTools_IndexedDataMapOfShapeListOfShape preparedOwners;
                                TopExp::MapShapesAndAncestors(preparedShape, TopAbs_FACE, TopAbs_SOLID, preparedOwners);
                                int commonFaces = 0;
                                for (int k=1; k<=preparedOwners.Extent(); ++k)
                                    if (preparedOwners.FindFromIndex(k).Extent() == 2) ++commonFaces;
                                TopoDS_Shape verifiedMainShape = preparedShape;
                                if (subsetMainShape) {
                                    TopoDS_Compound subset;
                                    BRep_Builder subsetBuilder;
                                    subsetBuilder.MakeCompound(subset);
                                    for (int index : domainIndices)
                                        subsetBuilder.Add(subset, restoredSolids(index));
                                    if (!BRepCheck_Analyzer(subset).IsValid())
                                        throw std::runtime_error("Verified Partition subset Main Shape is invalid");
                                    verifiedMainShape = subset;
                                }
                                // Commit only after all mappings and the subset
                                // geometry passed validation; no guessed Wall IDs.
                                mainShape = verifiedMainShape;
                                solids = restoredSolids;
                                faceImages = std::move(restoredFaces);
                                edgeImages = std::move(restoredEdges);
                                usedFinalizedGeometry = true;
                                notify(std::string(u8"[SMESH几何最终化] 已核验源 CAD、容差、域体积和拓扑映射；复用 Partition BREP。共享拓扑面=") +
                                    std::to_string(commonFaces) + u8"，本次不再执行 OCC Partition。" +
                                    (subsetMainShape ?
                                        (std::string(u8" Mesh 主几何选用其中 ") +
                                         std::to_string(domainIndices.size()) + u8" 个实体。") : std::string()));
                            } catch (const std::exception& e) {
                                finalizedFailure = e.what();
                                notify(std::string(u8"[SMESH几何最终化] 已有 Partition 缓存未通过校验：") + e.what() +
                                    u8"；未复用。请核对工程几何、网格容差和缓存。");
                            }
                        } else {
                            finalizedFailure = "Prepared BREP or topology-map.json is missing beside the source BREP";
                            notify(std::string(u8"[SMESH几何最终化] 缺少准备文件：") +
                                (!std::filesystem::exists(preparedPath) ? u8"最终化 BREP " : "") +
                                (!std::filesystem::exists(mappingPath) ? u8"拓扑映射 JSON" : "") +
                                u8"；请检查工程打开时的缓存恢复日志。");
                        }
                    }
                    if (!usedFinalizedGeometry && !autoPartition)
                        notify(u8"[SMESH拓扑] 未复用几何最终化结果，且未启用自动 OCC Partition：使用源 CAD 已有拓扑；独立接触面不能保证共形。可在几何模块完成最终化后重新生成网格。");
                    if (!usedFinalizedGeometry && autoPartition && domainIndices.size() > 1) {
                    if (request->boundaryLayerEnabled)
                        throw std::runtime_error("Boundary-layer mesh cannot use the verified Partition: " +
                            (finalizedFailure.empty() ? std::string("Unknown cache state") : finalizedFailure) +
                            ". The existing Wall face assignments are preserved; no implicit repartition was performed.");
                    const auto partitionStarted = std::chrono::steady_clock::now();
                    notify(u8"[SMESH共形] 开始 OCC 批量 Partition；保留原域，建立共享拓扑。");
                    notify(std::string(u8"[SMESH共形] OCC Fuzzy 容差=") + std::to_string(fuzzy));
                    BOPAlgo_Splitter partition;
                    partition.SetNonDestructive(true);
                    partition.SetRunParallel(true);
                    partition.SetFuzzyValue(fuzzy);
                    for (int index : domainIndices) partition.AddArgument(solids(index));
                    partition.Perform();
                    if (partition.HasErrors() || partition.Shape().IsNull())
                        throw std::runtime_error("Automatic CAD Partition failed; repair/partition geometry in the Geometry module before meshing");
                    const TopoDS_Shape partitioned = partition.Shape();
                    TopTools_IndexedMapOfShape finalFaces;TopExp::MapShapes(partitioned,TopAbs_FACE,finalFaces);
                    TopTools_IndexedMapOfShape finalEdges;TopExp::MapShapes(partitioned,TopAbs_EDGE,finalEdges);
                    for(int i=1;i<=originalFaces.Extent();++i) {
                        TopTools_IndexedMapOfShape images;
                        for(TopTools_ListIteratorOfListOfShape it(partition.Modified(originalFaces(i)));it.More();it.Next())
                            TopExp::MapShapes(it.Value(),TopAbs_FACE,images);
                        if(images.IsEmpty() && finalFaces.Contains(originalFaces(i)))images.Add(originalFaces(i));
                        faceImages[i].clear();for(int k=1;k<=images.Extent();++k)if(finalFaces.Contains(images(k)))faceImages[i].push_back(images(k));
                    }
                    // OCC Splitter is the authoritative mapping for locally
                    // selected CAD edges. Never carry pre-partition ShapeIds
                    // straight into the new shared parent Mesh.
                    for(int i=1;i<=originalEdges.Extent();++i) {
                        TopTools_IndexedMapOfShape images;
                        for(TopTools_ListIteratorOfListOfShape it(partition.Modified(originalEdges(i)));it.More();it.Next())
                            TopExp::MapShapes(it.Value(),TopAbs_EDGE,images);
                        if(images.IsEmpty() && finalEdges.Contains(originalEdges(i))) images.Add(originalEdges(i));
                        edgeImages[i].clear();
                        for(int k=1;k<=images.Extent();++k)
                            if(finalEdges.Contains(images(k)))edgeImages[i].push_back(images(k));
                    }
                    if (!BRepCheck_Analyzer(partitioned).IsValid())
                        throw std::runtime_error("Automatic CAD Partition produced invalid geometry");
                    TopTools_IndexedMapOfShape finalSolids;
                    TopExp::MapShapes(partitioned, TopAbs_SOLID, finalSolids);
                    std::vector<TopoDS_Shape> mapped(static_cast<std::size_t>(solids.Extent()) + 1);
                    TopTools_IndexedMapOfShape assigned;
                    for (int index : domainIndices) {
                        TopTools_IndexedMapOfShape images;
                        for (TopTools_ListIteratorOfListOfShape it(partition.Modified(solids(index))); it.More(); it.Next())
                            if (it.Value().ShapeType() == TopAbs_SOLID && finalSolids.Contains(it.Value()))
                                images.Add(it.Value());
                        if (images.IsEmpty() && finalSolids.Contains(solids(index))) images.Add(solids(index));
                        if (images.Extent() != 1 || assigned.Contains(images(1)))
                            throw std::runtime_error("CAD Partition split/merged original solid " + std::to_string(index) +
                                "; cannot preserve the SubMesh-to-solid mapping. Partition the CAD in Geometry and reselect the resulting domains.");
                        GProp_GProps actualVolume;
                        BRepGProp::VolumeProperties(images(1), actualVolume);
                        const double deviation = std::abs(std::abs(actualVolume.Mass()) - bounds.at(index).volume)
                            / bounds.at(index).volume;
                        if (!std::isfinite(deviation) || deviation > 1.0e-6)
                            throw std::runtime_error("CAD Partition changed the volume of solid " +
                                std::to_string(index) + "; source domains overlap or require new region assignments");
                        mapped[index] = images(1);
                        assigned.Add(images(1));
                    }
                    if (finalSolids.Extent() != assigned.Extent())
                        throw std::runtime_error("CAD Partition created additional/unassigned solid domains; edit Geometry Partition and reassign SubMesh regions");
                    TopTools_IndexedMapOfShape updated;
                    for (int i = 1; i <= solids.Extent(); ++i)
                        updated.Add(mapped[i].IsNull() ? solids(i) : mapped[i]);
                    solids = updated;
                    mainShape = partitioned;
                    // Confirm the result *actually* shares topological faces;
                    // a successful OCC Boolean is not proof of conformity.
                    TopTools_IndexedDataMapOfShapeListOfShape partitionOwners;
                    TopExp::MapShapesAndAncestors(mainShape, TopAbs_FACE, TopAbs_SOLID, partitionOwners);
                    int sharedFaces = 0;
                    for (int k = 1; k <= partitionOwners.Extent(); ++k)
                        if (partitionOwners.FindFromIndex(k).Extent() == 2) ++sharedFaces;
                    notify(std::string(u8"[SMESH共形] Partition 建立共享拓扑面 ") +
                        std::to_string(sharedFaces) + u8" 个；原 Solid 索引保持一一对应。");
                    notify(std::string(u8"[SMESH耗时] OCC Partition 与拓扑核对：") +
                        std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now()-partitionStarted).count()) +
                        u8" 秒。");
                    } // Cache absent/invalid: run the original, unchanged OCC Partition.
                }
            }
        }
        if (!reuse && !reusePreparation) {
            auto prepared = std::make_unique<NativeCadPreparation>();
            prepared->key = preparationKey.str();
            prepared->inputShape = shape;
            prepared->mainShape = mainShape;
            prepared->solids = solids;
            prepared->faceImages=faceImages;
            prepared->edgeImages=edgeImages;
            g_nativeCadPreparation = std::move(prepared);
        }
        for (const auto& entry : perSolidPrecision)
            if (entry.first > solids.Extent())
                throw std::invalid_argument("Solid chordal precision index outside BREP Solid range");
        std::map<int, CaeSMESHScopedNetgenControl> perSolidOptions;
        for (int n = 0; n < scopedNetgenCount; ++n) {
            const auto& c = scopedNetgen[n];
            const std::string at = "SMESH scoped NETGEN #" + std::to_string(n + 1) +
                " (BREP Solid " + std::to_string(c.solidShapeIndex) + "): ";
            const auto reject = [&](const char* field, double value) {
                throw std::invalid_argument(at + "invalid " + field + "=" + std::to_string(value));
            };
            if (c.solidShapeIndex < 1 || c.solidShapeIndex > solids.Extent())
                throw std::invalid_argument(at + "index outside 1.." + std::to_string(solids.Extent()));
            if (c.fineness < -1 || c.fineness > 5) reject("fineness", c.fineness);
            if (c.surfaceCurvature < -1 || c.surfaceCurvature > 1) reject("surfaceCurvature", c.surfaceCurvature);
            if (c.optimize < -1 || c.optimize > 1) reject("optimize", c.optimize);
            if (c.automaticMinSize < -1 || c.automaticMinSize > 1) reject("automaticMinSize", c.automaticMinSize);
            if (!std::isfinite(c.manualMinSize) || c.manualMinSize < 0.0) reject("manualMinSize", c.manualMinSize);
            if (!std::isfinite(c.growthRate) ||
                (c.growthRate > 0.0 && (c.growthRate < 0.01 || c.growthRate > 1.0)))
                reject("growthRate", c.growthRate);
            if (!std::isfinite(c.nbSegPerEdge) ||
                (c.nbSegPerEdge > 0.0 && (c.nbSegPerEdge < 0.05 || c.nbSegPerEdge > 20.0)))
                reject("nbSegPerEdge", c.nbSegPerEdge);
            if (!std::isfinite(c.nbSegPerRadius) ||
                (c.nbSegPerRadius > 0.0 && (c.nbSegPerRadius < 0.1 || c.nbSegPerRadius > 50.0)))
                reject("nbSegPerRadius", c.nbSegPerRadius);
            if (!perSolidOptions.emplace(c.solidShapeIndex, c).second)
                throw std::invalid_argument(at + "duplicate NETGEN configuration for this Solid");
        }
        notify(std::string(u8"[SMESH分域参数校验] 已验证 ") + std::to_string(perSolidOptions.size()) +
            u8" 个互不重复的 BREP Solid 参数。");
        const int mainCount = mainSolids.empty() ? solids.Extent() : int(mainSolids.size());
        const bool partial = !selectedSolids.empty() && int(selectedSolids.size()) < mainCount;
        notify(std::string(u8"[SMESH Main Shape] 主几何 ") + std::to_string(mainCount)
            + u8" 个 Solid，子网格计算由显式选择决定。");
        if (!selectedSolids.empty())
            notify(std::string(u8"[SMESH分域] 当前选中实体 ") + std::to_string(selectedSolids.size())
                + u8" / " + std::to_string(mainCount)
                + (partial ? u8"；其他体不会计算，结果仅可作局部预览" : u8"；覆盖全部实体"));
        auto isSelected = [&](int index) {
            return selectedSolids.empty()
                ? (mainSolids.empty() || mainSolids.count(index) != 0)
                : selectedSolids.count(index) != 0;
        };
        TopTools_IndexedDataMapOfShapeListOfShape faceOwners;
        TopExp::MapShapesAndAncestors(mainShape, TopAbs_FACE, TopAbs_SOLID, faceOwners);
        // The parent Main Shape can be just a subset of a prepared Partition.
        // For ViscousLayers, an "external Wall" must belong to EXACTLY ONE
        // solid in the entire prepared CAD, not one solid in the selected
        // boundary-layer scope or in the parent Mesh subset. Otherwise an
        // internal A/B interface gets a layer on A when B is not selected.
        TopTools_IndexedDataMapOfShapeListOfShape allSolidFaceOwners;
        if (request->boundaryLayerEnabled) {
            TopoDS_Compound fullPreparedCad;
            BRep_Builder fullBuilder;
            fullBuilder.MakeCompound(fullPreparedCad);
            for (int i = 1; i <= solids.Extent(); ++i)
                fullBuilder.Add(fullPreparedCad, solids(i));
            TopExp::MapShapesAndAncestors(fullPreparedCad, TopAbs_FACE, TopAbs_SOLID,
                                         allSolidFaceOwners);
            notify(std::string(u8"[SMESH边界层] 使用完整已准备 CAD 判定外表面：") +
                   std::to_string(solids.Extent()) + u8" 个实体；内部共享面不允许加层。");
        }
        // OCC's ancestor list can contain repeated occurrences of the same
        // solid (e.g. seam representations); count UNIQUE owner solids.
        const auto fullOwnersOf = [&](const TopoDS_Shape& face) {
            std::set<int> owners;
            const int key = allSolidFaceOwners.FindIndex(face);
            if (key <= 0) return owners;
            for (TopTools_ListIteratorOfListOfShape it(
                     allSolidFaceOwners.FindFromIndex(key)); it.More(); it.Next()) {
                const int owner = solids.FindIndex(it.Value());
                if (owner > 0) owners.insert(owner);
            }
            return owners;
        };

        SMESH_Gen* gen = generator();
        std::unique_ptr<NativeTetSession> transient;
        NativeTetSession* session = nullptr;
        if (reuse) {
            session = g_nativeTetSession.get();
            notify(std::string(u8"[SMESH增量] 复用父 Mesh，已完成 ") +
                std::to_string(session->completed.size()) + u8" 个实体。");
        } else {
            auto fresh = std::make_unique<NativeTetSession>();
            fresh->key = persistentKey;
            fresh->geometryKey = geometryKey;
            fresh->inputShape = shape;
            fresh->mainShape = mainShape;
            fresh->solids = solids;
            fresh->faceImages=faceImages;
            fresh->edgeImages=edgeImages;
            fresh->mesh.reset(gen->CreateMesh(true));
            fresh->mesh->ShapeToMesh(mainShape);
            if (!persistentKey.empty()) {
                g_nativeTetSession = std::move(fresh);
                session = g_nativeTetSession.get();
                notify(u8"[SMESH增量] 创建父 Mesh 会话；仅当前选中域参与体网格计算。");
            } else {
                transient = std::move(fresh);
                session = transient.get();
            }
        }
        SMESH_Mesh* mesh = session->mesh.get();
        if(surfaceSolidCount<0 || (surfaceSolidCount>0 && !surfaceSolidIndices))
            throw std::invalid_argument("Invalid Solid surface sizing scope");
        std::set<int> surfaceSolids;
        for(int n=0;n<surfaceSolidCount;++n) {
            const int id=surfaceSolidIndices[n];
            if(id<1 || id>solids.Extent() || (!mainSolids.empty() && !mainSolids.count(id)))
                throw std::invalid_argument("Solid surface sizing is outside the parent Main Shape");
            surfaceSolids.insert(id);
        }
        for (const auto& entry : perSolidPrecision)
            if (!surfaceSolids.count(entry.first))
                throw std::invalid_argument("Solid chordal override requires whole-domain surface scope");
        // Native SMESH uses Mesh Order when selecting hypotheses on shared
        // subshapes. UI solid numbering is not a refinement priority: a coarse
        // neighbour must not override a smaller domain's shared curved skin.
        // Express the default fine-first policy through the native API; never
        // mesh an interface twice or change either domain's numeric settings.
        std::map<int,double> priorityMaximum;
        for(int n=0;n<request->localSizeCount;++n) {
            const auto& local=request->localSizes[n];
            if(local.dimension!=3 || local.shapeIndex<=0 || !std::isfinite(local.targetSize) || local.targetSize<=0)continue;
            auto found=priorityMaximum.find(local.shapeIndex);
            if(found==priorityMaximum.end() || local.targetSize<found->second)
                priorityMaximum[local.shapeIndex]=local.targetSize;
        }
        auto nativeDomainPriority=[&] {
            TListOfInt order;
            std::vector<int> controls;
            for(int i=0;i<scopedNetgenCount;++i)controls.push_back(i);
            auto policy=[&](int i) {
                const auto& control=scopedNetgen[i];
                const auto size=priorityMaximum.find(control.solidShapeIndex);
                const double maximum=size==priorityMaximum.end()?request->maxSize:size->second;
                const bool curvature=control.surfaceCurvature<0?request->surfaceCurvature!=0:control.surfaceCurvature!=0;
                return std::make_pair(maximum,!curvature);
            };
            std::stable_sort(controls.begin(),controls.end(),[&](int a,int b){return policy(a)<policy(b);});
            for(int i:controls) {
                const int index=scopedNetgen[i].solidShapeIndex;
                if(!mainSolids.empty() && !mainSolids.count(index))continue;
                const auto group=session->surfaceScopes.find(index);
                if(group!=session->surfaceScopes.end()) {
                    const int id=mesh->GetSubMesh(group->second)->GetId();
                    if(std::find(order.begin(),order.end(),id)==order.end())order.push_back(id);
                }
                const int id=mesh->GetSubMesh(solids(index))->GetId();
                if(std::find(order.begin(),order.end(),id)==order.end())order.push_back(id);
            }
            return order;
        };
        TListOfInt domainPriority=nativeDomainPriority();
        TListOfListOfInt nativeOrder;
        if(!domainPriority.empty())nativeOrder.push_back(domainPriority);
        if(!session->completed.empty() && mesh->GetMeshOrder()!=nativeOrder)
            throw std::runtime_error("Native SubMesh priority changed after computation; rebuild parent Mesh");
        mesh->SetMeshOrder(nativeOrder);
        if(!domainPriority.empty())notify(std::string(u8"[SMESH原生优先级] 已登记 ")+std::to_string(domainPriority.size())+
            u8" 个域的 SubMesh 顺序；共享面优先较小尺寸，相同尺寸优先曲率开启，显式面规则优先；由 SMESH 原生解析。");
        const auto previouslyMeshedVolumes = mesh->NbVolumes();
        auto make = [&](auto* object) {
            session->hypotheses.emplace_back(object);
            return object;
        };
        const int requestedThreads = requestedNetgenThreads();
        constexpr bool nativeThreadApi = HasNativeNetgenThreads<NETGENPlugin_Hypothesis>::value;
        // A missing setter does not imply serial execution: NETGEN 6 defaults
        // to internal parallel meshing. Respect the official SDK serial flag;
        // otherwise report an unreadable SDK default instead of a fictitious 1.
        const bool nativeSerialFlag = std::getenv("SALOME_NETGEN_DISABLE_MULTITHREADING") != nullptr;
        const int configuredThreads = nativeSerialFlag ? 1 : nativeThreadApi ? requestedThreads : 0;
        auto applyNetgenFineness = [&](NETGENPlugin_Hypothesis* hyp, int fineness,
                                       const CaeSMESHScopedNetgenControl* local = nullptr)
        {
            (void)configureNativeNetgenThreads(hyp, configuredThreads);
            hyp->SetFineness(static_cast<NETGENPlugin_Hypothesis::Fineness>(fineness));
            if (fineness == NETGENPlugin_Hypothesis::UserDefined)
            {
                hyp->SetGrowthRate(local && local->growthRate > 0.0 ? local->growthRate : request->growthRate);
                hyp->SetNbSegPerEdge(local && local->nbSegPerEdge > 0.0 ? local->nbSegPerEdge : request->nbSegPerEdge);
                hyp->SetNbSegPerRadius(local && local->nbSegPerRadius > 0.0 ? local->nbSegPerRadius : request->nbSegPerRadius);
            }
        };

        // 1D-2D-3D on *each* Solid is not a safe incremental strategy:
        // later independent surface meshing may replace the already completed
        // CAD interface. The shared parent must own the surface dependency,
        // while each Solid uses NETGEN 3D-only on the previously generated faces.
        // Keep separate native 2D and 3D hypotheses so explicit Face/Edge rules
        // retain precedence. Legacy Solid settings remain volume-only; the
        // additive domain scope also assigns native 2D parameters on the Solid.
        // Unscoped legacy single-Solid requests retain the integrated algorithm.
        const bool hasVolumeSize = request->localSizes && request->localSizeCount > 0 &&
            std::any_of(request->localSizes, request->localSizes + request->localSizeCount,
                [](const CaeSMESHLocalSizeControl& control) { return control.dimension == 3; });
        // Explicit 1D/2D controls on a single Solid must also keep the surface
        // mesher independent from the volume hypothesis. Otherwise a legacy
        // integrated NETGEN 1D-2D-3D algorithm can override Face parameters.
        const bool hasSurfaceControls = request->edgeSegmentCount > 0 ||
            faceCurvatureCount > 0 || faceMinimumCount > 0 ||
            (request->localSizes && std::any_of(
                request->localSizes, request->localSizes + request->localSizeCount,
                [](const CaeSMESHLocalSizeControl& control) { return control.dimension == 2; }));
        const bool splitSurfaceAndVolume =
            mainCount > 1 || scopedNetgenCount > 0 || hasVolumeSize || hasSurfaceControls || !surfaceSolids.empty() || surfaceOnly || surfaceElementLimit>0 || !perSolidPrecision.empty();
        // The standalone trelau/SMESH native library supports a parent
        // hypothesis and explicit Face/Solid overrides. Face/Edge rules should
        // never force N otherwise-identical 3D Solid hypotheses. Use the root
        // for complete jobs; partial/incremental jobs and BL retain the proven
        // conservative per-Solid path to avoid invalidating shared interfaces.
        bool allParentSurfaces = !surfaceSolids.empty() && surfaceSolids.size() == std::size_t(mainCount);
        if (allParentSurfaces && !mainSolids.empty())
            for (const int index : mainSolids)
                allParentSurfaces = allParentSurfaces && surfaceSolids.count(index) != 0;
        const bool parentSessionCompatible = session->solidConfiguration.empty() ||
            session->volumeAlgorithmInstalled;
        const bool unifiedParentSurface = splitSurfaceAndVolume && !partial && !surfaceOnly &&
            !request->boundaryLayerEnabled && perSolidOptions.empty() && perSolidPrecision.empty() && !hasVolumeSize &&
            allParentSurfaces && parentSessionCompatible;
        // Use a parent 3D algorithm for every complete, non-BL split job.
        // Only a genuinely different Solid policy needs a local 3D SubMesh.
        // This is the native inheritance model exercised by trelau/SMESH's
        // SMESH_Mesh::AddHypothesis() and SMESH_Gen::Compute(). Never change
        // the scope of a running partial/incremental session in-place.
        const bool parentVolumeDefaults = splitSurfaceAndVolume && !partial && !surfaceOnly &&
            !request->boundaryLayerEnabled && parentSessionCompatible;
        if (parentVolumeDefaults)
            notify(u8"[SMESH父级标准化] 默认 3D 假设绑定父 Mesh；只有实际不同的实体参数才建立 3D SubMesh。");
        if (splitSurfaceAndVolume && !session->surfaceAlgorithmInstalled) {
            auto* surface = make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(), gen));
            auto* surfaceHyp = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
            surfaceHyp->SetMaxSize(request->maxSize);
            surfaceHyp->SetMinSize(std::min(request->minSize, request->maxSize));
            applyNetgenFineness(surfaceHyp, request->fineness);
            surfaceHyp->SetOptimize(request->optimize != 0);
            surfaceHyp->SetSurfaceCurvature(request->surfaceCurvature != 0);
            surfaceHyp->SetChordalErrorEnabled(chordalErrorEnabled);
            if (chordalErrorEnabled) surfaceHyp->SetChordalError(chordalError);
            surfaceHyp->SetQuadAllowed(false);
            requireHypothesis(*mesh, mainShape, *surface, "shared NETGEN 1D-2D surface");
            requireHypothesis(*mesh, mainShape, *surfaceHyp, "shared NETGEN surface parameters");
            session->surfaceAlgorithmInstalled = true;
            notify(u8"[SMESH增量共形] 父 Mesh 已建立统一 NETGEN 1D-2D 表面算法；各 Solid 使用 NETGEN 3D-only，不再独立重划共享面。");
        }

        // Map all UI controls to the exact BREP before adding any hypothesis.
        // Multiple rules resolving to the same shape are merged conservatively:
        // the smaller local size wins, while the larger edge division count wins.
        struct SizeItem { TopoDS_Shape shape; double target = 0.0; };
        struct EdgeItem { TopoDS_Shape shape; int divisions = 0; double bias = 1.0; };
        std::map<int, SizeItem> explicitFaceTargets;
        std::map<int, SizeItem> faceTargets;
        std::map<int, SizeItem> volumeTargets;
        std::map<int, EdgeItem> edgeTargets;
        std::map<int, int> faceCurvatureTargets;

        for (int i = 0; i < request->localSizeCount; ++i)
        {
            const CaeSMESHLocalSizeControl& c = request->localSizes[i];
            if ((c.dimension != 2 && c.dimension != 3) ||
                !std::isfinite(c.targetSize) || c.targetSize <= 0.0 ||
                c.shapeIndex <= 0)
                throw std::invalid_argument("Invalid SMESH local size control");
            const TopAbs_ShapeEnum type = c.dimension == 2 ? TopAbs_FACE : TopAbs_SOLID;
            TopoDS_Shape mapped = indexedShape(shape, type, c.shapeIndex, "NETGEN local size");
            // Local Solid controls refer to the imported BREP, while native
            // submeshes must be attached to the canonical partitioned Solid.
            if (c.dimension == 3 && !mapped.IsNull()) {
                const int originalIndex = originalSolids.FindIndex(mapped);
                if (originalIndex > 0) mapped = solids(originalIndex);
            }
            if (mapped.IsNull())
                throw std::runtime_error(c.dimension == 2
                    ? "Cannot map a local Face control to the BREP"
                    : "Cannot map a local Solid control to the BREP");
            std::vector<TopoDS_Shape> images;
            if (c.dimension == 2) {
                if (!sourceMainFaces.Contains(mapped)) continue;
                const int sourceIndex = originalFaces.FindIndex(mapped);
                if (sourceIndex <= 0)
                    throw std::runtime_error("Local Face is missing from the original OCC topology");
                images = faceImages.at(sourceIndex);
                if (images.empty())
                    throw std::runtime_error("OCC Partition cannot map selected Face " + std::to_string(sourceIndex) + "; perform Geometry Partition and reselect this Face");
            } else {
                images.push_back(mapped);
            }
            for (const auto& image : images) {
                const int shapeId = mesh->GetMeshDS()->ShapeToIndex(image);
                if (shapeId <= 0)
                    throw std::runtime_error("Mapped local Face/Solid is not owned by the parent Mesh Main Shape");
                auto& dst = c.dimension == 2 ? explicitFaceTargets : volumeTargets;
                auto it = dst.find(shapeId);
                if (it == dst.end() || c.targetSize < it->second.target)
                    dst[shapeId] = {image, c.targetSize};
            }
            if (images.size() > 1)
                notify(std::string(u8"[SMESH局部规则映射] 原面 ") + std::to_string(c.shapeIndex)
                    + u8" 分为 " + std::to_string(images.size()) + u8" 个子面；尺寸规则已传播至所有子面。");
        }

        for (int i = 0; i < request->edgeSegmentCount; ++i)
        {
            const CaeSMESHEdgeSegmentsControl& c = request->edgeSegments[i];
            if (c.divisions < 1 || !std::isfinite(c.bias) || c.bias <= 0.0 ||
                c.shapeIndex <= 0)
                throw std::invalid_argument("Invalid SMESH edge segment control");
            const TopoDS_Shape sourceEdge = indexedShape(shape, TopAbs_EDGE, c.shapeIndex, "NETGEN edge control");
            if (sourceEdge.IsNull()) throw std::runtime_error("Cannot map a local Edge control to the BREP");
            if (!sourceMainEdges.Contains(sourceEdge)) continue;
            const int sourceIndex = originalEdges.FindIndex(sourceEdge);
            if (sourceIndex <= 0) throw std::runtime_error("Selected Edge is absent from original OCC topology");
            const auto& images = edgeImages.at(sourceIndex);
            if (images.empty())
                throw std::runtime_error("OCC Partition cannot map selected Edge " + std::to_string(sourceIndex) + "; perform Geometry Partition and reselect this Edge");
            std::vector<int> pieceDivisions(images.size(), 1);
            if (images.size() == 1) pieceDivisions[0] = c.divisions;
            else {
                // An edge split changes the meaning of NumberOfSegments:
                // do NOT apply the old total to every child. Preserve the
                // original total using a length-weighted integer allocation.
                if (std::abs(c.bias - 1.0) > 1.0e-12)
                    throw std::runtime_error("An OCC Partition split a biased Edge rule; nonuniform bias cannot be remapped safely. Partition in Geometry and reassign this rule.");
                if (c.divisions < static_cast<int>(images.size()))
                    throw std::runtime_error("An OCC Partition split a selected Edge into more pieces than its requested segment count; increase divisions or reassign after Geometry Partition.");
                std::vector<double> lengths(images.size());
                double totalLength = 0.0;
                for (std::size_t k = 0; k < images.size(); ++k) {
                    GProp_GProps props;
                    BRepGProp::LinearProperties(images[k], props);
                    lengths[k] = std::abs(props.Mass());
                    if (!std::isfinite(lengths[k]) || lengths[k] <= 0.0)
                        throw std::runtime_error("Partition produced a zero/invalid length Edge child; cannot safely redistribute segments");
                    totalLength += lengths[k];
                }
                const int remaining = c.divisions - static_cast<int>(images.size());
                std::vector<std::pair<double,std::size_t>> remainders;
                int assigned = 0;
                for (std::size_t k = 0; k < images.size(); ++k) {
                    const double share = remaining * lengths[k] / totalLength;
                    const int whole = static_cast<int>(std::floor(share));
                    pieceDivisions[k] += whole;
                    assigned += whole;
                    remainders.emplace_back(share - whole, k);
                }
                std::sort(remainders.begin(), remainders.end(), [](const auto& a, const auto& b) {
                    if (a.first != b.first) return a.first > b.first;
                    return a.second < b.second;
                });
                for (int k = 0; k < remaining - assigned; ++k)
                    ++pieceDivisions[remainders[static_cast<std::size_t>(k)].second];
                notify(std::string(u8"[SMESH局部规则映射] 原边 ") + std::to_string(c.shapeIndex)
                    + u8" 分为 " + std::to_string(images.size())
                    + u8" 个子边；按长度分配原始 " + std::to_string(c.divisions) + u8" 个分段。");
            }
            for (std::size_t k = 0; k < images.size(); ++k) {
                const int shapeId = mesh->GetMeshDS()->ShapeToIndex(images[k]);
                if (shapeId <= 0)
                    throw std::runtime_error("Mapped local Edge is not owned by parent Mesh Main Shape");
                const auto it = edgeTargets.find(shapeId);
                if (it == edgeTargets.end() || pieceDivisions[k] > it->second.divisions)
                    edgeTargets[shapeId] = {images[k], pieceDivisions[k], c.bias};
            }
        }

        for (int i = 0; i < faceCurvatureCount; ++i)
        {
            const auto& control = faceCurvatures[i];
            if (control.shapeIndex <= 0 || control.elementsPer2Pi < 3 || control.elementsPer2Pi > 1000)
                throw std::invalid_argument("Invalid exact Face curvature control");
            const auto sourceFace = indexedShape(shape, TopAbs_FACE, control.shapeIndex, "NETGEN local curvature");
            if (!sourceMainFaces.Contains(sourceFace)) continue;
            const int sourceIndex = originalFaces.FindIndex(sourceFace);
            if (sourceIndex <= 0) throw std::runtime_error("Local curvature Face is missing from original OCC topology");
            const auto& images = faceImages.at(sourceIndex);
            if (images.empty())
                throw std::runtime_error("OCC Partition cannot map selected curvature Face " + std::to_string(sourceIndex) + "; perform Geometry Partition and reselect this Face");
            for (const auto& face : images) {
                const int faceId = mesh->GetMeshDS()->ShapeToIndex(face);
                if (faceId <= 0)
                    throw std::runtime_error("Mapped local curvature Face is not owned by the parent Mesh Main Shape");
                faceCurvatureTargets[faceId] = std::max(faceCurvatureTargets[faceId], control.elementsPer2Pi);
                if (!explicitFaceTargets.count(faceId)) explicitFaceTargets[faceId] = {face, request->maxSize};
            }
        }

        // Face rules stay face-local. The parent Mesh owns the baseline 2D
        std::map<int,double> faceMinimumTargets;
        for (int i=0;i<faceMinimumCount;++i) {
            const auto& c=faceMinimums[i];
            if(c.shapeIndex<=0 || c.mode < -1 || c.mode>1 || !std::isfinite(c.manualSize) || c.manualSize<0)
                throw std::invalid_argument("Invalid exact Face minimum control");
            if(c.mode<0)continue;
            const auto source=indexedShape(shape,TopAbs_FACE,c.shapeIndex,"NETGEN Face minimum");
            if(!sourceMainFaces.Contains(source))continue;
            const int sourceIndex=originalFaces.FindIndex(source);
            const auto& images=faceImages.at(sourceIndex);
            if(images.empty())throw std::runtime_error("OCC Partition cannot map selected Face minimum");
            for(const auto& image:images) {
                const int id=mesh->GetMeshDS()->ShapeToIndex(image);
                if(id<=0)throw std::runtime_error("Face minimum is not owned by parent Mesh");
                if(!explicitFaceTargets.count(id))explicitFaceTargets[id]={image,request->maxSize};
                const double target=explicitFaceTargets.at(id).target;
                // MinSize=0 leaves sizing to NETGEN. Do not insert a CAD-derived
                // application floor into an explicitly automatic Face control.
                const double minimum=c.mode==0?0.0:c.manualSize;
                if(minimum>target)throw std::invalid_argument("Local Face minimum must not exceed its maximum size");
                auto found=faceMinimumTargets.find(id);
                if(found!=faceMinimumTargets.end() && minimum!=found->second)
                    throw std::invalid_argument("Conflicting minimum sizes on one shared Face; assign one native Face hypothesis");
                faceMinimumTargets[id]=minimum;
            }
        }
        // hypothesis; do not attach an additional 2D hypothesis to each Solid.
        // Native SMESH resolves explicit Face/Edge submesh dependencies.
        faceTargets = explicitFaceTargets;
        // One visual position can correspond to different pre-Partition CAD
        // Face IDs owned by different Solids. Do not pretend a rule affects
        // the selected preview domain merely because its 2D hypothesis was
        // registered on the shared parent Mesh.
        for (const auto& target : faceTargets) {
            const int ownerPosition = faceOwners.FindIndex(target.second.shape);
            bool belongsToSelectedSolid = false;
            if (ownerPosition > 0) {
                const auto& owners = faceOwners.FindFromIndex(ownerPosition);
                for (TopTools_ListIteratorOfListOfShape it(owners); it.More(); it.Next()) {
                    const int solidIndex = solids.FindIndex(it.Value());
                    if (solidIndex > 0 && isSelected(solidIndex)) {
                        belongsToSelectedSolid = true;
                        break;
                    }
                }
            }
            if (!belongsToSelectedSolid)
                notify(std::string(u8"[SMESH面作用域提醒] Face ShapeId=") +
                    std::to_string(target.first) +
                    u8" 不属于本次指定的 Solid，本次 3D 试划分不会显示该面的加密；请核对原 CAD 面编号和所属实体。");
        }
        notify(std::string(u8"[SMESH规则] 体尺寸 ") + std::to_string(volumeTargets.size())
            + u8" 组（仅绑定所属体）；面尺寸/曲率 " + std::to_string(faceTargets.size())
            + u8" 组（仅绑定选中面）；边分段 " + std::to_string(edgeTargets.size())
            + u8" 组（仅绑定选中边）。共享面由同一父 Mesh 管理。");
        // Surface-local rules can override the apparent global Max/Min.
        // Name and size each exact BREP target before a costly full compute.
        for (const auto& kv : volumeTargets)
            notify(std::string(u8"[SMESH局部规则] 体 ShapeId=") + std::to_string(kv.first)
                + u8"，目标尺寸=" + std::to_string(kv.second.target));
        for (const auto& kv : faceTargets)
            notify(std::string(u8"[SMESH局部规则] 面 ShapeId=") + std::to_string(kv.first)
                + u8"，目标尺寸=" + std::to_string(kv.second.target));
        for (const auto& kv : edgeTargets)
            notify(std::string(u8"[SMESH局部规则] 边 ShapeId=") + std::to_string(kv.first)
                + u8"，分段数=" + std::to_string(kv.second.divisions));

        // Only explicit user Edge rules are applied. Face curvature rules do not
        // create extra 1D segment constraints automatically.
        const int manualEdgeSubmeshCount = static_cast<int>(edgeTargets.size());

        // Edge controls are native SMESH SubMeshes: Regular_1D + NumberOfSegments.
        for (const auto& kv : edgeTargets)
        {
            const EdgeItem& item = kv.second;
            // A new edge rule must not force a remesh of an already completed
            // neighbouring Solid's surface dependency.
            for (int completedIndex : session->completed) {
                TopTools_IndexedMapOfShape oldEdges;
                TopExp::MapShapes(solids(completedIndex), TopAbs_EDGE, oldEdges);
                if (oldEdges.Contains(item.shape) && !session->edgeConfiguration.count(kv.first))
                    throw std::runtime_error("Shared NETGEN Edge rule introduced after adjacent SubMesh was computed; rebuild parent Mesh");
            }
            const std::string config = std::to_string(item.divisions) + ":" + std::to_string(item.bias);
            const auto existing = session->edgeConfiguration.find(kv.first);
            if (existing != session->edgeConfiguration.end()) {
                if (existing->second != config)
                    throw std::runtime_error("Shared NETGEN Edge rule changed after meshing; reset and regenerate parent Mesh");
                continue;
            }
            session->edgeConfiguration[kv.first] = config;
            auto* regular = make(new StdMeshers_Regular_1D(gen->GetANewId(), gen));
            auto* segments = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
            segments->SetNumberOfSegments(item.divisions);
            if (std::abs(item.bias - 1.0) > 1.0e-12)
            {
                segments->SetDistrType(StdMeshers_NumberOfSegments::DT_Scale);
                segments->SetScaleFactor(item.bias);
            }
            requireHypothesis(*mesh, item.shape, *regular, "Regular_1D local Edge");
            requireHypothesis(*mesh, item.shape, *segments, "NumberOfSegments local Edge");
        }

        // Install Solid surface defaults before resolving any explicit Face
        // overrides. Ask SMESH for the inherited hypothesis on a Face; never
        // choose one of its Solid owners in application code.
        for(const auto& target:volumeTargets) {
            const int index=solids.FindIndex(target.second.shape);
            if(index>0)priorityMaximum[index]=target.second.target;
        }
        struct DomainDefault { int index;TopoDS_Shape solid;NETGENPlugin_Hypothesis_2D* hypothesis; };
        std::map<std::string,std::vector<DomainDefault>> domainBatches;
        for(int i:surfaceSolids) {
            // The parent 2D hypothesis already represents this entire uniform
            // surface scope. Adding an equal Solid/group 2D hypothesis would
            // duplicate it and fragment a connected NETGEN surface collection.
            if (unifiedParentSurface) break;
            if(session->solidConfiguration.count(i)) continue;
            const auto solid=solids(i);
            const int id=mesh->GetMeshDS()->ShapeToIndex(solid);
            const auto size=volumeTargets.find(id);
            const double maximum=size==volumeTargets.end()?request->maxSize:size->second.target;
            const auto found=perSolidOptions.find(i);
            const auto* settings=found==perSolidOptions.end()?nullptr:&found->second;
            const int fineness=settings && settings->fineness>=0?settings->fineness:request->fineness;
            const bool optimize=settings && settings->optimize>=0?settings->optimize!=0:request->optimize!=0;
            const bool curvature=settings && settings->surfaceCurvature>=0?settings->surfaceCurvature!=0:request->surfaceCurvature!=0;
            // A Solid-scoped 2D hypothesis must follow the *effective* Solid
            // minimum, rather than silently using 0 (native automatic). Users
            // opt into automatic minimum explicitly via the domain/parent
            // setting, or override it on a particular Face SubMesh.
            const bool useAutoMinimum = settings && settings->automaticMinSize >= 0
                ? settings->automaticMinSize != 0 : automaticMinSize;
            const double minimum = useAutoMinimum ? 0.0
                : (settings && settings->automaticMinSize >= 0
                    ? settings->manualMinSize : request->minSize);
            if (!std::isfinite(minimum) || minimum < 0.0 || minimum > maximum)
                throw std::invalid_argument("NETGEN Solid " + std::to_string(i) +
                    ": surface minimum size must not exceed its maximum; change the Solid minimum or use automatic minimum");
            auto* hyp=make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(),gen));
            hyp->SetMaxSize(maximum);hyp->SetMinSize(minimum);
            applyNetgenFineness(hyp,fineness,settings);
            hyp->SetOptimize(optimize);hyp->SetSurfaceCurvature(curvature);
            const auto precision = precisionForSolid(i);
            hyp->SetChordalErrorEnabled(precision.chordalErrorEnabled != 0);
            if(precision.chordalErrorEnabled)hyp->SetChordalError(precision.chordalError);
            hyp->SetQuadAllowed(false);
            std::ostringstream config;config<<std::setprecision(17)<<hyp->GetMaxSize()<<':'<<hyp->GetMinSize()
                <<':'<<hyp->GetFineness()<<':'<<hyp->GetGrowthRate()<<':'<<hyp->GetNbSegPerEdge()
                <<':'<<hyp->GetNbSegPerRadius()<<':'<<hyp->GetOptimize()<<':'<<hyp->GetSurfaceCurvature()
                <<':'<<hyp->GetChordalErrorEnabled()<<':'<<(hyp->GetChordalErrorEnabled()?hyp->GetChordalError():0.0);
            domainBatches[config.str()].push_back({i,solid,hyp});
            notify(std::string(u8"[SMESH整域尺寸] 实体 ")+std::to_string(i)+
                " SurfaceMax="+std::to_string(hyp->GetMaxSize())+
                " SurfaceMin="+std::to_string(hyp->GetMinSize())+
                " Growth="+std::to_string(hyp->GetGrowthRate())+
                " SegRadius="+std::to_string(hyp->GetNbSegPerRadius())+
                " Chordal="+(hyp->GetChordalErrorEnabled()?std::to_string(hyp->GetChordalError()):std::string("OFF"))+
                u8"；表面与体内部使用本域参数，显式面/边规则优先，共享面由原生 Mesh Order 解析。");
        }
        for(const auto& batch:domainBatches) {
            const auto& defaults=batch.second;
            // This is a 2D algorithm: register its scope as a native FACE group,
            // not as a compound of SOLIDs. In dimension-limited previews the
            // native scheduler sorts Face submeshes; Mesh Order on a Solid
            // compound expands only to Solids and cannot order those Faces.
            // Domain selection remains unchanged; this only binds its skin.
            TopTools_IndexedMapOfShape skin;
            for(const auto& entry:defaults)TopExp::MapShapes(entry.solid,TopAbs_FACE,skin);
            TopoDS_Compound group;BRep_Builder builder;builder.MakeCompound(group);
            for(int f=1;f<=skin.Extent();++f)builder.Add(group,skin(f));
            const TopoDS_Shape scope=group;
            if(defaults.size()>1) {
            notify(std::string(u8"[SMESH整域面组] 相同参数实体 ")+std::to_string(defaults.size())+
                u8" 个，使用一个原生几何组假设；保持连续表面收集。");
            }
            // Bind algorithm and parameters at the same native scope so Mesh
            // Order can schedule the finer group's shared boundaries first.
            // A root-only algorithm may collect a coarse set of Faces first
            // even when a shared Face inherits finer local parameters.
            auto* algorithm=make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(),gen));
            requireHypothesis(*mesh,scope,*algorithm,"NETGEN domain/group 1D-2D algorithm");
            requireHypothesis(*mesh,scope,*defaults.front().hypothesis,"NETGEN domain/group surface parameters");
            for(const auto& entry:defaults)session->surfaceScopes[entry.index]=scope;
        }
        if(!domainBatches.empty()) {
            nativeOrder.clear();domainPriority=nativeDomainPriority();
            if(!domainPriority.empty())nativeOrder.push_back(domainPriority);
            mesh->SetMeshOrder(nativeOrder);
        }

        // Face/volume sizing uses native local NETGEN SubMeshes. Everything is
        // configured before the first Compute to preserve SMESH dependency state.
        struct FaceBatch {
            NETGENPlugin_Hypothesis_2D* parameters = nullptr;
            std::vector<TopoDS_Shape> faces;
        };
        std::map<std::string, FaceBatch> faceBatches;
        for (const auto& kv : faceTargets)
        {
            // The session policy includes every sizing rule. Existing Face
            // configurations are unchanged; reading them as their own defaults
            // would erase the distinction between inherited and explicit values.
            if(session->faceConfiguration.count(kv.first))continue;
            const SizeItem& item = kv.second;
            const int faceOwnerIndex = faceOwners.FindIndex(item.shape);
            if (faceOwnerIndex > 0 && !session->faceConfiguration.count(kv.first)) {
                const auto& owners = faceOwners.FindFromIndex(faceOwnerIndex);
                for (TopTools_ListIteratorOfListOfShape it(owners); it.More(); it.Next()) {
                    const int solidIndex = solids.FindIndex(it.Value());
                    if (session->completed.count(solidIndex))
                        throw std::runtime_error("Shared NETGEN Face rule introduced after adjacent SubMesh was computed; rebuild parent Mesh");
                }
            }
            const auto curvature = faceCurvatureTargets.find(kv.first);
            const auto minimum=faceMinimumTargets.find(kv.first);
            SMESH_HypoFilter parentFilter(SMESH_HypoFilter::HasName("NETGEN_Parameters_2D"));
            std::list<const SMESHDS_Hypothesis*> inherited;
            mesh->GetHypotheses(item.shape,parentFilter,inherited,true);
            const auto* base=inherited.empty()?nullptr:dynamic_cast<const NETGENPlugin_Hypothesis_2D*>(inherited.front());
            if(!base)throw std::runtime_error("Explicit Face has no inherited NETGEN 2D parameters");
            const double faceMaximum=explicitFaceTargets.count(kv.first)?item.target:base->GetMaxSize();
            const double localMinimum = minimum == faceMinimumTargets.end()
                ? std::min(base->GetMinSize(), faceMaximum) : minimum->second;
            const bool localCurvature = curvature != faceCurvatureTargets.end() || base->GetSurfaceCurvature();
            // Preserve the native NETGEN automatic minimum (0) selected by
            // the user. Do not replace it with an application-computed floor.
            const double effectiveMinimum = localMinimum;
            std::ostringstream faceConfiguration;
            faceConfiguration << std::setprecision(17) << faceMaximum << ':' <<
                base->GetFineness() << ':' << effectiveMinimum << ':' << localCurvature << ':' <<
                (curvature == faceCurvatureTargets.end() ? 0 : curvature->second) << ':' <<
                base->GetGrowthRate() << ':' << base->GetNbSegPerEdge() << ':' << base->GetNbSegPerRadius()
                << ':' << base->GetOptimize()<<':'<<base->GetChordalErrorEnabled()
                <<':'<<(base->GetChordalErrorEnabled()?base->GetChordalError():0.0);
            // The exact Face rules have already been resolved into disjoint
            // targets above. Batch equal native parameters together, including
            // curvature. Forcing a separate algorithm per Face prevents NETGEN
            // from meshing/smoothing the common skin as one native face group;
            // ViscousLayers can then receive folded/degenerate surface facets.
            // Different controls still have distinct settings and scopes.
            const std::string config = faceConfiguration.str();
            const auto existing = session->faceConfiguration.find(kv.first);
            if (existing != session->faceConfiguration.end()) {
                if (existing->second != config)
                    throw std::runtime_error("Shared NETGEN Face rule changed after meshing; reset and regenerate parent Mesh");
                continue;
            }
            session->faceConfiguration[kv.first] = config;
            NETGENPlugin_Hypothesis_2D* faceHyp = nullptr;
            const auto shared = faceBatches.find(config);
            if (shared != faceBatches.end()) {
                faceHyp = shared->second.parameters;
            } else {
                faceHyp = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
                faceHyp->SetMaxSize(faceMaximum);
                faceHyp->SetMinSize(effectiveMinimum);
                applyNetgenFineness(faceHyp, base->GetFineness());
                if(base->GetFineness()==NETGENPlugin_Hypothesis::UserDefined) {
                    faceHyp->SetGrowthRate(base->GetGrowthRate());
                    faceHyp->SetNbSegPerEdge(base->GetNbSegPerEdge());
                    faceHyp->SetNbSegPerRadius(base->GetNbSegPerRadius());
                }
                faceHyp->SetOptimize(base->GetOptimize());
                faceHyp->SetSurfaceCurvature(localCurvature);
                faceHyp->SetChordalErrorEnabled(base->GetChordalErrorEnabled());
                if (base->GetChordalErrorEnabled()) faceHyp->SetChordalError(base->GetChordalError());
                if (curvature != faceCurvatureTargets.end()) {
                    // This native setter selects UserDefined. Preserve the native
                    // geometry minimum explicitly instead of losing automatic sizing.
                    constexpr double twoPi = 6.28318530717958647692;
                    // An explicit local curvature choice must not silently be capped
                    // by the Moderate preset (otherwise 4/10/12 all mean 2).
                    // It is still a *size reference*, not a fixed edge count.
                    faceHyp->SetNbSegPerRadius(std::max(0.05, curvature->second / twoPi));
                    faceHyp->SetSurfaceCurvature(true);
                    notify(std::string(u8"[SMESH局部曲率] SMESHShapeId=") + std::to_string(mesh->GetMeshDS()->ShapeToIndex(item.shape))
                        + " requestedCircleSegments=" + std::to_string(curvature->second)
                        + " nativeSegmentsPerRadius=" + std::to_string(faceHyp->GetNbSegPerRadius())
                        + " nativeMinSize=" + std::to_string(faceHyp->GetMinSize()));
                }
                faceHyp->SetQuadAllowed(false);
            }
            notify(std::string(u8"[SMESH面实际参数] ShapeId=")+std::to_string(kv.first)+
                " Max="+std::to_string(faceHyp->GetMaxSize())+" Min="+std::to_string(faceHyp->GetMinSize())+
                " Chordal="+(faceHyp->GetChordalErrorEnabled()?std::to_string(faceHyp->GetChordalError()):std::string("OFF"))+
                " NativeHypothesisId="+std::to_string(faceHyp->GetID())+
                (minimum == faceMinimumTargets.end() ? u8"；下限来源=原生继承" :
                 minimum->second == 0.0 ? u8"；下限来源=NETGEN 原生自动" : u8"；下限来源=本面手动设置"));
            auto& batch = faceBatches[config];
            batch.parameters = faceHyp;
            batch.faces.push_back(item.shape);
        }
        for (const auto& entry : faceBatches) {
            const auto& batch = entry.second;
            TopoDS_Shape scope = batch.faces.front();
            if (batch.faces.size() > 1) {
                // Official SMESH SubMesh on a geometry group. getCollection()
                // requires both the same hypothesis and the same assigned
                // geometry; assigning one object separately to each Face
                // still creates independent surface computations.
                TopoDS_Compound group; BRep_Builder builder; builder.MakeCompound(group);
                for (const auto& face : batch.faces) builder.Add(group, face);
                scope = group;
            }
            auto* algo = make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(), gen));
            requireHypothesis(*mesh, scope, *algo, "NETGEN_2D local Face group");
            requireHypothesis(*mesh, scope, *batch.parameters, "NETGEN local Face group parameters");
            notify(std::string(u8"[SMESH原生面组] 面数量=") + std::to_string(batch.faces.size()) +
                " NativeHypothesisId=" + std::to_string(batch.parameters->GetID()) +
                u8"；使用原生 Group SubMesh，由 SMESH 管理依赖与合并计算。");
        }

        // Manual Wall picks come from the ORIGINAL exported BREP. Re-map through
        // the verified OCC Partition history; a single source Face may have
        // several prepared Face images. Shape IDs are not interchangeable.
        // A shared CAD face can be a physical fluid/solid wall on ONE side.
        // In that case the user must explicitly select the receiving domain;
        // never infer it from the viewing selection or mesh main-shape subset.
        TopTools_IndexedMapOfShape selectedWalls;
        if (request->boundaryLayerEnabled && request->wallCount > 0)
        {
            for (int wi = 0; wi < request->wallCount; ++wi)
            {
                const CaeSMESHViscousFace& wall = request->walls[wi];
                const TopoDS_Shape sourceFace = indexedShape(shape, TopAbs_FACE, wall.shapeIndex, "Viscous Wall");
                if (sourceFace.IsNull())
                    throw std::runtime_error("BL_WALL_SELECTION: Cannot locate selected source CAD face; reselect the Wall after geometry changes");
                const int sourceId = originalFaces.FindIndex(sourceFace);
                // The exported BREP + its validated UI-to-BREP index map
                // provide source identity; explicit topology indices only.
                const auto images = faceImages.find(sourceId);
                if (sourceId <= 0 || images == faceImages.end() || images->second.empty())
                    throw std::runtime_error("BL_WALL_SELECTION: Selected CAD face " + std::to_string(sourceId) +
                        " has no verified image in the prepared Partition; reselect the Wall on current CAD");

                int accepted = 0;
                int outside = 0;
                int ignoredInterfaces = 0;
                int oneSidedInterfaces = 0;
                for (const auto& preparedFace : images->second)
                {
                    const auto owners = fullOwnersOf(preparedFace);
                    if (owners.empty() || owners.size() > 2)
                        throw std::runtime_error("BL_WALL_SELECTION: Source CAD face " + std::to_string(sourceId) +
                            " has an unmapped or non-manifold prepared Face (owners=" +
                            std::to_string(owners.size()) + "); verify Partition topology");
                    std::ostringstream ownerNames;
                    bool firstOwner = true;
                    for (const int owner : owners) {
                        if (!firstOwner) ownerNames << ',';
                        firstOwner = false;
                        ownerNames << owner;
                    }
                    // A target must be present in the Mesh Main Shape and, if
                    // the explicit boundary scope is enabled, in that scope.
                    std::vector<int> targets;
                    for (const int owner : owners)
                        if ((mainSolids.empty() || mainSolids.count(owner)) &&
                            (boundarySolids.empty() || boundarySolids.count(owner)))
                            targets.push_back(owner);

                    notify(std::string(u8"[SMESH Wall映射] 原始BREP面索引=") + std::to_string(sourceId) +
                        u8"；Partition面序号=" + std::to_string(allSolidFaceOwners.FindIndex(preparedFace)) +
                        u8"；Partition所属实体=" + ownerNames.str() +
                        u8"；本次可加层侧=" + std::to_string(targets.size()) +
                        (request->automaticWall ? u8"；用途=排除面" : u8"；用途=手动Wall"));

                    if (request->automaticWall)
                    {
                        // Internal interfaces need no exclusion: the automatic
                        // mode only uses true OUTER assembly faces. An interior
                        // exclusion is harmless, not an invalid Wall request.
                        if (owners.size() != 1) { ++ignoredInterfaces; continue; }
                        if (targets.empty()) { ++outside; continue; }
                        if (request->selectedWallsToIgnore) {
                            selectedWalls.Add(preparedFace);
                            ++accepted;
                        }
                        continue;
                    }
                    if (targets.empty()) { ++outside; continue; }
                    if (owners.size() == 2)
                    {
                        if (boundarySolids.empty())
                            throw std::runtime_error("BL_WALL_SELECTION: Source CAD Wall face " +
                                std::to_string(sourceId) + " is a shared interface (Solid owners=" +
                                ownerNames.str() + "). Enable '仅向指定域内加层' and select exactly "
                                "one receiving fluid/target Solid; the shared face is not necessarily a wrong pick");
                        int explicitlySelectedSides = 0;
                        for (const int owner : owners)
                            if (boundarySolids.count(owner)) ++explicitlySelectedSides;
                        if (explicitlySelectedSides != 1 || targets.size() != 1)
                            throw std::runtime_error("BL_WALL_SELECTION: Source CAD Wall face " +
                                std::to_string(sourceId) + " is shared by Solids " + ownerNames.str() +
                                "; boundary-layer target scope must contain exactly one side");
                        ++oneSidedInterfaces;
                    }
                    selectedWalls.Add(preparedFace);
                    ++accepted;
                }
                notify(std::string(u8"[SMESH Wall映射汇总] 原始CAD面=") + std::to_string(sourceId) +
                    u8"；Partition面=" + std::to_string(images->second.size()) +
                    u8"；有效=" + std::to_string(accepted) +
                    u8"；不在目标域=" + std::to_string(outside) +
                    u8"；自动模式忽略内部面=" + std::to_string(ignoredInterfaces) +
                    u8"；手动单侧内部Wall=" + std::to_string(oneSidedInterfaces));
                if (!request->automaticWall && accepted == 0)
                    throw std::runtime_error("BL_WALL_SELECTION: Selected CAD Wall face " +
                        std::to_string(sourceId) +
                        " has no prepared face inside the active Mesh/ViscousLayers target Solid scope");
            }
        }

        if (parentVolumeDefaults && !session->volumeAlgorithmInstalled)
        {
            auto* algorithm = make(new NETGENPlugin_NETGEN_3D(gen->GetANewId(), gen));
            auto* parameters = make(new NETGENPlugin_Hypothesis(gen->GetANewId(), gen));
            parameters->SetMaxSize(request->maxSize);
            parameters->SetMinSize(std::min(request->minSize, request->maxSize));
            applyNetgenFineness(parameters, request->fineness);
            parameters->SetOptimize(request->optimize != 0);
            parameters->SetSurfaceCurvature(request->surfaceCurvature != 0);
            parameters->SetQuadAllowed(false);
            requireHypothesis(*mesh, mainShape, *algorithm, "NETGEN parent 3D-only algorithm");
            requireHypothesis(*mesh, mainShape, *parameters, "NETGEN parent 3D parameters");
            session->volumeAlgorithmInstalled = true;
            notify(std::string(u8"[SMESH父级有效参数][体] Max=") + std::to_string(parameters->GetMaxSize()) +
                " Min=" + std::to_string(parameters->GetMinSize()) +
                " Fineness=" + std::to_string(int(parameters->GetFineness())) +
                u8"；各实体继承父级，不重复建立算法。");
        }
        std::map<int, int> regions;
        std::int64_t wallFaceCount = 0;
        // Keep the actual applied Wall shape IDs for this netgen-tet session's
        // failed-solid report. Must live in the same scope as the configuring
        // and diagnosing loops below, not in the standalone ViscousLayers API.
        std::map<int, std::set<int>> wallIdsBySolid;
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            if (!mainSolids.empty() && !mainSolids.count(i)) continue;
            const TopoDS_Shape solid = solids(i);
            const int solidShapeId = mesh->GetMeshDS()->ShapeToIndex(solid);
            if (solidShapeId > 0) regions[solidShapeId] = i;
            // The parent algorithm is inherited by default. A Solid override
            // is considered only after its effective settings are resolved.

            double solidMax = request->maxSize;
            const auto localVolume = volumeTargets.find(solidShapeId);
            if (localVolume != volumeTargets.end()) solidMax = localVolume->second.target;

            const auto custom = perSolidOptions.find(i);
            const bool hasCustom = custom != perSolidOptions.end();
            const auto* settings = hasCustom ? &custom->second : nullptr;
            const int solidFineness = settings && settings->fineness >= 0
                ? settings->fineness : request->fineness;
            const bool solidCurvature = settings && settings->surfaceCurvature >= 0
                ? settings->surfaceCurvature != 0 : request->surfaceCurvature != 0;
            const bool solidOptimize = settings && settings->optimize >= 0
                ? settings->optimize != 0 : request->optimize != 0;
            const bool solidAutoMinimum = settings && settings->automaticMinSize >= 0
                ? settings->automaticMinSize != 0 : automaticMinSize;
            double solidMin = request->minSize;
            if (settings && settings->automaticMinSize >= 0)
                solidMin = solidAutoMinimum ? 0.0 : settings->manualMinSize;
            if (solidMin < 0.0 || solidMin > solidMax)
                throw std::invalid_argument("Per-solid minimum size must not exceed target size");
            const auto effectiveGrowth = settings && settings->growthRate > 0.0
                ? settings->growthRate : request->growthRate;
            const auto effectiveEdgeDensity = settings && settings->nbSegPerEdge > 0.0
                ? settings->nbSegPerEdge : request->nbSegPerEdge;
            const auto effectiveRadiusDensity = settings && settings->nbSegPerRadius > 0.0
                ? settings->nbSegPerRadius : request->nbSegPerRadius;
            const bool usesCustomFineness = solidFineness == NETGENPlugin_Hypothesis::UserDefined;
            const MeshNativeDomainPolicy parentPolicy {
                request->maxSize, request->minSize, request->fineness,
                request->surfaceCurvature != 0, request->optimize != 0,
                request->growthRate, request->nbSegPerEdge, request->nbSegPerRadius
            };
            const MeshNativeDomainPolicy solidPolicy {
                solidMax, solidMin, solidFineness, solidCurvature, solidOptimize,
                effectiveGrowth, effectiveEdgeDensity, effectiveRadiusDensity
            };
            const bool inheritedFromParent = parentVolumeDefaults &&
                MeshNativeDomainPolicy::inheritsParent(solidPolicy, parentPolicy, usesCustomFineness);
            if (inheritedFromParent) {
                // No redundant AddHypothesis() on this Solid. Its NETGEN 2D
                // surface policy, when explicitly requested, is independent.
                continue;
            }
            // Every genuinely different Solid owns a native 3D override.
            // Never silently replace the hypothesis of an already computed Solid.
            std::ostringstream configuration;
            configuration << std::setprecision(17) << solidMax << ':' << solidMin << ':'
                      << solidFineness << ':' << solidCurvature << ':' << solidOptimize << ':'
                      << request->growthRate << ':' << request->nbSegPerEdge << ':'
                      << request->nbSegPerRadius << ':' << request->boundaryLayerEnabled << ':'
                      << (settings ? settings->growthRate : -1.0) << ':'
                      << (settings ? settings->nbSegPerEdge : -1.0) << ':'
                      << (settings ? settings->nbSegPerRadius : -1.0);
            configuration << ":surface:" << surfaceSolids.count(i);
            const auto previous = session->solidConfiguration.find(i);
            if (previous != session->solidConfiguration.end()) {
                if (previous->second != configuration.str())
                    throw std::runtime_error("NETGEN settings changed for previously configured SubMesh solid " +
                        std::to_string(i) + "; reset/rebuild the parent Mesh before continuing");
                continue;
            }
            // Configure every parent domain before computing selected children.
            // Adding new surface algorithms after Compute can invalidate the
            // shared edges and already-completed neighbouring volumes.
            const bool discreteVolumeAlgorithm=splitSurfaceAndVolume;
            auto* netgen = discreteVolumeAlgorithm
                ? static_cast<SMESH_Hypothesis*>(make(new NETGENPlugin_NETGEN_3D(gen->GetANewId(), gen)))
                : static_cast<SMESH_Hypothesis*>(make(new NETGENPlugin_NETGEN_2D3D(gen->GetANewId(), gen)));
            auto* netgenHyp = make(new NETGENPlugin_Hypothesis(gen->GetANewId(), gen));
            session->solidConfiguration[i] = configuration.str();
            netgenHyp->SetMaxSize(solidMax);
            netgenHyp->SetMinSize(std::min(solidMin, solidMax));
            applyNetgenFineness(netgenHyp, solidFineness, settings);
            netgenHyp->SetOptimize(solidOptimize);
            netgenHyp->SetSurfaceCurvature(solidCurvature);
            if (!splitSurfaceAndVolume) {
                netgenHyp->SetChordalErrorEnabled(chordalErrorEnabled);
                if (chordalErrorEnabled) netgenHyp->SetChordalError(chordalError);
            }
            if (hasCustom && isSelected(i)) notify(std::string(u8"[SMESH分域参数] 实体 ") + std::to_string(i) +
                " Max=" + std::to_string(solidMax) + " Min=" + (solidAutoMinimum ? "auto" : std::to_string(solidMin)) +
                " Fineness=" + std::to_string(solidFineness) + " Curvature=" + (solidCurvature ? "ON" : "OFF"));
            netgenHyp->SetQuadAllowed(false);
            if (splitSurfaceAndVolume && isSelected(i)) {
                // Legacy requests retain their independent parent surface.
                if(!surfaceSolids.count(i)) notify(std::string(u8"[SMESH参数作用域][实体 ") + std::to_string(i) +
                    u8"] 共享边/面使用父 Mesh 统一 NETGEN 1D-2D 假设（如有显式 Face/Edge 规则则由原生 SubMesh 解析）；本节点尺寸参数仅作用于 NETGEN 3D-only，不再向 Solid 重复绑定 2D 假设。");
            }
            if (isSelected(i))
            {
                // Report the actual native hypothesis AFTER Fineness has expanded
                // into growth/edge/radius settings. The GUI presets are not proof
                // of what NETGEN receives, particularly on older standalone DLLs.
                std::ostringstream effective;
                effective << "[SMESH实际参数][实体 " << i << "][体网格] Max=" << netgenHyp->GetMaxSize()
                          << " Min=" << (solidAutoMinimum ? "auto(native=0)" : std::to_string(netgenHyp->GetMinSize()))
                          << " Fineness=" << static_cast<int>(netgenHyp->GetFineness())
                          << " Growth=" << netgenHyp->GetGrowthRate()
                          << " SegEdge=" << netgenHyp->GetNbSegPerEdge()
                          << " SegRadius=" << netgenHyp->GetNbSegPerRadius()
                          << " Curvature=" << (netgenHyp->GetSurfaceCurvature() ? "ON" : "OFF")
                          << " Optimize=" << (netgenHyp->GetOptimize() ? "ON" : "OFF");
                notify(effective.str());
            }
            requireHypothesis(*mesh, solid, *netgen,
                discreteVolumeAlgorithm ? "NETGEN 3D-only" : "NETGEN 1D-2D-3D");
            requireHypothesis(*mesh, solid, *netgenHyp, "NETGEN parameters");

            if (request->boundaryLayerEnabled && (boundarySolids.empty() || boundarySolids.count(i)))
            {
                std::vector<int> wallIds;
                int sharedFaceCount = 0;
                int externalFaceCount = 0;
                int oneSidedSharedWalls = 0;
                TopTools_IndexedMapOfShape solidFaces;
                TopExp::MapShapes(solid, TopAbs_FACE, solidFaces);
                for (int f = 1; f <= solidFaces.Extent(); ++f)
                {
                    const TopoDS_Shape face = solidFaces(f);
                    const auto globalOwners = fullOwnersOf(face);
                    if (globalOwners.empty() || globalOwners.size() > 2)
                        throw std::runtime_error("BL_WALL_SELECTION: CAD face owner mapping is invalid for Solid " +
                            std::to_string(i) + "; do not generate ViscousLayers on uncertain topology");
                    if (globalOwners.size() == 2) ++sharedFaceCount;
                    else ++externalFaceCount;
                    if (request->automaticWall)
                    {
                        // Automatic Wall means the external skin of the FULL
                        // prepared assembly, never its inner shared interfaces.
                        if (globalOwners.size() != 1) continue;
                        if (request->selectedWallsToIgnore && selectedWalls.Contains(face)) continue;
                    }
                    else
                    {
                        if (!selectedWalls.Contains(face)) continue;
                        if (globalOwners.size() == 2)
                        {
                            // The scope was already validated against the
                            // original->prepared face history above. Apply the
                            // native hypothesis ONLY to this chosen owner.
                            if (boundarySolids.empty() || !boundarySolids.count(i))
                                throw std::runtime_error("BL_WALL_SELECTION: Shared interface must have one explicit ViscousLayers target Solid");
                            ++oneSidedSharedWalls;
                        }
                    }
                    const int faceShapeId = mesh->GetMeshDS()->ShapeToIndex(face);
                    if (faceShapeId > 0) wallIds.push_back(faceShapeId);
                }
                notify(std::string(u8"[SMESH边界层选面] 实体 ") + std::to_string(i) +
                    u8"：装配外表面=" + std::to_string(externalFaceCount) +
                    u8"；内部共享面=" + std::to_string(sharedFaceCount) +
                    u8"；实际加层面=" + std::to_string(wallIds.size()) +
                    u8"；其中明确单侧接口Wall=" + std::to_string(oneSidedSharedWalls) +
                    (request->automaticWall
                        ? u8"；自动模式始终排除内部共享面，请核对入口/出口"
                        : u8"；按手动 Wall 原始CAD→Partition映射"));
                if (!wallIds.empty())
                {
                    wallIdsBySolid[i] = std::set<int>(wallIds.begin(), wallIds.end());
                    auto* viscous = make(new StdMeshers_ViscousLayers(gen->GetANewId(), gen));
                    viscous->SetTotalThickness(request->totalThickness);
                    viscous->SetNumberLayers(request->layers);
                    viscous->SetStretchFactor(request->stretchFactor);
                    viscous->SetBndShapes(wallIds, /*toIgnore=*/false);
                    viscous->SetMethod(static_cast<StdMeshers_ViscousLayers::ExtrusionMethod>(request->extrusionMethod));
                    requireHypothesis(*mesh, solid, *viscous, "ViscousLayers");
                    notify(std::string(u8"[SMESH边界层作用域] 实体 ")+std::to_string(i)+
                        u8"；单侧加层面="+std::to_string(wallIds.size()));
                    wallFaceCount += static_cast<std::int64_t>(wallIds.size());
                }
            }
        }

        if (request->boundaryLayerEnabled && wallFaceCount <= 0)
            throw std::runtime_error("BL_WALL_SELECTION: No usable Wall face in the requested ViscousLayers target domain; inspect CAD Wall mapping and chosen solids");
        if (mesh->NbVolumes() < previouslyMeshedVolumes)
            throw std::runtime_error("SMESH invalidated a completed SubMesh after hypothesis changes; rebuild the parent Mesh before continuing");

        // Read back native inheritance, rather than inferring shared-face
        // settings from the 3D hypothesis or assigning a face owner ourselves.
        if(splitSurfaceAndVolume && session->completed.empty()) {
            SMESH_HypoFilter filter(SMESH_HypoFilter::HasName("NETGEN_Parameters_2D"));
            std::map<int,int> inherited;
            std::map<int,const NETGENPlugin_Hypothesis_2D*> parameters;
            for(int i=1;i<=faceOwners.Extent();++i)if(faceOwners.FindFromIndex(i).Extent()>1) {
                std::list<const SMESHDS_Hypothesis*> used;std::list<TopoDS_Shape> assigned;
                mesh->GetHypotheses(faceOwners.FindKey(i),filter,used,true,&assigned);
                if(used.empty() || assigned.empty())continue;
                const auto* hyp=dynamic_cast<const NETGENPlugin_Hypothesis_2D*>(used.front());
                // Several native Face groups are all "not a Solid". Do not
                // merge them into source 0 and print one group's parameters
                // against the combined count of unrelated shared interfaces.
                if(hyp){++inherited[hyp->GetID()];parameters[hyp->GetID()]=hyp;}
            }
            std::map<int, int> selectedFaceBySource;
            for (int fi = 1; fi <= faceOwners.Extent(); ++fi) {
                const auto& owners = faceOwners.FindFromIndex(fi);
                if (owners.Extent() != 2) continue;
                bool current = false;
                for (TopTools_ListIteratorOfListOfShape it(owners); it.More(); it.Next())
                    if (selectedSolids.count(solids.FindIndex(it.Value()))) current = true;
                if (!current) continue;
                std::list<const SMESHDS_Hypothesis*> used;
                std::list<TopoDS_Shape> assigned;
                mesh->GetHypotheses(faceOwners.FindKey(fi), filter, used, true, &assigned);
                if (used.empty() || assigned.empty()) continue;
                const auto* hyp=dynamic_cast<const NETGENPlugin_Hypothesis_2D*>(used.front());
                if(hyp)++selectedFaceBySource[hyp->GetID()];
            }
            for (const auto& source : selectedFaceBySource) {
                const std::string owner = std::string("NETGEN_Parameters_2D #")+std::to_string(source.first);
                notify(std::string(u8"[SMESH所选域共享面来源] 假设来源=") + owner +
                    u8"；共享面数量=" + std::to_string(source.second));
            }
            for(const auto& entry:inherited) {
                const auto* hyp=parameters[entry.first];
                const std::string owner = std::string("NETGEN_Parameters_2D #")+std::to_string(entry.first);
                notify(std::string(u8"[SMESH共享面参数] 来源=")+owner+
                    " Shared="+std::to_string(entry.second)+" Max="+std::to_string(hyp->GetMaxSize())+
                    " Fineness="+std::to_string(int(hyp->GetFineness()))+
                    " Curvature="+(hyp->GetSurfaceCurvature()?"ON":"OFF"));
            }
        }

        // Keep the native NETGEN algorithms reachable by the UI cancel action
        // while SMESH_Gen::Compute() is blocking this worker thread. They
        // belong to this native session and must be unregistered before it
        // can be destroyed/replaced.
        NativeNetgenCancellationScope activeCancellation(session->hypotheses);
        const auto requireNotCancelled = [&]() {
            if (g_netgenCancelRequested.load(std::memory_order_acquire))
                throw std::runtime_error("SMESH_CANCELLED: NETGEN native cancel requested");
        };
        requireNotCancelled();
        const auto computeStarted = std::chrono::steady_clock::now();
        std::vector<std::pair<int, double>> slowSolids;
        slowSolids.reserve(static_cast<std::size_t>(solids.Extent()));
        std::vector<std::pair<int, std::int64_t>> largestSolids;
        largestSolids.reserve(static_cast<std::size_t>(solids.Extent()));
        bool largeMeshWarningSent = false;
        auto surfacePreflight=[&](const TopoDS_Shape& target,int flags) {
            std::map<std::string,int> inheritedSurfaceSettings;
            TopTools_MapOfShape inspectedFaces;
            for(TopExp_Explorer ex(target,TopAbs_FACE);ex.More();ex.Next()) {
                if(!inspectedFaces.Add(ex.Current()))continue;
                std::list<const SMESHDS_Hypothesis*> inherited;
                mesh->GetHypotheses(ex.Current(),SMESH_HypoFilter(SMESH_HypoFilter::HasName("NETGEN_Parameters_2D")),inherited,true);
                if(inherited.empty())continue;
                const auto* hyp=dynamic_cast<const NETGENPlugin_Hypothesis_2D*>(inherited.front());
                if(!hyp)continue;
                std::ostringstream settings;settings<<"Hyp="<<hyp->GetID()<<" Max="<<hyp->GetMaxSize()
                    <<" Min="<<hyp->GetMinSize()<<" Growth="<<hyp->GetGrowthRate()
                    <<" SegRadius="<<hyp->GetNbSegPerRadius()<<" Curvature="<<hyp->GetSurfaceCurvature();
                ++inheritedSurfaceSettings[settings.str()];
            }
            for(const auto& setting:inheritedSurfaceSettings)
                notify(std::string(u8"[SMESH表面有效假设] ")+setting.first+" CADFaces="+std::to_string(setting.second));
            // Official dimension-limited preview. Retain the native 2D mesh
            // and its dependency state for the following 3D Compute.
            // SMESH_Gen checks aDim only when the preview shape set is supplied.
            // A null set means a normal (unlimited-dimension) Compute.
            const auto volumesBefore=mesh->NbVolumes();
            TSetOfInt previewShapes;
            const bool surfaceComputed = gen->Compute(*mesh,target,flags,::MeshDim_2D,&previewShapes);
            requireNotCancelled();
            if (!surfaceComputed)
                throw std::runtime_error("SMESH surface preview failed"+computeError(*mesh,target));
            if(mesh->NbVolumes()!=volumesBefore)
                throw std::runtime_error("SMESH surface preview unexpectedly changed volume elements");
            const auto count=static_cast<std::int64_t>(mesh->NbFaces());
            if(surfaceElements)*surfaceElements=count;
            notify(std::string(u8"[SMESH表面预检] 实际表面单元=")+std::to_string(count)+
                u8"；尚未进行本批体填充。");
            std::map<int,std::int64_t> faceCounts;
            auto faces=mesh->GetMeshDS()->facesIterator();
            while(faces->more()) {const auto* face=faces->next();if(face)++faceCounts[face->GetShapeID()];}
            std::map<int,std::int64_t> domainCounts;
            for(int f=1;f<=faceOwners.Extent();++f) {
                const int shapeId=mesh->GetMeshDS()->ShapeToIndex(faceOwners.FindKey(f));
                const auto found=faceCounts.find(shapeId);if(found==faceCounts.end())continue;
                for(TopTools_ListIteratorOfListOfShape it(faceOwners.FindFromIndex(f));it.More();it.Next()) {
                    const int domain=solids.FindIndex(it.Value());
                    if(domain>0 && isSelected(domain))domainCounts[domain]+=found->second;
                }
            }
            for(const auto& entry:domainCounts)notify(std::string(u8"[SMESH域表面数量] 实体 ")+std::to_string(entry.first)+
                " Faces="+std::to_string(entry.second)+u8"（含本域共享接口）。");
            std::vector<std::pair<std::int64_t,int>> hotspots;
            for(const auto& entry:faceCounts)hotspots.emplace_back(entry.second,entry.first);
            std::sort(hotspots.begin(),hotspots.end(),std::greater<std::pair<std::int64_t,int>>());
            for(std::size_t n=0;n<std::min<std::size_t>(5,hotspots.size());++n) {
                std::string sources;
                for(const auto& source:session->faceImages)for(const auto& image:source.second)
                    if(mesh->GetMeshDS()->ShapeToIndex(image)==hotspots[n].second) {
                        if(!sources.empty())sources+=',';sources+=std::to_string(source.first);break;
                    }
                notify(std::string(u8"[SMESH表面热点] CAD面=")+sources+" ShapeId="+std::to_string(hotspots[n].second)+
                    " Cells="+std::to_string(hotspots[n].first));
            }
            if(surfaceElementLimit>0 && count>surfaceElementLimit)
                throw std::runtime_error(std::string("SURFACE_ELEMENT_LIMIT: ")+u8"表面单元 "+std::to_string(count)+
                    u8" 超过设置上限 "+std::to_string(surfaceElementLimit)+u8"，已在体填充前停止。请先检查各域表面尺寸、曲面精度与局部面/边规则；最大尺寸不能覆盖几何细节约束。");
            result->nodes=mesh->NbNodes();
        };
        // A full build is one native parent Compute, just as Mesh.Compute() in
        // SALOME. SHAPE_ONLY is reserved for an explicitly selected incremental
        // child; imposing an application solid order on a full build prevents
        // SMESH from scheduling/collecting multi-face algorithms at root scope.
        // Compute a requested region group as ONE native SubMesh. Separate
        // Solid SHAPE_ONLY calls fragment a group-owned 1D-2D surface collection.
        // SMESH supports groups as SubMesh shapes; it schedules their dependencies.
        auto recordFailedSolid=[&](int index) {
            const auto path=utf8Path(request->vtkPathUtf8).parent_path()/
                ("failed_solid_"+std::to_string(index)+".brep");
            std::ofstream output(path);if(output)BRepTools::Write(solids(index),output);
            try{dumpFailedSurface(*mesh,solids(index),path.parent_path()/
                ("failed_solid_"+std::to_string(index)+".surface.vtk"));}
            catch(const std::exception& e){notify(std::string("[SMESH diagnostic surface] ")+e.what());}
            notify(std::string(u8"[SMESH失败域] 实体 ")+std::to_string(index)+
                " ShapeId="+std::to_string(mesh->GetMeshDS()->ShapeToIndex(solids(index)))+
                u8"；已保存 CAD 与失败表面："+path.u8string());
        };
        const bool nativeLayerPreparation=request->boundaryLayerEnabled && splitSurfaceAndVolume;
        if(nativeLayerPreparation&&!surfaceOnly)
            for(const auto& entry:wallIdsBySolid)
                if(!session->completed.count(entry.first)&&!isSelected(entry.first))
                    throw std::runtime_error(u8"BL_TARGET_SCOPE: 请先同时选择尚未生成的边界层目标域，或从总节点生成；原生边界层需在相邻域体填充前准备共享端面。");
        bool layeredScopeComputed=false;
        auto computeLayeredScope=[&] {
            if(!nativeLayerPreparation||layeredScopeComputed||wallIdsBySolid.empty())return;
            // Public SubMesh Compute, then parent Compute. SMESH itself builds
            // layers, consumes proxies and fills their core. No SDK extension
            // or direct access to the native builder/proxy caches is involved.
            TopoDS_Compound group;BRep_Builder builder;builder.MakeCompound(group);
            for(const auto& entry:wallIdsBySolid)builder.Add(group,solids(entry.first));
            notify(u8"[SMESH边界层子网格] 先使用公开 Compute 计算加层域组，再由父 Mesh 计算其余域；层与体填充均由原版 SMESH 执行。");
            if(!gen->Compute(*mesh,group,SMESH_Gen::SHAPE_ONLY)) {
                for(const auto& entry:wallIdsBySolid) {
                    const auto failure=mesh->GetSubMesh(solids(entry.first))->GetComputeError();
                    if(failure&&!failure->IsOK())recordFailedSolid(entry.first);
                }
                throw std::runtime_error("SMESH layered SubMesh Compute failed"+computeError(*mesh,group));
            }
            layeredScopeComputed=true;
        };
        const MeshNativeComputePolicy computePolicy = MeshNativeComputePolicy::resolve(
            partial, !surfaceSolids.empty(), surfaceOnly, surfaceElementLimit > 0,
            nativeLayerPreparation, static_cast<int>(selectedSolids.size()),
            !session->completed.empty());
        const bool groupedSelection = computePolicy.selectedGroup;
        const bool parentCompute = computePolicy.parentCompute;
        if (computePolicy.standardRoot)
            notify(u8"[SMESH标准计算] 普通完整网格：一次父 Mesh Compute；1D/2D/3D 依赖由原生 SMESH 调度。");
        if (parentCompute) {
            TopoDS_Shape computeShape=mainShape;
            if(groupedSelection) {
                TopoDS_Shape selectedScope;
                const auto knownScope=session->surfaceScopes.find(*selectedSolids.begin());
                if(knownScope!=session->surfaceScopes.end()) {
                    TopTools_IndexedMapOfShape members;TopExp::MapShapes(knownScope->second,TopAbs_SOLID,members);
                    bool same=members.Extent()==int(selectedSolids.size());
                    for(int id:selectedSolids)same=same && members.Contains(solids(id));
                    if(same)selectedScope=knownScope->second;
                }
                if(!selectedScope.IsNull())computeShape=selectedScope;
                else {
                    TopoDS_Compound group;BRep_Builder builder;builder.MakeCompound(group);
                    for(int id:selectedSolids)builder.Add(group,solids(id));
                    computeShape=group;
                }
                notify(u8"[SMESH原生调度] 所选域作为一个几何组 SubMesh 计算；不逐体拆开共享表面依赖。");
            } else notify(nativeLayerPreparation
                ?u8"[SMESH原生调度] 加层域先计算原生子网格，随后父 Mesh Compute 复用已完成部分；共享面依赖由 SMESH 调度。"
                :u8"[SMESH原生调度] 全部域使用一次父 Mesh Compute；共享面和子网格依赖由 SMESH 调度。");
            if (!gen->CheckAlgoState(*mesh, computeShape))
                throw std::runtime_error("SMESH NETGEN hypothesis state is invalid on parent Mesh");
            // A normal full parent Compute already schedules all lower-
            // dimensional dependencies in trelau/SMESH. Calling a separate
            // MeshDim_2D Compute for ordinary Solid surface scopes is an extra
            // pass and can leave a different native compute state. Preserve
            // the existing explicit preview/limits/BL and scoped group paths.
            if(computePolicy.surfacePreflight) {
                surfacePreflight(computeShape,groupedSelection?SMESH_Gen::SHAPE_ONLY:SMESH_Gen::COMPACT_MESH);
                if(surfaceOnly)return 0;
            }
            requireNotCancelled();
            computeLayeredScope();
            requireNotCancelled();
            const bool computed=groupedSelection?gen->Compute(*mesh,computeShape,SMESH_Gen::SHAPE_ONLY)
                :gen->Compute(*mesh,computeShape);
            requireNotCancelled();
            if (!computed) {
                bool failedLayerDomain=false,failedOtherDomain=false;
                for(int i=1;i<=solids.Extent();++i)if(isSelected(i)) {
                    const auto failure=mesh->GetSubMesh(solids(i))->GetComputeError();
                    if(failure&&!failure->IsOK()) {
                        recordFailedSolid(i);
                        if(wallIdsBySolid.count(i))failedLayerDomain=true;
                        else failedOtherDomain=true;
                    }
                }
                const std::string category=request->boundaryLayerEnabled&&failedOtherDomain&&!failedLayerDomain
                    ? "SMESH_NON_LAYERED_DOMAIN_FAILURE: " : "";
                throw std::runtime_error(category+"SMESH NETGEN parent Mesh failed" + computeError(*mesh, computeShape));
            }
        }
        for (int i = 1; i <= solids.Extent(); ++i)
        {
            if (!isSelected(i)) continue;
            if (session->completed.count(i)) {
                notify(std::string(u8"[SMESH增量] 实体 ") + std::to_string(i) +
                    u8" 已计算，跳过重复剖分。");
                continue;
            }
            if (g_netgenCancelRequested.load(std::memory_order_acquire))
                throw std::runtime_error("SMESH_CANCELLED: cancelled before next solid");
            const auto currentSolidStart = std::chrono::steady_clock::now();
            const TopoDS_Shape solid = solids(i);
            const std::int64_t beforeVolumes = static_cast<std::int64_t>(mesh->NbVolumes());
            if (!parentCompute)
                notify(std::string(u8"[SMESH] 正在划分几何体 ") + std::to_string(i) + "/" + std::to_string(solids.Extent()));
            // Compute the selected Solid through SMESH's dependency engine.
            // It schedules inherited 1D-2D surface algorithms and 3D-only fill,
            // and reuses completed shared submeshes in the parent Mesh.
            // Computing each Face with SHAPE_ONLY bypasses this native scope.
            if (!gen->CheckAlgoState(*mesh, solid))
                throw std::runtime_error("SMESH NETGEN hypothesis state is invalid on solid " + std::to_string(i));
            if(!parentCompute && computePolicy.surfacePreflight) {
                surfacePreflight(solid,SMESH_Gen::SHAPE_ONLY);
                if(surfaceOnly)return 0;
            }
            if(!parentCompute)computeLayeredScope();
            requireNotCancelled();
            const bool solidComputed = parentCompute
                ? mesh->GetSubMesh(solid)->IsMeshComputed()
                : gen->Compute(*mesh, solid, SMESH_Gen::SHAPE_ONLY);
            requireNotCancelled();
            if (!solidComputed)
            {
                const auto diagnostic = utf8Path(request->vtkPathUtf8).parent_path() /
                    ("failed_solid_" + std::to_string(i) + ".brep");
                std::ofstream failed(diagnostic); if (failed) BRepTools::Write(solid, failed);
                try {
                    dumpFailedSurface(*mesh, solid,
                        diagnostic.parent_path() / ("failed_solid_" + std::to_string(i) + ".surface.vtk"));
                } catch (const std::exception& e) {
                    notify(std::string("[SMESH diagnostic surface] ") + e.what());
                }
                if (request->boundaryLayerEnabled) {
                    // Publish a compact per-face audit. Shape IDs are the SAME
                    // SMESH IDs used in native messages (e.g. "face 16901").
                    // A mesh failure does not prove any single face is at fault.
                    const auto faceReport = diagnostic.parent_path() /
                        ("failed_solid_" + std::to_string(i) + ".boundary_faces.csv");
                    std::ofstream csv(faceReport);
                    if (csv) {
                        csv << "smesh_face_shape_id,is_viscous_wall,full_cad_owner_count,owner_solid_indices,source_brep_face_indices\n";
                        // Reverse the verified OCC image map, so a failed SMESH
                        // face can be traced back to the CAD face IDs shown in
                        // the manual Wall selector (a prepared face can have
                        // more than one original predecessor).
                        std::map<int, std::set<int>> sourceIndicesByShapeId;
                        for (const auto& entry : faceImages)
                            for (const auto& prepared : entry.second) {
                                const int id = mesh->GetMeshDS()->ShapeToIndex(prepared);
                                if (id > 0) sourceIndicesByShapeId[id].insert(entry.first);
                            }
                        TopTools_IndexedMapOfShape failedFaces;
                        TopExp::MapShapes(solid, TopAbs_FACE, failedFaces);
                        const auto applied = wallIdsBySolid.find(i);
                        for (int f = 1; f <= failedFaces.Extent(); ++f) {
                            const TopoDS_Shape& face = failedFaces(f);
                            const int id = mesh->GetMeshDS()->ShapeToIndex(face);
                            const auto owners = fullOwnersOf(face);
                            csv << id << ',' <<
                                (applied != wallIdsBySolid.end() && applied->second.count(id) ? 1 : 0) << ',' <<
                                owners.size() << ',';
                            bool first = true;
                            for (int owner : owners) {
                                if (!first) csv << ';';
                                first = false;
                                csv << owner;
                            }
                            csv << ',';
                            first = true;
                            for (int source : sourceIndicesByShapeId[id]) {
                                if (!first) csv << ';';
                                first = false;
                                csv << source;
                            }
                            csv << '\n';
                        }
                        notify(std::string(u8"[SMESH边界层诊断] 失败实体 ") + std::to_string(i) +
                            u8" 的 Wall/非 Wall/共享面明细：" + faceReport.u8string() +
                            u8"；按 face/shape 编号检查 CAD 面。若编号不在表内，可能是计算产生的临时面。");
                    }
                }
                const auto nativeFailure=computeError(*mesh,solid);
                const auto layersForSolid=wallIdsBySolid.find(i);
                const bool solidHasLayers=layersForSolid!=wallIdsBySolid.end() && !layersForSolid->second.empty();
                if(request->boundaryLayerEnabled && !solidHasLayers) {
                    // A mixed BL/non-BL request is not a layer failure on every
                    // Solid. Keep the native details, but do not make the GUI
                    // repeat the entire mesh with unrelated extrusion methods.
                    notify(std::string(u8"[SMESH原生诊断][未加层实体 ")+std::to_string(i)+"]"+nativeFailure);
                    auto report=diagnostic;report += ".error.txt";
                    {std::ofstream out(report);out<<nativeFailure;}
                    const bool overlap=nativeFailure.find("boundary mesh is overlapping")!=std::string::npos;
                    throw std::runtime_error(std::string("SMESH_NON_LAYERED_DOMAIN_FAILURE: ")+u8"实体 "+std::to_string(i)+
                        (overlap?u8" 未加边界层，其表面网格重叠，无法生成体网格。":u8" 未加边界层，其普通体网格计算失败。")+
                        u8"无需更换边界层挤出方式重复计算；原生详情见日志及 "+report.u8string()+
                        "; diagnostic BREP="+diagnostic.u8string());
                }
                throw std::runtime_error(std::string(solidHasLayers
                    ? "SMESH NETGEN + ViscousLayers failed on solid "
                    : "SMESH NETGEN failed on solid ") + std::to_string(i) + nativeFailure
                    + "; diagnostic BREP=" + diagnostic.u8string());
            }
            session->completed.insert(i);
            if (g_netgenCancelRequested.load(std::memory_order_acquire))
                throw std::runtime_error("SMESH_CANCELLED: cancelled after current solid");
            const std::int64_t nowVolumes = static_cast<std::int64_t>(mesh->NbVolumes());
            std::int64_t addedVolumes = std::max<std::int64_t>(0, nowVolumes - beforeVolumes);
            if (parentCompute) {
                addedVolumes = 0;
                auto* sub = mesh->GetSubMesh(solid)->GetSubMeshDS();
                if (sub) {
                    auto elements = sub->GetElements();
                    while (elements->more())
                        if (elements->next()->GetType() == SMDSAbs_Volume) ++addedVolumes;
                }
            }
            const int solidShapeId = mesh->GetMeshDS()->ShapeToIndex(solid);
            double effectiveMaxSize = request->maxSize;
            const auto localSolidSize = volumeTargets.find(solidShapeId);
            if (localSolidSize != volumeTargets.end()) effectiveMaxSize = localSolidSize->second.target;
            if (!parentCompute)
                slowSolids.emplace_back(i,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - currentSolidStart).count());
            largestSolids.emplace_back(i, addedVolumes);
            notify(std::string(u8"[SMESH] 已完成几何体 ") + std::to_string(i) + "/" + std::to_string(solids.Extent())
                + u8"；本体新增 " + std::to_string(addedVolumes)
                + u8"；累计体单元 " + std::to_string(nowVolumes)
                + u8"；本体尺寸上限 " + std::to_string(effectiveMaxSize));
            if (addedVolumes >= 100000)
                notify(std::string(u8"[SMESH网格量提醒] 几何体 ") + std::to_string(i)
                    + u8" 单独新增 " + std::to_string(addedVolumes)
                    + u8" 个体单元；请优先检查该实体的 CAD 小特征与局部尺寸。 ");
            if (!largeMeshWarningSent && nowVolumes >= 1000000) {
                largeMeshWarningSent = true;
                notify(std::string(u8"[SMESH网格量提醒] 累计体单元已超过 100 万，仍将继续计算；需要限量时请主动停止并检查逐实体日志。"));
            }
        }

        if (g_netgenCancelRequested.load(std::memory_order_acquire))
            throw std::runtime_error("SMESH_CANCELLED: cancelled before mesh export");
        const auto computeFinished = std::chrono::steady_clock::now();
        std::sort(largestSolids.begin(), largestSolids.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t k = 0; k < std::min<std::size_t>(5, largestSolids.size()); ++k)
            notify(std::string(u8"[SMESH网格热点] 第 ") + std::to_string(k + 1) + u8" 名：几何体 "
                + std::to_string(largestSolids[k].first) + u8"，新增体单元 "
                + std::to_string(largestSolids[k].second));
        // Fail closed: a native shared Mesh is necessary but not sufficient.
        // Every triangle on an interface between two *completed* domains must
        // be the face of exactly two volume cells, one from each domain, using
        // the very same SMDS node IDs (not merely coincident coordinates).
        using TetFacet = std::array<smIdType,5>;
        struct TetInterface { int faceId = 0; int left = 0; int right = 0; std::vector<int> actual; };
        std::map<TetFacet,TetInterface> expectedInterfaces;
        auto facetKey = [](const SMDS_MeshNode* const* nodes, int count) {
            TetFacet key{};
            if (count < 3 || count > 4)
                throw std::runtime_error("Only tri/quad SMESH interface facets are supported");
            key[0] = count;
            for (int n = 0; n < count; ++n) key[n+1] = nodes[n]->GetID();
            std::sort(key.begin() + 1, key.begin() + 1 + count);
            return key;
        };
        std::int64_t sharedCompletedFaces = 0;
        for (int i = 1; i <= faceOwners.Extent(); ++i) {
            const auto& owningSolids = faceOwners.FindFromIndex(i);
            if (owningSolids.Extent() != 2) continue;
            TopTools_ListIteratorOfListOfShape it(owningSolids);
            const int first = solids.FindIndex(it.Value()); it.Next();
            const int second = solids.FindIndex(it.Value());
            if (!session->completed.count(first) || !session->completed.count(second)) continue;
            const TopoDS_Shape& face = faceOwners.FindKey(i);
            auto* faceSubmesh = mesh->GetSubMesh(face)->GetSubMeshDS();
            if (!faceSubmesh || faceSubmesh->NbElements() == 0)
                throw std::runtime_error("No mesh elements on CAD interface face between solids " +
                    std::to_string(first) + "/" + std::to_string(second));
            ++sharedCompletedFaces;
            auto itFace = faceSubmesh->GetElements();
            while (itFace->more()) {
                const SMDS_MeshElement* element = itFace->next();
                if (!element || element->GetType() != SMDSAbs_Face) continue;
                std::vector<const SMDS_MeshNode*> nodes;
                for (int n = 0; n < element->NbNodes(); ++n) nodes.push_back(element->GetNode(n));
                TetInterface item;
                item.faceId = mesh->GetMeshDS()->ShapeToIndex(face);
                item.left = mesh->GetMeshDS()->ShapeToIndex(solids(first));
                item.right = mesh->GetMeshDS()->ShapeToIndex(solids(second));
                if (!expectedInterfaces.emplace(facetKey(nodes.data(), int(nodes.size())), item).second)
                    throw std::runtime_error("Duplicate SMESH interface facet on CAD face " + std::to_string(item.faceId));
            }
        }
        if (sharedCompletedFaces > 0 && expectedInterfaces.empty())
            throw std::runtime_error("Shared CAD interface exists but no 2D interface facets were generated");
        if (!expectedInterfaces.empty()) {
            auto cellIt = mesh->GetMeshDS()->volumesIterator();
            while (cellIt->more()) {
                const SMDS_MeshElement* cell = cellIt->next();
                if (!cell) continue;
                SMDS_VolumeTool tool(cell);
                for (int f = 0; f < tool.NbFaces(); ++f) {
                    auto hit = expectedInterfaces.find(facetKey(tool.GetFaceNodes(f), tool.NbFaceNodes(f)));
                    if (hit != expectedInterfaces.end()) hit->second.actual.push_back(cell->GetShapeID());
                }
            }
        }
        std::int64_t badInterfaceFacets = 0;
        std::int64_t missingSecondVolume = 0;
        std::int64_t wrongVolumeOwnership = 0;
        for (const auto& kv : expectedInterfaces) {
            const auto& item = kv.second;
            if (item.actual.size() == 2 &&
                ((item.actual[0] == item.left && item.actual[1] == item.right) ||
                 (item.actual[1] == item.left && item.actual[0] == item.right))) continue;
            ++badInterfaceFacets;
            if (item.actual.size() != 2) ++missingSecondVolume;
            else ++wrongVolumeOwnership;
            if (badInterfaceFacets <= 5)
                notify(std::string(u8"[SMESH共形错误] CAD 面 ") + std::to_string(item.faceId) +
                    u8" 的接口单元两侧匹配失败，邻接体单元=" + std::to_string(item.actual.size()));
        }
        notify(std::string(u8"[SMESH共形检查] 已完成域共享 CAD 面 ") +
            std::to_string(sharedCompletedFaces) + u8" 个，接口单元 " +
            std::to_string(expectedInterfaces.size()) + u8"，不匹配 " +
            std::to_string(badInterfaceFacets) +
            u8"（缺少两侧体面=" + std::to_string(missingSecondVolume) +
            u8"，归属异常=" + std::to_string(wrongVolumeOwnership) + u8"）");
        if (sharedCompletedFaces == 0 && session->completed.size() <= 1)
            notify(u8"[SMESH共形未验证] 当前只完成一个实体，没有两侧均已计算的共享接口。请把相邻的两个实体一起试划分，再判断面加密是否保持共形。");
        else if (sharedCompletedFaces == 0)
            notify(u8"[SMESH共形未验证] 已完成多个实体但没有共同 CAD 接口；0 个不匹配不代表相邻接触面共形。请检查 OCC Partition 和接触拓扑。");
        else if (badInterfaceFacets == 0)
            notify(std::string(u8"[SMESH共形已验证] ") + std::to_string(sharedCompletedFaces) +
                u8" 个共享 CAD 面、" + std::to_string(expectedInterfaces.size()) +
                u8" 个接口单元复用相同节点，两侧体单元归属正确。");
        if (badInterfaceFacets)
            throw std::runtime_error("Non-conformal NETGEN interface: adjacent domains do not reuse the same face nodes/facets; cumulative mesh NOT published");

        // Export all completed volumes, not just the most recent SubMesh.
        regions.clear();
        for (int index : session->completed) {
            const int shapeId = mesh->GetMeshDS()->ShapeToIndex(solids(index));
            if (shapeId <= 0) throw std::runtime_error("Computed solid missing ShapeID");
            regions[shapeId] = index;
        }
        notify(std::string(persistentKey.empty()?u8"[SMESH完成] 当前父 Mesh 已完成 ":u8"[SMESH增量] 当前父 Mesh 累积完成 ") +
            std::to_string(session->completed.size()) + "/" + std::to_string(mainCount) +
            (persistentKey.empty()?u8" 个实体；导出本次网格。":u8" 个实体；导出累积网格。"));
        result->nodes = mesh->NbNodes();
        result->solids = static_cast<std::int64_t>(session->completed.size());
        // Report user-defined controls, not internal boundary refinements.
        result->localEdges = static_cast<std::int64_t>(manualEdgeSubmeshCount);
        result->localFaces = static_cast<std::int64_t>(faceTargets.size());
        result->localVolumes = static_cast<std::int64_t>(volumeTargets.size());
        result->wallFaces = wallFaceCount;
        result->volumes = mesh->NbVolumes();
        result->tetrahedra = mesh->NbTetras();
        result->prisms = mesh->NbPrisms();
        result->pyramids = mesh->NbPyramids();
        result->hexahedra = mesh->NbHexas();
        result->actualFirstHeight = request->boundaryLayerEnabled ? request->firstHeight : 0.0;
        const std::int64_t known = result->tetrahedra + result->prisms +
                                   result->pyramids + result->hexahedra;
        result->otherVolumes = std::max<std::int64_t>(0, result->volumes - known);
        if (result->volumes <= 0) throw std::runtime_error("SMESH NETGEN produced no 3D cells");
        if (request->boundaryLayerEnabled && result->prisms <= 0)
            throw std::runtime_error("SMESH ViscousLayers produced no Prism cells");

        // Compare generated 3D cell volumes with each OCC solid for diagnostics.
        // A large CAD/mesh volume deviation can reveal under-resolved geometry,
        // but it is not a NETGEN Compute failure. Report it without rejecting
        // an otherwise successfully generated mesh; keep structural checks below.
        {
            std::map<int, int> regionToSolid;
            std::map<int, double> cadVolumes;
            for (int i = 1; i <= solids.Extent(); ++i)
            {
                if (!session->completed.count(i)) continue;
                const int id = mesh->GetMeshDS()->ShapeToIndex(solids(i));
                if (id <= 0) throw std::runtime_error("NETGEN CAD volume has no ShapeID");
                GProp_GProps p;
                BRepGProp::VolumeProperties(solids(i), p);
                const double cadVolume = std::abs(p.Mass());
                if (!std::isfinite(cadVolume) || cadVolume <= 0.0)
                    throw std::runtime_error("Invalid CAD solid volume for solid " + std::to_string(i));
                regionToSolid.emplace(id, i);
                cadVolumes.emplace(id, cadVolume);
            }
            std::map<int, double> meshVolumes;
            std::int64_t orphanCells = 0;
            std::int64_t invalidCells = 0;
            std::ofstream invalidReport;
            auto cells = mesh->GetMeshDS()->volumesIterator();
            while (cells->more())
            {
                const auto* cell = cells->next();
                if (!cell) continue;
                if (!regionToSolid.count(cell->GetShapeID())) { ++orphanCells; continue; }
                SMDS_VolumeTool tool(cell);
                const double v = tool.GetSize();
                if (!std::isfinite(v) || v <= 0.0) {
                    ++invalidCells;
                    if(invalidCells<=20) {
                        if(!invalidReport.is_open()) {
                            auto path=utf8Path(request->vtkPathUtf8);path += ".invalid-cells.txt";
                            invalidReport.open(path);invalidReport<<std::setprecision(17);
                        }
                        const int solid=regionToSolid.at(cell->GetShapeID());
                        std::ostringstream detail;
                        detail<<std::setprecision(17)<<"[SMESH invalid volume] cell="<<cell->GetID()
                              <<" solid="<<solid<<" nodes="<<cell->NbNodes()<<" signedVolume="<<v;
                        notify(detail.str());invalidReport<<detail.str()<<'\n';
                        for(int n=0;n<cell->NbNodes();++n) {
                            const auto* node=cell->GetNode(n);
                            invalidReport<<node->GetID()<<' '<<node->X()<<' '<<node->Y()<<' '<<node->Z()<<'\n';
                        }
                    }
                    continue;
                }
                meshVolumes[cell->GetShapeID()] += v;
            }
            std::vector<std::pair<double, int>> deviations;
            double largest = 0.0;
            int worstSolid = 0;
            for (const auto& kv : cadVolumes)
            {
                const double actual = meshVolumes[kv.first];
                const double errorRatio = std::abs(actual - kv.second) / kv.second;
                deviations.emplace_back(errorRatio, regionToSolid.at(kv.first));
                if (errorRatio > largest) { largest = errorRatio; worstSolid = regionToSolid.at(kv.first); }
            }
            std::sort(deviations.begin(), deviations.end(), std::greater<std::pair<double, int>>());
            for (int i = 0; i < std::min<int>(5, deviations.size()); ++i)
            {
                std::ostringstream m;
                m << "[SMESH几何核对] 实体 " << deviations[i].second
                  << " CAD/网格体积相对偏差=" << std::fixed << std::setprecision(2)
                  << (deviations[i].first * 100.0) << "%";
                notify(m.str());
            }
            if (orphanCells > 0 || invalidCells > 0)
                throw std::runtime_error("NETGEN mesh volume check failed: unassigned 3D cells="
                    + std::to_string(orphanCells) + ", invalid cell volume=" + std::to_string(invalidCells));
            // CAD/mesh volume deviation is a user-facing quality warning only.
            // Never reject or suppress mesh export solely for crossing a custom
            // percentage threshold. NETGEN/SMESH native failures and invalid or
            // unassigned 3D cells are handled separately above.
            if (largest > 0.10)
            {
                std::ostringstream m;
                m << "[SMESH几何核对][警告] 实体 " << worstSolid
                  << " 的 CAD/网格体积相对偏差=" << std::fixed << std::setprecision(2)
                  << largest * 100.0
                  << "%（超过 10%，仅提示，不阻止网格导出）；请检查几何曲面、孔槽和网格尺寸。";
                notify(m.str());
            }
            else if (largest > 0.02)
                notify("[SMESH几何核对][警告] 存在超过 2% 的 CAD/网格体积偏差；仅提示，不阻止网格导出。");
        }

        if (elementOrder == 2) {
            SMESH_MeshEditor(mesh).ConvertToQuadratic(false, false);
            result->nodes = mesh->NbNodes();
        }
        const std::filesystem::path finalVtk = utf8Path(request->vtkPathUtf8);
        std::filesystem::path pendingVtk = finalVtk;
        pendingVtk += ".pending";
        std::vector<std::pair<int,int>> faceBrepMap;
        for(const auto& entry:session->faceImages)for(const auto& image:entry.second) {
            const int id=mesh->GetMeshDS()->ShapeToIndex(image);if(id>0)faceBrepMap.emplace_back(id,entry.first);
        }
        writeNetgenBinaryVtk(*mesh, pendingVtk, regions,
                             /*markPrismsAsBoundaryLayer=*/request->boundaryLayerEnabled != 0,faceBrepMap);
        // The official/preview result must never be truncated by an unfinished
        // batch. Replace it only after a complete, validated VTK was written.
#ifdef _WIN32
        if (!MoveFileExW(pendingVtk.c_str(), finalVtk.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Cannot commit cumulative NETGEN VTK output");
#else
        std::filesystem::rename(pendingVtk, finalVtk);
#endif
        const auto exportFinished = std::chrono::steady_clock::now();
        // Profile next to output; don't modify bridge ABI or existing mesh formats.
        if (request->vtkPathUtf8 && *request->vtkPathUtf8)
        {
            std::filesystem::path profilePath = utf8Path(request->vtkPathUtf8);
            profilePath += ".profile.txt";
            std::ofstream profile(profilePath, std::ios::trunc);
            if (profile)
            {
                const auto seconds = [](auto from, auto to) {
                    return std::chrono::duration<double>(to - from).count();
                };
                profile << "THREAD_API " << (nativeThreadApi ? "AVAILABLE" : "UNAVAILABLE") << '\n'
                        << "THREAD_REQUESTED " << requestedThreads << '\n'
                        << "THREAD_CONFIGURED " << configuredThreads << '\n'
                        << "THREAD_POLICY " << (nativeSerialFlag ? "SDK_SERIAL_FLAG" : nativeThreadApi ? "HYPOTHESIS_API" : "SDK_DEFAULT") << '\n';
                profile << std::fixed << std::setprecision(3)
                        << "PREP " << seconds(bridgeStarted, computeStarted) << '\n'
                        << "NETGEN " << seconds(computeStarted, computeFinished) << '\n'
                        << "VTK " << seconds(computeFinished, exportFinished) << '\n'
                        << "TOTAL " << seconds(bridgeStarted, exportFinished) << '\n';
                std::sort(slowSolids.begin(), slowSolids.end(),
                          [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
                for (std::size_t j = 0; j < std::min<std::size_t>(5, slowSolids.size()); ++j)
                    profile << "SOLID_" << slowSolids[j].first << ' '
                            << slowSolids[j].second << '\n';
            }
        }
        return 0;
    }
    catch (const std::exception& ex)
    {
        // A failed Compute may have left an incomplete submesh. Do not use it
        // as a future incremental baseline. Cancellation is cooperative and
        // happens only between successfully completed solids.
        const std::string reason(ex.what());
        if (sessionKeyUtf8 && *sessionKeyUtf8) {
            // A partially computed session is unsafe even on cancellation.
            g_nativeTetSession.reset();
            if (previousValidSession) g_nativeTetSession = std::move(previousValidSession);
        }
        setError(error, errorCapacity,
            g_netgenCancelRequested.load(std::memory_order_acquire)
                ? "SMESH_CANCELLED: native NETGEN Compute interrupted"
                : ex.what());
        return 1;
    }
    catch (...)
    {
        if (sessionKeyUtf8 && *sessionKeyUtf8) {
            g_nativeTetSession.reset();
            if (previousValidSession) g_nativeTetSession = std::move(previousValidSession);
        }
        setError(error, errorCapacity,
            g_netgenCancelRequested.load(std::memory_order_acquire)
                ? "SMESH_CANCELLED: native NETGEN Compute interrupted"
                : "Unknown exception in SMESH NETGEN");
        return 2;
    }
}

extern "C" CAE_SMESH_API int cae_smesh_netgen(const CaeSMESHNetgenRequest* request,
    CaeSMESHNetgenResult* result, char* error, std::size_t errorCapacity)
{
    return runNetgen(request, result, error, errorCapacity, nullptr, nullptr);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_progress(const CaeSMESHNetgenRequest* request,
    CaeSMESHNetgenResult* result, char* error, std::size_t errorCapacity,
    CaeSMESHProgress progress, void* progressContext)
{
    return runNetgen(request, result, error, errorCapacity, progress, progressContext);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_curvature(const CaeSMESHNetgenCurvatureRequest* request,
    CaeSMESHNetgenResult* result, char* error, std::size_t errorCapacity,
    CaeSMESHProgress progress, void* progressContext)
{
    return runNetgen(request ? &request->parameters : nullptr, result, error, errorCapacity,
        progress, progressContext, request ? request->faceCurvatures : nullptr,
        request ? request->faceCurvatureCount : 0, request && request->automaticMinSize,
        request ? request->selectedSolidIndices : nullptr,
        request ? request->selectedSolidCount : 0,
        request ? request->scopedNetgenControls : nullptr,
        request ? request->scopedNetgenCount : 0,
        request ? request->mainSolidIndices : nullptr,
        request ? request->mainSolidCount : 0,
        request ? request->meshSessionUtf8 : nullptr,
        request && request->resetSession != 0,
        request && request->autoPartition != 0);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_face_sizing(
    const CaeSMESHNetgenFaceSizingRequest* request,CaeSMESHNetgenResult* result,
    char* error,std::size_t errorCapacity,CaeSMESHProgress progress,void* progressContext)
{
    const auto* r=request?&request->netgen:nullptr;
    return runNetgen(r?&r->parameters:nullptr,result,error,errorCapacity,progress,progressContext,
        r?r->faceCurvatures:nullptr,r?r->faceCurvatureCount:0,r&&r->automaticMinSize,
        r?r->selectedSolidIndices:nullptr,r?r->selectedSolidCount:0,
        r?r->scopedNetgenControls:nullptr,r?r->scopedNetgenCount:0,
        r?r->mainSolidIndices:nullptr,r?r->mainSolidCount:0,r?r->meshSessionUtf8:nullptr,
        r&&r->resetSession!=0,r&&r->autoPartition!=0,
        request?request->faceMinimums:nullptr,request?request->faceMinimumCount:0);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_surface_precision(
    const CaeSMESHNetgenSurfacePrecisionRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext)
{
    const auto* sizing = request ? &request->sizing : nullptr;
    const auto* r = sizing ? &sizing->netgen : nullptr;
    return runNetgen(r ? &r->parameters : nullptr, result, error, errorCapacity, progress, progressContext,
        r ? r->faceCurvatures : nullptr, r ? r->faceCurvatureCount : 0, r && r->automaticMinSize,
        r ? r->selectedSolidIndices : nullptr, r ? r->selectedSolidCount : 0,
        r ? r->scopedNetgenControls : nullptr, r ? r->scopedNetgenCount : 0,
        r ? r->mainSolidIndices : nullptr, r ? r->mainSolidCount : 0, r ? r->meshSessionUtf8 : nullptr,
        r && r->resetSession != 0, r && r->autoPartition != 0,
        sizing ? sizing->faceMinimums : nullptr, sizing ? sizing->faceMinimumCount : 0,
        request && request->chordalErrorEnabled != 0, request ? request->chordalError : 0.0);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_boundary_scope(
    const CaeSMESHNetgenBoundaryScopeRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext)
{
    const auto* precision=request?&request->precision:nullptr;
    const auto* sizing=precision?&precision->sizing:nullptr;
    const auto* r=sizing?&sizing->netgen:nullptr;
    return runNetgen(r?&r->parameters:nullptr,result,error,errorCapacity,progress,progressContext,
        r?r->faceCurvatures:nullptr,r?r->faceCurvatureCount:0,r&&r->automaticMinSize,
        r?r->selectedSolidIndices:nullptr,r?r->selectedSolidCount:0,
        r?r->scopedNetgenControls:nullptr,r?r->scopedNetgenCount:0,
        r?r->mainSolidIndices:nullptr,r?r->mainSolidCount:0,r?r->meshSessionUtf8:nullptr,
        r&&r->resetSession!=0,r&&r->autoPartition!=0,
        sizing?sizing->faceMinimums:nullptr,sizing?sizing->faceMinimumCount:0,
        precision&&precision->chordalErrorEnabled!=0,precision?precision->chordalError:0.0,
        request?request->boundarySolidIndices:nullptr,request?request->boundarySolidCount:0);
}

static int runDomainSizing(
    const CaeSMESHNetgenDomainSizingRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext,
    const CaeSMESHScopedSurfacePrecision* precisionControls = nullptr, int precisionCount = 0)
{
    const auto* boundary=request?&request->boundary:nullptr;
    const auto* precision=boundary?&boundary->precision:nullptr;
    const auto* sizing=precision?&precision->sizing:nullptr;
    const auto* r=sizing?&sizing->netgen:nullptr;
    return runNetgen(r?&r->parameters:nullptr,result,error,errorCapacity,progress,progressContext,
        r?r->faceCurvatures:nullptr,r?r->faceCurvatureCount:0,r&&r->automaticMinSize,
        r?r->selectedSolidIndices:nullptr,r?r->selectedSolidCount:0,
        r?r->scopedNetgenControls:nullptr,r?r->scopedNetgenCount:0,
        r?r->mainSolidIndices:nullptr,r?r->mainSolidCount:0,r?r->meshSessionUtf8:nullptr,
        r&&r->resetSession!=0,r&&r->autoPartition!=0,
        sizing?sizing->faceMinimums:nullptr,sizing?sizing->faceMinimumCount:0,
        precision&&precision->chordalErrorEnabled!=0,precision?precision->chordalError:0.0,
        boundary?boundary->boundarySolidIndices:nullptr,boundary?boundary->boundarySolidCount:0,
        request?request->surfaceSolidIndices:nullptr,request?request->surfaceSolidCount:0,
        request&&request->surfaceOnly!=0,request?request->surfaceElementLimit:0,request?request->surfaceElements:nullptr,precisionControls,precisionCount);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_domain_sizing(
    const CaeSMESHNetgenDomainSizingRequest* request,CaeSMESHNetgenResult* result,
    char* error,std::size_t errorCapacity,CaeSMESHProgress progress,void* progressContext)
{return runDomainSizing(request,result,error,errorCapacity,progress,progressContext);}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_scoped_precision(
    const CaeSMESHNetgenScopedPrecisionRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext)
{
    return runDomainSizing(request?&request->sizing:nullptr,result,error,errorCapacity,progress,progressContext,
        request?request->controls:nullptr,request?request->controlCount:0);
}

// Deprecated ABI shims. Do not silently emulate a requested proximity switch:
// this standalone NETGENPlugin SDK exposes no supported Hypothesis setter.
extern "C" CAE_SMESH_API int cae_smesh_netgen_with_proximity_sizing(
    const CaeSMESHNetgenProximityRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext)
{
    // -1 means use native defaults; 1 historically mapped to the native
    // factor of 2, but cannot guarantee that the feature is enabled.
    if (request && request->closeEdgeRefinement != -1) {
        if (result) *result = {};
        setError(error, errorCapacity,
            "SDK_CAPABILITY: legacy proximity option is unsupported by this SMESH build");
        return 1;
    }
    return runDomainSizing(request ? &request->sizing : nullptr, result,
        error, errorCapacity, progress, progressContext);
}

extern "C" CAE_SMESH_API int cae_smesh_netgen_with_proximity_factor(
    const CaeSMESHNetgenProximityFactorRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext)
{
    if (request && request->closeEdgeFactor != -1.) {
        if (result) *result = {};
        setError(error, errorCapacity,
            "SDK_CAPABILITY: legacy proximity factor is unsupported by this SMESH build");
        return 1;
    }
    return runDomainSizing(request ? &request->sizing : nullptr, result,
        error, errorCapacity, progress, progressContext);
}

static int runConformalMesh(const CaeSMESHMixedRequest* request,
                            CaeSMESHMixedResult* result,
                            char* error, std::size_t errorCapacity, bool preferHex,
                            const CaeSMESHScopedNetgenControl* scopedNetgen = nullptr,
                            int scopedNetgenCount = 0,
                            bool chordalEnabled = false, double chordalError = 1.0,
                            const CaeSMESHScopedSurfacePrecision* precisionControls = nullptr,
                            int precisionCount = 0,
                            const std::int32_t* boundarySolidIndices = nullptr,
                            int boundarySolidCount = 0,
                            const CaeSMESHApplicationFaceSettings* faceSettings = nullptr,
                            int faceSettingCount = 0)
{
    if (result) *result = {};
    setError(error, errorCapacity, {});
    std::ofstream report;
    const auto started = std::chrono::steady_clock::now();
    auto log = [&](const std::string& message) {
        if (report) {
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            report << '[' << std::fixed << std::setprecision(3) << elapsed << " s] " << message << '\n'; report.flush();
        }
        if (request && request->progress) request->progress(request->progressContext, message.c_str());
    };
    try
    {
        ComputeLock lock;
        if (!request || !result || !request->sweeps || request->sweepCount < 1)
            throw std::invalid_argument("No sweep tasks supplied");
        if (!std::isfinite(request->meshSize) || request->meshSize <= 0 ||
            !std::isfinite(request->tolerance) || request->tolerance <= 0)
            throw std::invalid_argument("Invalid mixed mesh size/tolerance");
        if (!std::isfinite(request->netgenMinSize) || request->netgenMinSize < 0.0 ||
            request->netgenMinSize > request->meshSize ||
            request->netgenFineness < 0 || request->netgenFineness > 5 ||
            !std::isfinite(request->netgenGrowthRate) || request->netgenGrowthRate <= 0.0 ||
            !std::isfinite(request->netgenNbSegPerEdge) || request->netgenNbSegPerEdge <= 0.0 ||
            !std::isfinite(request->netgenNbSegPerRadius) || request->netgenNbSegPerRadius <= 0.0)
            throw std::invalid_argument("Invalid mixed NETGEN min/fineness parameters");
        if (request->localSizeCount < 0 || (request->localSizeCount > 0 && !request->localSizes))
            throw std::invalid_argument("Invalid mixed local size controls");
        if (request->edgeSegmentCount < 0 || (request->edgeSegmentCount > 0 && !request->edgeSegments))
            throw std::invalid_argument("Invalid mixed edge segment controls");
        if (request->boundaryLayerEnabled)
        {
            if (request->wallCount < 0 || (request->wallCount > 0 && !request->walls))
                throw std::invalid_argument("Invalid mixed boundary-layer wall list");
            if (!(request->firstHeight > 0.0) || !(request->totalThickness > 0.0) ||
                request->boundaryLayers < 1 || !(request->stretchFactor >= 1.0) ||
                request->extrusionMethod < 0 || request->extrusionMethod > 2)
                throw std::invalid_argument("Invalid mixed boundary-layer parameters");
            if (!request->automaticWall && request->wallCount <= 0)
                throw std::invalid_argument("Manual mixed boundary layer has no Wall faces");
            if (!preferHex)
                throw std::invalid_argument("Sweep + BoundaryLayer requires Hex-preferred mixed mode");
        }
        if (!preferHex && (request->localSizeCount > 0 || request->edgeSegmentCount > 0))
            throw std::invalid_argument("Generic local size controls require Hex-dominant Sweep mode; Strict Sweep keeps its legacy hypothesis chain");
        const auto reportPath = utf8Path(request->reportPathUtf8);
        if (!reportPath.empty())
        {
            std::filesystem::create_directories(reportPath.parent_path());
            report.open(reportPath, std::ios::trunc);
            if (!report) throw std::runtime_error("Cannot create conformity report");
        }
        log("SMESH mixed mesh: one CAD partition, one SMESH_Mesh; no Gmsh, no node welding.");
        log("NETGEN mixed hypothesis: Max=" + std::to_string(request->meshSize) +
            " Min=" + std::to_string(request->netgenMinSize) +
            " Fineness=" + std::to_string(request->netgenFineness) +
            " Curvature="+(request->netgenSurfaceCurvature?"ON":"OFF"));
        // Fineness is a native NETGEN enum. Changing it also changes edge and
        // radius subdivisions. Forward the native curvature switch unchanged.
        if(scopedNetgenCount<0 || (scopedNetgenCount>0 && !scopedNetgen))
            throw std::invalid_argument("Invalid mixed SubMesh NETGEN controls");
        if(!preferHex && scopedNetgenCount>0)
            throw std::invalid_argument("Legacy mixed mode cannot apply independent Tet SubMesh settings; choose the preconfigured Hex-dominant mixed mode");
        std::map<int,CaeSMESHScopedNetgenControl> domainNetgen;
        for(int n=0;n<scopedNetgenCount;++n) {
            const auto& c=scopedNetgen[n];
            if(c.solidShapeIndex<1 || c.fineness< -1 || c.fineness>5 ||
               c.surfaceCurvature< -1 || c.surfaceCurvature>1 ||
               c.optimize< -1 || c.optimize>1 || c.automaticMinSize< -1 ||
               c.automaticMinSize>1 || !std::isfinite(c.manualMinSize) || c.manualMinSize<0 ||
               !domainNetgen.emplace(c.solidShapeIndex,c).second)
                throw std::invalid_argument("Invalid/duplicate mixed SubMesh NETGEN options");
        }
        std::map<int,CaeSMESHScopedSurfacePrecision> domainPrecision;
        if ((chordalEnabled && (!std::isfinite(chordalError) || chordalError <= 0.0)) ||
            precisionCount < 0 || (precisionCount > 0 && !precisionControls))
            throw std::invalid_argument("Invalid mixed chordal precision");
        for(int n=0;n<precisionCount;++n) {
            const auto& c=precisionControls[n];
            if(c.solidShapeIndex<1 || c.chordalErrorEnabled<0 || c.chordalErrorEnabled>1 ||
                (c.chordalErrorEnabled && (!std::isfinite(c.chordalError)||c.chordalError<=0.0)) ||
                !domainPrecision.emplace(c.solidShapeIndex,c).second)
                throw std::invalid_argument("Invalid/duplicate mixed Solid chordal precision");
        }
        auto configureMixedNetgen = [&](NETGENPlugin_Hypothesis* hyp, double maxSize, int domain = 0) {
            const auto it=domainNetgen.find(domain);
            const auto* c=it==domainNetgen.end()?nullptr:&it->second;
            const int fineness=c && c->fineness>=0?c->fineness:request->netgenFineness;
            const bool curvature=c && c->surfaceCurvature>=0?c->surfaceCurvature!=0:request->netgenSurfaceCurvature!=0;
            const bool optimize=c && c->optimize>=0?c->optimize!=0:request->netgenOptimize!=0;
            const double minimum=c && c->automaticMinSize>=0
                ? (c->automaticMinSize ? 0.0:c->manualMinSize):request->netgenMinSize;
            if(minimum>maxSize)throw std::invalid_argument("Mixed SubMesh minimum size exceeds its target size");
            hyp->SetMaxSize(maxSize);
            hyp->SetMinSize(minimum);
            hyp->SetFineness(static_cast<NETGENPlugin_Hypothesis::Fineness>(fineness));
            if (fineness == NETGENPlugin_Hypothesis::UserDefined) {
                hyp->SetGrowthRate(c && c->growthRate > 0 ? c->growthRate : request->netgenGrowthRate);
                hyp->SetNbSegPerEdge(c && c->nbSegPerEdge > 0 ? c->nbSegPerEdge : request->netgenNbSegPerEdge);
                hyp->SetNbSegPerRadius(c && c->nbSegPerRadius > 0 ? c->nbSegPerRadius : request->netgenNbSegPerRadius);
            }
            hyp->SetOptimize(optimize);
            hyp->SetSurfaceCurvature(curvature);
            const auto precision=domainPrecision.find(domain);
            const bool enabled=precision==domainPrecision.end()?chordalEnabled:precision->second.chordalErrorEnabled!=0;
            const double errorValue=precision==domainPrecision.end()?chordalError:precision->second.chordalError;
            hyp->SetChordalErrorEnabled(enabled);
            if(enabled)hyp->SetChordalError(errorValue);
            if(domain>0)log("[SMESH SubMesh] Solid="+std::to_string(domain)+
                " Max="+std::to_string(maxSize)+" Min="+std::to_string(minimum)+
                " Fineness="+std::to_string(fineness)+" Curvature="+(curvature?"ON":"OFF")+
                " Optimize="+(optimize?"ON":"OFF")+" Chordal="+(enabled?std::to_string(errorValue):std::string("OFF")));
        };
        auto configureSweepSource = [&](NETGENPlugin_Hypothesis_2D* hyp,double size) {
            hyp->SetMaxSize(size);hyp->SetMinSize(0.0);
            hyp->SetFineness(NETGENPlugin_Hypothesis::Moderate);
            hyp->SetSurfaceCurvature(true);hyp->SetOptimize(true);
        };
        log("Sweep source uses its SubMesh SourceSize and native Moderate surface parameters; root Tet settings do not change source precision.");
        if (preferHex) log("Policy: Sweep source follows configured QuadAllowed; Hex/Prism retained, remaining regions Tet with Pyramid transitions.");
        TopoDS_Shape imported;
        BRep_Builder builder;
        std::ifstream input(utf8Path(request->brepPathUtf8), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open BREP");
        BRepTools::Read(imported, input, builder);
        if (imported.IsNull()) throw std::runtime_error("Empty BREP");
        TopTools_IndexedMapOfShape originals;
        TopExp::MapShapes(imported, TopAbs_SOLID, originals);
        if (originals.IsEmpty()) throw std::runtime_error("BREP contains no solids");
        for(const auto& item:domainNetgen)
            if(item.first>originals.Extent())throw std::invalid_argument("Mixed SubMesh options reference an unknown original solid");

        struct Sweep { int original; int label; TopoDS_Shape solid, source, target; CaeSMESHSweepRequest settings; };
        std::vector<Sweep> sweeps;
        std::set<int> sweepOriginals;
        for (int i = 0; i < request->sweepCount; ++i)
        {
            const auto& req = request->sweeps[i];
            if (req.layers < 1 || !std::isfinite(req.sourceSize) || req.sourceSize <= 0)
                throw std::runtime_error("Invalid sweep layers/source size");
            int match = req.volumeShapeIndex;
            const int label = request->volumeUiIds ? request->volumeUiIds[i] : match;
            if (match <= 0)
                throw std::runtime_error("Sweep requires an explicit BREP Solid index");
            if (match > originals.Extent())
                throw std::runtime_error("Sweep Solid topology index is outside input BREP");
            if (!sweepOriginals.insert(match).second)
                throw std::runtime_error("Duplicate sweep task for V" + std::to_string(label));
            Sweep s{match, label, originals(match), {}, {}, req};
            s.source = indexedShape(imported, TopAbs_FACE, req.sourceShapeIndex, "Mixed Sweep Source");
            s.target = indexedShape(imported, TopAbs_FACE, req.targetShapeIndex, "Mixed Sweep Target");
            TopTools_IndexedMapOfShape ownedFaces;
            TopExp::MapShapes(s.solid, TopAbs_FACE, ownedFaces);
            if (!ownedFaces.Contains(s.source) || !ownedFaces.Contains(s.target))
                throw std::runtime_error("Mixed Sweep Source/Target does not belong to selected solid");
            if (s.source.IsNull() || s.target.IsNull() || s.source.IsSame(s.target))
                throw std::runtime_error("Cannot map Source/Target for sweep V" + std::to_string(label));
            TopTools_IndexedMapOfShape sourceWires;
            TopExp::MapShapes(s.source, TopAbs_WIRE, sourceWires);
            if (!preferHex && req.quadrangleSource && sourceWires.Extent() != 1)
                throw std::runtime_error("Sweep V" + std::to_string(label) + " source has " +
                    std::to_string(sourceWires.Extent()) + " boundary wires. Quadrangle_2D mapping cannot mesh this source; "
                    "partition the block or explicitly choose a triangle source. No automatic Tet fallback was applied.");
            sweeps.push_back(s);
        }

        // SALOME GEOM MakePartition works on private copies of the input
        // shapes, executes one splitter operation, and gives the resulting
        // partition shape directly to SMESH. Reproduce that core sequence in
        // the standalone bridge without depending on the full GEOM module.
        //
        // Full-model mode always partitions ALL CAD solids in one operation.
        // The neighbour-only command remains a local diagnostic and partitions
        // the selected sweep body plus bounding-box neighbour candidates.
        std::set<int> partitionOriginals = sweepOriginals;
        std::set<int> diagnosticSeeds = sweepOriginals;
        std::vector<Bnd_Box> diagnosticBounds;
        if (request->neighboursOnly)
        {
            for (const auto& s : sweeps)
            {
                Bnd_Box box; BRepBndLib::Add(s.solid, box, false);
                box.Enlarge(request->tolerance); diagnosticBounds.push_back(box);
            }

            // A boundary-layer parameter may be the thing the user is tuning.
            // Include the solid that owns every selected wall in the local diagnostic
            // even when that solid is not itself a Sweep body. The wall index is
            // already part of ABI v4, so no persistent geometry/history is required.
            if (request->boundaryLayerEnabled && request->wallCount > 0)
            {
                for (int wi = 0; wi < request->wallCount; ++wi)
                {
                    const auto& wall = request->walls[wi];
                    const TopoDS_Shape wallFace = indexedShape(imported, TopAbs_FACE, wall.shapeIndex, "BoundaryLayer diagnostic wall");
                    if (wallFace.IsNull()) continue;
                    for (int i = 1; i <= originals.Extent(); ++i)
                    {
                        TopTools_IndexedMapOfShape fs;
                        TopExp::MapShapes(originals(i), TopAbs_FACE, fs);
                        if (!fs.Contains(wallFace)) continue;
                        diagnosticSeeds.insert(i);
                        partitionOriginals.insert(i);
                        Bnd_Box box; BRepBndLib::Add(originals(i), box, false);
                        box.Enlarge(request->tolerance); diagnosticBounds.push_back(box);
                    }
                }
            }

            for (int i = 1; i <= originals.Extent(); ++i)
            {
                if (partitionOriginals.count(i)) continue;
                Bnd_Box box; BRepBndLib::Add(originals(i), box, false);
                box.Enlarge(request->tolerance);
                for (const auto& focusBox : diagnosticBounds)
                    if (!box.IsOut(focusBox)) { partitionOriginals.insert(i); break; }
            }
        }
        else
        {
            for (int i = 1; i <= originals.Extent(); ++i) partitionOriginals.insert(i);
        }
        log("CAD Partition started: total solids=" + std::to_string(originals.Extent()) +
            ", partition inputs=" + std::to_string(partitionOriginals.size()) +
            (request->neighboursOnly ? " (local diagnostic)" : " (full model)"));

        // SALOME's GEOMImpl_PartitionDriver first copies its input shapes and
        // then runs GEOMAlgo_Splitter on those copies. BRepBuilderAPI_Copy is
        // sufficient here because ExportedModel.brep is only a mesh-side CAD
        // snapshot and we do not need OCAF naming/history.
        std::vector<TopoDS_Shape> partitionInputs(originals.Extent());
        std::vector<std::unique_ptr<BRepBuilderAPI_Copy>> copies(originals.Extent());
        for (int i : partitionOriginals)
        {
            auto copy = std::make_unique<BRepBuilderAPI_Copy>(originals(i), true, false);
            if (!copy->IsDone() || copy->Shape().IsNull())
                throw std::runtime_error("Cannot copy original solid " + std::to_string(i) + " for CAD Partition");
            partitionInputs[i - 1] = copy->Shape();
            copies[i - 1] = std::move(copy);
        }

        // Source/Target were selected on the imported model. Remap them onto
        // the private Partition input copies before running the splitter; after
        // Partition we use Splitter history to reach the final shared topology.
        for (auto& s : sweeps)
        {
            const TopoDS_Shape originalSource = s.source;
            const TopoDS_Shape originalTarget = s.target;
            s.solid = partitionInputs[s.original - 1];
            s.source = copies[s.original - 1]->ModifiedShape(originalSource);
            s.target = copies[s.original - 1]->ModifiedShape(originalTarget);
            if (s.source.IsNull() || s.target.IsNull() || s.source.IsSame(s.target))
                throw std::runtime_error("Cannot remap Source/Target to Partition copy for sweep V" +
                                         std::to_string(s.label));
        }

        // Standalone equivalent of the core GEOM MakePartition stage. SALOME
        // uses GEOMAlgo_Splitter; OCCT BOPAlgo_Splitter is its General-Fuse
        // based lower-level splitter. All solids are Arguments, exactly as in
        // MakePartition([solid1, solid2, ...], ...).
        BOPAlgo_Splitter partition;
        partition.SetNonDestructive(false); // inputs above are already private copies
        partition.SetRunParallel(true);
        if (request->tolerance > 0.0) partition.SetFuzzyValue(request->tolerance);
        for (int i : partitionOriginals) partition.AddArgument(partitionInputs[i - 1]);
        if (partitionOriginals.size() > 1) partition.Perform();
        if (partition.HasErrors())
        {
            std::ostringstream detail; partition.DumpErrors(detail);
            throw std::runtime_error("CAD Partition failed: " + detail.str());
        }
        TopoDS_Shape partitioned = partitionOriginals.size() > 1
            ? partition.Shape() : partitionInputs[*partitionOriginals.begin() - 1];
        if (partitioned.IsNull()) throw std::runtime_error("CAD Partition returned an empty shape");
        if (!BRepCheck_Analyzer(partitioned).IsValid())
            throw std::runtime_error("CAD Partition returned an invalid shape");

        TopTools_IndexedMapOfShape partitionSolids;
        TopExp::MapShapes(partitioned, TopAbs_SOLID, partitionSolids);
        auto imageOf = [&](const TopoDS_Shape& shape, TopAbs_ShapeEnum type, const std::string& label) {
            TopTools_IndexedMapOfShape images;
            if (partitionOriginals.size() > 1)
                for (TopTools_ListIteratorOfListOfShape it(partition.Modified(shape)); it.More(); it.Next())
                    if (it.Value().ShapeType() == type) images.Add(it.Value());
            if (images.IsEmpty()) return shape;
            if (images.Extent() != 1)
                throw std::runtime_error(label + " was split by CAD Partition into " +
                                         std::to_string(images.Extent()) +
                                         " pieces; this bridge requires one material domain per original solid.");
            return images(1);
        };

        // Every Partition result solid must map to exactly one original domain.
        // This rejects overlapping material volumes instead of silently meshing
        // a newly created overlap fragment as another region.
        // Resolve an original BREP subshape through OCC Copy + Splitter history.
        // No geometric search on the direct-index path.
        TopTools_IndexedMapOfShape partitionFaces, partitionEdges;
        TopExp::MapShapes(partitioned, TopAbs_FACE, partitionFaces);
        TopExp::MapShapes(partitioned, TopAbs_EDGE, partitionEdges);
        // Build ownership only once. Resolving 5,000 CAD faces must NOT traverse
        // every solid's entire face list for every source face.
        TopTools_IndexedDataMapOfShapeListOfShape inputFaceOwners, inputEdgeOwners;
        TopExp::MapShapesAndAncestors(imported, TopAbs_FACE, TopAbs_SOLID, inputFaceOwners);
        TopExp::MapShapesAndAncestors(imported, TopAbs_EDGE, TopAbs_SOLID, inputEdgeOwners);
        auto finalImages = [&](const TopoDS_Shape& original, TopAbs_ShapeEnum type) {
            TopTools_IndexedMapOfShape found;
            std::vector<int> originalOwners;
            if (type == TopAbs_SOLID)
            {
                const int index = originals.FindIndex(original);
                if (index > 0) originalOwners.push_back(index);
            }
            else
            {
                const auto& map = type == TopAbs_FACE ? inputFaceOwners : inputEdgeOwners;
                const int index = map.FindIndex(original);
                if (index > 0)
                    for (TopTools_ListIteratorOfListOfShape it(map.FindFromIndex(index)); it.More(); it.Next())
                    {
                        const int originalIndex = originals.FindIndex(it.Value());
                        if (originalIndex > 0) originalOwners.push_back(originalIndex);
                    }
            }
            for (int i : originalOwners)
            {
                if (!partitionOriginals.count(i)) continue;
                const TopoDS_Shape copied = copies[i - 1]->ModifiedShape(original);
                if (copied.IsNull()) continue;
                auto append = [&](const TopoDS_Shape& image) {
                    if (image.IsNull() || image.ShapeType() != type) return;
                    const auto& finalMap = type == TopAbs_FACE ? partitionFaces :
                        type == TopAbs_EDGE ? partitionEdges : partitionSolids;
                    if (finalMap.Contains(image)) found.Add(image);
                };
                if (partitionOriginals.size() <= 1) { append(copied); continue; }
                const auto& modified = partition.Modified(copied);
                if (modified.IsEmpty()) append(copied);
                else for (TopTools_ListIteratorOfListOfShape it(modified); it.More(); it.Next())
                    append(it.Value());
            }
            return found;
        };
        std::vector<TopoDS_Shape> solids(originals.Extent());
        TopTools_IndexedMapOfShape mappedSolids;
        for (int i : partitionOriginals)
        {
            auto solid = imageOf(partitionInputs[i - 1], TopAbs_SOLID,
                                 "Original solid " + std::to_string(i));
            if (!partitionSolids.Contains(solid) || mappedSolids.Contains(solid))
                throw std::runtime_error("Overlapping/removed CAD domain at original solid " + std::to_string(i));
            mappedSolids.Add(solid);
            solids[i - 1] = solid;
        }
        if (partitionSolids.Extent() != mappedSolids.Extent())
            throw std::runtime_error("CAD Partition introduced unassigned domains (intersecting original solids)");

        for (auto& s : sweeps)
        {
            s.solid = solids[s.original - 1];
            s.source = imageOf(s.source, TopAbs_FACE, "Source of V" + std::to_string(s.label));
            s.target = imageOf(s.target, TopAbs_FACE, "Target of V" + std::to_string(s.label));
            TopTools_IndexedMapOfShape solidFaces; TopExp::MapShapes(s.solid, TopAbs_FACE, solidFaces);
            if (!solidFaces.Contains(s.source) || !solidFaces.Contains(s.target))
                throw std::runtime_error("Sweep face no longer belongs to V" + std::to_string(s.label) +
                                         " after CAD Partition");
        }
        TopTools_IndexedMapOfShape faces;
        std::map<int, std::vector<int>> owners;
        for (int i = 0; i < int(solids.size()); ++i)
        {
            if (solids[i].IsNull()) continue;
            TopTools_IndexedMapOfShape solidFaces;
            TopExp::MapShapes(solids[i], TopAbs_FACE, solidFaces);
            for (int f = 1; f <= solidFaces.Extent(); ++f)
                owners[faces.Add(solidFaces(f))].push_back(i + 1);
        }
        std::set<int> included = request->neighboursOnly ? diagnosticSeeds : sweepOriginals;
        if (boundarySolidCount < 0 || (boundarySolidCount > 0 && !boundarySolidIndices))
            throw std::invalid_argument("Invalid mixed boundary-layer receiving solid scope");
        std::set<int> boundarySolids;
        for (int k=0;k<boundarySolidCount;++k) {
            const int id=boundarySolidIndices[k];
            if(id<1 || id>originals.Extent() || !boundarySolids.insert(id).second)
                throw std::invalid_argument("Invalid/duplicate mixed boundary-layer receiving solid");
        }
        if (!request->neighboursOnly && request->restrictTetToSelection)
        {
            if (request->tetShapeIndexCount < 0 ||
                (request->tetShapeIndexCount > 0 && !request->tetShapeIndices))
                throw std::invalid_argument("Invalid explicit Tet selection");
            for (int k = 0; k < request->tetShapeIndexCount; ++k)
            {
                const int id = request->tetShapeIndices[k];
                if (id <= 0 || id > originals.Extent())
                    throw std::runtime_error("Selected Tet Solid index is outside input BREP");
                if (sweepOriginals.count(id))
                    throw std::runtime_error("A CAD Solid is assigned to both Sweep and Tet operations");
                included.insert(id);
            }
            log("Explicit Tet operations: selected solids=" +
                std::to_string(request->tetShapeIndexCount) +
                "; all other unassigned solids remain unmeshed.");
        }
        for (const auto& entry : owners)
        {
            if (entry.second.size() > 2)
            {
                std::ostringstream detail;
                detail << "Non-manifold CAD face " << entry.first << " belongs to original solids";
                for (int owner : entry.second) detail << ' ' << owner;
                throw std::runtime_error(detail.str());
            }
            bool touchesFocus = false;
            for (int owner : entry.second) touchesFocus |= diagnosticSeeds.count(owner) != 0;
            if (touchesFocus || (!request->neighboursOnly && !request->restrictTetToSelection))
                included.insert(entry.second.begin(), entry.second.end());
        }
        // Prism_3D supports multiple neighboring extrusions only when their
        // shared CAD faces carry a compatible side/cap mesh.  IsApplicable()
        // checks *individual* solids and cannot validate cross-solid directions.
        // Reject perpendicular neighboring sweep assignments before Compute:
        // the native algorithm otherwise fails with an opaque
        // "More than 2 faces with not quadrangle elements" error.
        std::map<int, const Sweep*> sweepByOriginal;
        for (const Sweep& sweep : sweeps)
            sweepByOriginal.emplace(sweep.original, &sweep);
        auto sweepDisplacement = [](const Sweep& sweep) {
            GProp_GProps sourceProps, targetProps;
            BRepGProp::SurfaceProperties(sweep.source, sourceProps);
            BRepGProp::SurfaceProperties(sweep.target, targetProps);
            return gp_Vec(sourceProps.CentreOfMass(), targetProps.CentreOfMass());
        };
        for (const auto& entry : owners)
        {
            if (entry.second.size() != 2) continue;
            const auto first = sweepByOriginal.find(entry.second[0]);
            const auto second = sweepByOriginal.find(entry.second[1]);
            if (first == sweepByOriginal.end() || second == sweepByOriginal.end()) continue;
            const Sweep& a = *first->second;
            const Sweep& b = *second->second;
            const TopoDS_Shape& interfaceFace = faces(entry.first);
            const bool aCap = interfaceFace.IsSame(a.source) || interfaceFace.IsSame(a.target);
            const bool bCap = interfaceFace.IsSame(b.source) || interfaceFace.IsSame(b.target);
            const gp_Vec aDir = sweepDisplacement(a);
            const gp_Vec bDir = sweepDisplacement(b);
            const double lengths = aDir.Magnitude() * bDir.Magnitude();
            if (lengths < 1.0e-16)
                throw std::runtime_error("Sweep source/target face centers coincide on adjacent volumes V" +
                                         std::to_string(a.label) + " and V" + std::to_string(b.label));
            const double alignment = std::abs(aDir.Dot(bDir)) / lengths;
            if (alignment < 0.98 || aCap != bCap)
                throw std::runtime_error("Adjacent sweep volumes V" + std::to_string(a.label) +
                    " and V" + std::to_string(b.label) +
                    " have incompatible directions across their shared face; "
                    "use the same sweep axis for connected volumes or mesh one as Tet");
        }
        // Use only the CAD topology returned by the native OCC Partition.
        // No application-level overlap or co-planarity search is performed.
        // SMESH will generate each shared TopoDS_Face once. If a geometrically
        // touching interface is not actually topologically shared, this audit
        // does not mistake matching coordinates for a conformal connection.
        std::size_t sharedCadFaces = 0;
        std::size_t externalCadFaces = 0;
        for (const auto& entry : owners) {
            int activeOwners = 0;
            for (int owner : entry.second)
                if (included.count(owner)) ++activeOwners;
            if (activeOwners == 2) ++sharedCadFaces;
            else if (activeOwners == 1) ++externalCadFaces;
        }
        log("OCC Partition topology: shared CAD faces=" +
            std::to_string(sharedCadFaces) + ", one-owner faces=" +
            std::to_string(externalCadFaces) +
            "; no custom geometrical face-contact test was performed.");

        log("CAD Partition completed: included=" + std::to_string(included.size()) +
            ", sweep=" + std::to_string(sweeps.size()) + ", remaining=" +
            std::to_string(included.size() - sweeps.size()));
        for (int id:boundarySolids)
            if (!included.count(id)) throw std::runtime_error("Boundary-layer receiver is outside the computed mixed mesh");

        // This is the important SALOME rule: mesh the Partition result itself.
        // Do not explode the result into solids and rebuild a new full-model
        // Compound before ShapeToMesh(), because the Partition shape is the
        // authoritative owner of the shared interface topology.
        TopoDS_Shape meshShape;
        if (!request->neighboursOnly)
        {
            // Keep the full, already conformal CAD Partition as parent even
            // if only selected Solid SubMeshes are computed in this pass.
            if (!request->restrictTetToSelection && included.size() != partitionOriginals.size())
                throw std::runtime_error("Internal error: full-model Partition did not include all result domains");
            meshShape = partitioned;
            log(request->restrictTetToSelection
                ? "SMESH geometry = full conformal Partition; only explicitly assigned operations will Compute."
                : "SMESH geometry = exact full CAD Partition result (single ShapeToMesh input).");
        }
        else if (included.size() == partitionOriginals.size())
        {
            meshShape = partitioned;
            log("SMESH geometry = exact local CAD Partition result (single ShapeToMesh input).");
        }
        else
        {
            // Diagnostic-only mode may discard bbox candidates that turned out
            // not to be topological neighbours. A sub-compound is unavoidable
            // here, but its solids/faces are taken directly from the same
            // Partition result, so shared TopoDS sub-shapes are preserved.
            TopoDS_Compound localShape; builder.MakeCompound(localShape);
            for (int i : included) builder.Add(localShape, solids[i - 1]);
            meshShape = localShape;
            log("SMESH geometry = subset of one local CAD Partition result (neighbour diagnostic only).");
        }
        if (meshShape.IsNull() || !BRepCheck_Analyzer(meshShape).IsValid())
            throw std::runtime_error("Invalid mesh geometry after CAD Partition");

        SMESH_Gen* gen = generator();
        // A native geometry precheck is a necessary condition, not a guarantee.
        // Reject unsupported tasks explicitly; never convert them to Tet here.
        std::string inapplicable;
        for (const auto& s : sweeps)
            if (!StdMeshers_Prism_3D::IsApplicable(s.solid, true))
                inapplicable += " [original solid=" + std::to_string(s.original) +
                    ", UI volume=" + std::to_string(s.label) + "]";
        if (!inapplicable.empty())
            throw std::runtime_error("SMESH Prism_3D native geometry applicability check rejected" +
                inapplicable + "; sweep assignments unchanged, no Compute attempted");
        for (const auto& s : sweeps)
        {
            const int sourceEdges = SMESH_MesherHelper::Count(s.source, TopAbs_EDGE, true);
            const int targetEdges = SMESH_MesherHelper::Count(s.target, TopAbs_EDGE, true);
            if (sourceEdges != targetEdges)
                throw std::runtime_error("Native sweep cap topology mismatch [original solid=" +
                    std::to_string(s.original) + ", source edges=" + std::to_string(sourceEdges) +
                    ", target edges=" + std::to_string(targetEdges) +
                    "]; source/target cannot use one-to-one Projection_1D2D; choose an explicit Tet operation or compatible CAD partition");
        }
        log("Prism_3D native geometry precheck passed; final mesh still requires Compute and conformity/volume checks.");

        // Algorithms and hypotheses are configured on one parent Mesh.
        // SMESH owns dependency traversal; application code does not stage or retry Compute.
        *result = {};
        std::size_t nativeCompletedDomains = 0;
        // Mesh borrows these observers; destroy it before observers/hypotheses.
        std::vector<std::unique_ptr<NativeDomainProgress>> nativeProgress;
        std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
        std::unique_ptr<SMESH_Mesh> mesh(gen->CreateMesh(true));
        mesh->ShapeToMesh(meshShape);

        // Prove that SMESH is indexing the actual Partition sub-shapes. If a
        // solid or a shared interface face is missing here, continuing would
        // mean hypotheses are being attached to shapes outside ShapeToMesh().
        std::size_t indexedSharedFaces = 0;
        for (int i : included)
            if (mesh->GetMeshDS()->ShapeToIndex(solids[i - 1]) <= 0)
                throw std::runtime_error("Partition solid is not a sub-shape of SMESH ShapeToMesh: original solid " +
                                         std::to_string(i));
        for (const auto& entry : owners)
        {
            std::size_t activeOwners = 0;
            for (int owner : entry.second) if (included.count(owner)) ++activeOwners;
            if (activeOwners != 2) continue;
            if (mesh->GetMeshDS()->ShapeToIndex(faces(entry.first)) <= 0)
                throw std::runtime_error("Shared Partition face is not a sub-shape of SMESH ShapeToMesh: CADFaceId=" +
                                         std::to_string(entry.first));
            ++indexedSharedFaces;
        }
        log("SMESH Partition topology indexed: solids=" + std::to_string(included.size()) +
            ", shared faces=" + std::to_string(indexedSharedFaces) + ".");

        auto make = [&](auto* obj) { hypotheses.emplace_back(obj); return obj; };
        // Keep the legacy mixed mode unchanged. Hex-dominant follows the
        // SALOME GUI hierarchy on one parent SMESH_Mesh: local Source-FACE
        // SubMeshes use NETGEN 1D-2D, local vertical-EDGE SubMeshes use
        // Regular 1D + NumberOfSegments, Prism_3D is assigned to Sweep SOLIDs,
        // and FULL NETGEN 1D-2D-3D to the remaining SOLIDs. In this mode the
        // Target cap is intentionally NOT given a second Projection_1D2D
        // SubMesh: Prism_3D performs its native bottom-to-top cap projection.
        if (!preferHex)
        {
            auto* length = make(new StdMeshers_LocalLength(gen->GetANewId(), gen));
            length->SetLength(request->meshSize);
            auto* regular = make(new StdMeshers_Regular_1D(gen->GetANewId(), gen));
            requireHypothesis(*mesh, meshShape, *length, "LocalLength");
            requireHypothesis(*mesh, meshShape, *regular, "Regular_1D");

            // Legacy source policy needs a discrete boundary before its 2D-only
            // mesher. Preserve the old circular/elliptical minimum here only.
            TopTools_IndexedMapOfShape curvedEdges;
            TopExp::MapShapes(meshShape, TopAbs_EDGE, curvedEdges);
            std::map<int, double> sourceEdgeSizes;
            for (int i = 0; i < int(sweeps.size()); ++i)
            {
                TopTools_IndexedMapOfShape capEdges;
                TopExp::MapShapes(sweeps[i].source, TopAbs_EDGE, capEdges);
                TopExp::MapShapes(sweeps[i].target, TopAbs_EDGE, capEdges);
                for (int e = 1; e <= capEdges.Extent(); ++e)
                {
                    const int edgeId = curvedEdges.FindIndex(capEdges(e));
                    const double h = sweeps[i].settings.sourceSize;
                    const auto previous = sourceEdgeSizes.find(edgeId);
                    sourceEdgeSizes[edgeId] = previous == sourceEdgeSizes.end() ? h : std::min(h, previous->second);
                }
            }
            int curvatureConstraints = 0;
            for (int e = 1; e <= curvedEdges.Extent(); ++e)
            {
                if (BRep_Tool::Degenerated(TopoDS::Edge(curvedEdges(e)))) continue;
                BRepAdaptor_Curve curve(TopoDS::Edge(curvedEdges(e)));
                if (curve.GetType() != GeomAbs_Circle && curve.GetType() != GeomAbs_Ellipse) continue;
                const int angularSegments = std::max(1, int(std::ceil(
                    std::abs(curve.LastParameter() - curve.FirstParameter()) / (std::acos(-1.0) / 6.0) - 1e-8)));
                GProp_GProps props; BRepGProp::LinearProperties(curvedEdges(e), props);
                const double h = sourceEdgeSizes.count(e) ? std::min(request->meshSize, sourceEdgeSizes[e]) : request->meshSize;
                if (std::ceil(std::abs(props.Mass()) / h) >= angularSegments) continue;
                auto* segments = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
                segments->SetNumberOfSegments(angularSegments);
                requireHypothesis(*mesh, curvedEdges(e), *segments, "Curvature minimum on arc");
                ++curvatureConstraints;
            }
            log("Curved-edge minimum segmentation constraints=" + std::to_string(curvatureConstraints));
        }
        else
        {
            log("SALOME GUI hierarchy: one parent Mesh; Source Face=NETGEN 1D-2D Quad-dominated, Target cap=native Prism_3D projection, vertical Edges=Regular 1D + NumberOfSegments, Sweep Solid=Prism_3D, remaining Solid=FULL NETGEN 1D-2D-3D.");
        }
        // Apply the source-size part of Face/Solid refinement BEFORE configuring
        // Source SubMeshes. These hypotheses are immutable after Compute.
        // A swept side wall cannot be independently remeshed: it is generated
        // by Prism_3D from the source/target caps and vertical edge segments.
        int sweepSourceSizeOverrides = 0;
        if (preferHex)
        {
            for (int ci = 0; ci < request->localSizeCount; ++ci)
            {
                const auto& rule = request->localSizes[ci];
                if ((rule.dimension != 2 && rule.dimension != 3) ||
                    !std::isfinite(rule.targetSize) || rule.targetSize <= 0)
                    throw std::invalid_argument("Invalid mixed Face/Solid size rule");
                const auto type = rule.dimension == 2 ? TopAbs_FACE : TopAbs_SOLID;
                TopTools_IndexedMapOfShape images;
                images = finalImages(indexedShape(imported, type, rule.shapeIndex, "Sweep-aware local size"), type);
                for (int im = 1; im <= images.Extent(); ++im)
                {
                    const auto& selected = images(im);
                    for (auto& sw : sweeps)
                    {
                        if (rule.dimension == 3 && selected.IsSame(sw.solid))
                        {
                            sw.settings.sourceSize = std::min(sw.settings.sourceSize, rule.targetSize);
                            ++sweepSourceSizeOverrides;
                        }
                        else if (rule.dimension == 2)
                        {
                            if (selected.IsSame(sw.source) || selected.IsSame(sw.target))
                            {
                                sw.settings.sourceSize = std::min(sw.settings.sourceSize, rule.targetSize);
                                ++sweepSourceSizeOverrides;
                            }
                            else
                            {
                                TopTools_IndexedMapOfShape sweptFaces;
                                TopExp::MapShapes(sw.solid, TopAbs_FACE, sweptFaces);
                                if (sweptFaces.Contains(selected))
                                    throw std::runtime_error(
                                        "Face size on a Sweep side wall is not a separate SubMesh: "
                                        "set the Sweep source size or refine its source Face instead.");
                            }
                        }
                    }
                }
            }
            if (sweepSourceSizeOverrides > 0)
                log("Sweep SourceSize refined by " + std::to_string(sweepSourceSizeOverrides) +
                    " Face/Solid rules before SMESH hypothesis configuration.");
        }
        std::map<int, int> regions;
        for (int i : included) regions[mesh->GetMeshDS()->ShapeToIndex(solids[i - 1])] = i;
        TopTools_IndexedMapOfShape layerEdges;
        std::map<int, int> edgeLayers;
        TopTools_IndexedMapOfShape sweepFaces;
        TopTools_IndexedMapOfShape sweepEdges;
        TopTools_IndexedMapOfShape sourceFaces;
        std::map<int, std::pair<bool, double>> sourceSettings;

        // Boundary-layer face selection is resolved against the final Partition
        // topology.  This stays deliberately simple: no OCAF history and no
        // automatic repartitioning.  Automatic mode means all external faces
        // except user exclusions; manual mode means only explicitly selected
        // external faces.
        TopTools_IndexedMapOfShape selectedBoundaryFaces;
        if (request->boundaryLayerEnabled && request->wallCount > 0)
        {
            for (int wi = 0; wi < request->wallCount; ++wi)
            {
                const auto& wall = request->walls[wi];
                TopTools_IndexedMapOfShape selected;
                selected = finalImages(indexedShape(imported, TopAbs_FACE, wall.shapeIndex, "Mixed Wall"), TopAbs_FACE);
                if (selected.IsEmpty())
                    throw std::runtime_error("Selected BoundaryLayer face not present after CAD Partition");
                for (int k = 1; k <= selected.Extent(); ++k)
                {
                    const auto& face = selected(k);
                    const int faceId = faces.FindIndex(face);
                    if (faceId <= 0)
                        throw std::runtime_error("BoundaryLayer face is outside the mixed Partition topology");
                    int activeOwners = 0;
                    for (int owner : owners[faceId])
                        if (included.count(owner) && (boundarySolids.empty() || boundarySolids.count(owner))) ++activeOwners;
                    if (activeOwners != 1)
                        throw std::runtime_error("BoundaryLayer Wall/exclusion must have exactly one receiving Solid side");
                    selectedBoundaryFaces.Add(face);
                }
            }
        }
        auto isExternalBoundaryFace = [&](const TopoDS_Shape& face)
        {
            const int faceId = faces.FindIndex(face);
            if (faceId <= 0) return false;
            int activeOwners = 0;
            for (int owner : owners[faceId])
                if (included.count(owner) && (boundarySolids.empty() || boundarySolids.count(owner))) ++activeOwners;
            return activeOwners == 1;
        };
        auto wantsBoundaryLayerOnFace = [&](const TopoDS_Shape& face, int owner)
        {
            if (!boundarySolids.empty() && !boundarySolids.count(owner)) return false;
            if (!request->boundaryLayerEnabled || !isExternalBoundaryFace(face)) return false;
            if (request->automaticWall)
                return !(request->selectedWallsToIgnore && selectedBoundaryFaces.Contains(face));
            return selectedBoundaryFaces.Contains(face);
        };
        std::int64_t boundaryLayerSweepSideFaces = 0;
        std::int64_t boundaryLayerTetFaces = 0;
        std::int64_t boundaryLayerSkippedSweepCaps = 0;

        auto* sweepRegular = preferHex ? make(new StdMeshers_Regular_1D(gen->GetANewId(), gen)) : nullptr;
        // A target face can be the next block's source. Assigning independent
        // 2D algorithms to both caps makes Prism_3D premesh them differently.
        // Keep one seed mesh per chain and propagate through shared cap faces.
        std::vector<int> dependencies(sweeps.size(), 0), sweepOrder;
        std::vector<std::vector<int>> successors(sweeps.size());
        std::vector<bool> inheritedSource(sweeps.size(), false);
        for (int i = 0; i < int(sweeps.size()); ++i)
            for (int j = 0; j < int(sweeps.size()); ++j)
            {
                if (i == j) continue;
                if (i < j && sweeps[i].target.IsSame(sweeps[j].target))
                    throw std::runtime_error("Two sweeps converge on the same target cap; orient the sweep chain consistently: V" +
                                             std::to_string(sweeps[i].label) + ", V" + std::to_string(sweeps[j].label));
                if (!sweeps[i].source.IsSame(sweeps[j].target)) continue;
                inheritedSource[i] = true; ++dependencies[i]; successors[j].push_back(i);
                if (sweeps[i].settings.quadrangleSource != sweeps[j].settings.quadrangleSource ||
                    sweeps[i].settings.sourceSize != sweeps[j].settings.sourceSize)
                    throw std::runtime_error("Incompatible source settings along shared-cap sweep chain");
                log("Shared cap: V" + std::to_string(sweeps[i].label) + " inherits source mesh from V" +
                    std::to_string(sweeps[j].label) + "; no independent 2D remesh.");
            }
        for (int i = 0; i < int(sweeps.size()); ++i)
            if (!dependencies[i]) sweepOrder.push_back(i);
        for (std::size_t k = 0; k < sweepOrder.size(); ++k)
            for (int next : successors[sweepOrder[k]])
                if (--dependencies[next] == 0) sweepOrder.push_back(next);
        if (sweepOrder.size() != sweeps.size())
            throw std::runtime_error("Cyclic sweep cap dependency; choose a consistent Source/Target direction");

        // SALOME GUI does not require a second horizontal-edge discretizer
        // here. NETGEN 1D-2D owns the Source-face boundary edges and Prism_3D
        // performs its native bottom-to-top 1D/2D projection. Only logically
        // vertical edges get an explicit 1D hypothesis.
        if (preferHex)
            log("Sweep horizontal edges are owned by Source NETGEN 1D-2D and native Prism_3D cap projection; only vertical edges receive explicit NumberOfSegments.");

        for (int i = 0; i < int(sweeps.size()); ++i)
        {
            const auto& s = sweeps[i]; const auto& req = s.settings;
            log("Sweep UI V" + std::to_string(s.label) + " maps to CADSolidId=" + std::to_string(s.original));
            if (!preferHex)
            {
                auto* size = make(new StdMeshers_LocalLength(gen->GetANewId(), gen));
                size->SetLength(req.sourceSize);
                requireHypothesis(*mesh, s.solid, *size, "Sweep LocalLength");
            }
            else
            {
                // Prism_3D computes composite side-face submeshes itself. Every
                // side edge must have an applicable 1D algorithm, including cap
                // edges projected internally by Prism_3D. Source NETGEN and the
                // more-local vertical segment hypotheses still take precedence.
                auto* size = make(new StdMeshers_LocalLength(gen->GetANewId(), gen));
                size->SetLength(req.sourceSize);
                requireHypothesis(*mesh, s.solid, *sweepRegular, "Regular_1D sweep default");
                requireHypothesis(*mesh, s.solid, *size, "LocalLength sweep default");
            }
            TopTools_IndexedMapOfShape caps, edges;
            TopExp::MapShapes(s.source, TopAbs_EDGE, caps); TopExp::MapShapes(s.target, TopAbs_EDGE, caps);
            TopExp::MapShapes(s.solid, TopAbs_EDGE, edges);
            TopExp::MapShapes(s.solid, TopAbs_EDGE, sweepEdges);
            TopExp::MapShapes(s.solid, TopAbs_FACE, sweepFaces);
            for (int e = 1; e <= edges.Extent(); ++e)
            {
                if (caps.Contains(edges(e))) continue;
                int id = layerEdges.Add(edges(e));
                if (edgeLayers.count(id))
                {
                    if (edgeLayers[id] != req.layers)
                        throw std::runtime_error("Incompatible layer counts on shared edge near V" + std::to_string(s.label));
                    continue;
                }
                edgeLayers[id] = req.layers;
                if (sweepRegular)
                    requireHypothesis(*mesh, edges(e), *sweepRegular, "Regular_1D on sweep layer edge");
                auto* layers = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
                layers->SetNumberOfSegments(req.layers);
                requireHypothesis(*mesh, edges(e), *layers, "Shared sweep layer count");
            }
            const int sourceId = sourceFaces.Add(s.source);
            const std::pair<bool, double> settings{req.quadrangleSource != 0, req.sourceSize};
            if (inheritedSource[i])
            {
                // This exact face will be meshed by the preceding Prism_3D.
            }
            else if (sourceSettings.count(sourceId))
            {
                if (sourceSettings[sourceId] != settings)
                    throw std::runtime_error("Incompatible source settings on a shared face near V" + std::to_string(s.label));
            }
            else
            {
                sourceSettings[sourceId] = settings;
                if (preferHex)
                {
                    // Match the SALOME GUI Source-face SubMesh exactly:
                    // NETGEN 1D-2D owns both the Source boundary edges and the
                    // Quad-dominated surface. No extra horizontal 1D hypothesis
                    // is imposed by the bridge. QuadAllowed permits triangles
                    // around holes while keeping the source Hex-dominant.
                    auto* source2d = make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(), gen));
                    requireHypothesis(*mesh, s.source, *source2d, "NETGEN_1D2D quad-dominated sweep source");
                    auto* params = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
                    configureSweepSource(params, req.sourceSize);
                    params->SetQuadAllowed(req.quadrangleSource != 0);
                    requireHypothesis(*mesh, s.source, *params, "NETGEN sweep source parameters");
                }
                else
                {
                    SMESH_Hypothesis* source2d = req.quadrangleSource
                        ? static_cast<SMESH_Hypothesis*>(make(new StdMeshers_Quadrangle_2D(gen->GetANewId(), gen)))
                        : static_cast<SMESH_Hypothesis*>(make(new NETGENPlugin_NETGEN_2D_ONLY(gen->GetANewId(), gen)));
                    requireHypothesis(*mesh, s.source, *source2d, "Sweep source 2D");
                    if (!req.quadrangleSource)
                    {
                        auto* params = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
                        configureSweepSource(params, req.sourceSize);
                        requireHypothesis(*mesh, s.source, *params, "Triangle source parameters");
                    }
                }
            }
            // Sweep-side boundary layers are created on the Source face only.
            // ViscousLayers2D builds near-wall 2D layers along Source boundary
            // edges; Prism_3D then carries that 2D pattern through the volume.
            // This is the native, non-conflicting way to combine Sweep and wall
            // layers. Source/Target cap faces themselves are intentionally not
            // treated as wall layers in this simple workflow.
            if (request->boundaryLayerEnabled && preferHex && !inheritedSource[i])
            {
                TopTools_IndexedMapOfShape sourceBoundaryEdges;
                TopExp::MapShapes(s.source, TopAbs_EDGE, sourceBoundaryEdges);
                std::set<int> viscousEdgeIds;
                TopTools_IndexedMapOfShape solidBoundaryFaces;
                TopExp::MapShapes(s.solid, TopAbs_FACE, solidBoundaryFaces);
                for (int sf = 1; sf <= solidBoundaryFaces.Extent(); ++sf)
                {
                    const TopoDS_Shape side = solidBoundaryFaces(sf);
                    if (side.IsSame(s.source) || side.IsSame(s.target))
                    {
                        if (!request->automaticWall && selectedBoundaryFaces.Contains(side))
                            ++boundaryLayerSkippedSweepCaps;
                        continue;
                    }
                    if (!wantsBoundaryLayerOnFace(side,s.original)) continue;
                    TopTools_IndexedMapOfShape sideEdges;
                    TopExp::MapShapes(side, TopAbs_EDGE, sideEdges);
                    bool contributes = false;
                    for (int se = 1; se <= sideEdges.Extent(); ++se)
                    {
                        const TopoDS_Shape edge = sideEdges(se);
                        if (!sourceBoundaryEdges.Contains(edge)) continue;
                        const int edgeShapeId = mesh->GetMeshDS()->ShapeToIndex(edge);
                        if (edgeShapeId > 0)
                        {
                            viscousEdgeIds.insert(edgeShapeId);
                            contributes = true;
                        }
                    }
                    if (contributes) ++boundaryLayerSweepSideFaces;
                }
                if (!viscousEdgeIds.empty())
                {
                    auto* viscous2d = make(makeHypothesisCompat<StdMeshers_ViscousLayers2D>(gen));
                    viscous2d->SetTotalThickness(request->totalThickness);
                    viscous2d->SetNumberLayers(request->boundaryLayers);
                    viscous2d->SetStretchFactor(request->stretchFactor);
                    viscous2d->SetBndShapes(std::vector<int>(viscousEdgeIds.begin(), viscousEdgeIds.end()),
                                            /*toIgnore=*/false);
                    viscous2d->SetMethod(static_cast<StdMeshers_ViscousLayers::ExtrusionMethod>(request->extrusionMethod));
                    requireHypothesis(*mesh, s.source, *viscous2d,
                                      "ViscousLayers2D on Sweep Source");
                    log("BoundaryLayer on Sweep V" + std::to_string(s.label) +
                        ": Source ViscousLayers2D edges=" + std::to_string(viscousEdgeIds.size()) +
                        ", layers=" + std::to_string(request->boundaryLayers) + ".");
                }
            }

            // In Hex-dominant mode do not attach a second explicit
            // Projection_1D2D SubMesh to the target cap. Prism_3D already owns
            // the native bottom-to-top 1D/2D projection. Keeping an explicit
            // target projection here can encounter target edges already carrying
            // nodes and fail with "Different number of nodes on edges" before
            // Prism_3D gets a chance to handle the cap itself.
            //
            // Preserve the historical explicit target projection only in the
            // legacy non-Hex path.
            if (!preferHex)
            {
                auto* targetProjection =
                    make(new StdMeshers_Projection_1D2D(gen->GetANewId(), gen));
                requireHypothesis(*mesh, s.target, *targetProjection,
                                  "Projection_1D2D on sweep Target");

                auto* projectionSource =
                    make(new StdMeshers_ProjectionSource2D(gen->GetANewId(), gen));
                projectionSource->SetSourceFace(s.source);
                requireHypothesis(*mesh, s.target, *projectionSource,
                                  "ProjectionSource2D on sweep Target");
            }

            auto* prism = make(new StdMeshers_Prism_3D(gen->GetANewId(), gen));
            requireHypothesis(*mesh, s.solid, *prism, "Prism_3D");
            log("SALOME Extrusion 3D SubMeshes V" + std::to_string(s.label) +
                (preferHex
                    ? std::string(": Source Face=NETGEN 1D-2D ") + (req.quadrangleSource ? "QuadAllowed" : "Triangle") + "; Target cap=Prism_3D native projection; vertical Edges=NumberOfSegments; Solid=Prism_3D"
                    : ": Source Face=2D; Target Face=Projection 1D-2D; vertical Edges=NumberOfSegments; Solid=Prism_3D"));
        }
        // Generic size controls coexist with Sweep, but Sweep owns its own
        // topology. Therefore controls mapped onto a Sweep solid/face/edge are
        // intentionally skipped: Source size + layer count remain the single
        // source of truth there. Controls on the remaining Tet topology are
        // native SMESH SubMeshes configured before the first Compute().
        std::map<int, double> localVolumeTargets;
        std::map<int, std::pair<TopoDS_Shape, double>> localFaceTargets;
        // Rules on swept geometry must never be silently treated as applied.
        // A sweep owns its source/caps and vertical layer topology. The generic
        // tet controls below may only modify genuinely non-swept topology.
        // Users can still change SourceSize/layers on the Sweep task itself.
        int rejectedSweepLocalSizes = 0;
        int rejectedSweepEdges = 0;
        int skippedRemoteLocalSizes = 0;
        int skippedRemoteEdges = 0;

        if (preferHex)
        {
            auto mergeFaceTarget = [&](const TopoDS_Shape& face, double target)
            {
                if (face.IsNull()) throw std::runtime_error("Local Face size resolved to a null face");
                if (sweepFaces.Contains(face))
                {
                    // Applied to the SourceSize before the Sweep hypotheses were
                    // built. Side-wall rules have already been rejected above.
                    ++rejectedSweepLocalSizes;
                    return;
                }
                const int faceId = mesh->GetMeshDS()->ShapeToIndex(face);
                if (faceId <= 0) throw std::runtime_error("Local Face size mapped outside SMESH mesh shape");
                const auto it = localFaceTargets.find(faceId);
                if (it == localFaceTargets.end() || target < it->second.second)
                    localFaceTargets[faceId] = {face, target};
            };

            for (int c = 0; c < request->localSizeCount; ++c)
            {
                const auto& ctrl = request->localSizes[c];
                if ((ctrl.dimension != 2 && ctrl.dimension != 3) ||
                    !std::isfinite(ctrl.targetSize) || ctrl.targetSize <= 0.0 ||
                    ctrl.shapeIndex <= 0)
                    throw std::invalid_argument("Invalid mixed local size control");

                const TopAbs_ShapeEnum shapeType = ctrl.dimension == 2 ? TopAbs_FACE : TopAbs_SOLID;
                TopTools_IndexedMapOfShape mappedShapes;
                mappedShapes = finalImages(indexedShape(imported, shapeType, ctrl.shapeIndex, "Mixed local size"), shapeType);
                if (mappedShapes.IsEmpty())
                {
                    if (request->neighboursOnly) { ++skippedRemoteLocalSizes; continue; }
                    throw std::runtime_error("Cannot trace mixed local size source through CAD Partition");
                }
                for (int mi = 1; mi <= mappedShapes.Extent(); ++mi)
                {
                    const TopoDS_Shape& mapped = mappedShapes(mi);
                    if (ctrl.dimension == 2)
                    {
                        mergeFaceTarget(mapped, ctrl.targetSize);
                        continue;
                    }
                    bool sweepOwned = false;
                    for (const auto& sw : sweeps)
                        if (mapped.IsSame(sw.solid)) { sweepOwned = true; break; }
                    if (sweepOwned)
                    {
                        // This refinement was merged into Sweep SourceSize before
                        // assigning the Source Face hypotheses.
                        ++rejectedSweepLocalSizes;
                        continue;
                    }
                    const int solidId = mesh->GetMeshDS()->ShapeToIndex(mapped);
                    if (solidId <= 0) throw std::runtime_error("Local Solid size outside mixed SMESH shape");
                    const auto vit = localVolumeTargets.find(solidId);
                    if (vit == localVolumeTargets.end() || ctrl.targetSize < vit->second)
                        localVolumeTargets[solidId] = ctrl.targetSize;
                    // A Solid rule belongs to its native Solid hypothesis.
                    // Expanding it into Face rules would apply root curvature/
                    // fineness and override this SubMesh's own surface settings.
                }
            }

            for (int c = 0; c < request->edgeSegmentCount; ++c)
            {
                const auto& ctrl = request->edgeSegments[c];
                if (ctrl.divisions < 1 || !std::isfinite(ctrl.bias) || ctrl.bias <= 0.0 ||
                    ctrl.shapeIndex <= 0)
                    throw std::invalid_argument("Invalid mixed Edge size control");
                TopTools_IndexedMapOfShape mappedEdges;
                mappedEdges = finalImages(indexedShape(imported, TopAbs_EDGE, ctrl.shapeIndex, "Mixed edge"), TopAbs_EDGE);
                if (mappedEdges.IsEmpty())
                {
                    if (request->neighboursOnly) { ++skippedRemoteEdges; continue; }
                    throw std::runtime_error("Cannot trace local Edge through CAD Partition");
                }
                for (int ei = 1; ei <= mappedEdges.Extent(); ++ei)
                {
                    const TopoDS_Shape& edge = mappedEdges(ei);
                    if (sweepEdges.Contains(edge) && layerEdges.Contains(edge))
                    {
                        const int layerId = layerEdges.FindIndex(edge);
                        if (layerId <= 0 || edgeLayers.at(layerId) != ctrl.divisions ||
                            std::abs(ctrl.bias - 1.0) > 1.0e-12)
                            throw std::runtime_error(
                                "Sweep vertical Edge segmentation conflicts with its layer count/bias. "
                                "Change the Sweep layers instead.");
                        ++rejectedSweepEdges; // Equivalent rule already applied by Sweep.
                        continue;
                    }
                    // Sweep cap edges can be constrained using native Regular_1D
                    // before computing the quad-dominated Source Face.
                    auto* regular = make(new StdMeshers_Regular_1D(gen->GetANewId(), gen));
                    auto* segments = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
                    segments->SetNumberOfSegments(ctrl.divisions);
                    if (std::abs(ctrl.bias - 1.0) > 1.0e-12)
                    {
                        segments->SetDistrType(StdMeshers_NumberOfSegments::DT_Scale);
                        segments->SetScaleFactor(ctrl.bias);
                    }
                    requireHypothesis(*mesh, edge, *regular, "Regular_1D local Edge in mixed mesh");
                    requireHypothesis(*mesh, edge, *segments, "NumberOfSegments local Edge in mixed mesh");
                }
            }

            std::map<int,CaeSMESHApplicationFaceSettings> localFaceSettings;
            for(int c=0;c<faceSettingCount;++c) {
                const auto& setting=faceSettings[c];
                const auto images=finalImages(indexedShape(imported,TopAbs_FACE,setting.shapeIndex,"Face SubMesh settings"),TopAbs_FACE);
                for(int i=1;i<=images.Extent();++i)localFaceSettings[mesh->GetMeshDS()->ShapeToIndex(images(i))]=setting;
            }
            for (const auto& kv : localFaceTargets)
            {
                const auto& item = kv.second;
                auto* faceAlgo = make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(), gen));
                auto* faceHyp = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
                configureMixedNetgen(faceHyp, item.second);
                const auto inherited=localFaceSettings.find(kv.first);
                if(inherited!=localFaceSettings.end()) {
                    const auto& value=inherited->second;
                    faceHyp->SetMinSize(value.minimum);faceHyp->SetFineness(static_cast<NETGENPlugin_Hypothesis::Fineness>(value.fineness));
                    faceHyp->SetOptimize(value.optimize!=0);faceHyp->SetSurfaceCurvature(value.curvature!=0);
                    if(value.curvatureSegments>0){faceHyp->SetNbSegPerRadius(value.curvatureSegments/6.283185307179586);faceHyp->SetSurfaceCurvature(true);}
                }
                faceHyp->SetQuadAllowed(false);
                requireHypothesis(*mesh, item.first, *faceAlgo, "NETGEN_2D local Face in mixed mesh");
                requireHypothesis(*mesh, item.first, *faceHyp, "NETGEN local Face parameters in mixed mesh");
            }

            log("Mixed local controls: Tet Face/Solid targets=" + std::to_string(localFaceTargets.size()) +
                ", Tet Solid targets=" + std::to_string(localVolumeTargets.size()) +
                ", Edge controls=" + std::to_string(request->edgeSegmentCount - rejectedSweepEdges - skippedRemoteEdges) +
                "; Sweep size rules resolved at source=" + std::to_string(rejectedSweepLocalSizes) +
                ", equivalent vertical-edge rules=" + std::to_string(rejectedSweepEdges) +
                "; skipped outside local diagnostic: size=" + std::to_string(skippedRemoteLocalSizes) +
                ", edge=" + std::to_string(skippedRemoteEdges) + ".");
        }

        // Configure the remaining Tet regions exactly as SALOME GUI Solid
        // SubMeshes: one FULL NETGEN 1D-2D-3D algorithm/hypothesis on each
        // remaining SOLID. More-local Sweep Source/vertical SubMeshes override
        // this solid-level algorithm where applicable; target caps are created
        // by Prism_3D itself and are then reused as the Sweep/Tet interface.
        if (preferHex)
        {
            auto* fullTet = make(new NETGENPlugin_NETGEN_2D3D(gen->GetANewId(), gen));

            std::size_t tetSolidsConfigured = 0;
            for (int i : included)
            {
                if (sweepOriginals.count(i)) continue;
                const TopoDS_Shape& solid = solids[i - 1];
                const int solidId = mesh->GetMeshDS()->ShapeToIndex(solid);
                double solidSize = request->meshSize;
                const auto local = localVolumeTargets.find(solidId);
                if (local != localVolumeTargets.end()) solidSize = local->second;

                auto* fullHyp = make(new NETGENPlugin_Hypothesis(gen->GetANewId(), gen));
                configureMixedNetgen(fullHyp, solidSize, i);
                fullHyp->SetQuadAllowed(false);
                requireHypothesis(*mesh, solid, *fullTet,
                                  "FULL NETGEN 1D-2D-3D on remaining Solid SubMesh");
                requireHypothesis(*mesh, solid, *fullHyp,
                                  "FULL NETGEN parameters on remaining Solid SubMesh");

                if (request->boundaryLayerEnabled)
                {
                    std::vector<int> wallIds;
                    TopTools_IndexedMapOfShape solidFacesForLayer;
                    TopExp::MapShapes(solid, TopAbs_FACE, solidFacesForLayer);
                    for (int f = 1; f <= solidFacesForLayer.Extent(); ++f)
                    {
                        const TopoDS_Shape face = solidFacesForLayer(f);
                        if (!wantsBoundaryLayerOnFace(face,i)) continue;
                        const int faceShapeId = mesh->GetMeshDS()->ShapeToIndex(face);
                        if (faceShapeId > 0)
                        {
                            wallIds.push_back(faceShapeId);
                            ++boundaryLayerTetFaces;
                        }
                    }
                    if (!wallIds.empty())
                    {
                        auto* viscous = make(new StdMeshers_ViscousLayers(gen->GetANewId(), gen));
                        viscous->SetTotalThickness(request->totalThickness);
                        viscous->SetNumberLayers(request->boundaryLayers);
                        viscous->SetStretchFactor(request->stretchFactor);
                        viscous->SetBndShapes(wallIds, /*toIgnore=*/false);
                        viscous->SetMethod(static_cast<StdMeshers_ViscousLayers::ExtrusionMethod>(request->extrusionMethod));
                        requireHypothesis(*mesh, solid, *viscous,
                                          "ViscousLayers on remaining Tet Solid");
                    }
                }
                ++tetSolidsConfigured;
            }
            // Native priority resolves competing local all-dimensional algorithms.
            // Include seed faces: a volume-only list moves them behind Prism_3D.
            // Compute is still called once on the parent; SMESH owns traversal.
            TListOfInt nativePriority;
            TopTools_IndexedMapOfShape seedFaces;
            for (int sweepIndex : sweepOrder)
            {
                const auto& s = sweeps[sweepIndex];
                if (!inheritedSource[sweepIndex] && !seedFaces.Contains(s.source))
                {
                    seedFaces.Add(s.source);
                    nativePriority.push_back(mesh->GetSubMesh(s.source)->GetId());
                }
            }
            for (int sweepIndex : sweepOrder)
                nativePriority.push_back(mesh->GetSubMesh(sweeps[sweepIndex].solid)->GetId());
            for (int i : included)
                if (!sweepOriginals.count(i))
                    nativePriority.push_back(mesh->GetSubMesh(solids[i - 1])->GetId());
            TListOfListOfInt meshOrder;
            if (nativePriority.size() > 1) meshOrder.push_back(nativePriority);
            mesh->SetMeshOrder(meshOrder);
            log("All local SubMeshes configured before first Compute: remaining FULL NETGEN Solid SubMeshes=" +
                std::to_string(tetSolidsConfigured) + ". Native priority includes source-face and solid SubMeshes.");
        }

        if (request->boundaryLayerEnabled)
        {
            const std::int64_t configured = boundaryLayerSweepSideFaces + boundaryLayerTetFaces;
            if (configured <= 0)
                throw std::runtime_error(
                    "BoundaryLayer did not hit any Sweep side wall or remaining Tet external wall. "
                    "Sweep Source/Target caps are not wall-layer targets in the simple mixed workflow.");
            log("Mixed BoundaryLayer configured: Sweep side walls=" +
                std::to_string(boundaryLayerSweepSideFaces) +
                ", Tet external walls=" + std::to_string(boundaryLayerTetFaces) +
                ", skipped selected Sweep caps=" + std::to_string(boundaryLayerSkippedSweepCaps) + ".");
        }

        auto snapshotSweep = [&]() {
            std::map<smIdType, std::vector<std::tuple<smIdType, double, double, double>>> state;
            for (const auto& s : sweeps)
            {
                auto* sub = mesh->GetSubMesh(s.solid)->GetSubMeshDS();
                if (!sub || !sub->NbElements()) throw std::runtime_error("Empty swept volume");
                auto it = sub->GetElements();
                while (it->more())
                {
                    const auto* cell = it->next();
                    if (cell->GetType() != SMDSAbs_Volume) continue;
                    auto& values = state[cell->GetID()];
                    for (int n = 0; n < cell->NbNodes(); ++n)
                    {
                        const auto* node = cell->GetNode(n);
                        values.emplace_back(node->GetID(), node->X(), node->Y(), node->Z());
                    }
                }
            }
            return state;
        };

        if (preferHex)
        {
            for (int i : included)
            {
                auto observer = std::make_unique<NativeDomainProgress>([&, i]() {
                    log("原生域进度：" + std::to_string(++nativeCompletedDomains) + "/" +
                        std::to_string(included.size()) + "；已完成实体索引 " + std::to_string(i));
                });
                auto* sub = mesh->GetSubMesh(solids[i - 1]);
                sub->SetEventListener(observer.get(), nullptr, sub);
                nativeProgress.push_back(std::move(observer));
            }
            log("Compute parent Mesh once; native SMESH manages SubMesh dependencies with source-aware native priority.");
            if (!gen->Compute(*mesh, meshShape))
            {
                countElements(*mesh, result->elements);
                std::string failedDomains;
                for (int i : included)
                {
                    auto* sub = mesh->GetSubMesh(solids[i - 1]);
                    if (sub->GetComputeState() == SMESH_subMesh::FAILED_TO_COMPUTE)
                    {
                        const std::string detail = " [original solid=" + std::to_string(i) +
                            ", CAD shape=" + std::to_string(sub->GetId()) + "]" +
                            computeError(*mesh, solids[i - 1]);
                        log("Native failed domain:" + detail);
                        if (const auto error = sub->GetComputeError())
                        {
                            std::istringstream description(error->myComment);
                            std::string token;
                            while (description >> token)
                            {
                                if (token != "face") continue;
                                long long id = 0;
                                if (!(description >> id)) break;
                                const auto* element = mesh->GetMeshDS()->FindElement(id);
                                if (!element) continue;
                                std::ostringstream coordinates;
                                coordinates << "Failed face " << id << " CAD=" << element->GetShapeID() << " nodes:";
                                for (int n = 0; n < element->NbNodes(); ++n)
                                {
                                    const auto* point = element->GetNode(n);
                                    coordinates << " (" << point->X() << ',' << point->Y() << ',' << point->Z() << ')';
                                }
                                log(coordinates.str());
                            }
                        }
                        if (failedDomains.size() < 2400) failedDomains += detail;
                    }
                    else if (sub->IsMeshComputed())
                    {
                        if (sweepOriginals.count(i)) ++result->sweptVolumes;
                        else ++result->tetVolumes;
                    }
                }
                // Keep failed cells separate from the requested mesh: they are
                // useful for locating cap/transition intersections, but must
                // never be imported as a completed engineering mesh.
                const char* keepFailed = std::getenv("CAE_SMESH_KEEP_FAILED_MESH");
                if (keepFailed && std::string(keepFailed)=="1" && request->vtkPathUtf8 && *request->vtkPathUtf8)
                {
                    auto diagnostic = utf8Path(request->vtkPathUtf8);
                    diagnostic += ".partial.vtk";
                    try { writeLegacyVtk(*mesh, diagnostic, regions); }
                    catch (const std::exception& e) { log(std::string("Partial-mesh diagnostic unavailable: ") + e.what()); }
                }
                throw std::runtime_error("Native SMESH parent Compute failed" +
                    (failedDomains.empty() ? computeError(*mesh, meshShape) : failedDomains));
            }

            for (int i : included)
            {
                auto* sub = mesh->GetSubMesh(solids[i - 1])->GetSubMeshDS();
                std::size_t volumeCount = 0;
                if (sub)
                {
                    auto cells = sub->GetElements();
                    while (cells->more())
                        if (cells->next()->GetType() == SMDSAbs_Volume) ++volumeCount;
                }
                if (!volumeCount)
                    throw std::runtime_error("Native SMESH produced no volume cells on original solid " +
                                             std::to_string(i) + computeError(*mesh, solids[i - 1]));
                if (sweepOriginals.count(i)) ++result->sweptVolumes;
                else ++result->tetVolumes;
            }
            countElements(*mesh, result->elements);
            log("Native parent Compute completed: Sweep SubMeshes=" + std::to_string(result->sweptVolumes) +
                ", Tet SubMeshes=" + std::to_string(result->tetVolumes) + ".");
        }
        else
        {
            // Legacy mode keeps the historical staged Compute sequence.
            for (int sweepIndex : sweepOrder)
            {
                const auto& s = sweeps[sweepIndex];
                log("Sweep V" + std::to_string(s.label) + " started");
                if (!gen->Compute(*mesh, s.solid, SMESH_Gen::SHAPE_ONLY))
                {
                    if (mesh->NbHexas() + mesh->NbPrisms() > 0)
                    {
                        countElements(*mesh, result->elements);
                        writeLegacyVtk(*mesh, utf8Path(request->sweepVtkPathUtf8), regions);
                        log("SWEEP_PREVIEW_READY");
                    }
                    throw std::runtime_error("Sweep V" + std::to_string(s.label) + " failed" + computeError(*mesh, s.solid));
                }
                ++result->sweptVolumes;
            }
            countElements(*mesh, result->elements);
            if (!sweeps.empty())
            {
                writeLegacyVtk(*mesh, utf8Path(request->sweepVtkPathUtf8), regions);
                log("SWEEP_PREVIEW_READY");
            }
            const auto preservedSweep = snapshotSweep();
            // Legacy mixed mode: explicitly mesh non-sweep faces, then run the
            // 3D-only NETGEN algorithm.  Kept for ABI/behavior compatibility.
            auto* tri = make(new NETGENPlugin_NETGEN_2D_ONLY(gen->GetANewId(), gen));
            auto* hyp2d = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
            configureMixedNetgen(hyp2d, request->meshSize);
            auto* tet = make(new NETGENPlugin_NETGEN_3D(gen->GetANewId(), gen));
            auto* hyp3d = make(new NETGENPlugin_Hypothesis(gen->GetANewId(), gen));
            configureMixedNetgen(hyp3d, request->meshSize);
            for (const auto& entry : owners)
            {
                bool active = false;
                for (int i : entry.second) active |= included.count(i) != 0;
                if (!active || sweepFaces.Contains(faces(entry.first))) continue;
                requireHypothesis(*mesh, faces(entry.first), *tri, "NETGEN_2D");
                requireHypothesis(*mesh, faces(entry.first), *hyp2d, "NETGEN 2D parameters");
            }
            for (int i : included)
            {
                if (sweepOriginals.count(i)) continue;
                requireHypothesis(*mesh, solids[i - 1], *tet, "NETGEN_3D");
                requireHypothesis(*mesh, solids[i - 1], *hyp3d, "NETGEN 3D parameters");
            }
            std::size_t surfaceNumber = 0;
            for (const auto& entry : owners)
            {
                bool active = false;
                for (int i : entry.second) active |= included.count(i) != 0;
                if (!active || sweepFaces.Contains(faces(entry.first))) continue;
                ++surfaceNumber;
                const auto& face = faces(entry.first);
                if (surfaceNumber == 1 || surfaceNumber % 10 == 0)
                    log("Remaining 2D boundaries: face " + std::to_string(surfaceNumber) +
                        ", CADFaceId=" + std::to_string(entry.first));
                if (!gen->Compute(*mesh, face, SMESH_Gen::SHAPE_ONLY))
                {
                    GProp_GProps faceProps;
                    BRepGProp::SurfaceProperties(face, faceProps);
                    const auto faceCenter = faceProps.CentreOfMass();
                    std::ostringstream detail;
                    detail << std::setprecision(12) << "NETGEN_2D failed on CADFaceId=" << entry.first
                           << ", area=" << faceProps.Mass()
                           << ", center=(" << faceCenter.X() << "," << faceCenter.Y() << "," << faceCenter.Z() << ")"
                           << ", original solids=";
                    for (int owner : entry.second) detail << owner << ' ';
                    detail << computeError(*mesh, face);
                    if (!reportPath.empty())
                    {
                        const auto failedFacePath = reportPath.parent_path() / "failed_boundary_face.brep";
                        std::ofstream faceFile(failedFacePath, std::ios::binary);
                        if (faceFile) BRepTools::Write(face, faceFile);
                    }
                    throw std::runtime_error(detail.str());
                }
            }
            for (int i : included)
            {
                if (sweepOriginals.count(i)) continue;
                log("Remaining original solid " + std::to_string(i) + ": NETGEN Tet + pyramid transition started");
                if (!gen->Compute(*mesh, solids[i - 1], SMESH_Gen::SHAPE_ONLY))
                    throw std::runtime_error("NETGEN failed on original solid " + std::to_string(i) + computeError(*mesh, solids[i - 1]));
                ++result->tetVolumes;
                countElements(*mesh, result->elements);
            }
            if (preservedSweep != snapshotSweep())
                throw std::runtime_error("Tet meshing changed the existing swept cells/nodes; final mesh not published");
            log("Sweep preservation check passed: all swept cell connectivity and node coordinates unchanged.");
        }
        countElements(*mesh, result->elements);
        if (preferHex)
        {
            auto volumes = mesh->GetMeshDS()->volumesIterator();
            while (volumes->more())
            {
                const auto* cell = volumes->next();
                const auto type = cell->GetEntityType();
                if (type != SMDSEntity_Tetra && type != SMDSEntity_Pyramid &&
                    type != SMDSEntity_Hexa && type != SMDSEntity_Penta)
                    throw std::runtime_error("Hex-dominant/Tet policy violated: final mesh contains unsupported volume cells");
            }
            if (!sweeps.empty() &&
                result->elements.hexahedra + result->elements.prisms == 0)
                throw std::runtime_error("Sweep preservation failed: final mesh contains no Hex/Prism although Sweep volumes were configured");
            log("Cell type check passed: Hex/Prism sweep + Tet/Pyramid remainder. Final Hex=" + std::to_string(result->elements.hexahedra) +
                ", Prism=" + std::to_string(result->elements.prisms) + ".");
        }
        if (preferHex)
            log("One parent SMESH_Mesh was used throughout; all SubMeshes were preconfigured before computation, then one native parent Compute managed dependencies; no staged Compute or automatic conversion/retry.");

        // Index only expected interface facets, keeping verification memory
        // proportional to the interfaces instead of all tetrahedral faces.
        using Facet = std::array<smIdType, 5>;
        struct Interface { int face; std::set<int> expected; std::vector<int> actual; };
        std::map<Facet, Interface> interfaces;
        auto facetKey = [](const SMDS_MeshNode* const* nodes, int count) {
            Facet key{};
            if (count < 3 || count > 4) throw std::runtime_error("Only linear tri/quad interfaces are supported");
            key[0] = count;
            for (int i = 0; i < count; ++i) key[i + 1] = nodes[i]->GetID();
            std::sort(key.begin() + 1, key.begin() + 1 + count);
            return key;
        };
        for (const auto& entry : owners)
        {
            if (entry.second.size() != 2 || !included.count(entry.second[0]) || !included.count(entry.second[1])) continue;
            ++result->interfaceFaces;
            auto* sub = mesh->GetSubMesh(faces(entry.first))->GetSubMeshDS();
            if (!sub || sub->NbElements() == 0)
                throw std::runtime_error("Empty interface CAD face " + std::to_string(entry.first));
            auto it = sub->GetElements();
            while (it->more())
            {
                const auto* face = it->next();
                if (face->GetType() != SMDSAbs_Face) continue;
                std::vector<const SMDS_MeshNode*> nodes;
                for (int j = 0; j < face->NbNodes(); ++j) nodes.push_back(face->GetNode(j));
                Interface check{entry.first, {}, {}};
                for (int i : entry.second) check.expected.insert(mesh->GetMeshDS()->ShapeToIndex(solids[i - 1]));
                if (!interfaces.emplace(facetKey(nodes.data(), int(nodes.size())), check).second)
                    throw std::runtime_error("Duplicate interface mesh face");
                ++result->interfaceElements;
            }
        }
        auto cells = mesh->GetMeshDS()->volumesIterator();
        while (cells->more())
        {
            const auto* cell = cells->next();
            SMDS_VolumeTool volume(cell);
            for (int f = 0; f < volume.NbFaces(); ++f)
            {
                auto it = interfaces.find(facetKey(volume.GetFaceNodes(f), volume.NbFaceNodes(f)));
                if (it != interfaces.end()) it->second.actual.push_back(cell->GetShapeID());
            }
        }
        for (const auto& entry : interfaces)
        {
            const auto& c = entry.second;
            if (c.actual.size() == 2 && std::set<int>(c.actual.begin(), c.actual.end()) == c.expected)
                ++result->matchedInterfaceElements;
            else
            {
                ++result->badInterfaceElements;
                if (result->badInterfaceElements <= 20)
                    log("Bad interface CAD face=" + std::to_string(c.face) + ", adjacent volume cells=" + std::to_string(c.actual.size()));
            }
        }
        log("Interface check: CAD faces=" + std::to_string(result->interfaceFaces) +
            ", facets=" + std::to_string(result->interfaceElements) +
            ", matched=" + std::to_string(result->matchedInterfaceElements) +
            ", bad=" + std::to_string(result->badInterfaceElements));
        if (result->badInterfaceElements) throw std::runtime_error("Non-conforming interfaces detected; final mesh not published");
        if (!result->interfaceElements) log("No shared interfaces in this scope: conformity is NOT demonstrated.");
        std::vector<std::pair<int,int>> uiFaceMap;
        if (request->faceIndexUiPairCount > 0 && request->faceIndexUiPairs)
        {
            std::set<std::pair<int,int>> dedup;
            for (int index = 0; index < request->faceIndexUiPairCount; ++index)
            {
                const int sourceIndex = request->faceIndexUiPairs[2 * index];
                const int uiId = request->faceIndexUiPairs[2 * index + 1];
                if (sourceIndex <= 0 || uiId <= 0) continue;
                const auto original = indexedShape(imported, TopAbs_FACE, sourceIndex, "UI boundary");
                const auto images = finalImages(original, TopAbs_FACE);
                for (int j = 1; j <= images.Extent(); ++j)
                {
                    const int shapeId = mesh->GetMeshDS()->ShapeToIndex(images(j));
                    if (shapeId > 0) dedup.insert({shapeId, uiId});
                }
            }
            uiFaceMap.assign(dedup.begin(), dedup.end());
            log("Native CAD face identity propagated: " + std::to_string(uiFaceMap.size()) +
                " SMESH ShapeID -> UI face relations");
        }
        // Never publish a conformal yet geometrically distorted mixed mesh.
        // This catches loss of narrow CAD slots/fillets when a lower MinSize
        // or coarse Fineness trades too much CAD fidelity for cell count.
        // Check each included Partition solid, not only total model volume:
        // positive and negative per-domain errors otherwise cancel out.
        {
            std::map<int, double> cadVolumes;
            std::map<int, double> meshVolumes;
            std::map<int, int> labelForShape;
            for (int i : included)
            {
                const int shapeId = mesh->GetMeshDS()->ShapeToIndex(solids[i - 1]);
                if (shapeId <= 0)
                    throw std::runtime_error("Mixed volume audit: partition solid has no ShapeID");
                GProp_GProps props;
                BRepGProp::VolumeProperties(solids[i - 1], props);
                const double cad = std::abs(props.Mass());
                if (!std::isfinite(cad) || cad <= 0.0)
                    throw std::runtime_error("Mixed volume audit: invalid CAD solid volume");
                cadVolumes[shapeId] = cad;
                labelForShape[shapeId] = i;
            }
            std::int64_t invalidCells = 0;
            std::int64_t unassignedCells = 0;
            auto volumeIt = mesh->GetMeshDS()->volumesIterator();
            while (volumeIt->more())
            {
                const auto* cell = volumeIt->next();
                if (!cell) continue;
                const int shapeId = cell->GetShapeID();
                if (!cadVolumes.count(shapeId)) { ++unassignedCells; continue; }
                SMDS_VolumeTool tool(cell);
                const double volume = tool.GetSize();
                if (!std::isfinite(volume) || volume <= 0.0) { ++invalidCells; continue; }
                meshVolumes[shapeId] += volume;
            }
            if (invalidCells || unassignedCells)
                throw std::runtime_error("Mixed volume audit: invalid/unassigned cells=" +
                    std::to_string(invalidCells) + "/" + std::to_string(unassignedCells));
            double worstError = 0.0;
            int worstSolid = 0;
            for (const auto& item : cadVolumes)
            {
                const double ratio = std::abs(meshVolumes[item.first] - item.second) / item.second;
                if (ratio > worstError) { worstError = ratio; worstSolid = labelForShape[item.first]; }
            }
            log("CAD volume audit: worst solid=" + std::to_string(worstSolid) +
                " relative error=" + std::to_string(worstError * 100.0) + "%");
            if (worstError > 0.10)
                throw std::runtime_error("Mixed mesh differs from CAD by over 10% on solid " +
                    std::to_string(worstSolid) + "; increase curved-face precision or reduce the MinSize limit. No mesh published.");
            if (worstError > 0.02)
                log("WARNING: mixed CAD/mesh solid-volume difference exceeds 2%; inspect small curved features.");
        }
        writeLegacyVtk(*mesh, utf8Path(request->vtkPathUtf8), regions,
                       false, uiFaceMap);
        log("SUCCESS: mixed mesh published; sweep=" + std::to_string(result->sweptVolumes) +
            ", remaining=" + std::to_string(result->tetVolumes) + ", hex=" + std::to_string(result->elements.hexahedra) +
            ", prism=" + std::to_string(result->elements.prisms) + ", tet=" + std::to_string(result->elements.tetrahedra) +
            ", pyramid=" + std::to_string(result->elements.pyramids));
        return 0;
    }
    catch (const std::exception& ex)
    {
        log(std::string("FAILED: ") + ex.what()); setError(error, errorCapacity, ex.what()); return 1;
    }
    catch (...)
    {
        const char* message = "Unknown exception in SMESH mixed mesh";
        log(message); setError(error, errorCapacity, message); return 2;
    }
}

extern "C" CAE_SMESH_API int cae_smesh_mixed(const CaeSMESHMixedRequest* request,
                                            CaeSMESHMixedResult* result,
                                            char* error, std::size_t errorCapacity)
{
    return runConformalMesh(request, result, error, errorCapacity, false);
}

extern "C" CAE_SMESH_API int cae_smesh_hex_tet(const CaeSMESHMixedRequest* request,
                                              CaeSMESHMixedResult* result,
                                              char* error, std::size_t errorCapacity)
{
    return runConformalMesh(request, result, error, errorCapacity, true);
}

extern "C" CAE_SMESH_API int cae_smesh_mixed_with_scoped(const CaeSMESHScopedMixedRequest* request,
    CaeSMESHMixedResult* result,char* error,std::size_t errorCapacity)
{
    return runConformalMesh(request?&request->parameters:nullptr,result,error,errorCapacity,
        request && request->preferHex!=0,request?request->scopedNetgenControls:nullptr,
        request?request->scopedNetgenCount:0);
}

extern "C" CAE_SMESH_API int cae_smesh_mixed_with_precision(const CaeSMESHPrecisionMixedRequest* request,
    CaeSMESHMixedResult* result,char* error,std::size_t errorCapacity)
{
    const auto* r=request?&request->scoped:nullptr;
    return runConformalMesh(r?&r->parameters:nullptr,result,error,errorCapacity,
        r&&r->preferHex!=0,r?r->scopedNetgenControls:nullptr,r?r->scopedNetgenCount:0,
        request&&request->chordalErrorEnabled!=0,request?request->chordalError:1.0,
        request?request->controls:nullptr,request?request->controlCount:0);
}

extern "C" CAE_SMESH_API int cae_smesh_mixed_with_boundary_scope(const CaeSMESHBoundaryScopedMixedRequest* request,
    CaeSMESHMixedResult* result,char* error,std::size_t errorCapacity)
{
    const auto* p=request?&request->precision:nullptr;
    const auto* r=p?&p->scoped:nullptr;
    return runConformalMesh(r?&r->parameters:nullptr,result,error,errorCapacity,
        r&&r->preferHex!=0,r?r->scopedNetgenControls:nullptr,r?r->scopedNetgenCount:0,
        p&&p->chordalErrorEnabled!=0,p?p->chordalError:1.0,
        p?p->controls:nullptr,p?p->controlCount:0,
        request?request->boundarySolidIndices:nullptr,request?request->boundarySolidCount:0);
}
// The CAE application also meshes open surfaces. Use the same standalone SMESH
// generator and native hypotheses without requiring a containing CAD Solid.
extern "C" CAE_SMESH_API int cae_smesh_generate_workflow(const CaeSMESHWorkflowRequest* workflow,
    CaeSMESHNetgenResult* result, char* error, std::size_t capacity,
    CaeSMESHProgress progress, void* context)
{
    const auto* input=workflow?&workflow->mesh:nullptr;
    if (result) *result = {};
    setError(error, capacity, {});
    if (!input || !result || (input->dimension != 2 && input->dimension != 3) ||
        (input->elementOrder != 1 && input->elementOrder != 2)) {
        setError(error, capacity, "Invalid SMESH generation request"); return 1;
    }
    if (input->parameters.localSizeCount < 0 || input->parameters.edgeSegmentCount < 0 ||
        (input->parameters.localSizeCount && !input->parameters.localSizes) ||
        (input->parameters.edgeSegmentCount && !input->parameters.edgeSegments)) {
        setError(error, capacity, "Invalid SMESH local control arrays"); return 1;
    }
    if(workflow->sweepCount<0 || (workflow->sweepCount&&!workflow->sweeps) ||
       workflow->scopeCount<0 || (workflow->scopeCount&&!workflow->scopes)) {
        setError(error,capacity,"Invalid SMESH workflow arrays");return 1;
    }
    if(workflow->sweepCount>0) {
        if(input->dimension!=3||input->elementOrder!=1||workflow->selectedSolidCount>0) {
            setError(error,capacity,"Sweep/Tet workflows require a complete first-order parent Mesh");return 1;
        }
        const auto& p=input->parameters;
        CaeSMESHMixedRequest mixed{};mixed.brepPathUtf8=p.brepPathUtf8;mixed.vtkPathUtf8=p.vtkPathUtf8;
        const std::string preview=std::string(p.vtkPathUtf8)+".sweep.vtk",report=std::string(p.vtkPathUtf8)+".sweep.log";
        mixed.sweepVtkPathUtf8=preview.c_str();mixed.reportPathUtf8=report.c_str();
        mixed.sweeps=workflow->sweeps;mixed.sweepCount=workflow->sweepCount;
        std::vector<std::int32_t> labels;
        for(int i=0;i<workflow->sweepCount;++i)labels.push_back(workflow->sweeps[i].volumeShapeIndex);
        mixed.volumeUiIds=labels.data();mixed.meshSize=p.maxSize;mixed.tolerance=p.tolerance;
        mixed.progress=progress;mixed.progressContext=context;
        mixed.localSizes=p.localSizes;mixed.localSizeCount=p.localSizeCount;mixed.edgeSegments=p.edgeSegments;mixed.edgeSegmentCount=p.edgeSegmentCount;
        mixed.netgenMinSize=p.minSize;mixed.netgenFineness=p.fineness;mixed.netgenOptimize=p.optimize;mixed.netgenSurfaceCurvature=p.surfaceCurvature;
        mixed.netgenGrowthRate=p.growthRate;mixed.netgenNbSegPerEdge=p.nbSegPerEdge;mixed.netgenNbSegPerRadius=p.nbSegPerRadius;
        CaeSMESHMixedResult output{};
        const int status=runConformalMesh(&mixed,&output,error,capacity,workflow->preferHex!=0,workflow->scopes,workflow->scopeCount,
            false,1.,nullptr,0,nullptr,0,workflow->faces,workflow->faceCount);
        if(status==0) {
            result->nodes=output.elements.nodes;result->solids=output.sweptVolumes+output.tetVolumes;
            result->tetrahedra=output.elements.tetrahedra;result->prisms=output.elements.prisms;
            result->pyramids=output.elements.pyramids;result->hexahedra=output.elements.hexahedra;
            result->volumes=result->tetrahedra+result->prisms+result->pyramids+result->hexahedra;
        }return status;
    }
    if (input->dimension == 3) {
        std::vector<std::int32_t> surfaceSolids;
        if(workflow->explicitSurfaceScope) {
            if(workflow->surfaceSolidCount>0)surfaceSolids.assign(workflow->surfaceSolids,workflow->surfaceSolids+workflow->surfaceSolidCount);
        } else for (int i=0; i<input->parameters.localSizeCount; ++i) {
            const auto& local=input->parameters.localSizes[i];
            if (local.dimension==3 && local.shapeIndex>0)surfaceSolids.push_back(local.shapeIndex);
        }
        return runNetgen(&input->parameters, result, error, capacity, progress, context,
            workflow->faceCurvatures,workflow->faceCurvatureCount,input->parameters.minSize==0,
            workflow->selectedSolids,workflow->selectedSolidCount,workflow->scopes,workflow->scopeCount,nullptr,0,
            workflow->sessionUtf8,workflow->resetSession!=0,false,
            workflow->faceMinimums,workflow->faceMinimumCount,false,0.,
            nullptr,0,surfaceSolids.data(),int(surfaceSolids.size()),false,0,nullptr,nullptr,0,input->elementOrder);
    }
    try {
        ComputeLock lock;
        const auto& request = input->parameters;
        if (!request.brepPathUtf8 || !request.vtkPathUtf8 ||
            !std::isfinite(request.maxSize) || request.maxSize <= 0.0 ||
            !std::isfinite(request.minSize) || request.minSize < 0.0 || request.minSize > request.maxSize)
            throw std::invalid_argument("Invalid SMESH surface geometry or sizes");
        TopoDS_Shape shape; BRep_Builder builder;
        std::ifstream stream(utf8Path(request.brepPathUtf8), std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open surface BREP");
        BRepTools::Read(shape, stream, builder);
        if (shape.IsNull()) throw std::runtime_error("Cannot read surface BREP");
        TopTools_IndexedMapOfShape faces, edges;
        TopExp::MapShapes(shape, TopAbs_FACE, faces); TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        if (!faces.Extent()) throw std::runtime_error("No CAD faces to mesh");
        SMESH_Gen* gen = generator();
        std::vector<std::unique_ptr<SMESH_Hypothesis>> hypotheses;
        std::unique_ptr<SMESH_Mesh> mesh(gen->CreateMesh(true));
        mesh->ShapeToMesh(shape);
        auto make = [&](auto* item) { hypotheses.emplace_back(item); return item; };
        auto configure = [&](NETGENPlugin_Hypothesis* hyp, double size) {
            hyp->SetMaxSize(size); hyp->SetMinSize(std::min(size, request.minSize));
            hyp->SetFineness(static_cast<NETGENPlugin_Hypothesis::Fineness>(request.fineness));
            if(request.fineness==NETGENPlugin_Hypothesis::UserDefined) {
                hyp->SetGrowthRate(request.growthRate);hyp->SetNbSegPerEdge(request.nbSegPerEdge);hyp->SetNbSegPerRadius(request.nbSegPerRadius);
            }
            hyp->SetOptimize(request.optimize != 0); hyp->SetSurfaceCurvature(request.surfaceCurvature != 0);
            hyp->SetQuadAllowed(input->quadrangle != 0); hyp->SetSecondOrder(false);
        };
        auto* algo = make(new NETGENPlugin_NETGEN_2D(gen->GetANewId(), gen));
        auto* params = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
        configure(params, request.maxSize);
        requireHypothesis(*mesh, shape, *algo, "SMESH surface algorithm");
        requireHypothesis(*mesh, shape, *params, "SMESH surface parameters");
        for (int i = 0; i < request.localSizeCount; ++i) {
            const auto& control = request.localSizes[i];
            if (control.dimension != 2 || control.shapeIndex < 1 || control.shapeIndex > faces.Extent() ||
                !std::isfinite(control.targetSize) || control.targetSize <= 0.0)
                throw std::invalid_argument("Invalid native SMESH Face sizing");
            auto* local = make(new NETGENPlugin_Hypothesis_2D(gen->GetANewId(), gen));
            configure(local, control.targetSize);
            for(int f=0;f<workflow->faceCount;++f)if(workflow->faces[f].shapeIndex==control.shapeIndex){
                const auto& settings=workflow->faces[f];local->SetMinSize(settings.minimum);
                local->SetFineness(static_cast<NETGENPlugin_Hypothesis::Fineness>(settings.fineness));local->SetOptimize(settings.optimize!=0);local->SetSurfaceCurvature(settings.curvature!=0);
                if(settings.curvatureSegments>0){local->SetNbSegPerRadius(settings.curvatureSegments/6.283185307179586);local->SetSurfaceCurvature(true);}
            }
            requireHypothesis(*mesh, faces(control.shapeIndex), *local, "SMESH Face SubMesh sizing");
            mesh->GetSubMesh(faces(control.shapeIndex)); ++result->localFaces;
        }
        for (int i = 0; i < request.edgeSegmentCount; ++i) {
            const auto& control = request.edgeSegments[i];
            if (control.shapeIndex < 1 || control.shapeIndex > edges.Extent() || control.divisions < 1)
                throw std::invalid_argument("Invalid native SMESH Edge divisions");
            auto* edgeAlgo = make(new StdMeshers_Regular_1D(gen->GetANewId(), gen));
            auto* segments = make(new StdMeshers_NumberOfSegments(gen->GetANewId(), gen));
            segments->SetNumberOfSegments(control.divisions);
            if(std::abs(control.bias-1.)>1.e-12){segments->SetDistrType(StdMeshers_NumberOfSegments::DT_Scale);segments->SetScaleFactor(control.bias);}
            requireHypothesis(*mesh, edges(control.shapeIndex), *edgeAlgo, "SMESH Edge algorithm");
            requireHypothesis(*mesh, edges(control.shapeIndex), *segments, "SMESH Edge divisions");
            mesh->GetSubMesh(edges(control.shapeIndex)); ++result->localEdges;
        }
        if (progress) progress(context, "SMESH: computing conformal surface SubMeshes");
        if (!gen->CheckAlgoState(*mesh, shape) || !gen->Compute(*mesh, shape))
            throw std::runtime_error("SMESH surface Compute failed" + computeError(*mesh, shape));
        if (input->elementOrder == 2) SMESH_MeshEditor(mesh.get()).ConvertToQuadratic(false, false);
        auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        auto points = vtkSmartPointer<vtkPoints>::New(); points->SetDataTypeToDouble();
        std::unordered_map<const SMDS_MeshNode*, vtkIdType> pointIds;
        auto nodes = mesh->GetMeshDS()->nodesIterator();
        while (nodes->more()) { const auto* n = nodes->next(); pointIds[n] = points->InsertNextPoint(n->X(), n->Y(), n->Z()); }
        grid->SetPoints(points);
        std::map<int, int> shapeRegions;
        for (int i = 1; i <= faces.Extent(); ++i) shapeRegions[mesh->GetMeshDS()->ShapeToIndex(faces(i))] = i;
        auto regions = vtkSmartPointer<vtkIntArray>::New(); regions->SetName("matype");
        auto elements = mesh->GetMeshDS()->facesIterator();
        while (elements->more()) {
            const auto* cell = elements->next();
            const auto& order = SMDS_MeshCell::toVtkOrder(cell->GetVtkType());
            std::vector<vtkIdType> ids;
            for (int i = 0; i < cell->NbNodes(); ++i) ids.push_back(pointIds.at(cell->GetNode(order.empty() ? i : order.at(i))));
            grid->InsertNextCell(cell->GetVtkType(), vtkIdType(ids.size()), ids.data());
            const int region = shapeRegions[cell->GetShapeID()];
            if (!region) throw std::runtime_error("SMESH surface element has no owning CAD Face");
            regions->InsertNextValue(region);
        }
        if (!grid->GetNumberOfCells()) throw std::runtime_error("SMESH produced no surface elements");
        grid->GetCellData()->AddArray(regions);
        grid->GetFieldData()->AddArray(caeBoundaryFacets(*mesh, pointIds, 2));
        auto writer = vtkSmartPointer<vtkUnstructuredGridWriter>::New();
        writer->SetFileName(request.vtkPathUtf8); writer->SetInputData(grid); writer->SetFileTypeToBinary();
        if (!writer->Write()) throw std::runtime_error("Cannot write SMESH surface VTK");
        result->nodes = mesh->NbNodes(); result->solids = faces.Extent(); return 0;
    } catch (const std::exception& ex) { setError(error, capacity, ex.what()); return 1; }
    catch (...) { setError(error, capacity, "Unknown SMESH surface exception"); return 1; }
}

extern "C" CAE_SMESH_API int cae_smesh_generate(const CaeSMESHGenerateRequest* input,
    CaeSMESHNetgenResult* result,char* error,std::size_t capacity,CaeSMESHProgress progress,void* context)
{
    if(!input){setError(error,capacity,"Null SMESH request");return 1;}
    CaeSMESHWorkflowRequest workflow{};workflow.mesh=*input;
    return cae_smesh_generate_workflow(&workflow,result,error,capacity,progress,context);
}
