#include "CaveBlockPlanePage.h"

#include <QDoubleValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariant>
#include <QColor>
#include <QHash>
#include <QSet>

#include <ServiceRegistry.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/VisualWidgetService.h>

#include <algorithm>
#include <cmath>

#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkCutter.h>
#include <vtkIdList.h>
#include <vtkActor.h>
#include <vtkLineSource.h>
#include <vtkPlane.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>
#include <vtkMath.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include "ui_CaveBlockPlanePage.h"

#include "SolverUiLayoutHelper.h"
namespace
{
constexpr vtkIdType kInvalidFaceKey = -1;
constexpr float kEps2 = 1.0e-12f;

struct CaveBlockSearchBounds
{
    double v[6] = {-1.0e6, 1.0e6, -1.0e6, 1.0e6, -1.0e6, 1.0e6};
    bool valid = false;
};

QHash<QString, QSet<CaveBlockPlanePage*>>& caveBlockPlaneGroups()
{
    static QHash<QString, QSet<CaveBlockPlanePage*>> groups;
    return groups;
}

QHash<QString, CaveBlockSearchBounds>& caveBlockSearchBounds()
{
    static QHash<QString, CaveBlockSearchBounds> bounds;
    return bounds;
}

QString groupFillPreviewKey(const QString& groupId)
{
    return QStringLiteral("cave.block.group.%1.candidate.fill").arg(groupId);
}

QString groupLinePreviewKey(const QString& groupId)
{
    return QStringLiteral("cave.block.group.%1.candidate.lines").arg(groupId);
}

QString fmt(double v)
{
    return QString::number(v, 'f', 6);
}

double editValue(QDoubleSpinBox* edit, double def = 0.0)
{
    return edit ? edit->value() : def;
}

QString spinText(const QDoubleSpinBox* edit)
{
    return edit ? QString::number(edit->value(), 'g', 15) : QString();
}

double jsonDouble(const QJsonObject& obj, const char* key, double fallback)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isDouble())
        return v.toDouble(fallback);
    bool ok = false;
    const double parsed = v.toString().trimmed().toDouble(&ok);
    return ok ? parsed : fallback;
}

vtkIdType faceCellId(vtkIdType faceKey)
{
    return faceKey >> 4;
}

int faceLocalId(vtkIdType faceKey)
{
    return int(faceKey & 0xF);
}

void appendUniquePoint(QVector<QVector3D>& pts, const QVector3D& p)
{
    for (const QVector3D& old : pts)
    {
        if ((old - p).lengthSquared() <= kEps2)
            return;
    }
    pts.push_back(p);
}

QVector3D averageOfPoints(vtkPoints* points)
{
    QVector3D c(0.0f, 0.0f, 0.0f);
    if (!points || points->GetNumberOfPoints() <= 0)
        return c;

    const vtkIdType n = points->GetNumberOfPoints();
    for (vtkIdType i = 0; i < n; ++i)
    {
        double p[3] = {0.0, 0.0, 0.0};
        points->GetPoint(i, p);
        c += QVector3D(float(p[0]), float(p[1]), float(p[2]));
    }
    return c / float(n);
}

QVector3D averageOfVectorPoints(const QVector<QVector3D>& pts)
{
    QVector3D c(0.0f, 0.0f, 0.0f);
    if (pts.isEmpty())
        return c;

    for (const QVector3D& p : pts)
        c += p;
    return c / float(pts.size());
}

bool isNonCollinear(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3)
{
    return QVector3D::crossProduct(p2 - p1, p3 - p1).lengthSquared() > kEps2;
}

