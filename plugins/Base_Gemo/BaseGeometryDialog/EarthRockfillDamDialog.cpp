#include "EarthRockfillDamDialog.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget*)
    : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam)
{
    initUi();
}

QJsonObject EarthRockfillDamDialog::saveJson()
{
    QJsonObject o;
    o["h"] = hEdit->text().toDouble();
    o["crest"] = crestEdit->text().toDouble();
    o["up"] = upEdit->text().toDouble();
    o["down"] = downEdit->text().toDouble();
    o["len"] = lenEdit->text().toDouble();
    o["core"] = coreWidthEdit->text().toDouble();
    o["layers"] = layerEdit->text().toInt();
    return o;
}

void EarthRockfillDamDialog::loadJson(const QJsonObject& o)
{
    if (o.contains("h")) hEdit->setText(QString::number(o["h"].toDouble()));
    if (o.contains("crest")) crestEdit->setText(QString::number(o["crest"].toDouble()));
    if (o.contains("up")) upEdit->setText(QString::number(o["up"].toDouble()));
    if (o.contains("down")) downEdit->setText(QString::number(o["down"].toDouble()));
    if (o.contains("len")) lenEdit->setText(QString::number(o["len"].toDouble()));
    if (o.contains("core")) coreWidthEdit->setText(QString::number(o["core"].toDouble()));
    if (o.contains("layers")) layerEdit->setText(QString::number(o["layers"].toInt()));
    updateDiagram();
}

void EarthRockfillDamDialog::initUi()
{
    auto* l = new QVBoxLayout(this);
    auto* f = new QFormLayout;
    auto* dv = new QDoubleValidator(0.01, 1e9, 3, this);
    hEdit = new QLineEdit("90");
    crestEdit = new QLineEdit("12");
    upEdit = new QLineEdit("2.2");
    downEdit = new QLineEdit("1.9");
    lenEdit = new QLineEdit("260");
    coreWidthEdit = new QLineEdit("6");
    layerEdit = new QLineEdit("10");
    for (auto* e : {hEdit, crestEdit, upEdit, downEdit, lenEdit, coreWidthEdit}) e->setValidator(dv);
    layerEdit->setValidator(new QIntValidator(1, 200, this));
    f->addRow("坝高H", hEdit);
    f->addRow("坝顶宽Bt", crestEdit);
    f->addRow("上游坡比", upEdit);
    f->addRow("下游坡比", downEdit);
    f->addRow("坝轴线长度L", lenEdit);
    f->addRow("心墙宽度", coreWidthEdit);
    f->addRow("分层数N", layerEdit);
    l->addLayout(f);

    diagramLabel = new QLabel(this);
    diagramLabel->setMinimumHeight(190);
    l->addWidget(diagramLabel);

    auto* row = new QHBoxLayout;
    auto* prev = new QPushButton("更新说明图");
    auto* gen = new QPushButton("生成土石坝");
    row->addWidget(prev);
    row->addWidget(gen);
    l->addLayout(row);

    connect(prev, &QPushButton::clicked, this, &EarthRockfillDamDialog::onPreviewClicked);
    connect(gen, &QPushButton::clicked, this, &EarthRockfillDamDialog::onGenerateClicked);
    updateDiagram();
}

void EarthRockfillDamDialog::updateDiagram()
{
    const double H = hEdit->text().toDouble();
    const double Bt = crestEdit->text().toDouble();
    const double mu = upEdit->text().toDouble();
    const double md = downEdit->text().toDouble();
    const double core = coreWidthEdit->text().toDouble();
    const int N = qMax(1, layerEdit->text().toInt());

    const double x1 = H * mu, x2 = x1 + Bt, x3 = x2 + H * md;
    QPixmap pix(480, 190);
    pix.fill(Qt::white);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = qMin(420.0 / qMax(1.0, x3), 130.0 / qMax(1.0, H));
    auto m = [&](double x, double z){ return QPointF(20 + x * s, 165 - z * s); };

    p.setBrush(QColor(196, 182, 136));
    p.drawPolygon(QPolygonF() << m(0,0) << m(x1,H) << m(x2,H) << m(x3,0));
    const double c = (x1 + x2) * 0.5;
    p.setBrush(QColor(220, 90, 90, 200)); // 心墙
    p.drawRect(QRectF(m(c - core*0.5, H).x(), m(0,0).y(), core*s, -H*s));
    p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    for (int i=1;i<N;i++)
    {
        const double z = H * i / N;
        p.drawLine(m(0,z), m(x3,z));
    }
    p.setPen(Qt::black);
    p.drawText(8, 16, "土石坝：堆石区 + 心墙 + 分层");
    diagramLabel->setPixmap(pix);
}

void EarthRockfillDamDialog::onPreviewClicked() { updateDiagram(); }

void EarthRockfillDamDialog::buildLayeredDam(double H, double Bt, double mu, double md, double L, int layers)
{
    Q_UNUSED(layers);
    TopoDS_Compound comp;
    BRep_Builder b; b.MakeCompound(comp);

    // 主坝体（连续整体）
    const double x1 = H * mu;
    const double x2 = x1 + Bt;
    const double x3 = x2 + H * md;
    BRepBuilderAPI_MakePolygon damPoly;
    damPoly.Add(gp_Pnt(0, 0, 0));
    damPoly.Add(gp_Pnt(x1, 0, H));
    damPoly.Add(gp_Pnt(x2, 0, H));
    damPoly.Add(gp_Pnt(x3, 0, 0));
    damPoly.Close();
    b.Add(comp, BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(damPoly.Wire()), gp_Vec(0, L, 0)).Shape());

    // 心墙体（独立分区，避免全部都被分层条带切碎）
    const double core = coreWidthEdit->text().toDouble();
    const double cx = (x1 + x2) * 0.5;
    BRepBuilderAPI_MakePolygon corePoly;
    corePoly.Add(gp_Pnt(cx - core * 0.5, 0, 0));
    corePoly.Add(gp_Pnt(cx - core * 0.5, 0, H));
    corePoly.Add(gp_Pnt(cx + core * 0.5, 0, H));
    corePoly.Add(gp_Pnt(cx + core * 0.5, 0, 0));
    corePoly.Close();
    b.Add(comp, BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(corePoly.Wire()), gp_Vec(0, L, 0)).Shape());

    auto* obj = new GeomShapeObject(m_uuid, new TopoDS_Shape(comp), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", obj->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", obj->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid, false);
}

void EarthRockfillDamDialog::onGenerateClicked()
{
    updateDiagram();
    GeometryCleanup::removeExistingGeometry(m_uuid);
    buildLayeredDam(hEdit->text().toDouble(), crestEdit->text().toDouble(), upEdit->text().toDouble(),
                    downEdit->text().toDouble(), lenEdit->text().toDouble(), qMax(1, layerEdit->text().toInt()));
}
