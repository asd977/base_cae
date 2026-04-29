#include "CaveBlockPlanePage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QTimer>
#include <QVector3D>
#include <QVector>
#include <utility>

#include <vtkCell.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include "ui_CaveBlockPlanePage.h"

CaveBlockPlanePage::CaveBlockPlanePage(QWidget* parent)
    : BasePage("CaveBlockPlane"), ui(new Ui::CaveBlockPlanePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(120);
    connect(m_syncTimer, &QTimer::timeout, this, &CaveBlockPlanePage::onSelectionSyncTick);
}

CaveBlockPlanePage::~CaveBlockPlanePage()
{
    delete ui;
}

void CaveBlockPlanePage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().setBoxSelectEnabled(getPageType(), m_uuid, false);
    // 滑块面仅允许单面拾取：进入页面时若已有多面，保留第一个
    const auto keys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    if (keys.size() > 1)
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    {keys.first()}, true);
    m_lastFaceKey = -1;
    if (m_syncTimer)
        m_syncTimer->start();
}

void CaveBlockPlanePage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
    if (m_syncTimer)
        m_syncTimer->stop();
}

QJsonObject CaveBlockPlanePage::saveJson()
{
    QJsonObject obj;
    QJsonArray p1;
    p1.append(ui->x1Edit->text().trimmed());
    p1.append(ui->y1Edit->text().trimmed());
    p1.append(ui->z1Edit->text().trimmed());
    QJsonArray p2;
    p2.append(ui->x2Edit->text().trimmed());
    p2.append(ui->y2Edit->text().trimmed());
    p2.append(ui->z2Edit->text().trimmed());
    QJsonArray p3;
    p3.append(ui->x3Edit->text().trimmed());
    p3.append(ui->y3Edit->text().trimmed());
    p3.append(ui->z3Edit->text().trimmed());
    obj["p1"] = p1;
    obj["p2"] = p2;
    obj["p3"] = p3;
    obj["fp"] = ui->fpEdit->text().trimmed();
    obj["c"] = ui->cEdit->text().trimmed();
    obj["rt"] = ui->rtEdit->text().trimmed();

    QJsonArray faceKeys;
    const auto keys = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face,
                                                        GridKind::Solid);
    for (vtkIdType key : keys)
        faceKeys.append(static_cast<qint64>(key));
    obj["face_keys"] = faceKeys;
    return obj;
}

void CaveBlockPlanePage::loadJson(const QJsonObject& obj)
{
    const auto p1 = obj.value("p1").toArray();
    const auto p2 = obj.value("p2").toArray();
    const auto p3 = obj.value("p3").toArray();
    if (p1.size() >= 3)
    {
        ui->x1Edit->setText(p1.at(0).toString(ui->x1Edit->text()));
        ui->y1Edit->setText(p1.at(1).toString(ui->y1Edit->text()));
        ui->z1Edit->setText(p1.at(2).toString(ui->z1Edit->text()));
    }
    if (p2.size() >= 3)
    {
        ui->x2Edit->setText(p2.at(0).toString(ui->x2Edit->text()));
        ui->y2Edit->setText(p2.at(1).toString(ui->y2Edit->text()));
        ui->z2Edit->setText(p2.at(2).toString(ui->z2Edit->text()));
    }
    if (p3.size() >= 3)
    {
        ui->x3Edit->setText(p3.at(0).toString(ui->x3Edit->text()));
        ui->y3Edit->setText(p3.at(1).toString(ui->y3Edit->text()));
        ui->z3Edit->setText(p3.at(2).toString(ui->z3Edit->text()));
    }

    ui->fpEdit->setText(obj.value("fp").toString(ui->fpEdit->text()));
    ui->cEdit->setText(obj.value("c").toString(ui->cEdit->text()));
    ui->rtEdit->setText(obj.value("rt").toString(ui->rtEdit->text()));

    QVector<vtkIdType> faceKeys;
    const auto faceArr = obj.value("face_keys").toArray();
    faceKeys.reserve(faceArr.size());
    for (const auto& v : faceArr)
        faceKeys.push_back(static_cast<vtkIdType>(v.toVariant().toLongLong()));
    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys,
                                                true);
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

QVector3D CaveBlockPlanePage::p1Vector()
{
    return QVector3D(valueOrDefault(ui->x1Edit, "0.0").toDouble(),
                     valueOrDefault(ui->y1Edit, "0.0").toDouble(),
                     valueOrDefault(ui->z1Edit, "0.0").toDouble());
}

QVector3D CaveBlockPlanePage::normalVector()
{
    const QVector3D p1(valueOrDefault(ui->x1Edit, "0.0").toDouble(),
                       valueOrDefault(ui->y1Edit, "0.0").toDouble(),
                       valueOrDefault(ui->z1Edit, "0.0").toDouble());
    const QVector3D p2(valueOrDefault(ui->x2Edit, "0.0").toDouble(),
                       valueOrDefault(ui->y2Edit, "0.0").toDouble(),
                       valueOrDefault(ui->z2Edit, "0.0").toDouble());
    const QVector3D p3(valueOrDefault(ui->x3Edit, "0.0").toDouble(),
                       valueOrDefault(ui->y3Edit, "0.0").toDouble(),
                       valueOrDefault(ui->z3Edit, "0.0").toDouble());
    QVector3D n = QVector3D::crossProduct(p2 - p1, p3 - p1);
    if (n.length() <= 1e-12)
        return QVector3D(0.0f, 0.0f, 1.0f);
    n.normalize();
    return n;
}