QVector<QVector3D> pointsOfEdge(vtkUnstructuredGrid* grid, vtkIdType edgeKey)
{
    QVector<QVector3D> pts;
    if (!grid || edgeKey < 0)
        return pts;

    PickManager& pm = PickManager::instance();
    const vtkIdType cellId = pm.unpackEdgeCellId(edgeKey);
    const int localId = pm.unpackEdgeLocalId(edgeKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return pts;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return pts;

    vtkPoints* points = nullptr;
    if (cell->GetCellDimension() == 1 || localId == 15)
    {
        points = cell->GetPoints();
    }
    else if (localId >= 0 && localId < cell->GetNumberOfEdges())
    {
        vtkCell* edge = cell->GetEdge(localId);
        points = edge ? edge->GetPoints() : nullptr;
    }

    if (!points || points->GetNumberOfPoints() < 2)
        return pts;

    double p[3] = {0.0, 0.0, 0.0};
    points->GetPoint(0, p);
    pts.push_back(QVector3D(float(p[0]), float(p[1]), float(p[2])));
    points->GetPoint(points->GetNumberOfPoints() - 1, p);
    pts.push_back(QVector3D(float(p[0]), float(p[1]), float(p[2])));
    return pts;
}

QVector<QVector3D> orientedTwoLinePoints(vtkUnstructuredGrid* grid, vtkIdType edgeKey)
{
    QVector<QVector3D> pts = pointsOfEdge(grid, edgeKey);
    if (!grid || pts.size() < 2)
        return pts;

    PickManager& pm = PickManager::instance();
    const vtkIdType cellId = pm.unpackEdgeCellId(edgeKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return pts;
    vtkCell* cell = grid->GetCell(cellId);
    if (!cell || !cell->GetPoints())
        return pts;

    // 二维求解器约定：P1->P2 的面内法向 (dy,-dx) 指向块体内部。
    // 点选网格边时，用包含该边的单元中心统一方向；手工输入坐标则保留用户给定方向。
    const QVector3D center = averageOfPoints(cell->GetPoints());
    const QVector3D edgeCenter = (pts[0] + pts[1]) * 0.5f;
    const QVector3D d = pts[1] - pts[0];
    const QVector3D n(d.y(), -d.x(), 0.0f);
    if (n.lengthSquared() > kEps2 && QVector3D::dotProduct(n, center - edgeCenter) < 0.0f)
        std::swap(pts[0], pts[1]);
    return pts;
}

QVector<QVector3D> pointsOfFace(vtkUnstructuredGrid* grid, vtkIdType faceKey)
{
    QVector<QVector3D> pts;
    if (!grid || faceKey < 0)
        return pts;

    const vtkIdType cellId = faceCellId(faceKey);
    const int localId = faceLocalId(faceKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return pts;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return pts;

    auto appendPoints = [&pts](vtkPoints* points)
    {
        if (!points)
            return;
        const vtkIdType n = points->GetNumberOfPoints();
        pts.reserve(int(n));
        for (vtkIdType i = 0; i < n; ++i)
        {
            double p[3] = {0.0, 0.0, 0.0};
            points->GetPoint(i, p);
            appendUniquePoint(pts, QVector3D(float(p[0]), float(p[1]), float(p[2])));
        }
    };

    // 关键点：3D 体单元的 face->GetPointIds() 是 face 局部点号，不能再拿去 grid->GetPoint()。
    // 这里直接从 cell/face 自身的 vtkPoints 取坐标，避免 P1/P2 重复或坐标错乱。
    if (cell->GetCellDimension() == 2 || localId == 15)
    {
        appendPoints(cell->GetPoints());
    }
    else if (localId >= 0 && localId < cell->GetNumberOfFaces())
    {
        vtkCell* face = cell->GetFace(localId);
        if (face)
            appendPoints(face->GetPoints());
    }

    return pts;
}

bool insidePointOfFace(vtkUnstructuredGrid* grid, vtkIdType faceKey, QVector3D& insidePoint)
{
    if (!grid || faceKey < 0)
        return false;

    const vtkIdType cellId = faceCellId(faceKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return false;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell || cell->GetCellDimension() < 3 || !cell->GetPoints())
        return false;

    insidePoint = averageOfPoints(cell->GetPoints());
    return true;
}

QVector<QVector3D> chooseThreePlanePoints(const QVector<QVector3D>& pts)
{
    QVector<QVector3D> out;
    if (pts.size() < 3)
        return out;

    // 普通三角面/四边面：优先保持 VTK face 自身点序，只跳过共线组合。
    // 这样线性单元不会因为“最远点规则”改变起点，输出更接近原单元面定义。
    if (pts.size() <= 4)
    {
        for (int i = 0; i < pts.size(); ++i)
        {
            for (int j = i + 1; j < pts.size(); ++j)
            {
                for (int k = j + 1; k < pts.size(); ++k)
                {
                    if (isNonCollinear(pts[i], pts[j], pts[k]))
                    {
                        out.push_back(pts[i]);
                        out.push_back(pts[j]);
                        out.push_back(pts[k]);
                        return out;
                    }
                }
            }
        }
        return out;
    }

    // 高阶/细分面：用“最远一对点 + 离该线最远第三点”避开中间节点。
    int a = 0;
    int b = 1;
    double bestLen2 = -1.0;
    for (int i = 0; i < pts.size(); ++i)
    {
        for (int j = i + 1; j < pts.size(); ++j)
        {
            const double len2 = double((pts[j] - pts[i]).lengthSquared());
            if (len2 > bestLen2)
            {
                bestLen2 = len2;
                a = i;
                b = j;
            }
        }
    }

    int c = -1;
    double bestArea2 = -1.0;
    const QVector3D base = pts[b] - pts[a];
    for (int i = 0; i < pts.size(); ++i)
    {
        if (i == a || i == b)
            continue;
        const double area2 = double(QVector3D::crossProduct(base, pts[i] - pts[a]).lengthSquared());
        if (area2 > bestArea2)
        {
            bestArea2 = area2;
            c = i;
        }
    }

    if (c < 0)
        return out;

    out.push_back(pts[a]);
    out.push_back(pts[b]);
    out.push_back(pts[c]);
    return out;
}

void orientThreePointsToInside(QVector<QVector3D>& triPts,
                               const QVector<QVector3D>& facePts,
                               vtkUnstructuredGrid* grid,
                               vtkIdType faceKey)
{
    if (triPts.size() < 3)
        return;

    QVector3D insidePoint;
    if (!insidePointOfFace(grid, faceKey, insidePoint))
        return;

    const QVector3D faceCenter = averageOfVectorPoints(facePts.isEmpty() ? triPts : facePts);
    const QVector3D n = QVector3D::crossProduct(triPts[1] - triPts[0], triPts[2] - triPts[0]);
    if (n.lengthSquared() <= kEps2)
        return;

    // 右手螺旋法则：P1 -> P2 -> P3 的法向应指向块体内部。
    // 若当前法向背离单元中心，就交换两个点反转法向。
    if (QVector3D::dotProduct(n, insidePoint - faceCenter) < 0.0f)
        std::swap(triPts[1], triPts[2]);
}

QVector<QVector3D> orientedThreePlanePoints(vtkUnstructuredGrid* grid, vtkIdType faceKey)
{
    const QVector<QVector3D> facePts = pointsOfFace(grid, faceKey);
    QVector<QVector3D> triPts = chooseThreePlanePoints(facePts);
    orientThreePointsToInside(triPts, facePts, grid, faceKey);
    return triPts;
}

double polygonArea(const QVector<QVector3D>& pts)
{
    if (pts.size() < 3)
        return 0.0;

    double area = 0.0;
    const QVector3D& p0 = pts[0];
    for (int i = 1; i + 1 < pts.size(); ++i)
        area += 0.5 * QVector3D::crossProduct(pts[i] - p0, pts[i + 1] - p0).length();
    return area;
}
} // namespace

CaveBlockPlanePage::CaveBlockPlanePage(QWidget* parent)
    : BasePage("CaveBlockPlane"), ui(new Ui::CaveBlockPlanePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);
    setAttribute(Qt::WA_StyledBackground, true);

    for (QDoubleSpinBox* spin : {ui->x1Edit, ui->y1Edit, ui->z1Edit,
                                  ui->x2Edit, ui->y2Edit, ui->z2Edit,
                                  ui->x3Edit, ui->y3Edit, ui->z3Edit})
    {
        spin->setRange(-1.0e9, 1.0e9);
        spin->setDecimals(6);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
    }
    for (QDoubleSpinBox* spin : {ui->fpEdit, ui->cEdit, ui->rtEdit})
    {
        spin->setRange(0.0, 1.0e12);
        spin->setDecimals(6);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
    }

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText((ui->pickCheckBox && !ui->pickCheckBox->isChecked())
                                         ? QStringLiteral("点选已关闭")
                                         : QStringLiteral("未定义"));

    if (ui->pickCheckBox)
    {
        ui->pickCheckBox->setToolTip(QStringLiteral("在视图中点选滑面。"));
        connect(ui->pickCheckBox, &QCheckBox::toggled, this,
                [this](bool on)
                {
                    if (m_pageActive)
                        PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, on);

                    if (!ui->faceStatusLabel)
                        return;
                    if (!on)
                    {
                        ui->faceStatusLabel->setText(QStringLiteral("点选已关闭"));
                        return;
                    }
                    if (isTwoDimensionalPlane())
                    {
                        if (m_linePointIds.size() == 1)
                            ui->faceStatusLabel->setText(QStringLiteral("请选择点2"));
                        else if (m_has2DLineDefinition)
                            ui->faceStatusLabel->setText(QStringLiteral("已定义"));
                        else
                            ui->faceStatusLabel->setText(QStringLiteral("请选择点1"));
                    }
                    else
                    {
                        ui->faceStatusLabel->setText(selectedFaceKeys().isEmpty()
                                                         ? QStringLiteral("请选择滑面")
                                                         : QStringLiteral("已选择"));
                    }
                });
    }

    if (ui->flipSideBtn)
    {
        ui->flipSideBtn->setToolTip(QStringLiteral("切换候选滑块所在侧。"));
        connect(ui->flipSideBtn, &QPushButton::clicked, this,
                [this]() { flipTwoDimensionalBlockSide(); });
    }
    if (ui->clearFaceBtn)
        ui->clearFaceBtn->setToolTip(QStringLiteral("清除当前滑面。"));

    // 二维：两个点只用于确定一条无限滑面直线。交互上连续点选两个网格节点作为 P1/P2，
    //       也允许直接输入任意两点（原求解器的 ICUT 并不要求定义点必须是网格节点）。
    // 三维：滑面是网格面，点选后提取 P1/P2/P3。
    for (QDoubleSpinBox* edit : {ui->x1Edit, ui->y1Edit, ui->z1Edit,
                                  ui->x2Edit, ui->y2Edit, ui->z2Edit})
    {
        connect(edit, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this](double)
                {
                    if (isTwoDimensionalPlane())
                        m_has2DLineDefinition = true;
                    if (m_pageActive)
                        updateCoordinatePreview();
                    if (m_pageActive && !m_blockGroupId.isEmpty())
                        refreshGroupPreview(m_blockGroupId, true);
                });
    }

    if (ui->clearFaceBtn)
    {
        connect(ui->clearFaceBtn, &QPushButton::clicked, this,
                [this]()
                {
                    m_selectedFaceKey = kInvalidFaceKey;
                    m_linePointIds.clear();
                    m_has2DLineDefinition = false;
                    PickManager::instance().clearSelection(pickDomain(), m_uuid, currentPickMode());
                    if (isTwoDimensionalPlane())
                    {
                        const QSignalBlocker b1(ui->x1Edit); const QSignalBlocker b2(ui->y1Edit);
                        const QSignalBlocker b3(ui->z1Edit); const QSignalBlocker b4(ui->x2Edit);
                        const QSignalBlocker b5(ui->y2Edit); const QSignalBlocker b6(ui->z2Edit);
                        ui->x1Edit->setValue(0.0); ui->y1Edit->setValue(0.0); ui->z1Edit->setValue(0.0);
                        ui->x2Edit->setValue(0.0); ui->y2Edit->setValue(0.0); ui->z2Edit->setValue(0.0);
                        clearCoordinatePreview(true);
                        if (ui->faceStatusLabel)
                            ui->faceStatusLabel->setText(QStringLiteral("未定义"));
                    }
                    else
                    {
                        clearCoordinatePreview(true);
                        if (ui->faceStatusLabel)
                            ui->faceStatusLabel->setText(QStringLiteral("未选择"));
                    }
                    if (m_pageActive && !m_blockGroupId.isEmpty())
                        refreshGroupPreview(m_blockGroupId, true);
                });
    }
}

