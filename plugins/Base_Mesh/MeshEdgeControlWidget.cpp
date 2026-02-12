#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <QMessageBox>
#include <QTableWidgetItem>

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
    , m_highlightPropId("mesh.edge.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshEdgeControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshEdgeControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshEdgeControlWidget::onResetForm);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshEdgeControlWidget::onSelectionChanged);
    connect(ui->editEdgeIds, &QLineEdit::textChanged, this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinDivisions, qOverload<int>(&QSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinBias, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->checkHardConstraint, &QCheckBox::toggled, this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->btnRefreshEntity, &QPushButton::clicked, this, &MeshEdgeControlWidget::onRefreshEntities);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAppendSelectedEntity);
    connect(ui->btnHighlightEntity, &QPushButton::clicked, this, &MeshEdgeControlWidget::onHighlightSelectedEntity);
}

MeshEdgeControlWidget::~MeshEdgeControlWidget(){ beforeUnload(); delete ui; }
void MeshEdgeControlWidget::beforeLoad(){ rebuildEdgeCandidates(); refreshSummary(); }
void MeshEdgeControlWidget::beforeUnload(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }

void MeshEdgeControlWidget::onAddControl(){ QString err; if(!validateInput(err)){ QMessageBox::warning(this,tr("边控制"),err); return;} auto item=collectFormData(); m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); onResetForm(); }
void MeshEdgeControlWidget::onUpdateControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; QString err; if(!validateInput(err)){ QMessageBox::warning(this,tr("边控制"),err); return;} auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); refreshSummary(); }
void MeshEdgeControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; m_items.removeAt(row); ui->tableControls->removeRow(row); onResetForm(); }
void MeshEdgeControlWidget::onResetForm(){ ui->editEdgeIds->clear(); ui->spinSize->setValue(1.0); ui->spinDivisions->setValue(10); ui->spinBias->setValue(1.0); ui->checkHardConstraint->setChecked(false); ui->tableControls->clearSelection(); refreshButtons(); refreshSummary(); }
void MeshEdgeControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); refreshButtons(); }
void MeshEdgeControlWidget::onParameterEdited(){ refreshButtons(); refreshSummary(); }

void MeshEdgeControlWidget::onRefreshEntities(){ rebuildEdgeCandidates(); }
void MeshEdgeControlWidget::onAppendSelectedEntity(){ int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; QStringList p=ui->editEdgeIds->text().trimmed().split(',',Qt::SkipEmptyParts); const QString s=QString::number(id); if(!p.contains(s)){ p<<s; ui->editEdgeIds->setText(p.join(',')); }}
void MeshEdgeControlWidget::onHighlightSelectedEntity(){ int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntity(1,id); }

void MeshEdgeControlWidget::setupUiState(){ ui->tableControls->setColumnCount(5); ui->tableControls->setHorizontalHeaderLabels({tr("边 ID"),tr("目标尺寸"),tr("分段数"),tr("偏置比"),tr("硬约束")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildEdgeCandidates(); }
bool MeshEdgeControlWidget::validateInput(QString &err) const{ if(ui->editEdgeIds->text().trimmed().isEmpty()){ err=tr("请先输入或选择边 ID。"); return false;} return true; }
MeshEdgeControlWidget::EdgeControlItem MeshEdgeControlWidget::collectFormData() const{ EdgeControlItem i; i.edgeIds=ui->editEdgeIds->text().trimmed(); i.size=ui->spinSize->value(); i.divisions=ui->spinDivisions->value(); i.bias=ui->spinBias->value(); i.hardConstraint=ui->checkHardConstraint->isChecked(); return i; }
void MeshEdgeControlWidget::fillFormData(const EdgeControlItem &i){ ui->editEdgeIds->setText(i.edgeIds); ui->spinSize->setValue(i.size); ui->spinDivisions->setValue(i.divisions); ui->spinBias->setValue(i.bias); ui->checkHardConstraint->setChecked(i.hardConstraint);} 
void MeshEdgeControlWidget::appendOrUpdateTableRow(int row, const EdgeControlItem &i){ if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(i.edgeIds); ui->tableControls->item(row,1)->setText(QString::number(i.size,'f',3)); ui->tableControls->item(row,2)->setText(QString::number(i.divisions)); ui->tableControls->item(row,3)->setText(QString::number(i.bias,'f',2)); ui->tableControls->item(row,4)->setText(i.hardConstraint?tr("是"):tr("否")); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(i.edgeIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(QString::number(i.size,'f',3))); ui->tableControls->setItem(r,2,new QTableWidgetItem(QString::number(i.divisions))); ui->tableControls->setItem(r,3,new QTableWidgetItem(QString::number(i.bias,'f',2))); ui->tableControls->setItem(r,4,new QTableWidgetItem(i.hardConstraint?tr("是"):tr("否"))); }
void MeshEdgeControlWidget::refreshButtons(){ bool sel=ui->tableControls->currentRow()>=0; bool in=!ui->editEdgeIds->text().trimmed().isEmpty(); ui->btnAdd->setEnabled(in); ui->btnUpdate->setEnabled(sel&&in); ui->btnDelete->setEnabled(sel);} 
void MeshEdgeControlWidget::refreshSummary(){ ui->labelSummary->setText(tr("已配置 %1 条边控制规则，可选边实体 %2 个。先下拉选择，再高亮确认。").arg(m_items.size()).arg(m_entityIds.size())); }
void MeshEdgeControlWidget::rebuildEdgeCandidates(){ ui->cmbEntity->clear(); m_entityIds.clear(); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) m_entityIds = svc->listEntityIds(1); for(const auto& id:m_entityIds) ui->cmbEntity->addItem(tr("边 #%1").arg(id), id.toInt()); updateEntityUiState(); refreshSummary(); }
void MeshEdgeControlWidget::clearHighlight(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }
void MeshEdgeControlWidget::updateEntityUiState(){ bool has=!m_entityIds.isEmpty(); ui->cmbEntity->setEnabled(has); ui->btnAppendEntity->setEnabled(has); ui->btnHighlightEntity->setEnabled(has); }
