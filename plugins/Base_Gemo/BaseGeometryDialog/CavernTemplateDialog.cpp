#include "CavernTemplateDialog.h"
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>
CavernTemplateDialog::CavernTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessCavernTemplate){auto* l=new QVBoxLayout(this);auto* f=new QFormLayout;auto* v=new QDoubleValidator(0.01,1e9,3,this); spanEdit=new QLineEdit("18");wallH=new QLineEdit("12");archH=new QLineEdit("4");lenEdit=new QLineEdit("80"); for(auto* e:{spanEdit,wallH,archH,lenEdit}) e->setValidator(v); f->addRow("洞室跨度",spanEdit);f->addRow("直墙高",wallH);f->addRow("拱顶高",archH);f->addRow("洞室长度",lenEdit); l->addLayout(f); auto* b=new QPushButton("生成洞室"); l->addWidget(b); connect(b,&QPushButton::clicked,this,&CavernTemplateDialog::onGenerateClicked);}
QJsonObject CavernTemplateDialog::saveJson(){QJsonObject o;o["span"]=spanEdit->text().toDouble();o["wh"]=wallH->text().toDouble();o["ah"]=archH->text().toDouble();o["len"]=lenEdit->text().toDouble();return o;}
void CavernTemplateDialog::loadJson(const QJsonObject& o){if(o.contains("span"))spanEdit->setText(QString::number(o["span"].toDouble()));if(o.contains("wh"))wallH->setText(QString::number(o["wh"].toDouble()));if(o.contains("ah"))archH->setText(QString::number(o["ah"].toDouble()));if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble()));}
void CavernTemplateDialog::onGenerateClicked(){double span=spanEdit->text().toDouble(),wh=wallH->text().toDouble(),ah=archH->text().toDouble(),len=lenEdit->text().toDouble(); GeometryCleanup::removeExistingGeometry(m_uuid); BRepBuilderAPI_MakePolygon p; p.Add(gp_Pnt(0,0,0));p.Add(gp_Pnt(0,0,wh));p.Add(gp_Pnt(span*0.25,0,wh+ah));p.Add(gp_Pnt(span*0.75,0,wh+ah));p.Add(gp_Pnt(span,0,wh));p.Add(gp_Pnt(span,0,0));p.Close(); TopoDS_Shape shape=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()),gp_Vec(0,len,0)).Shape(); auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(shape),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
