#include "SlopeTemplateDialog.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
SlopeTemplateDialog::SlopeTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessSlopeTemplate){auto* l=new QVBoxLayout(this); auto* f=new QFormLayout; auto* v=new QDoubleValidator(0.01,1e9,3,this); hEdit=new QLineEdit("65"); benchWidthEdit=new QLineEdit("8"); slopeEdit=new QLineEdit("1.2"); lenEdit=new QLineEdit("120"); for(auto* e:{hEdit,benchWidthEdit,slopeEdit,lenEdit}) e->setValidator(v); f->addRow("坡高H",hEdit);f->addRow("平台宽",benchWidthEdit);f->addRow("坡比",slopeEdit);f->addRow("延伸长度L",lenEdit); l->addLayout(f); diagramLabel=new QLabel(this); diagramLabel->setMinimumHeight(170); l->addWidget(diagramLabel); auto* r=new QHBoxLayout; auto* p=new QPushButton("更新说明图"); auto* g=new QPushButton("生成边坡"); r->addWidget(p); r->addWidget(g); l->addLayout(r); connect(p,&QPushButton::clicked,this,&SlopeTemplateDialog::onPreviewClicked); connect(g,&QPushButton::clicked,this,&SlopeTemplateDialog::onGenerateClicked); updateDiagram();}
QJsonObject SlopeTemplateDialog::saveJson(){QJsonObject o;o["h"]=hEdit->text().toDouble();o["bw"]=benchWidthEdit->text().toDouble();o["s"]=slopeEdit->text().toDouble();o["l"]=lenEdit->text().toDouble();return o;}
void SlopeTemplateDialog::loadJson(const QJsonObject& o){if(o.contains("h"))hEdit->setText(QString::number(o["h"].toDouble()));if(o.contains("bw"))benchWidthEdit->setText(QString::number(o["bw"].toDouble()));if(o.contains("s"))slopeEdit->setText(QString::number(o["s"].toDouble()));if(o.contains("l"))lenEdit->setText(QString::number(o["l"].toDouble())); updateDiagram();}
void SlopeTemplateDialog::updateDiagram(){double H=hEdit->text().toDouble(),bw=benchWidthEdit->text().toDouble(),s=slopeEdit->text().toDouble(); double x1=(H*0.6)*s,x2=x1+bw,x3=x2+H*0.4*s; QPixmap pix(420,170); pix.fill(Qt::white); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); double sc=qMin(360.0/qMax(1.0,x3),120.0/qMax(1.0,H)); auto m=[&](double x,double z){return QPointF(20+x*sc,150-z*sc);}; QPolygonF poly; poly<<m(0,0)<<m(x1,H*0.6)<<m(x2,H*0.6)<<m(x3,0); p.setBrush(QColor(200,230,200)); p.drawPolygon(poly); p.drawText(8,16,"边坡参数说明图（含平台）"); diagramLabel->setPixmap(pix);}
void SlopeTemplateDialog::onPreviewClicked(){updateDiagram();}
void SlopeTemplateDialog::onGenerateClicked(){updateDiagram();double H=hEdit->text().toDouble(),bw=benchWidthEdit->text().toDouble(),s=slopeEdit->text().toDouble(),L=lenEdit->text().toDouble(); GeometryCleanup::removeExistingGeometry(m_uuid); double x1=(H*0.6)*s,x2=x1+bw,x3=x2+H*0.4*s; BRepBuilderAPI_MakePolygon pg; pg.Add(gp_Pnt(0,0,0)); pg.Add(gp_Pnt(x1,0,H*0.6)); pg.Add(gp_Pnt(x2,0,H*0.6)); pg.Add(gp_Pnt(x3,0,0)); pg.Close(); auto shape=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(pg.Wire()),gp_Vec(0,L,0)).Shape(); auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(shape),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
