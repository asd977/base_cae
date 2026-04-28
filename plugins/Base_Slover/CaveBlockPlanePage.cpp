#include "CaveBlockPlanePage.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>

#include <vtkCell.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include "ui_CaveBlockPlanePage.h"

CaveBlockPlanePage::CaveBlockPlanePage(QWidget* parent)
    : BasePage("CaveBlockPlane"), ui(new Ui::CaveBlockPlanePage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    connect(ui->pickFillBtn, &QPushButton::clicked, this, &CaveBlockPlanePage::onFillFromPickedFace);
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
}

void CaveBlockPlanePage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Face);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
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

QString CaveBlockPlanePage::icutLine(int ii) const
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

QString CaveBlockPlanePage::paraLine(int ii) const
{
    return QString("%1 %2 %3 %4")
        .arg(valueOrDefault(ui->fpEdit, "0.8391"))
        .arg(valueOrDefault(ui->cEdit, "12.3693"))
        .arg(valueOrDefault(ui->rtEdit, "0.0000"))
        .arg(ii);
}

void CaveBlockPlanePage::onFillFromPickedFace()
{
    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid)
        return;

    const auto keys = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face,
                                                        GridKind::Solid);
    if (keys.isEmpty())
        return;

    const vtkIdType cellId = PickManager::instance().unpackFaceCellId(keys.first());
    const int faceLocalId = PickManager::instance().unpackFaceLocalId(keys.first());
    if (cellId < 0 || cellId >= grid->GetNumberOfCells())
        return;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return;

    vtkCell* face = cell->GetFace(faceLocalId);
    if (!face || face->GetNumberOfPoints() < 3)
        return;

    double p[3] = {0.0, 0.0, 0.0};
    face->GetPoints()->GetPoint(0, p);
    ui->x1Edit->setText(QString::number(p[0], 'f', 6));
    ui->y1Edit->setText(QString::number(p[1], 'f', 6));
    ui->z1Edit->setText(QString::number(p[2], 'f', 6));

    face->GetPoints()->GetPoint(1, p);
    ui->x2Edit->setText(QString::number(p[0], 'f', 6));
    ui->y2Edit->setText(QString::number(p[1], 'f', 6));
    ui->z2Edit->setText(QString::number(p[2], 'f', 6));

    face->GetPoints()->GetPoint(2, p);
    ui->x3Edit->setText(QString::number(p[0], 'f', 6));
    ui->y3Edit->setText(QString::number(p[1], 'f', 6));
    ui->z3Edit->setText(QString::number(p[2], 'f', 6));
}

QString CaveBlockPlanePage::valueOrDefault(QLineEdit* edit, const QString& def) const
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}
