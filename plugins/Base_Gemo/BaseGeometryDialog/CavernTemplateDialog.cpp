#include "CavernTemplateDialog.h"
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
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

CavernTemplateDialog::CavernTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessCavernTemplate)
{
    auto* l=new QVBoxLayout(this); auto* f=new QFormLayout; auto* v=new QDoubleValidator(0.01,1e9,3,this);
    spanEdit=new QLineEdit("18"); wallHEdit=new QLineEdit("12"); archHEdit=new QLineEdit("4"); lenEdit=new QLineEdit("80"); liningEdit=new QLineEdit("0.8");
    for(auto* e:{spanEdit,wallHEdit,archHEdit,lenEdit,liningEdit}) e->setValidator(v);
    f->addRow("洞室跨度",spanEdit); f->addRow("直墙高",wallHEdit); f->addRow("拱顶高",archHEdit); f->addRow("洞室长度",lenEdit); f->addRow("衬砌厚度",liningEdit);
    l->addLayout(f); diagramLabel=new QLabel(this); diagramLabel->setMinimumHeight(180); l->addWidget(diagramLabel);
    auto* r=new QHBoxLayout; auto* p=new QPushButton("更新说明图"); auto* g=new QPushButton("生成洞室"); r->addWidget(p); r->addWidget(g); l->addLayout(r);
    connect(p,&QPushButton::clicked,this,&CavernTemplateDialog::onPreviewClicked); connect(g,&QPushButton::clicked,this,&CavernTemplateDialog::onGenerateClicked);
    updateDiagram();
}
QJsonObject CavernTemplateDialog::saveJson(){QJsonObject o; o["span"]=spanEdit->text().toDouble();o["wh"]=wallHEdit->text().toDouble();o["ah"]=archHEdit->text().toDouble();o["len"]=lenEdit->text().toDouble();o["lining"]=liningEdit->text().toDouble(); return o;}
void CavernTemplateDialog::loadJson(const QJsonObject& o){if(o.contains("span"))spanEdit->setText(QString::number(o["span"].toDouble()));if(o.contains("wh"))wallHEdit->setText(QString::number(o["wh"].toDouble()));if(o.contains("ah"))archHEdit->setText(QString::number(o["ah"].toDouble()));if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble()));if(o.contains("lining"))liningEdit->setText(QString::number(o["lining"].toDouble())); updateDiagram();}
void CavernTemplateDialog::updateDiagram(){double span=spanEdit->text().toDouble(),wh=wallHEdit->text().toDouble(),ah=archHEdit->text().toDouble(),lining=liningEdit->text().toDouble(); QPixmap pix(460,180); pix.fill(Qt::white); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); double H=wh+ah,sc=qMin(380.0/qMax(1.0,span),130.0/qMax(1.0,H)); auto m=[&](double x,double z){return QPointF(20+x*sc,160-z*sc);}; QPolygonF out; out<<m(0,0)<<m(0,wh)<<m(span*0.25,wh+ah)<<m(span*0.75,wh+ah)<<m(span,wh)<<m(span,0); p.setBrush(QColor(190,200,220)); p.drawPolygon(out); QPolygonF in; double t=qMax(0.1,lining); in<<m(t,t)<<m(t,wh-t)<<m(span*0.25,wh+ah-t)<<m(span*0.75,wh+ah-t)<<m(span-t,wh-t)<<m(span-t,t); p.setBrush(Qt::white); p.drawPolygon(in); p.drawText(8,16,"洞室：围岩 + 衬砌层"); diagramLabel->setPixmap(pix);}
void CavernTemplateDialog::onPreviewClicked(){updateDiagram();}
void CavernTemplateDialog::buildLayeredCavern(double span,double wh,double ah,double len,double lining){TopoDS_Compound comp; BRep_Builder b; b.MakeCompound(comp); // 围岩块
BRepBuilderAPI_MakePolygon rock; rock.Add(gp_Pnt(-span*0.5,0,0)); rock.Add(gp_Pnt(-span*0.5,0,wh+ah+4)); rock.Add(gp_Pnt(span*1.5,0,wh+ah+4)); rock.Add(gp_Pnt(span*1.5,0,0)); rock.Close(); b.Add(comp,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(rock.Wire()),gp_Vec(0,len,0)).Shape());
// 洞室空腔（用线框表达内轮廓边）
BRepBuilderAPI_MakePolygon cav; cav.Add(gp_Pnt(0,0,0)); cav.Add(gp_Pnt(0,0,wh)); cav.Add(gp_Pnt(span*0.25,0,wh+ah)); cav.Add(gp_Pnt(span*0.75,0,wh+ah)); cav.Add(gp_Pnt(span,0,wh)); cav.Add(gp_Pnt(span,0,0)); cav.Close(); b.Add(comp,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(cav.Wire()),gp_Vec(0,len,0)).Shape());
// 衬砌层（简化为外轮廓偏移）
BRepBuilderAPI_MakePolygon liningPoly; double t=qMax(0.1,lining); liningPoly.Add(gp_Pnt(-t,0,0)); liningPoly.Add(gp_Pnt(-t,0,wh+t)); liningPoly.Add(gp_Pnt(span*0.25,0,wh+ah+t)); liningPoly.Add(gp_Pnt(span*0.75,0,wh+ah+t)); liningPoly.Add(gp_Pnt(span+t,0,wh+t)); liningPoly.Add(gp_Pnt(span+t,0,0)); liningPoly.Close(); b.Add(comp,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(liningPoly.Wire()),gp_Vec(0,len,0)).Shape());
auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(comp),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
void CavernTemplateDialog::onGenerateClicked(){updateDiagram(); GeometryCleanup::removeExistingGeometry(m_uuid); buildLayeredCavern(spanEdit->text().toDouble(),wallHEdit->text().toDouble(),archHEdit->text().toDouble(),lenEdit->text().toDouble(),liningEdit->text().toDouble());}