CaveBlockPlanePage::~CaveBlockPlanePage()
{
    const QString oldGroup = m_blockGroupId;
    beforeUnload();
    clearCoordinatePreview(false);
    if (!oldGroup.isEmpty())
    {
        auto& groups = caveBlockPlaneGroups();
        if (groups.contains(oldGroup))
        {
            groups[oldGroup].remove(this);
            if (groups[oldGroup].isEmpty())
                groups.remove(oldGroup);
        }
        clearGroupPreview(oldGroup, false);
    }
    delete ui;
}

QString CaveBlockPlanePage::pickDomain() const
{
    return QStringLiteral("CaveBlockPlane/%1").arg(m_uuid);
}


void CaveBlockPlanePage::beforeLoad()
{
    m_pageActive = true;
    refreshDimensionUi();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(pickDomain(), m_uuid, currentPickMode());
    PickManager::instance().setClickPickEnabled(pickDomain(), m_uuid, !ui->pickCheckBox || ui->pickCheckBox->isChecked());
    PickManager::instance().setBoxSelectEnabled(pickDomain(), m_uuid, false);

    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            &CaveBlockPlanePage::onPickSelectionChanged, Qt::UniqueConnection);
    if (isTwoDimensionalPlane())
    {
        connect(&PickManager::instance(), &PickManager::nodePicked, this,
                &CaveBlockPlanePage::onNodePicked, Qt::UniqueConnection);
        if (!m_linePointIds.isEmpty())
            PickManager::instance().setSelectedPointIds(
                pickDomain(), m_uuid, currentGridKind(), m_linePointIds, true);
    }

    normalizeAndSyncSelectedFace();
    updateCoordinatePreview();
    if (m_pageActive && !m_blockGroupId.isEmpty())
        refreshGroupPreview(m_blockGroupId, true);
}

