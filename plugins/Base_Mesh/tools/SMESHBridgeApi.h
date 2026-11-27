#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && defined(CAE_SMESH_BRIDGE_BUILD)
#  define CAE_SMESH_API __declspec(dllexport)
#elif defined(_WIN32)
#  define CAE_SMESH_API __declspec(dllimport)
#else
#  define CAE_SMESH_API
#endif

extern "C" {

struct CaeSMESHSweepRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    std::int32_t layers;
    double sourceSize;
    std::int32_t quadrangleSource;
    double tolerance;
    // Index in TopExp::MapShapes(ExportedModel.brep, ...); zero is invalid.
    std::int32_t volumeShapeIndex;
    std::int32_t sourceShapeIndex;
    std::int32_t targetShapeIndex;
};

struct CaeSMESHSweepResult
{
    std::int64_t nodes;
    std::int64_t triangles;
    std::int64_t quadrangles;
    std::int64_t tetrahedra;
    std::int64_t pyramids;
    std::int64_t prisms;
    std::int64_t hexahedra;
};

// SALOME/SMESH StdMeshers Body Fitting: Cartesian Hex core with
// boundary cut cells (typically polyhedra). This is a core SMESH algorithm,
// not MG-Hybrid/MG-Hexa and does not require a commercial mesher.
struct CaeSMESHBodyFittedRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    double cellSize;
    double sizeThreshold;
    std::int32_t optimalAxes;
    std::int32_t addGeometryEdges;
    std::int32_t considerInternalFaces;
    std::int32_t createFaces;
};

struct CaeSMESHBodyFittedResult
{
    std::int64_t nodes;
    std::int64_t solids;
    std::int64_t volumes;
    std::int64_t hexahedra;
    std::int64_t polyhedra;
    std::int64_t tetrahedra;
    std::int64_t pyramids;
    std::int64_t prisms;
    std::int64_t otherVolumes;
};


// SALOME/SMESH native 3D viscous layers on top of NETGEN 1D-2D-3D.
// Explicit BREP face indices are required;
// an empty wall list with automaticWall!=0 means all topological external faces.
struct CaeSMESHViscousFace
{
    std::int32_t shapeIndex; // Face index in the input BREP
};

struct CaeSMESHViscousRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    const CaeSMESHViscousFace* walls;
    std::int32_t wallCount;
    double maxSize;
    double minSize;
    double firstHeight;
    double totalThickness;
    std::int32_t layers;
    double stretchFactor;
    std::int32_t automaticWall;
    // StdMeshers_ViscousLayers::ExtrusionMethod: 0=SURF_OFFSET_SMOOTH,
    // 1=FACE_OFFSET, 2=NODE_OFFSET.
    std::int32_t extrusionMethod;
    double tolerance;
};

struct CaeSMESHViscousResult
{
    std::int64_t nodes;
    std::int64_t solids;
    std::int64_t wallFaces;
    std::int64_t volumes;
    std::int64_t tetrahedra;
    std::int64_t prisms;
    std::int64_t pyramids;
    std::int64_t hexahedra;
    std::int64_t otherVolumes;
    double actualFirstHeight;
};

// Generic native SMESH/NETGEN tetrahedral meshing. Local controls are mapped
// from Base_Geometry BREP topology indices and assigned as native
// SMESH SubMeshes/Hypotheses before the first Compute().
struct CaeSMESHLocalSizeControl
{
    std::int32_t dimension; // 2=Face, 3=Solid
    double targetSize;
    std::int32_t shapeIndex; // index in the input BREP for dimension
};

struct CaeSMESHEdgeSegmentsControl
{
    std::int32_t divisions;
    double bias; // 1=uniform, otherwise NumberOfSegments DT_Scale
    std::int32_t shapeIndex; // Edge index in the input BREP
};

struct CaeSMESHFaceCurvatureControl
{
    std::int32_t shapeIndex; // exact Face index in the input BREP
    std::int32_t elementsPer2Pi;
};

