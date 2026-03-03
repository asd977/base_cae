#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/MeshModuleControllerService.h>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QSet>
#include <algorithm>
#include <CustomComboBox.h>

namespace {

static bool parseEntityIds(const QString &text, QSet<int> &ids, QString &err)
{
    ids.clear();
    const QStringList parts = text.split(',', QString::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty()) continue;
        if (part.contains('-')) {
            const QStringList bounds = part.split('-');
            if (bounds.size() != 2) { err = QObject::tr("ID 段 \"%1\" 格式错误，应为 a-b。") .arg(part); return false; }
            bool okStart = false, okEnd = false;
            const int start = bounds[0].trimmed().toInt(&okStart);
            const int end = bounds[1].trimmed().toInt(&okEnd);
            if (!okStart || !okEnd || start <= 0 || end <= 0 || start > end) {
                err = QObject::tr("ID 段 \"%1\" 非法，请使用正整数且起止满足 a<=b。") .arg(part);
                return false;
            }
            for (int id = start; id <= end; ++id) ids.insert(id);
        } else {
            bool ok = false;
            const int id = part.toInt(&ok);
            if (!ok || id <= 0) { err = QObject::tr("ID \"%1\" 非法，请输入正整数。") .arg(part); return false; }
            ids.insert(id);
        }
    }
    return true;
}

}

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
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshEdgeControlWidget::onSelectionChanged);
    connect(ui->editEdgeIds, &QLineEdit::textChanged, this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinDivisions, qOverload<int>(&QSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->spinBias, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->checkHardConstraint, &QCheckBox::toggled, this, &MeshEdgeControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo) {
        connect(entityCombo, &CustomComboBox::itemHovered, this, [this](const int &row){
            const int id = ui->cmbEntity->itemData(row).toInt();
            if(id<=0){ clearHighlight(); return; }
            if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntity(1,id);
        });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this, [this](){ clearHighlight(); });
    }
}

MeshEdgeControlWidget::~MeshEdgeControlWidget(){ beforeUnload(); delete ui; }
void MeshEdgeControlWidget::beforeLoad(){ rebuildEdgeCandidates(); loadRulesFromService(); }
void MeshEdgeControlWidget::beforeUnload(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }

void MeshEdgeControlWidget::onAddControl(){ QString err; if(!validateInput(err, -1)){ QMessageBox::warning(this,tr("边控制"),err); return;} auto item=collectFormData(); m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); syncRulesToService(); onResetForm(); }
void MeshEdgeControlWidget::onUpdateControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; QString err; if(!validateInput(err, row)){ QMessageBox::warning(this,tr("边控制"),err); return;} auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); syncRulesToService(); }
void MeshEdgeControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; m_items.removeAt(row); ui->tableControls->removeRow(row); syncRulesToService(); onResetForm(); }
void MeshEdgeControlWidget::onResetForm(){ ui->editEdgeIds->clear(); ui->spinSize->setValue(1.0); ui->spinDivisions->setValue(10); ui->spinBias->setValue(1.0); ui->checkHardConstraint->setChecked(false); ui->tableControls->clearSelection(); refreshButtons(); }
void MeshEdgeControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); refreshButtons(); }
void MeshEdgeControlWidget::onParameterEdited(){ refreshButtons(); }

void MeshEdgeControlWidget::onAppendSelectedEntity(){ int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; QStringList p=ui->editEdgeIds->text().trimmed().split(',',QString::SkipEmptyParts); const QString s=QString::number(id); if(!p.contains(s)){ p<<s; ui->editEdgeIds->setText(p.join(',')); }}