void CaveBlockPlanePage::beforeUnload()
{
    m_pageActive = false;
    clearCoordinatePreview(false);
    if (!m_blockGroupId.isEmpty())
        clearGroupPreview(m_blockGroupId, false);
    disconnect(&PickManager::instance(), &PickManager::selectionChanged, this,
               &CaveBlockPlanePage::onPickSelectionChanged);
    disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
               &CaveBlockPlanePage::onNodePicked);
    PickManager::instance().clearPreviewSelection(pickDomain(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(pickDomain(), m_uuid, currentPickMode());
}

QJsonObject CaveBlockPlanePage::saveJson()
{
    QJsonObject obj;
    obj["x1"] = spinText(ui->x1Edit);
    obj["y1"] = spinText(ui->y1Edit);
    obj["z1"] = spinText(ui->z1Edit);
    obj["x2"] = spinText(ui->x2Edit);
    obj["y2"] = spinText(ui->y2Edit);
    obj["z2"] = spinText(ui->z2Edit);
    obj["x3"] = spinText(ui->x3Edit);
    obj["y3"] = spinText(ui->y3Edit);
    obj["z3"] = spinText(ui->z3Edit);
    obj["fp"] = spinText(ui->fpEdit);
    obj["c"] = spinText(ui->cEdit);
    obj["rt"] = spinText(ui->rtEdit);
    obj["pick_enabled"] = ui->pickCheckBox ? ui->pickCheckBox->isChecked() : false;
    if (m_referenceDimension > 0)
        obj["reference_dimension"] = m_referenceDimension;

    if (isTwoDimensionalPlane())
    {
        QJsonArray pointIds;
        for (vtkIdType pid : m_linePointIds)
            pointIds.append(static_cast<qint64>(pid));
        if (!pointIds.isEmpty())
            obj["line_point_ids"] = pointIds;
    }
    else
    {
        const auto keys = selectedFaceKeys();
        if (!keys.isEmpty())
            obj["face_key"] = static_cast<qint64>(keys.first());
        else if (m_selectedFaceKey >= 0)
            obj["face_key"] = static_cast<qint64>(m_selectedFaceKey);
    }
    return obj;
}

void CaveBlockPlanePage::loadJson(const QJsonObject& obj)
{
    m_has2DLineDefinition = obj.contains("x1") && obj.contains("y1") && obj.contains("x2") && obj.contains("y2");
    ui->x1Edit->setValue(jsonDouble(obj, "x1", ui->x1Edit->value()));
    ui->y1Edit->setValue(jsonDouble(obj, "y1", ui->y1Edit->value()));
    ui->z1Edit->setValue(jsonDouble(obj, "z1", ui->z1Edit->value()));
    ui->x2Edit->setValue(jsonDouble(obj, "x2", ui->x2Edit->value()));
    ui->y2Edit->setValue(jsonDouble(obj, "y2", ui->y2Edit->value()));
    ui->z2Edit->setValue(jsonDouble(obj, "z2", ui->z2Edit->value()));
    ui->x3Edit->setValue(jsonDouble(obj, "x3", ui->x3Edit->value()));
    ui->y3Edit->setValue(jsonDouble(obj, "y3", ui->y3Edit->value()));
    ui->z3Edit->setValue(jsonDouble(obj, "z3", ui->z3Edit->value()));
    ui->fpEdit->setValue(jsonDouble(obj, "fp", ui->fpEdit->value()));
    ui->cEdit->setValue(jsonDouble(obj, "c", ui->cEdit->value()));
    ui->rtEdit->setValue(jsonDouble(obj, "rt", ui->rtEdit->value()));
    if (ui->pickCheckBox)
        ui->pickCheckBox->setChecked(obj.value("pick_enabled").toBool(false));
    m_referenceDimension = obj.value("reference_dimension").toInt(m_referenceDimension);
    refreshDimensionUi();

    m_linePointIds.clear();
    if (isTwoDimensionalPlane() && obj.contains("line_point_ids"))
    {
        const QJsonArray pointIds = obj.value("line_point_ids").toArray();
        for (const QJsonValue& value : pointIds)
        {
            const vtkIdType pid = static_cast<vtkIdType>(value.toVariant().toLongLong());
            if (pid >= 0 && m_linePointIds.size() < 2)
                m_linePointIds.push_back(pid);
        }
        if (m_pageActive && !m_linePointIds.isEmpty())
            PickManager::instance().setSelectedPointIds(
                pickDomain(), m_uuid, currentGridKind(), m_linePointIds, true);
    }
    else if (!isTwoDimensionalPlane() && obj.contains("face_key"))
    {
        m_selectedFaceKey = static_cast<vtkIdType>(obj.value("face_key").toVariant().toLongLong());
        if (m_pageActive)
            PickManager::instance().setSelectedFaceKeys(
                pickDomain(), m_uuid, currentGridKind(), QVector<vtkIdType>{m_selectedFaceKey}, true);
    }

    if (m_pageActive)
    {
        updateCoordinatePreview();
        if (!m_blockGroupId.isEmpty())
            refreshGroupPreview(m_blockGroupId, true);
    }
}

QString CaveBlockPlanePage::icutLine(int ii)
{
    if (isTwoDimensionalPlane())
    {
        return QString("%1 %2 %3 %4 %5")
            .arg(valueOrDefault(ui->x1Edit, "0.0"))
            .arg(valueOrDefault(ui->y1Edit, "0.0"))
            .arg(valueOrDefault(ui->x2Edit, "0.0"))
            .arg(valueOrDefault(ui->y2Edit, "0.0"))
            .arg(ii);
    }

    return QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10")
        .arg(valueOrDefault(ui->x1Edit, "0.0"))
        .arg(valueOrDefault(ui->y1Edit, "0.0"))
        .arg(valueOrDefault(ui->z1Edit, "0.0"))
        .arg(valueOrDefault(ui->x2Edit, "0.0"))
        .arg(valueOrDefault(ui->y2Edit, "0.0"))
        .arg(valueOrDefault(ui->z2Edit, "0.0"))
        .arg(valueOrDefault(ui->x3Edit, "0.0"))
        .arg(valueOrDefault(ui->y3Edit, "0.0"))
        .arg(valueOrDefault(ui->z3Edit, "0.0"))
        .arg(ii);
}

QString CaveBlockPlanePage::paraLine(int ii)
{
    return QString("%1 %2 %3 %4")
        .arg(valueOrDefault(ui->fpEdit, "0.8391"))
        .arg(valueOrDefault(ui->cEdit, "12.3693"))
        .arg(valueOrDefault(ui->rtEdit, "0.0000"))
        .arg(ii);
}

bool CaveBlockPlanePage::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    auto point = [](QDoubleSpinBox* x, QDoubleSpinBox* y, QDoubleSpinBox* z)
    {
        return QVector3D(float(x->value()), float(y->value()), float(z->value()));
    };
    const QVector3D p1 = point(ui->x1Edit, ui->y1Edit, ui->z1Edit);
    const QVector3D p2 = point(ui->x2Edit, ui->y2Edit, ui->z2Edit);
    const QVector3D p3 = point(ui->x3Edit, ui->y3Edit, ui->z3Edit);
    if (isTwoDimensionalPlane())
    {
        if (!m_has2DLineDefinition)
            return fail(QStringLiteral("请先定义二维滑面：依次选择两个点，或输入两个定义点坐标。"));
        const double dx = double(p2.x()) - double(p1.x());
        const double dy = double(p2.y()) - double(p1.y());
        if (dx * dx + dy * dy <= double(kEps2))
            return fail(QStringLiteral("二维滑面直线的两个定义点在 X/Y 平面内不能重合。"));
    }
    else if (!isNonCollinear(p1, p2, p3))
    {
        return fail(QStringLiteral("滑块面的三个点不能重合或共线。"));
    }

    const double fp = ui->fpEdit->value();
    const double cohesion = ui->cEdit->value();
    const double tension = ui->rtEdit->value();
    if (fp < 0.0 || cohesion < 0.0 || tension < 0.0)
        return fail(QStringLiteral("摩擦系数、黏聚力和抗拉强度必须为非负数。"));
    return true;
}

QVector3D CaveBlockPlanePage::p1Vector()
{
    return QVector3D(float(editValue(ui->x1Edit)), float(editValue(ui->y1Edit)),
                     float(editValue(ui->z1Edit)));
}

QVector3D CaveBlockPlanePage::normalVector()
{
    const QVector3D p1(float(editValue(ui->x1Edit)), float(editValue(ui->y1Edit)),
                       float(editValue(ui->z1Edit)));
    const QVector3D p2(float(editValue(ui->x2Edit)), float(editValue(ui->y2Edit)),
                       float(editValue(ui->z2Edit)));
    const QVector3D p3(float(editValue(ui->x3Edit)), float(editValue(ui->y3Edit)),
                       float(editValue(ui->z3Edit)));

    if (isTwoDimensionalPlane())
    {
        const QVector3D d = p2 - p1;
        QVector3D n(d.y(), -d.x(), 0.0f);
        if (n.lengthSquared() < kEps2)
            return QVector3D(0.0f, 1.0f, 0.0f);
        return n.normalized();
    }

    QVector3D n = QVector3D::crossProduct(p2 - p1, p3 - p1);
    if (n.lengthSquared() < kEps2)
        return QVector3D(0.0f, 0.0f, 1.0f);
    return n.normalized();
}

double CaveBlockPlanePage::pickedFaceArea()
{
    const auto keys = selectedFaceKeys();
    if (isTwoDimensionalPlane())
    {
        const QVector3D p1(float(editValue(ui->x1Edit)), float(editValue(ui->y1Edit)),
                           float(editValue(ui->z1Edit)));
        const QVector3D p2(float(editValue(ui->x2Edit)), float(editValue(ui->y2Edit)),
                           float(editValue(ui->z2Edit)));
        return double((p2 - p1).length());
    }

    if (!keys.isEmpty())
    {
        vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
        const QVector<QVector3D> pts = pointsOfFace(grid, keys.first());
        const double area = polygonArea(pts);
        if (area > 0.0)
            return area;
    }

    const QVector3D p1(float(editValue(ui->x1Edit)), float(editValue(ui->y1Edit)),
                       float(editValue(ui->z1Edit)));
    const QVector3D p2(float(editValue(ui->x2Edit)), float(editValue(ui->y2Edit)),
                       float(editValue(ui->z2Edit)));
    const QVector3D p3(float(editValue(ui->x3Edit)), float(editValue(ui->y3Edit)),
                       float(editValue(ui->z3Edit)));
    return 0.5 * QVector3D::crossProduct(p2 - p1, p3 - p1).length();
}

