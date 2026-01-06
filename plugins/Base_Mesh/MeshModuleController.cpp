#include "MeshModuleController.h"

#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <EventManager.h>
#include <GridEditingWidget.h>
#include <MeshGlobalMeshingWidget.h>
#include <PublicConstants.h>
#include <QMessageBox>
#include <qfiledialog.h>

// === 这些头文件你后面真正实现时再加 ===
// #include "MeshImportGridWidget.h"
// #include "MeshExportGridWidget.h"
// #include "MeshGlobalMeshingWidget.h"
// #include "MeshSingleMeshingWidget.h"
// #include "MeshMappedMeshingWidget.h"
// #include "MeshSweepMeshingWidget.h"
// #include "MeshVolumeControlWidget.h"
// #include "MeshFaceControlWidget.h"
// #include "MeshEdgeControlWidget.h"
// #include "MeshQualityStandardWidget.h"

MeshModuleController::MeshModuleController(QObject* parent)
    : QObject(parent)
{
    ServiceRegistry::instance().registerService<MeshModuleControllerService>(this);
    initAction();
}

void MeshModuleController::newProject()
{
    auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_gridNode = new ProjectNode("网格");
    projectManagerService->addTreeItem(m_gridNode);
}

bool MeshModuleController::saveProject()
{
    // TODO: 保存网格模块相关参数（剖分参数、控制参数、质量阈值、导入文件路径等）
    return true;
}

bool MeshModuleController::loadProject()
{
    // TODO: 读取并恢复网格模块相关参数
    return true;
}

void MeshModuleController::closeProject()
{
    // TODO: 释放/清理网格模块相关资源
}

void MeshModuleController::initAction()
{
    auto &em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId, [this, fn](const QVariantMap &)
                           {
                               fn();
                           });
    };

    // --- IO ---
    reg(PublicConstants::kActionPreprocessImportGrid, [this]{ onImportGrid(); });
    reg(PublicConstants::kActionPreprocessExportGrid, [this]{ onExportGrid(); });

    // --- Meshing ---
    reg(PublicConstants::kActionPreprocessMeshGlobal, [this]{ onMeshGlobal(); });
    reg(PublicConstants::kActionPreprocessMeshSingle, [this]{ onMeshSingle(); });
    reg(PublicConstants::kActionPreprocessMeshMapped, [this]{ onMeshMapped(); });
    reg(PublicConstants::kActionPreprocessMeshSweep,  [this]{ onMeshSweep(); });

    reg(PublicConstants::kActionPreprocessMeshEdit,  [this]{ onMeshEdit(); });


    // --- Control & Quality ---
    reg(PublicConstants::kActionPreprocessCtrlVolume,  [this]{ onCtrlVolume(); });
    reg(PublicConstants::kActionPreprocessCtrlSurface, [this]{ onCtrlSurface(); });
    reg(PublicConstants::kActionPreprocessCtrlEdge,    [this]{ onCtrlEdge(); });      // NEW
    reg(PublicConstants::kActionPreprocessQualityStd,  [this]{ onQualityStd(); });
}

// -------------------- handlers --------------------

void MeshModuleController::onImportGrid()
{
    QString fileName;
    QFileDialog dialog(nullptr, QStringLiteral("选择网格文件"));
    dialog.setNameFilter(QStringLiteral("Grid Files (*.nas *.inp *.flac3d *.f3grid *.DAT)"));
    dialog.setFilter(QDir::Files);
    if (dialog.exec() == QDialog::Accepted) {
        fileName = dialog.selectedFiles().first();
    }

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    const QString ext = fi.suffix().toLower();

    qDebug()<<ext;
    if(ext == "nas")
    {
        // importNas(fileName);
    }

    else if (ext == "flac3d" || ext == "f3grid")
    {
        importFlac3D(fileName);
    }
    else if (ext == "inp")
    {
        importInp(fileName);
    }
    else if(ext == "dat")
    {
        // w->loadIiinFile(fileName);
    }
    else
    {
        QMessageBox::warning(nullptr,
                             QStringLiteral("不支持的格式"),
                             QStringLiteral("当前只支持 .nas / .inp / .flac3d / .f3grid"));
    }
}

