#include "EarthRockfillDamDialog.h"
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
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam) { initUi(); }
QJsonObject EarthRockfillDamDialog::saveJson(){QJsonObject o; o["h"]=hEdit->text().toDouble(); o["crest"]=crestEdit->text().toDouble(); o["up"]=upEdit->text().toDouble(); o["down"]=downEdit->text().toDouble(); o["len"]=lenEdit->text().toDouble(); o["core"]=coreWidthEdit->text().toDouble(); return o;}
void EarthRockfillDamDialog::loadJson(const QJsonObject& o){if(o.contains("h"))hEdit->setText(QString::number(o["h"].toDouble())); if(o.contains("crest"))crestEdit->setText(QString::number(o["crest"].toDouble())); if(o.contains("up"))upEdit->setText(QString::number(o["up"].toDouble())); if(o.contains("down"))downEdit->setText(QString::number(o["down"].toDouble())); if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble())); if(o.contains("core"))coreWidthEdit->setText(QString::number(o["core"].toDouble()));}
void EarthRockfillDamDialog::initUi(){auto* l=new QVBoxLayout(this);auto* f=new QFormLayout;auto* v=new QDoubleValidator(0.01,1e9,3,this); hEdit=new QLineEdit("90");crestEdit=new QLineEdit("12");upEdit=new QLineEdit("2.2");downEdit=new QLineEdit("1.9");lenEdit=new QLineEdit("260");coreWidthEdit=new QLineEdit("6"); for(auto* e:{hEdit,crestEdit,upEdit,downEdit,lenEdit,coreWidthEdit}) e->setValidator(v); f->addRow("坝高H",hEdit);f->addRow("坝顶宽Bt",crestEdit);f->addRow("上游坡比",upEdit);f->addRow("下游坡比",downEdit);f->addRow("坝轴线长度L",lenEdit);f->addRow("心墙宽度",coreWidthEdit); l->addLayout(f); auto* b=new QPushButton("生成土石坝"); l->addWidget(b); connect(b,&QPushButton::clicked,this,&EarthRockfillDamDialog::onGenerateClicked);}
void EarthRockfillDamDialog::onGenerateClicked(){bool ok=true;double H=hEdit->text().toDouble(&ok); if(!ok)return; double Bt=crestEdit->text().toDouble(&ok); if(!ok)return; double mu=upEdit->text().toDouble(&ok); double md=downEdit->text().toDouble(&ok); double L=lenEdit->text().toDouble(&ok); if(!ok){QMessageBox::warning(this,"参数错误","参数无效");return;} GeometryCleanup::removeExistingGeometry(m_uuid); double x1=H*mu,x2=x1+Bt,x3=x2+H*md; BRepBuilderAPI_MakePolygon p; p.Add(gp_Pnt(0,0,0)); p.Add(gp_Pnt(x1,0,H)); p.Add(gp_Pnt(x2,0,H)); p.Add(gp_Pnt(x3,0,0)); p.Close(); TopoDS_Shape s=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()),gp_Vec(0,L,0)).Shape(); auto* shape=new TopoDS_Shape(s); auto* obj=new GeomShapeObject(m_uuid,shape,getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