double CaveBlockPlanePage::pickedFaceArea()
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return 0.0;

    const auto keys =
        PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    if (keys.isEmpty())
        return 0.0;

    vtkIdType faceKey = keys.first();
    vtkIdType cellId = PickManager::instance().unpackFaceCellId(faceKey);
    int faceLocalId = PickManager::instance().unpackFaceLocalId(faceKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return 0.0;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return 0.0;
    vtkCell* face = cell->GetFace(faceLocalId);
    if (!face || face->GetNumberOfPoints() < 3)
        return 0.0;

    auto toVec = [](double p[3]) { return QVector3D(p[0], p[1], p[2]); };
    double p0Raw[3] = {0.0, 0.0, 0.0};
    face->GetPoints()->GetPoint(0, p0Raw);
    const QVector3D p0 = toVec(p0Raw);
    double area = 0.0;
    for (vtkIdType i = 1; i + 1 < face->GetNumberOfPoints(); ++i)
    {
        double p1Raw[3] = {0.0, 0.0, 0.0};
        double p2Raw[3] = {0.0, 0.0, 0.0};
        face->GetPoints()->GetPoint(i, p1Raw);
        face->GetPoints()->GetPoint(i + 1, p2Raw);
        const QVector3D v1 = toVec(p1Raw) - p0;
        const QVector3D v2 = toVec(p2Raw) - p0;
        area += 0.5 * QVector3D::crossProduct(v1, v2).length();
    }
    return area;
}

void CaveBlockPlanePage::onSelectionSyncTick()
{
    const auto keys = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face,
                                                        GridKind::Solid);
    if (keys.isEmpty())
        return;

    // 单面约束：选中阶段实时生效，保留“最后选中”的那一个
    vtkIdType activeKey = keys.last();
    if (keys.size() > 1 && activeKey != m_lastFaceKey)
    {
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    {activeKey}, true);
        return;
    }

    if (activeKey != m_lastFaceKey)
    {
        onFillFromPickedFace(activeKey);
        m_lastFaceKey = activeKey;
    }
}

void CaveBlockPlanePage::onFillFromPickedFace(vtkIdType faceKey)
{
    if (!fillFromFaceKey(faceKey))
        return;
}

bool CaveBlockPlanePage::fillFromFaceKey(vtkIdType faceKey)
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return false;

    vtkIdType cellId = PickManager::instance().unpackFaceCellId(faceKey);
    int faceLocalId = PickManager::instance().unpackFaceLocalId(faceKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return false;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return false;

    vtkCell* face = cell->GetFace(faceLocalId);
    if (!face || face->GetNumberOfPoints() < 3)
        return false;

    QVector<QVector3D> uniqPoints;
    const double eps2 = 1e-16;
    for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
    {
        double p[3] = {0.0, 0.0, 0.0};
        face->GetPoints()->GetPoint(i, p);
        const QVector3D v(p[0], p[1], p[2]);
        bool duplicated = false;
        for (QVector3D ex : uniqPoints)
        {
            if ((ex - v).lengthSquared() <= eps2)
            {
                duplicated = true;
                break;
            }
        }
        if (!duplicated)
            uniqPoints.push_back(v);
        if (uniqPoints.size() >= 3)
            break;
    }
    if (uniqPoints.size() < 3)
        return false;

    // 右手螺旋：优先令法向指向单元内部（用“面中心->单元中心”方向判定）
    QVector3D p1 = uniqPoints[0];
    QVector3D p2 = uniqPoints[1];
    QVector3D p3 = uniqPoints[2];
    const QVector3D n = QVector3D::crossProduct(p2 - p1, p3 - p1);
    QVector3D faceCenter = (p1 + p2 + p3) / 3.0f;
    double c[3] = {0.0, 0.0, 0.0};
    cell->GetParametricCenter(c);
    double wc[3] = {0.0, 0.0, 0.0};
    double weights[32] = {0.0};
    int subId = 0;
    cell->EvaluateLocation(subId, c, wc, weights);
    QVector3D cellCenter(wc[0], wc[1], wc[2]);
    if (QVector3D::dotProduct(n, cellCenter - faceCenter) < 0.0)
        std::swap(p2, p3);

    ui->x1Edit->setText(QString::number(p1.x(), 'f', 6));
    ui->y1Edit->setText(QString::number(p1.y(), 'f', 6));
    ui->z1Edit->setText(QString::number(p1.z(), 'f', 6));
    ui->x2Edit->setText(QString::number(p2.x(), 'f', 6));
    ui->y2Edit->setText(QString::number(p2.y(), 'f', 6));
    ui->z2Edit->setText(QString::number(p2.z(), 'f', 6));
    ui->x3Edit->setText(QString::number(p3.x(), 'f', 6));
    ui->y3Edit->setText(QString::number(p3.y(), 'f', 6));
    ui->z3Edit->setText(QString::number(p3.z(), 'f', 6));
    return true;
}

QString CaveBlockPlanePage::valueOrDefault(QLineEdit* edit, QString def)
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}
