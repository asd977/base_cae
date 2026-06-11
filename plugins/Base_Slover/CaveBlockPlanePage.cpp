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

#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkMath.h>
#include <vtkUnstructuredGrid.h>

#include "ui_CaveBlockPlanePage.h"

namespace
{
constexpr vtkIdType kInvalidFaceKey = -1;

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

QVector<vtkIdType> pointIdsOfFace(vtkUnstructuredGrid* grid, vtkIdType faceKey)
{
    QVector<vtkIdType> ids;
    if (!grid || faceKey < 0)
        return ids;

    const vtkIdType cellId = faceCellId(faceKey);
    const int localId = faceLocalId(faceKey);
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return ids;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return ids;

    vtkIdList* src = nullptr;
    if (cell->GetCellDimension() == 2 || localId == 15)
    {
        src = cell->GetPointIds();
    }
    else if (localId >= 0 && localId < cell->GetNumberOfFaces())
    {
        vtkCell* face = cell->GetFace(localId);
        src = face ? face->GetPointIds() : nullptr;
    }

    if (!src || src->GetNumberOfIds() < 3)
        return ids;

    ids.reserve(int(src->GetNumberOfIds()));
    for (vtkIdType i = 0; i < src->GetNumberOfIds(); ++i)
        ids.push_back(src->GetId(i));
    return ids;
}

QVector3D pointOf(vtkUnstructuredGrid* grid, vtkIdType pointId)
{
    double p[3] = {0.0, 0.0, 0.0};
    if (grid && pointId >= 0 && pointId < grid->GetNumberOfPoints())
        grid->GetPoint(pointId, p);
    return QVector3D(float(p[0]), float(p[1]), float(p[2]));
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
    : BasePage("CaveBlockPlanePage"), ui(new Ui::CaveBlockPlanePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background-color: white;");

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
        ui->faceStatusLabel->setText(QStringLiteral("未选择切割面"));

    if (ui->clearFaceBtn)
    {
        connect(ui->clearFaceBtn, &QPushButton::clicked, this,
                [this]()
                {
                    m_selectedFaceKey = kInvalidFaceKey;
                    PickManager::instance().clearSelection(getPageType(), m_uuid, currentPickMode());
                    if (ui->faceStatusLabel)
                        ui->faceStatusLabel->setText(QStringLiteral("未选择切割面"));
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
    if (n.lengthSquared() < 1.0e-16)
        return QVector3D(0.0f, 0.0f, 1.0f);
    return n.normalized();
}

double CaveBlockPlanePage::pickedFaceArea()
{
    const auto keys = selectedFaceKeys();
    if (!keys.isEmpty())
    {
        vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
        const QVector<vtkIdType> pids = pointIdsOfFace(grid, keys.first());
        QVector<QVector3D> pts;
        pts.reserve(pids.size());
        for (vtkIdType pid : pids)
            pts.push_back(pointOf(grid, pid));
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
            ui->faceStatusLabel->setText(QStringLiteral("未选择切割面"));
        return;
    }

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    const QVector<vtkIdType> pids = pointIdsOfFace(grid, keys.first());
    if (!grid || pids.size() < 3)
    {
        if (ui->faceStatusLabel)
            ui->faceStatusLabel->setText(QStringLiteral("当前面无效，请重新选择"));
        return;
    }

    const QVector3D p1 = pointOf(grid, pids[0]);
    const QVector3D p2 = pointOf(grid, pids[1]);
    const QVector3D p3 = pointOf(grid, pids[2]);

    const QSignalBlocker b1(ui->x1Edit); const QSignalBlocker b2(ui->y1Edit); const QSignalBlocker b3(ui->z1Edit);
    const QSignalBlocker b4(ui->x2Edit); const QSignalBlocker b5(ui->y2Edit); const QSignalBlocker b6(ui->z2Edit);
    const QSignalBlocker b7(ui->x3Edit); const QSignalBlocker b8(ui->y3Edit); const QSignalBlocker b9(ui->z3Edit);

    ui->x1Edit->setText(fmt(p1.x())); ui->y1Edit->setText(fmt(p1.y())); ui->z1Edit->setText(fmt(p1.z()));
    ui->x2Edit->setText(fmt(p2.x())); ui->y2Edit->setText(fmt(p2.y())); ui->z2Edit->setText(fmt(p2.z()));
    ui->x3Edit->setText(fmt(p3.x())); ui->y3Edit->setText(fmt(p3.y())); ui->z3Edit->setText(fmt(p3.z()));

    if (ui->faceStatusLabel)
        ui->faceStatusLabel->setText(
            QStringLiteral("已选择 1 个切割面，面积：%1").arg(pickedFaceArea(), 0, 'f', 4));
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
