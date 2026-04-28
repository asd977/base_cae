#include "CaveBlockPlanePage.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <vtkCell.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

CaveBlockPlanePage::CaveBlockPlanePage(QWidget* parent)
    : BasePage("CaveBlockPlane")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel("滑块面（按 P1->P2->P3 右手法则，法向指向块体内部）", this));

    m_pickFillBtn = new QPushButton("从拾取面读取3点", this);
    root->addWidget(m_pickFillBtn);

    auto* pointGrid = new QGridLayout();
    pointGrid->addWidget(new QLabel("", this), 0, 0);
    pointGrid->addWidget(new QLabel("X", this), 0, 1);
    pointGrid->addWidget(new QLabel("Y", this), 0, 2);
    pointGrid->addWidget(new QLabel("Z", this), 0, 3);

    m_x1Edit = new QLineEdit("0.0", this);
    m_y1Edit = new QLineEdit("0.0", this);
    m_z1Edit = new QLineEdit("0.0", this);
    pointGrid->addWidget(new QLabel("P1", this), 1, 0);
    pointGrid->addWidget(m_x1Edit, 1, 1);
    pointGrid->addWidget(m_y1Edit, 1, 2);
    pointGrid->addWidget(m_z1Edit, 1, 3);

    m_x2Edit = new QLineEdit("5.0", this);
    m_y2Edit = new QLineEdit("-5.0", this);
    m_z2Edit = new QLineEdit("0.0", this);
    pointGrid->addWidget(new QLabel("P2", this), 2, 0);
    pointGrid->addWidget(m_x2Edit, 2, 1);
    pointGrid->addWidget(m_y2Edit, 2, 2);
    pointGrid->addWidget(m_z2Edit, 2, 3);

    m_x3Edit = new QLineEdit("5.0", this);
    m_y3Edit = new QLineEdit("0.0", this);
    m_z3Edit = new QLineEdit("-10.0", this);
    pointGrid->addWidget(new QLabel("P3", this), 3, 0);
    pointGrid->addWidget(m_x3Edit, 3, 1);
    pointGrid->addWidget(m_y3Edit, 3, 2);
    pointGrid->addWidget(m_z3Edit, 3, 3);

    root->addLayout(pointGrid);

    auto* paraForm = new QFormLayout();
    m_fpEdit = new QLineEdit("0.8391", this);
    m_cEdit = new QLineEdit("12.3693", this);
    m_rtEdit = new QLineEdit("0.0000", this);
    paraForm->addRow("剪摩 fp", m_fpEdit);
    paraForm->addRow("剪摩 C(kPa)", m_cEdit);
    paraForm->addRow("抗拉强度 Rt(kPa)", m_rtEdit);
    root->addLayout(paraForm);

    root->addStretch();

    connect(m_pickFillBtn, &QPushButton::clicked, this, &CaveBlockPlanePage::onFillFromPickedFace);
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
    p1.append(m_x1Edit->text().trimmed());
    p1.append(m_y1Edit->text().trimmed());
    p1.append(m_z1Edit->text().trimmed());
    QJsonArray p2;
    p2.append(m_x2Edit->text().trimmed());
    p2.append(m_y2Edit->text().trimmed());
    p2.append(m_z2Edit->text().trimmed());
    QJsonArray p3;
    p3.append(m_x3Edit->text().trimmed());
    p3.append(m_y3Edit->text().trimmed());
    p3.append(m_z3Edit->text().trimmed());
    obj["p1"] = p1;
    obj["p2"] = p2;
    obj["p3"] = p3;
    obj["fp"] = m_fpEdit->text().trimmed();
    obj["c"] = m_cEdit->text().trimmed();
    obj["rt"] = m_rtEdit->text().trimmed();

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
        m_x1Edit->setText(p1.at(0).toString(m_x1Edit->text()));
        m_y1Edit->setText(p1.at(1).toString(m_y1Edit->text()));
        m_z1Edit->setText(p1.at(2).toString(m_z1Edit->text()));
    }
    if (p2.size() >= 3)
    {
        m_x2Edit->setText(p2.at(0).toString(m_x2Edit->text()));
        m_y2Edit->setText(p2.at(1).toString(m_y2Edit->text()));
        m_z2Edit->setText(p2.at(2).toString(m_z2Edit->text()));
    }
    if (p3.size() >= 3)
    {
        m_x3Edit->setText(p3.at(0).toString(m_x3Edit->text()));
        m_y3Edit->setText(p3.at(1).toString(m_y3Edit->text()));
        m_z3Edit->setText(p3.at(2).toString(m_z3Edit->text()));
    }

    m_fpEdit->setText(obj.value("fp").toString(m_fpEdit->text()));
    m_cEdit->setText(obj.value("c").toString(m_cEdit->text()));
    m_rtEdit->setText(obj.value("rt").toString(m_rtEdit->text()));

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
        .arg(valueOrDefault(m_x1Edit, "0.0"))
        .arg(valueOrDefault(m_y1Edit, "0.0"))
        .arg(valueOrDefault(m_z1Edit, "0.0"))
        .arg(valueOrDefault(m_x2Edit, "0.0"))
        .arg(valueOrDefault(m_y2Edit, "0.0"))
        .arg(valueOrDefault(m_z2Edit, "0.0"))
        .arg(valueOrDefault(m_x3Edit, "0.0"))
        .arg(valueOrDefault(m_y3Edit, "0.0"))
        .arg(valueOrDefault(m_z3Edit, "0.0"))
        .arg(ii);
}

QString CaveBlockPlanePage::paraLine(int ii) const
{
    return QString("%1 %2 %3 %4")
        .arg(valueOrDefault(m_fpEdit, "0.8391"))
        .arg(valueOrDefault(m_cEdit, "12.3693"))
        .arg(valueOrDefault(m_rtEdit, "0.0000"))
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
    m_x1Edit->setText(QString::number(p[0], 'f', 6));
    m_y1Edit->setText(QString::number(p[1], 'f', 6));
    m_z1Edit->setText(QString::number(p[2], 'f', 6));

    face->GetPoints()->GetPoint(1, p);
    m_x2Edit->setText(QString::number(p[0], 'f', 6));
    m_y2Edit->setText(QString::number(p[1], 'f', 6));
    m_z2Edit->setText(QString::number(p[2], 'f', 6));

    face->GetPoints()->GetPoint(2, p);
    m_x3Edit->setText(QString::number(p[0], 'f', 6));
    m_y3Edit->setText(QString::number(p[1], 'f', 6));
    m_z3Edit->setText(QString::number(p[2], 'f', 6));
}

QString CaveBlockPlanePage::valueOrDefault(QLineEdit* edit, const QString& def) const
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}
