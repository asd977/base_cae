#include "CavernTemplateDialog.h"
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
CavernTemplateDialog::CavernTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessCavernTemplate){auto* l=new QVBoxLayout(this);auto* f=new QFormLayout;auto* v=new QDoubleValidator(0.01,1e9,3,this); spanEdit=new QLineEdit("18");wallH=new QLineEdit("12");archH=new QLineEdit("4");lenEdit=new QLineEdit("80"); for(auto* e:{spanEdit,wallH,archH,lenEdit}) e->setValidator(v); f->addRow("洞室跨度",spanEdit);f->addRow("直墙高",wallH);f->addRow("拱顶高",archH);f->addRow("洞室长度",lenEdit); l->addLayout(f); diagramLabel=new QLabel(this); diagramLabel->setMinimumHeight(170); l->addWidget(diagramLabel); auto* r=new QHBoxLayout; auto* p=new QPushButton("更新说明图"); auto* g=new QPushButton("生成洞室"); r->addWidget(p); r->addWidget(g); l->addLayout(r); connect(p,&QPushButton::clicked,this,&CavernTemplateDialog::onPreviewClicked); connect(g,&QPushButton::clicked,this,&CavernTemplateDialog::onGenerateClicked); updateDiagram();}
QJsonObject CavernTemplateDialog::saveJson(){QJsonObject o;o["span"]=spanEdit->text().toDouble();o["wh"]=wallH->text().toDouble();o["ah"]=archH->text().toDouble();o["len"]=lenEdit->text().toDouble();return o;}
void CavernTemplateDialog::loadJson(const QJsonObject& o){if(o.contains("span"))spanEdit->setText(QString::number(o["span"].toDouble()));if(o.contains("wh"))wallH->setText(QString::number(o["wh"].toDouble()));if(o.contains("ah"))archH->setText(QString::number(o["ah"].toDouble()));if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble())); updateDiagram();}
void CavernTemplateDialog::updateDiagram(){double span=spanEdit->text().toDouble(),wh=wallH->text().toDouble(),ah=archH->text().toDouble(); QPixmap pix(420,170); pix.fill(Qt::white); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); double H=wh+ah, sc=qMin(360.0/qMax(1.0,span),120.0/qMax(1.0,H)); auto m=[&](double x,double z){return QPointF(20+x*sc,150-z*sc);}; QPolygonF poly; poly<<m(0,0)<<m(0,wh)<<m(span*0.25,wh+ah)<<m(span*0.75,wh+ah)<<m(span,wh)<<m(span,0); p.setBrush(QColor(210,220,240)); p.drawPolygon(poly); p.drawText(8,16,"洞室参数说明图（直墙+拱顶）"); diagramLabel->setPixmap(pix);}
void CavernTemplateDialog::onPreviewClicked(){updateDiagram();}
void CavernTemplateDialog::onGenerateClicked(){updateDiagram(); double span=spanEdit->text().toDouble(),wh=wallH->text().toDouble(),ah=archH->text().toDouble(),len=lenEdit->text().toDouble(); GeometryCleanup::removeExistingGeometry(m_uuid); BRepBuilderAPI_MakePolygon pg; pg.Add(gp_Pnt(0,0,0));pg.Add(gp_Pnt(0,0,wh));pg.Add(gp_Pnt(span*0.25,0,wh+ah));pg.Add(gp_Pnt(span*0.75,0,wh+ah));pg.Add(gp_Pnt(span,0,wh));pg.Add(gp_Pnt(span,0,0));pg.Close(); auto shape=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(pg.Wire()),gp_Vec(0,len,0)).Shape(); auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(shape),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