void MeshModuleController::onExportGrid()
{
    // auto* w = new MeshExportGridWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshGlobal()
{
    MeshGlobalMeshingWidget* w = new MeshGlobalMeshingWidget();
    auto *node = new ProjectNode("网格划分", QUuid::createUuid().toString(), (BasePage*)w);
    m_gridNode->addChild(node);
}

void MeshModuleController::onMeshSingle()
{
    // auto* w = new MeshSingleMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshMapped()
{
    // auto* w = new MeshMappedMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshSweep()
{
    // auto* w = new MeshSweepMeshingWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlVolume()
{
    // auto* w = new MeshVolumeControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlSurface()
{
    // auto* w = new MeshFaceControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onCtrlEdge()
{
    // auto* w = new MeshEdgeControlWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onQualityStd()
{
    // auto* w = new MeshQualityStandardWidget();
    QWidget* w = new QWidget();
}

void MeshModuleController::onMeshEdit()
{
    GridEditingWidget* w = new GridEditingWidget();
    auto *node = new ProjectNode("网格编辑", QUuid::createUuid().toString(), (BasePage*)w);
    m_gridNode->addChild(node);
}


void MeshModuleController::importFlac3D(QString fileName)
{
    // === 新增：FLAC3D 文本网格 (.flac3d / .f3grid) ===
    vtkSmartPointer<vtkUnstructuredGrid> grid =
        loadFlac3dGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        qWarning() << "loadFlac3dGrid failed for" << fileName;
        return;
    }

    m_gridPath      = fileName;
    m_gridStructure = grid;
    m_pointStructure = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_pointStructure->DeepCopy(m_gridStructure);

    // 只显示网格，不做任何映射/物理场设置
    vtkSmartPointer<vtkDataSetMapper> mapper =
        vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(m_gridStructure);

    vtkDataArray *cellcolor = vtkUnsignedCharArray::New();
    cellcolor->SetNumberOfComponents(3);
    m_gridStructure->GetCellData()->SetScalars(cellcolor);
    cellcolor->SetNumberOfTuples(m_gridStructure->GetNumberOfCells());
    cellcolor->FillComponent(0, 200);
    cellcolor->FillComponent(1, 200);
    cellcolor->FillComponent(2, 255);

    m_cellActor = vtkSmartPointer<vtkActor>::New();
    m_cellActor->SetMapper(mapper);
    m_cellActor->GetProperty()->EdgeVisibilityOn();
    m_cellActor->GetProperty()->SetLineWidth(1.0);

    m_visualWidgetService->addActor(m_cellActor);
    m_visualWidgetService->getRender()->ResetCamera();
    m_visualWidgetService->getrenderWindow()->Render();

    // 不设置 m_faceToPoint 等，避免后面逻辑误用
    m_faceToPoint.clear();
    m_faceToCell.clear();
    m_faceToPoint_qset_qmap.clear();
    m_faceToCell_qvec_qmap.clear();
    m_solid_have_pshell_set_map.clear();
    m_solidHadPshellId2D.clear();
    m_faceToCellId2D.clear();
    m_grid2DCell.clear();

    m_modelType = 2; // 简单认为是 3D 模型
    m_status    = 1;
    m_type      = 4;

    return;
}

vtkSmartPointer<vtkUnstructuredGrid>
MeshModuleController::loadFlac3dGrid(const QString &fileName)
{
    // -------- 基本数据结构 --------
    struct Node {
        double x, y, z;
    };

    // 网格点：旧 ID -> 坐标
    QMap<int, Node> nodes;

    // 区域（Zone）：类型 + 旧 ID + 节点列表
    struct Zone {
        QString type;       // "B8", "W6", "T4", "P5" ...
        int id;
        QVector<int> verts; // 该 zone 所有节点的旧 ID（按文件顺序）
    };
    QVector<Zone> zones;

    // ZGROUP：zoneId -> groupName
    QMap<int, QString> zoneGroups;

    // -------- 1. 读取 FLAC3D 文本网格 --------
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "cannot open flac3d file:" << fileName;
        return nullptr;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith('*'))
            continue;                       // 注释行

        // 逗号统一替换为空格，方便用空白分割
        line.replace(',', ' ');
        QStringList tok = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
        if (tok.isEmpty())
            continue;

        // ---- 节点 G ----
        if (tok[0] == "G") {
            if (tok.size() < 5)
                continue;
            int id = tok[1].toInt();
            Node n;
            n.x = tok[2].toDouble();
            n.y = tok[3].toDouble();
            n.z = tok[4].toDouble();
            nodes[id] = n;
        }
        // ---- 单元 Z ----
        else if (tok[0] == "Z") {
            if (tok.size() < 4)
                continue;

            Zone z;
            z.type = tok[1];        // B8 / W6 / T4 / P5 ...
            z.id   = tok[2].toInt();

            // 按类型取前 N 个节点，剩下的忽略
            if (z.type == "B8") {
                if (tok.size() < 11) continue; // Z B8 id n1..n8
                for (int i = 3; i < 11; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "W6") {
                if (tok.size() < 9) continue;  // Z W6 id n1..n6
                for (int i = 3; i < 9; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "T4") {
                if (tok.size() < 7) continue;  // Z T4 id n1..n4
                for (int i = 3; i < 7; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else if (z.type == "P5") {
                if (tok.size() < 8) continue;  // Z P5 id n1..n5
                for (int i = 3; i < 8; ++i)
                    z.verts.push_back(tok[i].toInt());
            }
            else {
                // 其它类型先忽略
                continue;
            }

            zones.push_back(z);
        }
        // ---- ZGROUP：把分组信息解析出来，映射到 zone id ----
        else if (tok[0] == "ZGROUP") {
            // 这里假设格式类似：
            // ZGROUP groupName  1 2 5-10 ...
            if (tok.size() < 3)
                continue;

            QString groupName = tok[1];

            for (int i = 2; i < tok.size(); ++i) {
                QString t = tok[i];
                if (t.contains('-')) {
                    // 范围：a-b
                    const auto parts = t.split('-');
                    if (parts.size() != 2) continue;
                    bool ok1 = false, ok2 = false;
                    int a = parts[0].toInt(&ok1);
                    int b = parts[1].toInt(&ok2);
                    if (!ok1 || !ok2) continue;
                    if (a > b) std::swap(a, b);
                    for (int zid = a; zid <= b; ++zid) {
                        zoneGroups[zid] = groupName;
                    }
                } else {
                    // 单个 id
                    bool ok = false;
                    int zid = t.toInt(&ok);
                    if (ok)
                        zoneGroups[zid] = groupName;
                }
            }
        }
        // ---- 其它行先不处理 ----
        else {
            continue;
        }
    }
    f.close();

    if (nodes.isEmpty() || zones.isEmpty()) {
        qWarning() << "no nodes or zones parsed from flac3d file";
        return nullptr;
    }

    // -------- 2. oldId -> newId，构造 VTK Points --------
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    QMap<int, vtkIdType> idMap;   // 旧节点号 -> VTK 新 ID
    vtkIdType nextId = 0;

    // QMap 默认按 key 升序遍历，刚好我们希望节点 ID 升序
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        const Node &p = it.value();
        pts->InsertNextPoint(p.x, p.y, p.z);
        idMap[it.key()] = nextId++;
    }

    vtkSmartPointer<vtkUnstructuredGrid> grid =
        vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->SetPoints(pts);

    // 提前给 cell 分配容量（大致就是 zone 数量）
    grid->Allocate(zones.size());

    // -------- 3. 一些几何工具函数（给 W6 / P5 用） --------

    // 选出跨度最大的轴：0=x,1=y,2=z
    auto chooseMainAxis = [](const std::vector<double>& xs,
                             const std::vector<double>& ys,
                             const std::vector<double>& zs) -> int
    {
        double dx = *std::max_element(xs.begin(), xs.end()) - *std::min_element(xs.begin(), xs.end());
        double dy = *std::max_element(ys.begin(), ys.end()) - *std::min_element(ys.begin(), ys.end());
        double dz = *std::max_element(zs.begin(), zs.end()) - *std::min_element(zs.begin(), zs.end());
        if (dx >= dy && dx >= dz) return 0;
        if (dy >= dx && dy >= dz) return 1;
        return 2;
    };

    // 在“垂直于主轴”的平面上，按极角排序（用于三角/四边形的环形排序）
    auto sortByAngleInPlane = [](QVector<int>& idx,
                                 int axis,
                                 const std::vector<double>& xs,
                                 const std::vector<double>& ys,
                                 const std::vector<double>& zs)
    {
        // 计算质心
        double cx = 0, cy = 0, cz = 0;
        for (int i : idx) {
            cx += xs[i]; cy += ys[i]; cz += zs[i];
        }
        cx /= idx.size();
        cy /= idx.size();
        cz /= idx.size();

        struct Ang {
            int id;
            double ang;
        };
        QVector<Ang> arr;
        arr.reserve(idx.size());

        for (int i : idx) {
            double u, v;
            if (axis == 0) {        // 主轴 x，投影到 (y,z)
                u = ys[i] - cy;
                v = zs[i] - cz;
            } else if (axis == 1) { // 主轴 y，投影到 (x,z)
                u = xs[i] - cx;
                v = zs[i] - cz;
            } else {                // 主轴 z，投影到 (x,y)
                u = xs[i] - cx;
                v = ys[i] - cy;
            }
            double ang = std::atan2(v, u);
            arr.push_back({i, ang});
        }

        std::sort(arr.begin(), arr.end(),
                  [](const Ang& a, const Ang& b){ return a.ang < b.ang; });

        for (int k = 0; k < idx.size(); ++k)
            idx[k] = arr[k].id;
    };

    // -------- 4. 各种单元的构造函数（返回是否成功插入） --------

    // 4.1 B8：用坐标把 8 个点分成 8 个角，再映射到 VTK Hex 顺序
    auto addHex = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 8) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[8];

        std::vector<double> xs(8), ys(8), zs(8);
        for (int i = 0; i < 8; ++i) {
            const Node &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        // 1) 选一个跨度最大的主轴（和 W6/P5 一样）
        int axis = chooseMainAxis(xs, ys, zs);

        // 2) 按主轴把 8 个点分成 bottom(4) / top(4)
        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 8; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(4); top.reserve(4);
        for (int i = 0; i < 8; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid) bottom.push_back(i);
            else          top.push_back(i);
        }

        // 如果不是 4+4（比如有点刚好在 mid 上），按 v 排序选 4 个最小做 bottom，其余做 top
        if (bottom.size() != 4 || top.size() != 4) {
            QVector<QPair<double,int>> tmp;
            tmp.reserve(8);
            for (int i = 0; i < 8; ++i) {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto &a, auto &b){ return a.first < b.first; });
            bottom.clear(); top.clear();
            for (int i = 0; i < 4; ++i) bottom.push_back(tmp[i].second);
            for (int i = 4; i < 8; ++i) top.push_back(tmp[i].second);
        }

        // 3) 在垂直于主轴的平面上，对 bottom/top 各自做环形排序
        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top,    axis, xs, ys, zs);

        // 4) 按 VTK Hexahedron 的约定顺序填：
        //    0,1,2,3 = 底面环； 4,5,6,7 = 顶面环（方向一致）
        auto hex = vtkSmartPointer<vtkHexahedron>::New();
        for (int k = 0; k < 4; ++k) {
            hex->GetPointIds()->SetId(k,     p[ bottom[k] ].nid);
            hex->GetPointIds()->SetId(4 + k, p[ top[k]    ].nid);
        }

        grid->InsertNextCell(hex->GetCellType(), hex->GetPointIds());
        return true;
    };

    // 4.2 W6：按“主轴”拆成底/顶三角，再在平面上按角度排序
    auto addW6 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 6) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[6];

        std::vector<double> xs(6), ys(6), zs(6);
        for (int i = 0; i < 6; ++i) {
            const Node &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        double minv = 1e300, maxv = -1e300;
        for (int i = 0; i < 6; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
        double mid = 0.5 * (minv + maxv);

        QVector<int> bottom, top;
        bottom.reserve(3); top.reserve(3);
        for (int i = 0; i < 6; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v <= mid) bottom.push_back(i);
            else          top.push_back(i);
        }

        // 如果不是 3+3，按 v 值排序取 3 个做 bottom
        if (bottom.size() != 3 || top.size() != 3) {
            QVector<QPair<double,int>> tmp;
            tmp.reserve(6);
            for (int i = 0; i < 6; ++i) {
                double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
                tmp.push_back(qMakePair(v, i));
            }
            std::sort(tmp.begin(), tmp.end(),
                      [](auto &a, auto &b){ return a.first < b.first; });
            bottom.clear(); top.clear();
            for (int i = 0; i < 3; ++i) bottom.push_back(tmp[i].second);
            for (int i = 3; i < 6; ++i) top.push_back(tmp[i].second);
        }

        sortByAngleInPlane(bottom, axis, xs, ys, zs);
        sortByAngleInPlane(top,    axis, xs, ys, zs);

        auto wedge = vtkSmartPointer<vtkWedge>::New();
        for (int k = 0; k < 3; ++k) {
            wedge->GetPointIds()->SetId(k,   p[ bottom[k] ].nid); // 底三角 0,1,2
            wedge->GetPointIds()->SetId(3+k, p[ top[k]    ].nid); // 顶三角 3,4,5
        }

        grid->InsertNextCell(wedge->GetCellType(), wedge->GetPointIds());
        return true;
    };

    // 4.3 T4：四面体，直接用顺序即可（拓扑不会乱）
    auto addT4 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 4) return false;

        auto tet = vtkSmartPointer<vtkTetra>::New();
        for (int i = 0; i < 4; ++i)
            tet->GetPointIds()->SetId(i, idMap[ ids[i] ]);

        grid->InsertNextCell(tet->GetCellType(), tet->GetPointIds());
        return true;
    };

    // 4.4 P5：找主轴方向上最远的点当 apex，剩下 4 点作为底面 quad
    auto addP5 = [&](const QVector<int>& ids) -> bool
    {
        if (ids.size() != 5) return false;

        struct P { vtkIdType nid; double x,y,z; };
        P p[5];

        std::vector<double> xs(5), ys(5), zs(5);
        for (int i = 0; i < 5; ++i) {
            const Node &n = nodes[ ids[i] ];
            p[i].nid = idMap[ ids[i] ];
            p[i].x = n.x; p[i].y = n.y; p[i].z = n.z;
            xs[i] = n.x; ys[i] = n.y; zs[i] = n.z;
        }

        int axis = chooseMainAxis(xs, ys, zs);

        // 选在该轴上值最大的点当 apex
        int apex = 0;
        double best = -1e300;
        for (int i = 0; i < 5; ++i) {
            double v = (axis == 0 ? xs[i] : (axis == 1 ? ys[i] : zs[i]));
            if (v > best) { best = v; apex = i; }
        }

        QVector<int> baseIdx;
        baseIdx.reserve(4);
        for (int i = 0; i < 5; ++i)
            if (i != apex) baseIdx.push_back(i);

        // 底面在垂直于主轴的平面上做环形排序
        sortByAngleInPlane(baseIdx, axis, xs, ys, zs);

        auto pyr = vtkSmartPointer<vtkPyramid>::New();
        // 底 0..3
        for (int k = 0; k < 4; ++k)
            pyr->GetPointIds()->SetId(k, p[ baseIdx[k] ].nid);
        // 顶点 4
        pyr->GetPointIds()->SetId(4, p[apex].nid);

        grid->InsertNextCell(pyr->GetCellType(), pyr->GetPointIds());
        return true;
    };

    // -------- 5. 遍历所有 Zone，逐个插入单元，同时记录 ZoneId/Group --------

    // 我们在 C++ 侧用 QVector 先记下来，最后再塞到 VTK CellData 里
    QVector<int>   cellZoneIds;
    QVector<QString> cellGroupNames;
    cellZoneIds.reserve(zones.size());
    cellGroupNames.reserve(zones.size());

    for (const Zone &z : zones) {
        bool ok = false;
        if (z.type == "B8")
            ok = addHex(z.verts);
        else if (z.type == "W6")
            ok = addW6(z.verts);
        else if (z.type == "T4")
            ok = addT4(z.verts);
        else if (z.type == "P5")
            ok = addP5(z.verts);

        if (ok) {
            cellZoneIds.push_back(z.id);
            cellGroupNames.push_back(zoneGroups.value(z.id));
        }
    }

    // -------- 6. 把 ZoneId / GroupName 写入 VTK CellData --------
    vtkIdType nCells = grid->GetNumberOfCells();
    if (nCells != cellZoneIds.size()) {
        qWarning() << "[importFlac3D] cell count mismatch:"
                   << nCells << "vs" << cellZoneIds.size();
    }

    auto zoneIdArr = vtkSmartPointer<vtkIntArray>::New();
    zoneIdArr->SetName("ZoneId");
    zoneIdArr->SetNumberOfComponents(1);
    zoneIdArr->SetNumberOfTuples(cellZoneIds.size());

    auto groupArr = vtkSmartPointer<vtkStringArray>::New();
    groupArr->SetName("GroupName");
    groupArr->SetNumberOfValues(cellGroupNames.size());

    for (int i = 0; i < cellZoneIds.size(); ++i) {
        zoneIdArr->SetValue(i, cellZoneIds[i]);
        //        groupArr->SetValue(i, cellGroupNames[i]);
    }

    grid->GetCellData()->AddArray(zoneIdArr);
    grid->GetCellData()->AddArray(groupArr);

    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid>
MeshModuleController::loadAbaqusInpGrid(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open inp file:" << fileName;
        return nullptr;
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif

    QMap<int, QVector3D> nodeMap;

    struct Elem { QString type; QVector<int> conn; };
    QVector<Elem> elems;

    auto nodesPerType = [](const QString& tUpper) -> int {
        if (tUpper.startsWith("C3D8")) return 8;   // C3D8 / C3D8R ...
        if (tUpper.startsWith("C3D4")) return 4;   // C3D4 ...
        if (tUpper.startsWith("C3D6")) return 6;   // C3D6 ...
        return -1;
    };

    auto splitCsv = [](const QString& s) {
        return s.split(',', QString::SkipEmptyParts);
    };

    bool inNode = false;
    bool inElem = false;
    QString curElemTypeUpper;
    int expectedConn = -1;

    // 为处理元素续行准备
    bool elemContinuing = false;
    QVector<int> curConn;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // 1) Abaqus 注释：** 直接忽略，不能打断状态
        if (line.startsWith("**")) continue;

        // 2) 新 section
        if (line.startsWith('*')) {
            inNode = false;
            inElem = false;
            elemContinuing = false;
            curConn.clear();
            expectedConn = -1;
            curElemTypeUpper.clear();

            QString upper = line.toUpper();

            if (upper.startsWith("*NODE")) {
                inNode = true;
            } else if (upper.startsWith("*ELEMENT")) {
                inElem = true;

                int pos = upper.indexOf("TYPE=");
                if (pos != -1) {
                    curElemTypeUpper = upper.mid(pos + 5).trimmed();
                    int comma = curElemTypeUpper.indexOf(',');
                    if (comma != -1) curElemTypeUpper = curElemTypeUpper.left(comma).trimmed();
                    expectedConn = nodesPerType(curElemTypeUpper);
                    if (expectedConn < 0) {
                        // 你暂时不支持的类型：保持 inElem=true 但 expectedConn<0，后面会跳过
                        qWarning() << "Unsupported element type:" << curElemTypeUpper;
                    }
                }
            }
            continue;
        }

        // 3) 读节点
        if (inNode) {
            // id, x, y, z（可能后面还有别的列，忽略即可）
            QStringList tok = splitCsv(line);
            if (tok.size() >= 4) {
                int id = tok[0].toInt();
                double x = tok[1].toDouble();
                double y = tok[2].toDouble();
                double z = tok[3].toDouble();
                nodeMap[id] = QVector3D(x, y, z);
            }
            continue;
        }

        // 4) 读元素（支持续行）
        if (inElem && expectedConn > 0) {
            QStringList tok = splitCsv(line);
            if (tok.isEmpty()) continue;

            int start = 0;
            if (!elemContinuing) {
                // 第一行：tok[0] 是元素号
                if (tok.size() < 2) continue;
                start = 1;
            }

            for (int i = start; i < tok.size(); ++i) {
                curConn.push_back(tok[i].toInt());
            }

            if (curConn.size() < expectedConn) {
                // 还没收齐，下一行继续
                elemContinuing = true;
                continue;
            }

            // 收齐（若多了，截断并警告）
            if (curConn.size() > expectedConn) {
                qWarning() << "Element has extra nodes, type=" << curElemTypeUpper
                           << " got=" << curConn.size() << " expect=" << expectedConn
                           << " truncating.";
                curConn.resize(expectedConn);
            }

            Elem e;
            e.type = curElemTypeUpper;
            e.conn = curConn;
            elems.push_back(e);

            // 准备下一个元素
            elemContinuing = false;
            curConn.clear();
            continue;
        }
    }

    file.close();

    if (nodeMap.isEmpty() || elems.isEmpty()) {
        qWarning() << "inp file has no nodes or elements:" << fileName;
        return nullptr;
    }

    // oldId -> newId
    vtkNew<vtkPoints> pts;
    pts->SetDataTypeToDouble();

    QMap<int, vtkIdType> idMap;
    vtkIdType idx = 0;
    for (auto it = nodeMap.begin(); it != nodeMap.end(); ++it) {
        const QVector3D &p = it.value();
        pts->InsertNextPoint(p.x(), p.y(), p.z());
        idMap[it.key()] = idx++;
    }

    vtkNew<vtkUnstructuredGrid> grid;
    grid->SetPoints(pts);

    auto allIdsValid = [&](const QVector<int>& conn) -> bool {
        for (int nid : conn) if (!idMap.contains(nid)) return false;
        return true;
    };

    for (const Elem &e : elems) {
        if (!allIdsValid(e.conn)) {
            qWarning() << "Skip element: has undefined node id, type=" << e.type;
            continue;
        }

        if (e.type.startsWith("C3D8") && e.conn.size() == 8) {
            vtkIdType ids[8];
            for (int i = 0; i < 8; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_HEXAHEDRON, 8, ids);
        }
        else if (e.type.startsWith("C3D4") && e.conn.size() == 4) {
            vtkIdType ids[4];
            for (int i = 0; i < 4; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_TETRA, 4, ids);
        }
        else if (e.type.startsWith("C3D6") && e.conn.size() == 6) {
            vtkIdType ids[6];
            for (int i = 0; i < 6; ++i) ids[i] = idMap.value(e.conn[i]);
            grid->InsertNextCell(VTK_WEDGE, 6, ids);
        }
        // 其他类型你再扩展
    }

    return grid;
}


