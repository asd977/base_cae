#include "SlopeTemplateDialog.h"
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
SlopeTemplateDialog::SlopeTemplateDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessSlopeTemplate){auto* l=new QVBoxLayout(this);auto* f=new QFormLayout;auto* v=new QDoubleValidator(0.01,1e9,3,this); hEdit=new QLineEdit("65");benchWidthEdit=new QLineEdit("8");slopeEdit=new QLineEdit("1.2");lenEdit=new QLineEdit("120"); for(auto* e:{hEdit,benchWidthEdit,slopeEdit,lenEdit}) e->setValidator(v); f->addRow("坡高H",hEdit);f->addRow("平台宽",benchWidthEdit);f->addRow("坡比",slopeEdit);f->addRow("延伸长度L",lenEdit); l->addLayout(f); auto* b=new QPushButton("生成边坡"); l->addWidget(b); connect(b,&QPushButton::clicked,this,&SlopeTemplateDialog::onGenerateClicked);}
QJsonObject SlopeTemplateDialog::saveJson(){QJsonObject o;o["h"]=hEdit->text().toDouble();o["bw"]=benchWidthEdit->text().toDouble();o["s"]=slopeEdit->text().toDouble();o["l"]=lenEdit->text().toDouble();return o;}
void SlopeTemplateDialog::loadJson(const QJsonObject& o){if(o.contains("h"))hEdit->setText(QString::number(o["h"].toDouble()));if(o.contains("bw"))benchWidthEdit->setText(QString::number(o["bw"].toDouble()));if(o.contains("s"))slopeEdit->setText(QString::number(o["s"].toDouble()));if(o.contains("l"))lenEdit->setText(QString::number(o["l"].toDouble()));}
void SlopeTemplateDialog::onGenerateClicked(){double H=hEdit->text().toDouble(),bw=benchWidthEdit->text().toDouble(),s=slopeEdit->text().toDouble(),L=lenEdit->text().toDouble(); GeometryCleanup::removeExistingGeometry(m_uuid); double x1=(H*0.6)*s,x2=x1+bw,x3=x2+H*0.4*s; BRepBuilderAPI_MakePolygon p; p.Add(gp_Pnt(0,0,0));p.Add(gp_Pnt(x1,0,H*0.6));p.Add(gp_Pnt(x2,0,H*0.6));p.Add(gp_Pnt(x3,0,0));p.Close(); TopoDS_Shape shape=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()),gp_Vec(0,L,0)).Shape(); auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(shape),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