struct CaeSMESHNetgenRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    double maxSize;
    double minSize;
    std::int32_t optimize;
    std::int32_t surfaceCurvature;
    double tolerance; // CAD length units; explicit OCC Fuzzy value if autoPartition is enabled

    const CaeSMESHLocalSizeControl* localSizes;
    std::int32_t localSizeCount;
    const CaeSMESHEdgeSegmentsControl* edgeSegments;
    std::int32_t edgeSegmentCount;

    // Optional 3D ViscousLayers. If disabled, all fields below are ignored.
    std::int32_t boundaryLayerEnabled;
    const CaeSMESHViscousFace* walls;
    std::int32_t wallCount;
    double firstHeight;
    double totalThickness;
    std::int32_t layers;
    double stretchFactor;
    std::int32_t automaticWall;
    std::int32_t extrusionMethod;

    // NETGEN native fineness controls. Appended in ABI v2.
    std::int32_t fineness; // NETGENPlugin_Hypothesis::Fineness, 0..5
    double growthRate;
    double nbSegPerEdge;
    double nbSegPerRadius;
    // If automaticWall!=0, selected wall indices are exclusions when this is non-zero.
    std::int32_t selectedWallsToIgnore;
    // ABI v16: export file status and explicit preparation settings, no content hash.
    const char* geometryGenerationUtf8;
};

// ABI v6 additive request. Legacy NETGEN entries retain their original layout.
// ABI v10. Register all planned solid hypotheses before the first Compute;
// selectedSolidIndices independently limits which submeshes are computed.
// Overrides remain solid-local in the common parent Mesh.
struct CaeSMESHScopedNetgenControl
{
    std::int32_t solidShapeIndex;
    std::int32_t fineness;          // -1 = parent, otherwise 0..4
    std::int32_t surfaceCurvature;  // -1 = parent, otherwise 0/1
    std::int32_t optimize;          // -1 = parent, otherwise 0/1
    std::int32_t automaticMinSize;  // -1 = parent, otherwise 0/1
    double manualMinSize;
    // ABI v14: independently editable custom NETGEN sizing for this Solid.
    // Values <= 0 inherit the root Custom parameters.
    double growthRate;
    double nbSegPerEdge;
    double nbSegPerRadius;
};

struct CaeSMESHNetgenCurvatureRequest
{
    CaeSMESHNetgenRequest parameters;
    const CaeSMESHFaceCurvatureControl* faceCurvatures;
    std::int32_t faceCurvatureCount;
    std::int32_t automaticMinSize;
    // ABI v9: original BREP SOLID topology indices for scoped Tet computation.
    // Empty array preserves full-model behavior; parent SMESH Mesh is unchanged.
    const std::int32_t* selectedSolidIndices;
    std::int32_t selectedSolidCount;
    // Ordered Solid SubMeshes: native Mesh Order resolves shared-face settings.
    // Explicit scopes should precede the catch-all; no geometry ownership rule.
    const CaeSMESHScopedNetgenControl* scopedNetgenControls;
    std::int32_t scopedNetgenCount;
    // ABI v11: parent Mesh Main Shape. These CAD BREP indices determine
    // ShapeToMesh geometry; computed SubMesh indices are a subset of this.
    // Empty means entire input CAD, preserving existing project behavior.
    const std::int32_t* mainSolidIndices;
    std::int32_t mainSolidCount;
    // ABI v12: stable parent Mesh UUID. Empty preserves stateless legacy meshing.
    // Non-empty keeps one live SMESH_Mesh in this DLL for successive SubMesh calls.
    const char* meshSessionUtf8;
    std::int32_t resetSession; // ABI v12: root Mesh rebuilds every configured region.
    std::int32_t autoPartition; // ABI v13: optional OCC Partition, disabled by default.
};

struct CaeSMESHNetgenResult
{
    std::int64_t nodes;
    std::int64_t solids;
    std::int64_t localEdges;
    std::int64_t localFaces;
    std::int64_t localVolumes;
    std::int64_t wallFaces;
    std::int64_t volumes;
    std::int64_t tetrahedra;
    std::int64_t prisms;
    std::int64_t pyramids;
    std::int64_t hexahedra;
    std::int64_t otherVolumes;
    double actualFirstHeight;
};

