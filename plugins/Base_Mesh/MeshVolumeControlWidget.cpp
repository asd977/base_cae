#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <QMessageBox>
#include <QTableWidgetItem>

#include "../Base_Gemo/GeomManagerInstance.h"
#include "../Base_Gemo/GeomShapeObject.h"

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>

#include <vtkActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkNew.h>

namespace {
vtkSmartPointer<vtkActor> buildVolumeHighlightActor(const TopoDS_Shape& solid)
{
    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(solid);
    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    vtkNew<IVtkTools_DisplayModeFilter> dmSurf;
    dmSurf->SetInputConnection(src->GetOutputPort());
    dmSurf->SetDisplayMode(DM_Shading);
    dmSurf->SetSmoothShading(true);
    dmSurf->SetFaceBoundaryDraw(true);
    dmSurf->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(dmSurf->GetOutput());
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(0.3, 0.8, 1.0);
    actor->GetProperty()->SetOpacity(0.55);
    return actor;
}
}

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
    , m_highlightPropId("mesh.volume.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshVolumeControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshVolumeControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshVolumeControlWidget::onResetForm);

    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshVolumeControlWidget::onSelectionChanged);
    connect(ui->editVolumeIds, &QLineEdit::textChanged, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinGrowthRate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->checkBoundaryLayer, &QCheckBox::toggled, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinLayerCount, qOverload<int>(&QSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);

    connect(ui->btnRefreshEntity, &QPushButton::clicked, this, &MeshVolumeControlWidget::onRefreshEntities);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAppendSelectedEntity);
    connect(ui->btnHighlightEntity, &QPushButton::clicked, this, &MeshVolumeControlWidget::onHighlightSelectedEntity);
}

MeshVolumeControlWidget::~MeshVolumeControlWidget(){ clearHighlight(); delete ui; }
void MeshVolumeControlWidget::beforeLoad(){ rebuildVolumeCandidates(); refreshSummary(); }
void MeshVolumeControlWidget::beforeUnload(){ clearHighlight(); }

void MeshVolumeControlWidget::onAddControl(){ QString err; if(!validateInput(err)){ QMessageBox::warning(this,tr("体控制"),err); return;} auto item=collectFormData(); m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); onResetForm(); }
void MeshVolumeControlWidget::onUpdateControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; QString err; if(!validateInput(err)){ QMessageBox::warning(this,tr("体控制"),err); return;} auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); refreshSummary(); }
void MeshVolumeControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; m_items.removeAt(row); ui->tableControls->removeRow(row); onResetForm(); }
void MeshVolumeControlWidget::onResetForm(){ ui->editVolumeIds->clear(); ui->spinMinSize->setValue(0.5); ui->spinMaxSize->setValue(2.0); ui->spinGrowthRate->setValue(1.2); ui->checkBoundaryLayer->setChecked(false); ui->spinLayerCount->setValue(3); ui->tableControls->clearSelection(); refreshButtons(); refreshSummary(); }
void MeshVolumeControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); refreshButtons(); }
void MeshVolumeControlWidget::onParameterEdited(){ ui->spinLayerCount->setEnabled(ui->checkBoundaryLayer->isChecked()); refreshButtons(); refreshSummary(); }

void MeshVolumeControlWidget::onRefreshEntities(){ rebuildVolumeCandidates(); }
void MeshVolumeControlWidget::onAppendSelectedEntity(){ int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; QString t=ui->editVolumeIds->text().trimmed(); QStringList p=t.isEmpty()?QStringList{}:t.split(',',Qt::SkipEmptyParts); QString s=QString::number(id); if(!p.contains(s)){ p<<s; ui->editVolumeIds->setText(p.join(',')); }}
void MeshVolumeControlWidget::onHighlightSelectedEntity(){ clearHighlight(); int id=ui->cmbEntity->currentData().toInt(); if(!m_volumeMap.contains(id))return; if(auto* v=ServiceRegistry::instance().getService<VisualWidgetService>()){ v->addProp(m_highlightPropId, buildVolumeHighlightActor(m_volumeMap.value(id)), "MeshControl", tr("体高亮 #%1").arg(id), false, true);} }

