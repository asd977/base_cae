#include "CaveBlockPlanePage.h"

#include <QDoubleValidator>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVariant>

#include <algorithm>

#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkMath.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include "ui_CaveBlockPlanePage.h"

#include "SolverUiLayoutHelper.h"
namespace
{
constexpr vtkIdType kInvalidFaceKey = -1;
constexpr float kEps2 = 1.0e-12f;

QString fmt(double v)
{
    return QString::number(v, 'f', 6);
}

double editValue(QLineEdit* edit, double def = 0.0)
{
    if (!edit)
        return def;
    bool ok = false;
    const double v = edit->text().trimmed().toDouble(&ok);
    return ok ? v : def;
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

    auto* validator = new QDoubleValidator(-1.0e12, 1.0e12, 6, this);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    for (QLineEdit* edit : {ui->x1Edit, ui->y1Edit, ui->z1Edit,
                            ui->x2Edit, ui->y2Edit, ui->z2Edit,
                            ui->x3Edit, ui->y3Edit, ui->z3Edit,
                            ui->fpEdit, ui->cEdit, ui->rtEdit})
    {
        edit->setValidator(validator);
    }

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText(QStringLiteral("未选择滑块面"));

    if (ui->clearFaceBtn)
    {
        connect(ui->clearFaceBtn, &QPushButton::clicked, this,
                [this]()
                {
                    m_selectedFaceKey = kInvalidFaceKey;
                    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
                    if (ui->faceStatusLabel)
                        ui->faceStatusLabel->setText(QStringLiteral("未选择滑块面"));
                });
    }
}

CaveBlockPlanePage::~CaveBlockPlanePage()
{
    beforeUnload();
    delete ui;
}

void CaveBlockPlanePage::beforeLoad()
{
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, false);

    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            &CaveBlockPlanePage::onPickSelectionChanged, Qt::UniqueConnection);

    normalizeAndSyncSelectedFace();
}

void CaveBlockPlanePage::beforeUnload()
{
    disconnect(&PickManager::instance(), &PickManager::selectionChanged, this,
               &CaveBlockPlanePage::onPickSelectionChanged);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, currentPickMode());
    PickManager::instance().deactivate(getPageType(), m_uuid, currentPickMode());
}

QJsonObject CaveBlockPlanePage::saveJson()
{
    QJsonObject obj;
    obj["x1"] = ui->x1Edit->text().trimmed();
    obj["y1"] = ui->y1Edit->text().trimmed();
    obj["z1"] = ui->z1Edit->text().trimmed();
    obj["x2"] = ui->x2Edit->text().trimmed();
    obj["y2"] = ui->y2Edit->text().trimmed();
    obj["z2"] = ui->z2Edit->text().trimmed();
    obj["x3"] = ui->x3Edit->text().trimmed();
    obj["y3"] = ui->y3Edit->text().trimmed();
    obj["z3"] = ui->z3Edit->text().trimmed();
    obj["fp"] = ui->fpEdit->text().trimmed();
    obj["c"] = ui->cEdit->text().trimmed();
    obj["rt"] = ui->rtEdit->text().trimmed();

    const auto keys = selectedFaceKeys();
    if (!keys.isEmpty())
        obj["face_key"] = static_cast<qint64>(keys.first());
    return obj;
}

void CaveBlockPlanePage::loadJson(const QJsonObject& obj)
{
    ui->x1Edit->setText(obj.value("x1").toString(ui->x1Edit->text()));
    ui->y1Edit->setText(obj.value("y1").toString(ui->y1Edit->text()));
    ui->z1Edit->setText(obj.value("z1").toString(ui->z1Edit->text()));
    ui->x2Edit->setText(obj.value("x2").toString(ui->x2Edit->text()));
    ui->y2Edit->setText(obj.value("y2").toString(ui->y2Edit->text()));
    ui->z2Edit->setText(obj.value("z2").toString(ui->z2Edit->text()));
    ui->x3Edit->setText(obj.value("x3").toString(ui->x3Edit->text()));
    ui->y3Edit->setText(obj.value("y3").toString(ui->y3Edit->text()));
    ui->z3Edit->setText(obj.value("z3").toString(ui->z3Edit->text()));
    ui->fpEdit->setText(obj.value("fp").toString(ui->fpEdit->text()));
    ui->cEdit->setText(obj.value("c").toString(ui->cEdit->text()));
    ui->rtEdit->setText(obj.value("rt").toString(ui->rtEdit->text()));

    if (obj.contains("face_key"))
    {
        m_selectedFaceKey = static_cast<vtkIdType>(obj.value("face_key").toVariant().toLongLong());
        PickManager::instance().setSelectedFaceKeys(
            getPageType(), m_uuid, currentGridKind(), QVector<vtkIdType>{m_selectedFaceKey}, true);
    }
}