// Additive capability: original ABI v14 request layouts remain unchanged.
// Each minimum belongs to an exact CAD Face, independently of the parent.
struct CaeSMESHFaceMinimumControl
{
    std::int32_t shapeIndex;
    std::int32_t mode; // -1=inherit, 0=native automatic, 1=manual
    double manualSize;
};
struct CaeSMESHNetgenFaceSizingRequest
{
    CaeSMESHNetgenCurvatureRequest netgen;
    const CaeSMESHFaceMinimumControl* faceMinimums;
    std::int32_t faceMinimumCount;
};

// Additive entry: keep all existing ABI v14 request layouts unchanged.
// Surface approximation is independent of the 3D-only sizing hypotheses.
struct CaeSMESHNetgenSurfacePrecisionRequest
{
    CaeSMESHNetgenFaceSizingRequest sizing;
    std::int32_t chordalErrorEnabled;
    double chordalError;
};

// One-sided layers: faces are boundaries of these Solid SubMeshes, even when
// shared inside the complete assembly. Empty scope preserves legacy behavior.
struct CaeSMESHNetgenBoundaryScopeRequest
{
    CaeSMESHNetgenSurfacePrecisionRequest precision;
    const std::int32_t* boundarySolidIndices;
    std::int32_t boundarySolidCount;
};
CAE_SMESH_API int cae_smesh_netgen_with_boundary_scope(
    const CaeSMESHNetgenBoundaryScopeRequest*, CaeSMESHNetgenResult*,
    char*, std::size_t, void (*)(void*, const char*), void*);

// Additive capability. Existing structs/ABI and legacy volume-only scopes stay
// unchanged. Listed Solid SubMeshes own native 2D parameters as well as 3D ones.
struct CaeSMESHNetgenDomainSizingRequest
{
    CaeSMESHNetgenBoundaryScopeRequest boundary;
    const std::int32_t* surfaceSolidIndices;
    std::int32_t surfaceSolidCount;
    std::int32_t surfaceOnly; // Native dimension-2 preview; no volume export.
    std::int64_t surfaceElementLimit; // 0=unlimited; checked before 3D.
    std::int64_t* surfaceElements; // optional actual unique SMESH face count.
};
CAE_SMESH_API int cae_smesh_netgen_with_domain_sizing(
    const CaeSMESHNetgenDomainSizingRequest*, CaeSMESHNetgenResult*,
    char*, std::size_t, void (*)(void*, const char*), void*);

// Additive capability; existing request layouts remain unchanged.
struct CaeSMESHScopedSurfacePrecision
{
    std::int32_t solidShapeIndex;
    std::int32_t chordalErrorEnabled; // 0 off, 1 local; absence inherits parent
    double chordalError;
};
struct CaeSMESHNetgenScopedPrecisionRequest
{
    CaeSMESHNetgenDomainSizingRequest sizing;
    const CaeSMESHScopedSurfacePrecision* controls;
    std::int32_t controlCount;
};
CAE_SMESH_API int cae_smesh_netgen_with_scoped_precision(
    const CaeSMESHNetgenScopedPrecisionRequest*, CaeSMESHNetgenResult*,
    char*, std::size_t, void (*)(void*, const char*), void*);

// Legacy binary-compatibility declarations only.  These APIs are not used by
// the CAE application and DO NOT provide a proximity-control capability.
// Keep the structs and exports because existing standalone clients/tests may
// have been compiled against earlier Bridge versions.
struct CaeSMESHNetgenProximityRequest
{
    CaeSMESHNetgenDomainSizingRequest sizing;
    std::int32_t closeEdgeRefinement;
};
CAE_SMESH_API int cae_smesh_netgen_with_proximity_sizing(
    const CaeSMESHNetgenProximityRequest*, CaeSMESHNetgenResult*,
    char*, std::size_t, void (*)(void*, const char*), void*);