QString CaveBlockPlanePage::valueOrDefault(QDoubleSpinBox* edit, QString def)
{
    return edit ? QString::number(edit->value(), 'g', 15) : def;
}

void CaveBlockPlanePage::syncPointsFromSelectedFace()
{
    if (isTwoDimensionalPlane())
    {
        updateCoordinatePreview();
        if (m_pageActive && !m_blockGroupId.isEmpty())
            refreshGroupPreview(m_blockGroupId, true);
        if (ui->faceStatusLabel)
        {
            if (m_linePointIds.size() == 1)
                ui->faceStatusLabel->setText(QStringLiteral("请选择点2"));
            else if (m_linePointIds.size() >= 2)
                ui->faceStatusLabel->setText(
                    QStringLiteral("已定义"));
            else
                ui->faceStatusLabel->setText(
                    QStringLiteral("请选择点1"));
        }
        return;
    }

    const auto keys = selectedFaceKeys();
    if (keys.isEmpty())
    {
        if (ui->faceStatusLabel)
            ui->faceStatusLabel->setText(QStringLiteral("未选择"));
        return;
    }

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    const QVector<QVector3D> triPts = orientedThreePlanePoints(grid, keys.first());
    if (!grid || triPts.size() < 3)
    {
        if (ui->faceStatusLabel)
            ui->faceStatusLabel->setText(QStringLiteral("当前滑面无效"));
        return;
    }

    const QVector3D p1 = triPts[0];
    const QVector3D p2 = triPts[1];
    const QVector3D p3 = triPts[2];

    const QSignalBlocker b1(ui->x1Edit); const QSignalBlocker b2(ui->y1Edit); const QSignalBlocker b3(ui->z1Edit);
    const QSignalBlocker b4(ui->x2Edit); const QSignalBlocker b5(ui->y2Edit); const QSignalBlocker b6(ui->z2Edit);
    const QSignalBlocker b7(ui->x3Edit); const QSignalBlocker b8(ui->y3Edit); const QSignalBlocker b9(ui->z3Edit);

    ui->x1Edit->setValue(p1.x()); ui->y1Edit->setValue(p1.y()); ui->z1Edit->setValue(p1.z());
    ui->x2Edit->setValue(p2.x()); ui->y2Edit->setValue(p2.y()); ui->z2Edit->setValue(p2.z());
    ui->x3Edit->setValue(p3.x()); ui->y3Edit->setValue(p3.y()); ui->z3Edit->setValue(p3.z());

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText(
            QStringLiteral("已选择，面积 %1").arg(pickedFaceArea(), 0, 'f', 4));
}

int CaveBlockPlanePage::spatialDimension() const
{
    if (m_referenceDimension == 2 || m_referenceDimension == 3)
        return m_referenceDimension;

    const int publicDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (publicDim == 2 || publicDim == 3)
        return publicDim;

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (grid && grid->GetNumberOfCells() > 0)
    {
        for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
        {
            vtkCell* cell = grid->GetCell(cellId);
            if (cell)
                return cell->GetCellDimension() == 2 ? 2 : 3;
        }
    }
    return 3;
}

bool CaveBlockPlanePage::isTwoDimensionalPlane() const
{
    return spatialDimension() == 2;
}

void CaveBlockPlanePage::refreshDimensionUi()
{
    const bool is2D = isTwoDimensionalPlane();
    m_referenceDimension = is2D ? 2 : 3;

    if (ui->pickGroupBox)
        ui->pickGroupBox->setTitle(is2D ? QStringLiteral("滑面定义")
                                         : QStringLiteral("滑面选择"));
    if (ui->pointGroupBox)
        ui->pointGroupBox->setTitle(QStringLiteral("滑面位置"));
    if (ui->paraGroupBox)
        ui->paraGroupBox->setTitle(QStringLiteral("滑面强度"));

    ui->labelPoint1X->setText(QStringLiteral("点1 X"));
    ui->labelPoint1Y->setText(QStringLiteral("点1 Y"));
    ui->labelPoint2X->setText(QStringLiteral("点2 X"));
    ui->labelPoint2Y->setText(QStringLiteral("点2 Y"));

    // 二维 ICUT 只写 X1 Y1 X2 Y2；Z 和第三点属于三维平面定义。
    for (QWidget* w : {static_cast<QWidget*>(ui->labelPoint1Z), static_cast<QWidget*>(ui->z1Edit),
                       static_cast<QWidget*>(ui->labelPoint2Z), static_cast<QWidget*>(ui->z2Edit),
                       static_cast<QWidget*>(ui->labelPoint3X), static_cast<QWidget*>(ui->x3Edit),
                       static_cast<QWidget*>(ui->labelPoint3Y), static_cast<QWidget*>(ui->y3Edit),
                       static_cast<QWidget*>(ui->labelPoint3Z), static_cast<QWidget*>(ui->z3Edit)})
    {
        if (w)
            w->setVisible(!is2D);
    }

    if (ui->flipSideBtn)
        ui->flipSideBtn->setVisible(is2D);

    if (ui->faceStatusLabel)
    {
        if (ui->pickCheckBox && !ui->pickCheckBox->isChecked())
            ui->faceStatusLabel->setText(QStringLiteral("点选已关闭"));
        else if (is2D)
            ui->faceStatusLabel->setText(m_linePointIds.size() == 1
                                             ? QStringLiteral("请选择点2")
                                             : (m_has2DLineDefinition
                                                    ? QStringLiteral("已定义")
                                                    : QStringLiteral("请选择点1")));
        else
            ui->faceStatusLabel->setText(selectedFaceKeys().isEmpty()
                                             ? QStringLiteral("请选择滑面")
                                             : QStringLiteral("已选择"));
    }
}

QString CaveBlockPlanePage::coordinatePreviewKey() const
{
    return QStringLiteral("cave.block.plane.%1.coordinatePreview").arg(m_uuid);
}

void CaveBlockPlanePage::clearCoordinatePreview(bool renderNow)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    visual->removeProp(coordinatePreviewKey(), false);
    if (renderNow)
        visual->render();
}

bool CaveBlockPlanePage::lineDefinition2D(double& x1, double& y1, double& x2, double& y2) const
{
    if (!isTwoDimensionalPlane() || !m_has2DLineDefinition)
        return false;

    x1 = ui->x1Edit->value();
    y1 = ui->y1Edit->value();
    x2 = ui->x2Edit->value();
    y2 = ui->y2Edit->value();

    const double dx = x2 - x1;
    const double dy = y2 - y1;
    return dx * dx + dy * dy > 1.0e-20;
}

