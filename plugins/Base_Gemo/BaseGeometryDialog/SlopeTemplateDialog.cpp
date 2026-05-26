#include "SlopeTemplateDialog.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
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

SlopeTemplateDialog::SlopeTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessSlopeTemplate)
{
    auto* l = new QVBoxLayout(this);
    auto* f = new QFormLayout;
    auto* dv = new QDoubleValidator(0.01, 1e9, 3, this);
    hEdit = new QLineEdit("65");
    benchWidthEdit = new QLineEdit("8");
    slopeEdit = new QLineEdit("1.2");
    lenEdit = new QLineEdit("120");
    benchCountEdit = new QLineEdit("3");
    for (auto* e : {hEdit, benchWidthEdit, slopeEdit, lenEdit}) e->setValidator(dv);
    benchCountEdit->setValidator(new QIntValidator(1, 8, this));

    f->addRow("坡高H", hEdit);
    f->addRow("平台宽", benchWidthEdit);
    f->addRow("坡比", slopeEdit);
    f->addRow("延伸长度L", lenEdit);
    f->addRow("平台级数", benchCountEdit);
    l->addLayout(f);

    diagramLabel = new QLabel(this);
    diagramLabel->setMinimumHeight(180);
    l->addWidget(diagramLabel);

    auto* r = new QHBoxLayout;
    auto* p = new QPushButton("更新说明图");
    auto* g = new QPushButton("生成边坡");
    r->addWidget(p);
    r->addWidget(g);
    l->addLayout(r);

    connect(p, &QPushButton::clicked, this, &SlopeTemplateDialog::onPreviewClicked);
    connect(g, &QPushButton::clicked, this, &SlopeTemplateDialog::onGenerateClicked);
    updateDiagram();
}

QJsonObject SlopeTemplateDialog::saveJson()
{
    QJsonObject o;
    o["h"] = hEdit->text().toDouble();
    o["bw"] = benchWidthEdit->text().toDouble();
    o["s"] = slopeEdit->text().toDouble();
    o["l"] = lenEdit->text().toDouble();
    o["bc"] = benchCountEdit->text().toInt();
    return o;
}

void SlopeTemplateDialog::loadJson(const QJsonObject& o)
{
    if (o.contains("h")) hEdit->setText(QString::number(o["h"].toDouble()));
    if (o.contains("bw")) benchWidthEdit->setText(QString::number(o["bw"].toDouble()));
    if (o.contains("s")) slopeEdit->setText(QString::number(o["s"].toDouble()));
    if (o.contains("l")) lenEdit->setText(QString::number(o["l"].toDouble()));
    if (o.contains("bc")) benchCountEdit->setText(QString::number(o["bc"].toInt()));
    updateDiagram();
}

void SlopeTemplateDialog::updateDiagram()
{
    const double H = hEdit->text().toDouble();
    const double bw = benchWidthEdit->text().toDouble();
    const double s = slopeEdit->text().toDouble();
    const int bc = benchCountEdit->text().toInt();

    QPixmap pix(460, 180);
    pix.fill(Qt::white);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QVector<QPointF> profile;
    profile << QPointF(0, 0);
    double x = 0.0, z = H;
    profile << QPointF(x, z);
    const double dz = H / bc;
    const double dx = dz * s;
    for (int i = 0; i < bc; ++i)
    {
        x += dx; z -= dz;
        profile << QPointF(x, z);
        if (i != bc - 1)
        {
            x += bw;
            profile << QPointF(x, z);
        }
    }
    profile << QPointF(x + bw, 0);

    double xmax = 0;
    for (const auto& pt : profile) xmax = qMax(xmax, pt.x());
    const double sc = qMin(380.0 / qMax(1.0, xmax), 130.0 / qMax(1.0, H));
    auto m = [&](double xx, double zz){ return QPointF(20 + xx * sc, 160 - zz * sc); };

    QPolygonF poly;
    for (auto& pt : profile) poly << m(pt.x(), pt.y());
    p.setBrush(QColor(200, 230, 200));
    p.drawPolygon(poly);
    p.drawText(8, 16, "边坡：连续体 + 多级平台");
    diagramLabel->setPixmap(pix);
}

void SlopeTemplateDialog::onPreviewClicked() { updateDiagram(); }

void SlopeTemplateDialog::buildLayeredSlope(double H, double bw, double slope, double L, int bc)
{
    // 严格按“顶部折线 + 底部闭合边”构造闭合轮廓，避免自交与空白面
    QVector<QPointF> topPolyline;
    double x = 0.0, z = H;
    topPolyline << QPointF(x, z);
    const double dz = H / bc;
    const double dx = dz * slope;
    for (int i = 0; i < bc; ++i)
    {
        x += dx;
        z -= dz;
        topPolyline << QPointF(x, z);
        if (i != bc - 1)
        {
            x += bw;
            topPolyline << QPointF(x, z);
        }
    }

    const double toeX = x + bw;

    QPolygonF profile;
    profile << QPointF(0.0, 0.0);          // 坡脚左
    for (const auto& p : topPolyline)      // 顶部折线（左->右）
        profile << p;
    profile << QPointF(toeX, 0.0);         // 坡脚右

    BRepBuilderAPI_MakePolygon pg;
    for (const auto& pt : profile)
        pg.Add(gp_Pnt(pt.x(), 0, pt.y()));
    pg.Close();

    TopoDS_Shape slopeSolid = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(pg.Wire()), gp_Vec(0, L, 0)).Shape();
    auto* obj = new GeomShapeObject(m_uuid, new TopoDS_Shape(slopeSolid), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", obj->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", obj->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid, false);
}

void SlopeTemplateDialog::onGenerateClicked()
{
    updateDiagram();
    GeometryCleanup::removeExistingGeometry(m_uuid);
    buildLayeredSlope(hEdit->text().toDouble(), benchWidthEdit->text().toDouble(), slopeEdit->text().toDouble(),
                      lenEdit->text().toDouble(), benchCountEdit->text().toInt());
}