void MeshVolumeControlWidget::setupUiState(){ ui->tableControls->setColumnCount(6); ui->tableControls->setHorizontalHeaderLabels({tr("体 ID"),tr("最小尺寸"),tr("最大尺寸"),tr("增长率"),tr("边界层"),tr("层数")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildVolumeCandidates(); }
bool MeshVolumeControlWidget::validateInput(QString &err) const{ if(ui->editVolumeIds->text().trimmed().isEmpty()){ err=tr("请先输入或选择体 ID。"); return false;} if(ui->spinMinSize->value()>ui->spinMaxSize->value()){ err=tr("最小尺寸不能大于最大尺寸。"); return false;} return true; }
MeshVolumeControlWidget::VolumeControlItem MeshVolumeControlWidget::collectFormData() const{ VolumeControlItem i; i.volumeIds=ui->editVolumeIds->text().trimmed(); i.minSize=ui->spinMinSize->value(); i.maxSize=ui->spinMaxSize->value(); i.growthRate=ui->spinGrowthRate->value(); i.boundaryLayer=ui->checkBoundaryLayer->isChecked(); i.layerCount=ui->spinLayerCount->value(); return i; }
void MeshVolumeControlWidget::fillFormData(const VolumeControlItem &i){ ui->editVolumeIds->setText(i.volumeIds); ui->spinMinSize->setValue(i.minSize); ui->spinMaxSize->setValue(i.maxSize); ui->spinGrowthRate->setValue(i.growthRate); ui->checkBoundaryLayer->setChecked(i.boundaryLayer); ui->spinLayerCount->setValue(i.layerCount); }
void MeshVolumeControlWidget::appendOrUpdateTableRow(int row, const VolumeControlItem &i){ if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(i.volumeIds); ui->tableControls->item(row,1)->setText(QString::number(i.minSize,'f',3)); ui->tableControls->item(row,2)->setText(QString::number(i.maxSize,'f',3)); ui->tableControls->item(row,3)->setText(QString::number(i.growthRate,'f',2)); ui->tableControls->item(row,4)->setText(i.boundaryLayer?tr("启用"):tr("关闭")); ui->tableControls->item(row,5)->setText(i.boundaryLayer?QString::number(i.layerCount):"-"); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(i.volumeIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(QString::number(i.minSize,'f',3))); ui->tableControls->setItem(r,2,new QTableWidgetItem(QString::number(i.maxSize,'f',3))); ui->tableControls->setItem(r,3,new QTableWidgetItem(QString::number(i.growthRate,'f',2))); ui->tableControls->setItem(r,4,new QTableWidgetItem(i.boundaryLayer?tr("启用"):tr("关闭"))); ui->tableControls->setItem(r,5,new QTableWidgetItem(i.boundaryLayer?QString::number(i.layerCount):"-")); }
void MeshVolumeControlWidget::refreshButtons(){ bool sel=ui->tableControls->currentRow()>=0; bool in=!ui->editVolumeIds->text().trimmed().isEmpty(); ui->btnAdd->setEnabled(in); ui->btnUpdate->setEnabled(sel&&in); ui->btnDelete->setEnabled(sel);} 
void MeshVolumeControlWidget::refreshSummary(){ ui->labelSummary->setText(tr("已配置 %1 条体控制规则，可选体实体 %2 个。先下拉选择并高亮确认。").arg(m_items.size()).arg(m_volumeMap.size())); }
void MeshVolumeControlWidget::rebuildVolumeCandidates(){ clearHighlight(); m_volumeMap.clear(); ui->cmbEntity->clear(); auto* mgr=GeomManager::GeomManagerInstance::getInstance(); if(!mgr||mgr->isEmpty()){ updateEntityUiState(); refreshSummary(); return;} auto map=mgr->getShapMap(); GeomShapeObject* obj=nullptr; for(auto it=map.constBegin();it!=map.constEnd();++it){ if(it.value()&&it.value()->getShape()){ obj=it.value(); break; }} if(!obj){ updateEntityUiState(); refreshSummary(); return;} TopTools_IndexedMapOfShape sm; TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, sm); for(int i=1;i<=sm.Extent();++i){ m_volumeMap.insert(i, sm(i)); ui->cmbEntity->addItem(tr("体 #%1").arg(i), i);} updateEntityUiState(); refreshSummary(); }
void MeshVolumeControlWidget::clearHighlight(){ if(auto* v=ServiceRegistry::instance().getService<VisualWidgetService>()) v->removeProp(m_highlightPropId,true); }
void MeshVolumeControlWidget::updateEntityUiState(){ bool has=!m_volumeMap.isEmpty(); ui->cmbEntity->setEnabled(has); ui->btnAppendEntity->setEnabled(has); ui->btnHighlightEntity->setEnabled(has); }
