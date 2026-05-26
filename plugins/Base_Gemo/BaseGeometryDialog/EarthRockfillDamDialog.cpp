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

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam) { initUi(); }

QJsonObject EarthRockfillDamDialog::saveJson(){QJsonObject o; o["h"]=hEdit->text().toDouble(); o["crest"]=crestEdit->text().toDouble(); o["up"]=upEdit->text().toDouble(); o["down"]=downEdit->text().toDouble(); o["len"]=lenEdit->text().toDouble(); o["core"]=coreWidthEdit->text().toDouble(); o["layers"]=layerEdit->text().toInt(); return o;}

void EarthRockfillDamDialog::loadJson(const QJsonObject& o){if(o.contains("h"))hEdit->setText(QString::number(o["h"].toDouble()));if(o.contains("crest"))crestEdit->setText(QString::number(o["crest"].toDouble()));if(o.contains("up"))upEdit->setText(QString::number(o["up"].toDouble()));if(o.contains("down"))downEdit->setText(QString::number(o["down"].toDouble()));if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble()));if(o.contains("core"))coreWidthEdit->setText(QString::number(o["core"].toDouble()));if(o.contains("layers"))layerEdit->setText(QString::number(o["layers"].toInt())); updateDiagram();}

void EarthRockfillDamDialog::initUi(){auto* l=new QVBoxLayout(this); auto* f=new QFormLayout; auto* dv=new QDoubleValidator(0.01,1e9,3,this); hEdit=new QLineEdit("90"); crestEdit=new QLineEdit("12"); upEdit=new QLineEdit("2.2"); downEdit=new QLineEdit("1.9"); lenEdit=new QLineEdit("260"); coreWidthEdit=new QLineEdit("6"); layerEdit=new QLineEdit("10"); for(auto* e:{hEdit,crestEdit,upEdit,downEdit,lenEdit,coreWidthEdit}) e->setValidator(dv); layerEdit->setValidator(new QIntValidator(1,200,this)); f->addRow("坝高H",hEdit); f->addRow("坝顶宽Bt",crestEdit); f->addRow("上游坡比",upEdit); f->addRow("下游坡比",downEdit); f->addRow("坝轴线长度L",lenEdit); f->addRow("心墙宽度",coreWidthEdit); f->addRow("分层数N",layerEdit); l->addLayout(f); diagramLabel=new QLabel(this); diagramLabel->setMinimumHeight(190); l->addWidget(diagramLabel); auto* row=new QHBoxLayout; auto* prev=new QPushButton("更新说明图"); auto* gen=new QPushButton("生成土石坝"); row->addWidget(prev); row->addWidget(gen); l->addLayout(row); connect(prev,&QPushButton::clicked,this,&EarthRockfillDamDialog::onPreviewClicked); connect(gen,&QPushButton::clicked,this,&EarthRockfillDamDialog::onGenerateClicked); updateDiagram();}

void EarthRockfillDamDialog::updateDiagram(){const double H=hEdit->text().toDouble(),Bt=crestEdit->text().toDouble(),mu=upEdit->text().toDouble(),md=downEdit->text().toDouble(),core=coreWidthEdit->text().toDouble();const int N=layerEdit->text().toInt(); const double x1=H*mu,x2=x1+Bt,x3=x2+H*md; const double c=(x1+x2)*0.5; const double filt=core*0.6; QPixmap pix(500,190); pix.fill(Qt::white); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); const double s=qMin(430.0/qMax(1.0,x3),130.0/qMax(1.0,H)); auto m=[&](double x,double z){return QPointF(20+x*s,165-z*s);}; p.setBrush(QColor(196,182,136)); p.drawPolygon(QPolygonF()<<m(0,0)<<m(x1,H)<<m(x2,H)<<m(x3,0)); p.setBrush(QColor(230,210,150)); p.drawRect(QRectF(m(c-core*0.5-filt,H).x(),m(0,0).y(),filt*s,-H*s)); p.drawRect(QRectF(m(c+core*0.5,H).x(),m(0,0).y(),filt*s,-H*s)); p.setBrush(QColor(220,90,90,220)); p.drawRect(QRectF(m(c-core*0.5,H).x(),m(0,0).y(),core*s,-H*s)); p.setPen(QPen(Qt::gray,1,Qt::DashLine)); for(int i=1;i<N;i++){double z=H*i/N; p.drawLine(m(0,z),m(x3,z));} p.setPen(Qt::black); p.drawText(8,16,"土石坝：堆石区 + 反滤层 + 心墙 + 分层"); diagramLabel->setPixmap(pix);} 