void MeshModuleController::importInp(QString fileName)
{
    // === 新增：Abaqus .inp 网格 ===
    vtkSmartPointer<vtkUnstructuredGrid> grid =
        loadAbaqusInpGrid(fileName);
    if (!grid || grid->GetNumberOfCells() == 0) {
        qWarning() << "loadAbaqusInpGrid failed for" << fileName;
        return;
    }

    m_gridPath      = fileName;
    m_gridStructure = grid;
    m_pointStructure = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_pointStructure->DeepCopy(m_gridStructure);

    vtkSmartPointer<vtkDataSetMapper> mapper =
        vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(m_gridStructure);

    vtkDataArray *cellcolor = vtkUnsignedCharArray::New();
    cellcolor->SetNumberOfComponents(3);
    m_gridStructure->GetCellData()->SetScalars(cellcolor);
    cellcolor->SetNumberOfTuples(m_gridStructure->GetNumberOfCells());
    cellcolor->FillComponent(0, 255);
    cellcolor->FillComponent(1, 240);
    cellcolor->FillComponent(2, 200);

    m_cellActor = vtkSmartPointer<vtkActor>::New();
    m_cellActor->SetMapper(mapper);
    m_cellActor->GetProperty()->EdgeVisibilityOn();
    m_cellActor->GetProperty()->SetLineWidth(1.0);

    m_visualWidgetService->addActor(m_cellActor);
    m_visualWidgetService->getRender()->ResetCamera();
    m_visualWidgetService->getrenderWindow()->Render();

    // 清空额外映射
    m_faceToPoint.clear();
    m_faceToCell.clear();
    m_faceToPoint_qset_qmap.clear();
    m_faceToCell_qvec_qmap.clear();
    m_solid_have_pshell_set_map.clear();
    m_solidHadPshellId2D.clear();
    m_faceToCellId2D.clear();
    m_grid2DCell.clear();

    m_modelType = 2; // 简单认为 3D
    m_status    = 1;
    m_type      = 4;

    return;
}