void CaveBlockPlanePage::setBlockGroupId(const QString& groupId)
{
    const QString normalized = groupId.trimmed();
    if (m_blockGroupId == normalized)
        return;

    const QString oldGroup = m_blockGroupId;
    if (!oldGroup.isEmpty())
    {
        auto& groups = caveBlockPlaneGroups();
        if (groups.contains(oldGroup))
        {
            groups[oldGroup].remove(this);
            if (groups[oldGroup].isEmpty())
                groups.remove(oldGroup);
        }
    }

    m_blockGroupId = normalized;
    if (!m_blockGroupId.isEmpty())
        caveBlockPlaneGroups()[m_blockGroupId].insert(this);

    if (!oldGroup.isEmpty())
    {
        if (m_pageActive)
            refreshGroupPreview(oldGroup, false);
        else
            clearGroupPreview(oldGroup, false);
    }
    if (m_pageActive && !m_blockGroupId.isEmpty())
        refreshGroupPreview(m_blockGroupId, true);
}

void CaveBlockPlanePage::setGroupSearchBounds(const QString& groupId, const double bounds[6])
{
    if (groupId.trimmed().isEmpty() || !bounds)
        return;

    CaveBlockSearchBounds b;
    for (int i = 0; i < 6; ++i)
        b.v[i] = bounds[i];
    b.valid = b.v[1] > b.v[0] && b.v[3] > b.v[2] && b.v[5] > b.v[4];
    caveBlockSearchBounds()[groupId] = b;
}

void CaveBlockPlanePage::clearGroupPreview(const QString& groupId, bool renderNow)
{
    if (groupId.trimmed().isEmpty())
        return;
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;
    visual->removeProp(groupFillPreviewKey(groupId), false);
    visual->removeProp(groupLinePreviewKey(groupId), false);
    if (renderNow)
        visual->render();
}

void CaveBlockPlanePage::refreshGroupPreview(const QString& groupId, bool renderNow)
{
    if (groupId.trimmed().isEmpty())
        return;

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    visual->removeProp(groupFillPreviewKey(groupId), false);
    visual->removeProp(groupLinePreviewKey(groupId), false);

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid || grid->GetNumberOfPoints() <= 0 || grid->GetNumberOfCells() <= 0)
    {
        if (renderNow)
            visual->render();
        return;
    }

    QVector<CaveBlockPlanePage*> pages;
    const auto it = caveBlockPlaneGroups().constFind(groupId);
    if (it != caveBlockPlaneGroups().constEnd())
    {
        for (CaveBlockPlanePage* page : it.value())
        {
            if (!page || !page->isTwoDimensionalPlane())
                continue;
            double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
            if (page->lineDefinition2D(x1, y1, x2, y2))
                pages.push_back(page);
        }
    }

    if (pages.isEmpty())
    {
        if (renderNow)
            visual->render();
        return;
    }

    double modelBounds[6] = {0, 0, 0, 0, 0, 0};
    grid->GetBounds(modelBounds);
    const double span[3] = {
        std::abs(modelBounds[1] - modelBounds[0]),
        std::abs(modelBounds[3] - modelBounds[2]),
        std::abs(modelBounds[5] - modelBounds[4])};
    int normalAxis = 0;
    if (span[1] < span[normalAxis])
        normalAxis = 1;
    if (span[2] < span[normalAxis])
        normalAxis = 2;
    const double displayOffset = std::max({span[0], span[1], span[2], 1.0}) * 3.0e-4;
    const double z = 0.5 * (modelBounds[4] + modelBounds[5]);

    // 先按“点1 -> 点2 的右侧”为块体内部依次裁剪。
    vtkSmartPointer<vtkUnstructuredGrid> current = vtkSmartPointer<vtkUnstructuredGrid>::New();
    current->ShallowCopy(grid);
    for (CaveBlockPlanePage* page : pages)
    {
        double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
        if (!page->lineDefinition2D(x1, y1, x2, y2))
            continue;
        const double dx = x2 - x1;
        const double dy = y2 - y1;

        auto plane = vtkSmartPointer<vtkPlane>::New();
        plane->SetOrigin(x1, y1, z);
        // A->B 的右侧法向：(dy, -dx)。保留隐函数 > 0 的一侧。
        plane->SetNormal(dy, -dx, 0.0);

        auto clip = vtkSmartPointer<vtkClipDataSet>::New();
        clip->SetInputData(current);
        clip->SetClipFunction(plane);
        clip->SetValue(0.0);
        clip->SetInsideOut(false);
        clip->Update();

        auto next = vtkSmartPointer<vtkUnstructuredGrid>::New();
        next->ShallowCopy(clip->GetOutput());
        current = next;
        if (current->GetNumberOfCells() <= 0)
            break;
    }

    // 再叠加“滑块搜索范围”。普通用户不需要手填它，但旧求解器仍使用该范围
    // 来排除不属于目标块体的候选区域，所以预览也同步遵循这组约束。
    const CaveBlockSearchBounds search = caveBlockSearchBounds().value(groupId);
    if (search.valid && current->GetNumberOfCells() > 0)
    {
        struct BoundPlane { double origin[3]; double normal[3]; };
        const BoundPlane boundPlanes[4] = {
            {{search.v[0], 0.0, z}, { 1.0,  0.0, 0.0}}, // x >= xmin
            {{search.v[1], 0.0, z}, {-1.0,  0.0, 0.0}}, // x <= xmax
            {{0.0, search.v[2], z}, { 0.0,  1.0, 0.0}}, // y >= ymin
            {{0.0, search.v[3], z}, { 0.0, -1.0, 0.0}}  // y <= ymax
        };
        for (const BoundPlane& bp : boundPlanes)
        {
            auto plane = vtkSmartPointer<vtkPlane>::New();
            plane->SetOrigin(bp.origin);
            plane->SetNormal(bp.normal);
            auto clip = vtkSmartPointer<vtkClipDataSet>::New();
            clip->SetInputData(current);
            clip->SetClipFunction(plane);
            clip->SetValue(0.0);
            clip->SetInsideOut(false);
            clip->Update();
            auto next = vtkSmartPointer<vtkUnstructuredGrid>::New();
            next->ShallowCopy(clip->GetOutput());
            current = next;
            if (current->GetNumberOfCells() <= 0)
                break;
        }
    }

    if (current->GetNumberOfCells() > 0)
    {
        auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        mapper->SetInputData(current);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->SetColor(1.0, 0.62, 0.08);
        actor->GetProperty()->SetOpacity(0.20);
        actor->GetProperty()->LightingOff();
        double offset[3] = {0.0, 0.0, 0.0};
        offset[normalAxis] = displayOffset;
        actor->SetPosition(offset);

        visual->addProp(groupFillPreviewKey(groupId), actor,
                        QStringLiteral("块体稳定"),
                        QStringLiteral("候选滑块区域"), false, false);
    }

    // 同时画出该分析下所有滑面方向线，便于直接看出候选块体是由哪些滑面围成。
    auto linePoints = vtkSmartPointer<vtkPoints>::New();
    auto lineCells = vtkSmartPointer<vtkCellArray>::New();
    for (CaveBlockPlanePage* page : pages)
    {
        double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
        if (!page->lineDefinition2D(x1, y1, x2, y2))
            continue;
        const double dx = x2 - x1;
        const double dy = y2 - y1;
        QVector<QVector3D> hits;
        auto appendHit = [&](double x, double y)
        {
            const double eps = 1.0e-9 * std::max({span[0], span[1], 1.0});
            if (x < modelBounds[0] - eps || x > modelBounds[1] + eps ||
                y < modelBounds[2] - eps || y > modelBounds[3] + eps)
                return;
            const QVector3D hit{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
            for (const QVector3D& old : hits)
                if ((old - hit).lengthSquared() <= float(eps * eps))
                    return;
            hits.push_back(hit);
        };
        if (std::abs(dx) > 1.0e-15)
        {
            for (double x : {modelBounds[0], modelBounds[1]})
            {
                const double t = (x - x1) / dx;
                appendHit(x, y1 + t * dy);
            }
        }
        if (std::abs(dy) > 1.0e-15)
        {
            for (double y : {modelBounds[2], modelBounds[3]})
            {
                const double t = (y - y1) / dy;
                appendHit(x1 + t * dx, y);
            }
        }
        if (hits.size() < 2)
            continue;

        int ia = 0, ib = 1;
        float best = (hits[1] - hits[0]).lengthSquared();
        for (int i = 0; i < hits.size(); ++i)
        {
            for (int j = i + 1; j < hits.size(); ++j)
            {
                const float d2 = (hits[j] - hits[i]).lengthSquared();
                if (d2 > best)
                {
                    best = d2;
                    ia = i;
                    ib = j;
                }
            }
        }
        const vtkIdType p0 = linePoints->InsertNextPoint(hits[ia].x(), hits[ia].y(), hits[ia].z());
        const vtkIdType p1 = linePoints->InsertNextPoint(hits[ib].x(), hits[ib].y(), hits[ib].z());
        vtkIdType ids[2] = {p0, p1};
        lineCells->InsertNextCell(2, ids);
    }

    if (lineCells->GetNumberOfCells() > 0)
    {
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(linePoints);
        poly->SetLines(lineCells);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->SetColor(1.0, 0.45, 0.0);
        actor->GetProperty()->SetLineWidth(3.0);
        actor->GetProperty()->LightingOff();
        double offset[3] = {0.0, 0.0, 0.0};
        offset[normalAxis] = displayOffset * 1.5;
        actor->SetPosition(offset);
        visual->addProp(groupLinePreviewKey(groupId), actor,
                        QStringLiteral("块体稳定"),
                        QStringLiteral("滑面组合"), false, false);
    }

    if (renderNow)
        visual->render();
}