struct CaeSMESHNetgenProximityFactorRequest
{
    CaeSMESHNetgenDomainSizingRequest sizing;
    double closeEdgeFactor;
};
CAE_SMESH_API int cae_smesh_netgen_with_proximity_factor(
    const CaeSMESHNetgenProximityFactorRequest*, CaeSMESHNetgenResult*,
    char*, std::size_t, void (*)(void*, const char*), void*);

CAE_SMESH_API std::uint32_t cae_smesh_abi_version();
// Additive ABI v9 cancellation capability for native NETGEN Tet batches.
// One process-wide SMESH compute is allowed at a time. Cancellation is observed
// between solids (not inside an individual NETGEN Compute()).
CAE_SMESH_API void cae_smesh_reset_cancel();
CAE_SMESH_API void cae_smesh_request_cancel();


// Returns zero on success. The bridge never lets a C++ exception cross the ABI.
CAE_SMESH_API int cae_smesh_sweep(const CaeSMESHSweepRequest* request,
                                  CaeSMESHSweepResult* result,
                                  char* error,
                                  std::size_t errorCapacity);

// One-click Hex-dominant Cartesian Body Fitting on the complete BREP.
// Returns Hex in the Cartesian core and cut cells near geometry boundaries.
CAE_SMESH_API int cae_smesh_body_fitted(const CaeSMESHBodyFittedRequest* request,
                                        CaeSMESHBodyFittedResult* result,
                                        char* error,
                                        std::size_t errorCapacity);

// Native NETGEN + StdMeshers_ViscousLayers. Returns zero on success.
CAE_SMESH_API int cae_smesh_viscous_layers(const CaeSMESHViscousRequest* request,
                                           CaeSMESHViscousResult* result,
                                           char* error,
                                           std::size_t errorCapacity);

// Native NETGEN 1D-2D-3D with optional SMESH SubMesh local controls and
// optional StdMeshers_ViscousLayers. Returns zero on success.
CAE_SMESH_API int cae_smesh_netgen(const CaeSMESHNetgenRequest* request,
                                   CaeSMESHNetgenResult* result,
                                   char* error,
                                   std::size_t errorCapacity);

// Additive ABI: the single-solid v1 entry point remains unchanged.
typedef void (*CaeSMESHProgress)(void* context, const char* messageUtf8);
CAE_SMESH_API int cae_smesh_netgen_with_face_sizing(
    const CaeSMESHNetgenFaceSizingRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext);
// Callers must verify cae_smesh_abi_version() before passing a NETGEN request.
CAE_SMESH_API int cae_smesh_netgen_with_progress(const CaeSMESHNetgenRequest* request,
    CaeSMESHNetgenResult* result, char* error, std::size_t errorCapacity,
    CaeSMESHProgress progress, void* progressContext);

CAE_SMESH_API int cae_smesh_netgen_with_surface_precision(
    const CaeSMESHNetgenSurfacePrecisionRequest* request, CaeSMESHNetgenResult* result,
    char* error, std::size_t errorCapacity, CaeSMESHProgress progress, void* progressContext);

CAE_SMESH_API int cae_smesh_netgen_with_curvature(const CaeSMESHNetgenCurvatureRequest* request,
    CaeSMESHNetgenResult* result, char* error, std::size_t errorCapacity,
    CaeSMESHProgress progress, void* progressContext);