void EarthRockfillDamDialog::onPreviewClicked(){updateDiagram();}

void EarthRockfillDamDialog::buildLayeredDam(double H,double Bt,double mu,double md,double L,int layers)
{
    Q_UNUSED(layers);
    const double x1 = H * mu, x2 = x1 + Bt, x3 = x2 + H * md;
    const double core = coreWidthEdit->text().toDouble();
    const double filterW = core * 0.6;
    const double c = (x1 + x2) * 0.5;

    auto makeZone=[&](double xl,double xr){
        BRepBuilderAPI_MakePolygon pg;
        pg.Add(gp_Pnt(xl,0,0));
        pg.Add(gp_Pnt(xl + (x1-xl)*(H/H),0,H));
        pg.Add(gp_Pnt(xr - (x3-xr)*(H/H),0,H));
        pg.Add(gp_Pnt(xr,0,0));
        pg.Close();
        return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(pg.Wire()),gp_Vec(0,L,0)).Shape();
    };

    // 连续主坝体
    BRepBuilderAPI_MakePolygon damPoly;
    damPoly.Add(gp_Pnt(0,0,0)); damPoly.Add(gp_Pnt(x1,0,H)); damPoly.Add(gp_Pnt(x2,0,H)); damPoly.Add(gp_Pnt(x3,0,0)); damPoly.Close();
    TopoDS_Shape damBody = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(damPoly.Wire()),gp_Vec(0,L,0)).Shape();

    // 心墙与反滤层做“嵌入分区条带”
    BRepBuilderAPI_MakePolygon corePoly;
    corePoly.Add(gp_Pnt(c-core*0.5,0,0)); corePoly.Add(gp_Pnt(c-core*0.5,0,H)); corePoly.Add(gp_Pnt(c+core*0.5,0,H)); corePoly.Add(gp_Pnt(c+core*0.5,0,0)); corePoly.Close();
    TopoDS_Shape coreBody = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(corePoly.Wire()),gp_Vec(0,L,0)).Shape();

    BRepBuilderAPI_MakePolygon f1; f1.Add(gp_Pnt(c-core*0.5-filterW,0,0)); f1.Add(gp_Pnt(c-core*0.5-filterW,0,H)); f1.Add(gp_Pnt(c-core*0.5,0,H)); f1.Add(gp_Pnt(c-core*0.5,0,0)); f1.Close();
    BRepBuilderAPI_MakePolygon f2; f2.Add(gp_Pnt(c+core*0.5,0,0)); f2.Add(gp_Pnt(c+core*0.5,0,H)); f2.Add(gp_Pnt(c+core*0.5+filterW,0,H)); f2.Add(gp_Pnt(c+core*0.5+filterW,0,0)); f2.Close();
    TopoDS_Shape f1Body = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(f1.Wire()),gp_Vec(0,L,0)).Shape();
    TopoDS_Shape f2Body = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(f2.Wire()),gp_Vec(0,L,0)).Shape();

    TopoDS_Compound comp; BRep_Builder b; b.MakeCompound(comp); b.Add(comp, damBody); b.Add(comp, coreBody); b.Add(comp, f1Body); b.Add(comp, f2Body);

    auto* obj = new GeomShapeObject(m_uuid, new TopoDS_Shape(comp), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", obj->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", obj->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid, false);
}

void EarthRockfillDamDialog::onGenerateClicked(){updateDiagram(); GeometryCleanup::removeExistingGeometry(m_uuid); buildLayeredDam(hEdit->text().toDouble(),crestEdit->text().toDouble(),upEdit->text().toDouble(),downEdit->text().toDouble(),lenEdit->text().toDouble(),layerEdit->text().toInt());}