void CaveBlockPlanePage::flipTwoDimensionalBlockSide()
{
    if (!isTwoDimensionalPlane())
        return;

    const double x1 = ui->x1Edit->value();
    const double y1 = ui->y1Edit->value();
    const double z1 = ui->z1Edit->value();
    const double x2 = ui->x2Edit->value();
    const double y2 = ui->y2Edit->value();
    const double z2 = ui->z2Edit->value();

    {
        const QSignalBlocker b1(ui->x1Edit), b2(ui->y1Edit), b3(ui->z1Edit);
        const QSignalBlocker b4(ui->x2Edit), b5(ui->y2Edit), b6(ui->z2Edit);
        ui->x1Edit->setValue(x2);
        ui->y1Edit->setValue(y2);
        ui->z1Edit->setValue(z2);
        ui->x2Edit->setValue(x1);
        ui->y2Edit->setValue(y1);
        ui->z2Edit->setValue(z1);
    }

    if (m_linePointIds.size() == 2)
        std::swap(m_linePointIds[0], m_linePointIds[1]);

    updateCoordinatePreview();
    if (m_pageActive && !m_blockGroupId.isEmpty())
        refreshGroupPreview(m_blockGroupId, true);

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText(QStringLiteral("块体侧已切换"));
}

void CaveBlockPlanePage::updateCoordinatePreview()
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    visual->removeProp(coordinatePreviewKey(), false);

    if (!m_pageActive || !isTwoDimensionalPlane())
    {
        visual->render();
        return;
    }

    const double x1 = ui->x1Edit->value();
    const double y1 = ui->y1Edit->value();
    const double x2 = ui->x2Edit->value();
    const double y2 = ui->y2Edit->value();

    const double dx = x2 - x1;
    const double dy = y2 - y1;
    if (dx * dx + dy * dy <= 1.0e-20)
    {
        visual->render();
        return;
    }

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    bool hasModelIntersection = false;
    int normalAxis = 2;
    double displayOffset = 0.0;

    if (grid && grid->GetNumberOfPoints() > 0)
    {
        double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        grid->GetBounds(bounds);
        const double span[3] = {
            std::abs(bounds[1] - bounds[0]),
            std::abs(bounds[3] - bounds[2]),
            std::abs(bounds[5] - bounds[4])};
        normalAxis = 0;
        if (span[1] < span[normalAxis])
            normalAxis = 1;
        if (span[2] < span[normalAxis])
            normalAxis = 2;
        const double maxSpan = std::max({span[0], span[1], span[2], 1.0});
        displayOffset = maxSpan * 2.0e-4;

        // ICUT 的两个二维点定义的是一条无限直线，而不是有限线段。
        // 用该直线构造切割平面，让 VTK 直接求它与二维网格的交线：
        // 求解仍保留原始两点，预览只显示模型内部的有效切割部分。
        auto cutPlane = vtkSmartPointer<vtkPlane>::New();
        cutPlane->SetOrigin(x1, y1, 0.5 * (bounds[4] + bounds[5]));
        cutPlane->SetNormal(dy, -dx, 0.0);

        auto cutter = vtkSmartPointer<vtkCutter>::New();
        cutter->SetCutFunction(cutPlane);
        cutter->SetInputData(grid);
        cutter->Update();

        vtkPolyData* cut = cutter->GetOutput();
        if (cut && cut->GetNumberOfPoints() >= 2 && cut->GetNumberOfCells() > 0)
        {
            mapper->SetInputData(cut);
            hasModelIntersection = true;
        }
    }

    if (!hasModelIntersection)
    {
        // 极端情况下 Cutter 没有产出（例如网格退化/尚未加载），回退为
        // “无限直线与模型包围盒”的交段，仍避免把定义点之间的原始长线段直接画出来。
        double p1[3] = {x1, y1, 0.0};
        double p2[3] = {x2, y2, 0.0};
        if (grid && grid->GetNumberOfPoints() > 0)
        {
            double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            grid->GetBounds(bounds);
            QVector<QVector3D> hits;
            auto appendHit = [&hits, &bounds](double x, double y, double z)
            {
                const double eps = 1.0e-9 * std::max({std::abs(bounds[1] - bounds[0]),
                                                      std::abs(bounds[3] - bounds[2]), 1.0});
                if (x < bounds[0] - eps || x > bounds[1] + eps ||
                    y < bounds[2] - eps || y > bounds[3] + eps)
                    return;
                const QVector3D hit{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                for (const QVector3D& old : hits)
                {
                    if ((old - hit).lengthSquared() <= float(eps * eps))
                        return;
                }
                hits.push_back(hit);
            };

            const double z = 0.5 * (bounds[4] + bounds[5]);
            if (std::abs(dx) > 1.0e-15)
            {
                for (double x : {bounds[0], bounds[1]})
                {
                    const double t = (x - x1) / dx;
                    appendHit(x, y1 + t * dy, z);
                }
            }
            if (std::abs(dy) > 1.0e-15)
            {
                for (double y : {bounds[2], bounds[3]})
                {
                    const double t = (y - y1) / dy;
                    appendHit(x1 + t * dx, y, z);
                }
            }

            if (hits.size() >= 2)
            {
                int ia = 0;
                int ib = 1;
                float best = (hits[1] - hits[0]).lengthSquared();
                for (int i = 0; i < hits.size(); ++i)
                {
                    for (int j = i + 1; j < hits.size(); ++j)
                    {
                        const float d2 = (hits[j] - hits[i]).lengthSquared();
                        if (d2 > best)
                        {
                            best = d2;
                            ia = i;
                            ib = j;
                        }
                    }
                }
                p1[0] = hits[ia].x(); p1[1] = hits[ia].y(); p1[2] = hits[ia].z();
                p2[0] = hits[ib].x(); p2[1] = hits[ib].y(); p2[2] = hits[ib].z();
            }
        }

        auto line = vtkSmartPointer<vtkLineSource>::New();
        line->SetPoint1(p1);
        line->SetPoint2(p2);
        line->Update();
        mapper->SetInputData(line->GetOutput());
    }

    mapper->ScalarVisibilityOff();
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->SetColor(1.0, 0.55, 0.05);
    actor->GetProperty()->SetLineWidth(5.0);
    if (displayOffset > 0.0)
    {
        double offset[3] = {0.0, 0.0, 0.0};
        offset[normalAxis] = displayOffset;
        actor->SetPosition(offset);
    }

    visual->addProp(coordinatePreviewKey(), actor,
                    QStringLiteral("块体稳定"),
                    QStringLiteral("当前二维滑面"),
                    false, false);
    visual->render();

    if (ui->faceStatusLabel && m_linePointIds.isEmpty())
        ui->faceStatusLabel->setText(QStringLiteral("已定义"));
}