struct CaeSMESHMixedRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    const char* sweepVtkPathUtf8;
    const char* reportPathUtf8;
    const CaeSMESHSweepRequest* sweeps;
    const std::int32_t* volumeUiIds;
    std::int32_t sweepCount;
    double meshSize;
    double tolerance;
    // 0: all remaining solids; 1: immediate face neighbours of the sweeps.
    std::int32_t neighboursOnly;
    CaeSMESHProgress progress;
    void* progressContext;

    // ABI v3 additive tail. Generic local size controls are applied only to
    // non-sweep topology. Sweep-owned faces/edges/solids keep their own
    // SourceSize + layer-count rules.
    const CaeSMESHLocalSizeControl* localSizes;
    std::int32_t localSizeCount;
    const CaeSMESHEdgeSegmentsControl* edgeSegments;
    std::int32_t edgeSegmentCount;

    // ABI v4 additive tail. Boundary layers may coexist with Sweep. On Sweep
    // solids, side-wall layers are seeded on the Source face by
    // ViscousLayers2D and then carried through Prism_3D. On non-Sweep solids
    // the existing 3D ViscousLayers + FULL NETGEN path is used. Source/Target
    // caps themselves are not treated as Sweep wall layers.
    std::int32_t boundaryLayerEnabled;
    const CaeSMESHViscousFace* walls;
    std::int32_t wallCount;
    double firstHeight;
    double totalThickness;
    std::int32_t boundaryLayers;
    double stretchFactor;
    std::int32_t automaticWall;
    std::int32_t selectedWallsToIgnore;
    std::int32_t extrusionMethod;
    // Flat pairs: [original BREP Face index, CAE UI face id] of this snapshot.
    // Optional for manual mesh-only partition inputs.
    const std::int32_t* faceIndexUiPairs;
    std::int32_t faceIndexUiPairCount;
    // ABI v7: explicit Tet operation assignments by original BREP Solid index.
    // 0 means legacy behavior (mesh all non-Sweep domains).
    const std::int32_t* tetShapeIndices;
    std::int32_t tetShapeIndexCount;
    std::int32_t restrictTetToSelection;
    // ABI v8: NETGEN controls for remaining Tet/2D and Sweep source faces.
    // A zero minimum keeps the native geometry-based automatic lower size.
    double netgenMinSize;
    std::int32_t netgenFineness;  // 0..5, native NETGEN preset
    std::int32_t netgenSurfaceCurvature;
    std::int32_t netgenOptimize;
    double netgenGrowthRate;     // only used when fineness == UserDefined
    double netgenNbSegPerEdge;
    double netgenNbSegPerRadius;
};
struct CaeSMESHMixedResult
{
    CaeSMESHSweepResult elements;
    std::int64_t sweptVolumes;
    std::int64_t tetVolumes;
    std::int64_t interfaceFaces;
    std::int64_t interfaceElements;
    std::int64_t matchedInterfaceElements;
    std::int64_t badInterfaceElements;
};
// Additive capability: preserve independent Tet SubMesh settings in mixed
// workflows. The ABI-13 legacy request layout and entry points stay usable.
struct CaeSMESHScopedMixedRequest
{
    CaeSMESHMixedRequest parameters;
    const CaeSMESHScopedNetgenControl* scopedNetgenControls;
    std::int32_t scopedNetgenCount;
    std::int32_t preferHex;
};
struct CaeSMESHPrecisionMixedRequest
{
    CaeSMESHScopedMixedRequest scoped;
    std::int32_t chordalErrorEnabled;
    double chordalError;
    const CaeSMESHScopedSurfacePrecision* controls;
    std::int32_t controlCount;
};
// Optional capability; legacy mixed request layouts and ABI version stay unchanged.
// Solid indices use the original input BREP order, as in Sweep/Tet assignments.
struct CaeSMESHBoundaryScopedMixedRequest
{
    CaeSMESHPrecisionMixedRequest precision;
    const std::int32_t* boundarySolidIndices;
    std::int32_t boundarySolidCount;
};
CAE_SMESH_API int cae_smesh_mixed_with_boundary_scope(const CaeSMESHBoundaryScopedMixedRequest*,
    CaeSMESHMixedResult*, char*, std::size_t);
CAE_SMESH_API int cae_smesh_mixed_with_precision(const CaeSMESHPrecisionMixedRequest*,
    CaeSMESHMixedResult*, char*, std::size_t);
CAE_SMESH_API int cae_smesh_mixed_with_scoped(const CaeSMESHScopedMixedRequest* request,
                                           CaeSMESHMixedResult* result,
                                           char* error, std::size_t errorCapacity);
CAE_SMESH_API int cae_smesh_mixed(const CaeSMESHMixedRequest* request,
                                  CaeSMESHMixedResult* result,
                                  char* error, std::size_t errorCapacity);