void MeshEdgeControlWidget::setupUiState(){ ui->tableControls->setColumnCount(5); ui->tableControls->setHorizontalHeaderLabels({tr("边 ID"),tr("目标尺寸"),tr("分段数"),tr("偏置比"),tr("硬约束")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildEdgeCandidates(); }
bool MeshEdgeControlWidget::validateInput(QString &err, int ignoreRow) const{ if(ui->editEdgeIds->text().trimmed().isEmpty()){ err=tr("请先输入或选择边 ID。"); return false;} QSet<int> currentIds; if(!parseEntityIds(ui->editEdgeIds->text().trimmed(), currentIds, err)) return false; for(int i=0;i<m_items.size();++i){ if(i==ignoreRow) continue; QSet<int> existingIds; QString parseErr; if(!parseEntityIds(m_items[i].edgeIds, existingIds, parseErr)) continue; QSet<int> overlap = currentIds & existingIds; if(!overlap.isEmpty()){ QList<int> ids = overlap.values(); std::sort(ids.begin(), ids.end()); QStringList overlapStr; for(int id:ids) overlapStr << QString::number(id); err = tr("边 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。").arg(i+1).arg(overlapStr.join(',')); return false; }} return true; }
MeshEdgeControlWidget::EdgeControlItem MeshEdgeControlWidget::collectFormData() const{ EdgeControlItem i; i.edgeIds=ui->editEdgeIds->text().trimmed(); i.size=ui->spinSize->value(); i.divisions=ui->spinDivisions->value(); i.bias=ui->spinBias->value(); i.hardConstraint=ui->checkHardConstraint->isChecked(); return i; }
void MeshEdgeControlWidget::fillFormData(const EdgeControlItem &i){ ui->editEdgeIds->setText(i.edgeIds); ui->spinSize->setValue(i.size); ui->spinDivisions->setValue(i.divisions); ui->spinBias->setValue(i.bias); ui->checkHardConstraint->setChecked(i.hardConstraint);} 
void MeshEdgeControlWidget::appendOrUpdateTableRow(int row, const EdgeControlItem &i){ if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(i.edgeIds); ui->tableControls->item(row,1)->setText(QString::number(i.size,'f',3)); ui->tableControls->item(row,2)->setText(QString::number(i.divisions)); ui->tableControls->item(row,3)->setText(QString::number(i.bias,'f',2)); ui->tableControls->item(row,4)->setText(i.hardConstraint?tr("是"):tr("否")); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(i.edgeIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(QString::number(i.size,'f',3))); ui->tableControls->setItem(r,2,new QTableWidgetItem(QString::number(i.divisions))); ui->tableControls->setItem(r,3,new QTableWidgetItem(QString::number(i.bias,'f',2))); ui->tableControls->setItem(r,4,new QTableWidgetItem(i.hardConstraint?tr("是"):tr("否"))); }
void MeshEdgeControlWidget::refreshButtons(){ bool sel=ui->tableControls->currentRow()>=0; bool in=!ui->editEdgeIds->text().trimmed().isEmpty(); ui->btnAdd->setEnabled(in); ui->btnUpdate->setEnabled(sel&&in); ui->btnDelete->setEnabled(sel);} 
void MeshEdgeControlWidget::rebuildEdgeCandidates(){ ui->cmbEntity->clear(); m_entityIds.clear(); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) m_entityIds = svc->listEntityIds(1); for(const auto& id:m_entityIds) ui->cmbEntity->addItem(tr("边 #%1").arg(id), id.toInt()); updateEntityUiState(); }
void MeshEdgeControlWidget::clearHighlight(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }
void MeshEdgeControlWidget::updateEntityUiState(){ bool has=!m_entityIds.isEmpty(); ui->cmbEntity->setEnabled(has); ui->btnAppendEntity->setEnabled(has); }

void MeshEdgeControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    QVector<EdgeControlRule> rules;
    rules.reserve(m_items.size());
    for (const auto& it : m_items) { EdgeControlRule r; r.edgeIds=it.edgeIds; r.size=it.size; r.divisions=it.divisions; r.bias=it.bias; r.hardConstraint=it.hardConstraint; rules.push_back(r);}
    svc->setEdgeControlRules(rules);
}

void MeshEdgeControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const auto rules = svc->edgeControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules) { EdgeControlItem i; i.edgeIds=r.edgeIds; i.size=r.size; i.divisions=r.divisions; i.bias=r.bias; i.hardConstraint=r.hardConstraint; m_items.push_back(i); appendOrUpdateTableRow(ui->tableControls->rowCount(), i); }
}