QString CaveBlockPlanePage::icutLine(int ii)
{
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
    auto point = [](QLineEdit* x, QLineEdit* y, QLineEdit* z)
    {
        return QVector3D(float(x->text().trimmed().toDouble()),
                         float(y->text().trimmed().toDouble()),
                         float(z->text().trimmed().toDouble()));
    };
    const QVector3D p1 = point(ui->x1Edit, ui->y1Edit, ui->z1Edit);
    const QVector3D p2 = point(ui->x2Edit, ui->y2Edit, ui->z2Edit);
    const QVector3D p3 = point(ui->x3Edit, ui->y3Edit, ui->z3Edit);
    if (!isNonCollinear(p1, p2, p3))
        return fail(QStringLiteral("滑块面的三个点不能重合或共线。"));

    bool fpOk = false;
    bool cOk = false;
    bool rtOk = false;
    const double fp = ui->fpEdit->text().trimmed().toDouble(&fpOk);
    const double cohesion = ui->cEdit->text().trimmed().toDouble(&cOk);
    const double tension = ui->rtEdit->text().trimmed().toDouble(&rtOk);
    if (!fpOk || fp < 0.0 || !cOk || cohesion < 0.0 || !rtOk || tension < 0.0)
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

    QVector3D n = QVector3D::crossProduct(p2 - p1, p3 - p1);
    if (n.lengthSquared() < kEps2)
        return QVector3D(0.0f, 0.0f, 1.0f);
    return n.normalized();
}

double CaveBlockPlanePage::pickedFaceArea()
{
    const auto keys = selectedFaceKeys();
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

QString CaveBlockPlanePage::valueOrDefault(QLineEdit* edit, QString def)
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}

void CaveBlockPlanePage::syncPointsFromSelectedFace()
{
    const auto keys = selectedFaceKeys();
    if (keys.isEmpty())
    {
        if (ui->faceStatusLabel)
            ui->faceStatusLabel->setText(QStringLiteral("未选择滑块面"));
        return;
    }

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    const QVector<QVector3D> triPts = orientedThreePlanePoints(grid, keys.first());
    if (!grid || triPts.size() < 3)
    {
        if (ui->faceStatusLabel)
            ui->faceStatusLabel->setText(QStringLiteral("当前面无效，请重新选择"));
        return;
    }

    const QVector3D p1 = triPts[0];
    const QVector3D p2 = triPts[1];
    const QVector3D p3 = triPts[2];

    const QSignalBlocker b1(ui->x1Edit); const QSignalBlocker b2(ui->y1Edit); const QSignalBlocker b3(ui->z1Edit);
    const QSignalBlocker b4(ui->x2Edit); const QSignalBlocker b5(ui->y2Edit); const QSignalBlocker b6(ui->z2Edit);
    const QSignalBlocker b7(ui->x3Edit); const QSignalBlocker b8(ui->y3Edit); const QSignalBlocker b9(ui->z3Edit);

    ui->x1Edit->setText(fmt(p1.x())); ui->y1Edit->setText(fmt(p1.y())); ui->z1Edit->setText(fmt(p1.z()));
    ui->x2Edit->setText(fmt(p2.x())); ui->y2Edit->setText(fmt(p2.y())); ui->z2Edit->setText(fmt(p2.z()));
    ui->x3Edit->setText(fmt(p3.x())); ui->y3Edit->setText(fmt(p3.y())); ui->z3Edit->setText(fmt(p3.z()));

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText(
            QStringLiteral("已选择 1 个滑块面，面积：%1").arg(pickedFaceArea(), 0, 'f', 4));
}

PickMode CaveBlockPlanePage::currentPickMode()
{
    return PickMode::Face;
}

GridKind CaveBlockPlanePage::currentGridKind()
{
    return GridKind::Solid;
}

QVector<vtkIdType> CaveBlockPlanePage::selectedFaceKeys()
{
    return PickManager::instance().getIdList(
        getPageType(),
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
            getPageType(), m_uuid, currentGridKind(), QVector<vtkIdType>{keep}, true);
        m_enforcingSingleFace = false;
    }

    syncPointsFromSelectedFace();
}

void CaveBlockPlanePage::onPickSelectionChanged(const QString& domain,
                                                const QString& uuid,
                                                PickMode mode,
                                                GridKind kind)
{
    if (domain != getPageType() || uuid != m_uuid || mode != currentPickMode() ||
        kind != currentGridKind())
        return;

    normalizeAndSyncSelectedFace();
}