// Explicit opt-in policy: attempt all-Hex sweeps, classify unsuitable/failed
// candidates as Tet and allow pyramid transitions. Triangles in a quad-dominated
// source generate native prism cells alongside hexahedra.
// A remaining Tet fill failure is reported without publishing a partial mesh.
// Same request/result layout; the legacy mixed entry retains its strict policy.
CAE_SMESH_API int cae_smesh_hex_tet(const CaeSMESHMixedRequest* request,
                                    CaeSMESHMixedResult* result,
                                    char* error, std::size_t errorCapacity);

}

// CAE surface/volume adapter, additive to the imported TMSR ABI.
struct CaeSMESHGenerateRequest {
    CaeSMESHNetgenRequest parameters;
    std::int32_t dimension;
    std::int32_t quadrangle;
    std::int32_t elementOrder;
};
extern "C" CAE_SMESH_API int cae_smesh_generate(const CaeSMESHGenerateRequest*,
    CaeSMESHNetgenResult*, char*, std::size_t, CaeSMESHProgress, void*);

struct CaeSMESHApplicationFaceSettings {
    std::int32_t shapeIndex;
    double minimum;
    std::int32_t fineness;
    std::int32_t optimize;
    std::int32_t curvature;
    std::int32_t curvatureSegments;
};
struct CaeSMESHWorkflowRequest {
    CaeSMESHGenerateRequest mesh;
    const CaeSMESHScopedNetgenControl* scopes;
    std::int32_t scopeCount;
    const CaeSMESHFaceMinimumControl* faceMinimums;
    std::int32_t faceMinimumCount;
    const CaeSMESHFaceCurvatureControl* faceCurvatures;
    std::int32_t faceCurvatureCount;
    const CaeSMESHApplicationFaceSettings* faces;
    std::int32_t faceCount;
    const std::int32_t* surfaceSolids;
    std::int32_t surfaceSolidCount;
    std::int32_t explicitSurfaceScope;
    const CaeSMESHSweepRequest* sweeps;
    std::int32_t sweepCount;
    const std::int32_t* selectedSolids;
    std::int32_t selectedSolidCount;
    const char* sessionUtf8;
    std::int32_t resetSession;
    std::int32_t preferHex; // Workflow ABI v15; 0=strict native sweep, 1=hex preferred.
};
extern "C" CAE_SMESH_API int cae_smesh_generate_workflow(const CaeSMESHWorkflowRequest*,
    CaeSMESHNetgenResult*, char*, std::size_t, CaeSMESHProgress, void*);


// Conformal hybrid bridge: inject an external 2D quad mesh (e.g. from GMDS
// CurvedBlocking) onto shared interface faces of an OCC solid, freeze those
// interface edges/faces, mesh remaining free boundaries with NETGEN_2D_ONLY,
// and fill the volume with NETGEN_3D (auto-generating conformal Pyramids on
// the quad interface + Tetrahedra in the interior).
struct CaeSMESHInterfaceNode
{
    double x;
    double y;
    double z;
};

struct CaeSMESHInterfaceQuad
{
    std::int32_t faceShapeIndex; // 1-based Face index in TopExp::MapShapes(brep, TopAbs_FACE)
    std::int32_t n0;
    std::int32_t n1;
    std::int32_t n2;
    std::int32_t n3;
};

struct CaeSMESHConformalHybridRequest
{
    const char* brepPathUtf8;
    const char* vtkPathUtf8;
    const CaeSMESHInterfaceNode* nodes;
    std::int32_t nodeCount;
    const CaeSMESHInterfaceQuad* quads;
    std::int32_t quadCount;
    double maxSize;
    double minSize;
    std::int32_t optimize;
};

struct CaeSMESHConformalHybridResult
{
    std::int64_t nodes;
    std::int64_t tetrahedra;
    std::int64_t pyramids;
    std::int64_t interfaceQuadsInjected;
    std::int64_t interfaceEdgesInjected;
};

extern "C" CAE_SMESH_API int cae_smesh_conformal_tet_on_quad_interface(
    const CaeSMESHConformalHybridRequest* request,
    CaeSMESHConformalHybridResult* result,
    char* error, std::size_t errorCapacity);