PickMode CaveBlockPlanePage::currentPickMode()
{
    return isTwoDimensionalPlane() ? PickMode::Node : PickMode::Face;
}

GridKind CaveBlockPlanePage::currentGridKind()
{
    return GridKind::Solid;
}

QVector<vtkIdType> CaveBlockPlanePage::selectedFaceKeys()
{
    if (isTwoDimensionalPlane())
        return PickManager::instance().getSelectedPointIds(
            pickDomain(), m_uuid, currentGridKind());

    return PickManager::instance().getIdList(
        pickDomain(),
        m_uuid,
        currentPickMode(),
        currentGridKind());
}

vtkIdType CaveBlockPlanePage::chooseSingleFaceKey(const QVector<vtkIdType>& keys) const
{
    if (keys.isEmpty())
        return kInvalidFaceKey;

    if (m_selectedFaceKey != kInvalidFaceKey)
    {
        for (vtkIdType key : keys)
        {
            if (key != m_selectedFaceKey)
                return key;
        }
    }

    return keys.first();
}

void CaveBlockPlanePage::normalizeAndSyncSelectedFace()
{
    if (isTwoDimensionalPlane())
    {
        syncPointsFromSelectedFace();
        return;
    }

    if (m_enforcingSingleFace)
        return;

    QVector<vtkIdType> keys = selectedFaceKeys();
    if (keys.isEmpty())
    {
        m_selectedFaceKey = kInvalidFaceKey;
        syncPointsFromSelectedFace();
        return;
    }

    const vtkIdType keep = chooseSingleFaceKey(keys);
    if (keep == kInvalidFaceKey)
    {
        m_selectedFaceKey = kInvalidFaceKey;
        syncPointsFromSelectedFace();
        return;
    }

    const bool alreadySingle = (keys.size() == 1 && keys.first() == keep);
    m_selectedFaceKey = keep;

    // PickManager 的点击逻辑是 toggle。点击第二个面时，集合会临时变成“旧面 + 新面”。
    // 这里只在确实出现多个面时写回，避免 selectionChanged -> setSelectedFaceKeys 的递归。
    if (!alreadySingle)
    {
        m_enforcingSingleFace = true;
        PickManager::instance().setSelectedFaceKeys(
            pickDomain(), m_uuid, currentGridKind(), QVector<vtkIdType>{keep}, true);
        m_enforcingSingleFace = false;
    }

    syncPointsFromSelectedFace();
}

void CaveBlockPlanePage::onPickSelectionChanged(const QString& domain,
                                                const QString& uuid,
                                                PickMode mode,
                                                GridKind kind)
{
    if (domain != pickDomain() || uuid != m_uuid || mode != currentPickMode() ||
        kind != currentGridKind() || (ui->pickCheckBox && !ui->pickCheckBox->isChecked()))
        return;

    if (!isTwoDimensionalPlane())
        normalizeAndSyncSelectedFace();
}

void CaveBlockPlanePage::onNodePicked(const QString& domain,
                                      const QString& activeUuid,
                                      GridKind kind,
                                      vtkIdType pid,
                                      int nodeId,
                                      double x,
                                      double y,
                                      double z,
                                      const QString& label)
{
    Q_UNUSED(nodeId);
    Q_UNUSED(label);

    if (!isTwoDimensionalPlane() || domain != pickDomain() || activeUuid != m_uuid ||
        kind != currentGridKind() || pid < 0 || (ui->pickCheckBox && !ui->pickCheckBox->isChecked()))
        return;

    // 第一次点击确定定义点 P1，第二次点击确定定义点 P2。
    // 两点只用于确定一条无限直线；求解器再用该直线切割模型。
    // 已经有两个定义点时再次点击，从新的 P1 开始重新定义。
    if (m_linePointIds.size() >= 2)
    {
        m_linePointIds.clear();
        const QSignalBlocker b4(ui->x2Edit);
        const QSignalBlocker b5(ui->y2Edit);
        const QSignalBlocker b6(ui->z2Edit);
        ui->x2Edit->setValue(0.0);
        ui->y2Edit->setValue(0.0);
        ui->z2Edit->setValue(0.0);
    }

    if (!m_linePointIds.isEmpty() && m_linePointIds.first() == pid)
        return;

    m_linePointIds.push_back(pid);
    m_has2DLineDefinition = true;
    if (m_linePointIds.size() == 1)
    {
        const QSignalBlocker b1(ui->x1Edit);
        const QSignalBlocker b2(ui->y1Edit);
        const QSignalBlocker b3(ui->z1Edit);
        ui->x1Edit->setValue(x);
        ui->y1Edit->setValue(y);
        ui->z1Edit->setValue(z);
    }
    else
    {
        const QSignalBlocker b4(ui->x2Edit);
        const QSignalBlocker b5(ui->y2Edit);
        const QSignalBlocker b6(ui->z2Edit);
        ui->x2Edit->setValue(x);
        ui->y2Edit->setValue(y);
        ui->z2Edit->setValue(z);
    }

    PickManager::instance().setSelectedPointIds(
        pickDomain(), m_uuid, currentGridKind(), m_linePointIds, true);
    syncPointsFromSelectedFace();
}
