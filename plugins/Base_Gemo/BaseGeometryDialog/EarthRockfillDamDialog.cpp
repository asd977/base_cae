#include "EarthRockfillDamDialog.h"
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

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget*) : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam){ initUi(); }
QJsonObject EarthRockfillDamDialog::saveJson(){QJsonObject o; o["h"]=hEdit->text().toDouble(); o["crest"]=crestEdit->text().toDouble(); o["up"]=upEdit->text().toDouble(); o["down"]=downEdit->text().toDouble(); o["len"]=lenEdit->text().toDouble(); o["core"]=coreWidthEdit->text().toDouble(); return o;}
void EarthRockfillDamDialog::loadJson(const QJsonObject& o){if(o.contains("h"))hEdit->setText(QString::number(o["h"].toDouble()));if(o.contains("crest"))crestEdit->setText(QString::number(o["crest"].toDouble()));if(o.contains("up"))upEdit->setText(QString::number(o["up"].toDouble()));if(o.contains("down"))downEdit->setText(QString::number(o["down"].toDouble()));if(o.contains("len"))lenEdit->setText(QString::number(o["len"].toDouble()));if(o.contains("core"))coreWidthEdit->setText(QString::number(o["core"].toDouble())); updateDiagram();}
void EarthRockfillDamDialog::initUi(){auto* l=new QVBoxLayout(this); auto* f=new QFormLayout; auto* v=new QDoubleValidator(0.01,1e9,3,this); hEdit=new QLineEdit("90"); crestEdit=new QLineEdit("12"); upEdit=new QLineEdit("2.2"); downEdit=new QLineEdit("1.9"); lenEdit=new QLineEdit("260"); coreWidthEdit=new QLineEdit("6"); for(auto* e:{hEdit,crestEdit,upEdit,downEdit,lenEdit,coreWidthEdit}) e->setValidator(v); f->addRow("坝高H",hEdit);f->addRow("坝顶宽Bt",crestEdit);f->addRow("上游坡比",upEdit);f->addRow("下游坡比",downEdit);f->addRow("坝轴线长度L",lenEdit);f->addRow("心墙宽度",coreWidthEdit); l->addLayout(f); diagramLabel=new QLabel(this); diagramLabel->setMinimumHeight(180); l->addWidget(diagramLabel); auto* row=new QHBoxLayout; auto* prev=new QPushButton("更新说明图"); auto* gen=new QPushButton("生成土石坝"); row->addWidget(prev); row->addWidget(gen); l->addLayout(row); connect(prev,&QPushButton::clicked,this,&EarthRockfillDamDialog::onPreviewClicked); connect(gen,&QPushButton::clicked,this,&EarthRockfillDamDialog::onGenerateClicked); updateDiagram();}
void EarthRockfillDamDialog::updateDiagram(){double H=hEdit->text().toDouble(),Bt=crestEdit->text().toDouble(),mu=upEdit->text().toDouble(),md=downEdit->text().toDouble(),core=coreWidthEdit->text().toDouble(); QPixmap pix(440,180); pix.fill(Qt::white); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); double x1=H*mu,x2=x1+Bt,x3=x2+H*md; double sx=380.0/qMax(1.0,x3), sz=130.0/qMax(1.0,H), s=qMin(sx,sz), ox=20, oy=160; auto m=[&](double x,double z){return QPointF(ox+x*s,oy-z*s);}; QPolygonF poly; poly<<m(0,0)<<m(x1,H)<<m(x2,H)<<m(x3,0); p.setBrush(QColor(220,200,140)); p.drawPolygon(poly); double c=(x1+x2)/2.0; p.setPen(QPen(Qt::red,2)); p.drawLine(m(c-core/2,0),m(c-core/2,H)); p.drawLine(m(c+core/2,0),m(c+core/2,H)); p.drawText(8,16,"土石坝参数说明图（红色为心墙）"); diagramLabel->setPixmap(pix);}
void EarthRockfillDamDialog::onPreviewClicked(){updateDiagram();}
void EarthRockfillDamDialog::onGenerateClicked(){updateDiagram(); double H=hEdit->text().toDouble(),Bt=crestEdit->text().toDouble(),mu=upEdit->text().toDouble(),md=downEdit->text().toDouble(),L=lenEdit->text().toDouble(); GeometryCleanup::removeExistingGeometry(m_uuid); double x1=H*mu,x2=x1+Bt,x3=x2+H*md; BRepBuilderAPI_MakePolygon pg; pg.Add(gp_Pnt(0,0,0)); pg.Add(gp_Pnt(x1,0,H)); pg.Add(gp_Pnt(x2,0,H)); pg.Add(gp_Pnt(x3,0,0)); pg.Close(); TopoDS_Shape s=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(pg.Wire()),gp_Vec(0,L,0)).Shape(); auto* obj=new GeomShapeObject(m_uuid,new TopoDS_Shape(s),getDisplayName()); GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(obj); if(auto* vis=ServiceRegistry::instance().getService<VisualWidgetService>()){vis->addProp(m_uuid+"_face",obj->getSurfActor(),"Geometry",m_uuid+"_face",true);vis->addProp(m_uuid+"_edge",obj->getEdgeActor(),"Geometry",m_uuid+"_face",true);} if(auto* pm=ServiceRegistry::instance().getService<ProjectManagerService>()) pm->setNodeDimmed(m_uuid,false);}
