#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/MeshModuleControllerService.h>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QColor>
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
            if (bounds.size() != 2) { err = QObject::tr("ID 段 \"%1\" 格式错误，应为 a-b。").arg(part); return false; }
            bool okStart = false, okEnd = false;
            const int start = bounds[0].trimmed().toInt(&okStart);
            const int end = bounds[1].trimmed().toInt(&okEnd);
            if (!okStart || !okEnd || start <= 0 || end <= 0 || start > end) {
                err = QObject::tr("ID 段 \"%1\" 非法，请使用正整数且起止满足 a<=b。").arg(part);
                return false;
            }
            for (int id = start; id <= end; ++id) ids.insert(id);
        } else {
            bool ok = false;
            const int id = part.toInt(&ok);
            if (!ok || id <= 0) { err = QObject::tr("ID \"%1\" 非法，请输入正整数。").arg(part); return false; }
            ids.insert(id);
        }
    }
    return true;
}

}

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
    , m_highlightPropId("mesh.face.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshFaceControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshFaceControlWidget::onSelectionChanged);
    connect(ui->editFaceIds, &QLineEdit::textChanged, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinLocalSizeFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->checkCurvature, &QCheckBox::toggled, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinCurvatureLevel, qOverload<int>(&QSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshFaceControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo) {
        connect(entityCombo, &CustomComboBox::itemHovered, this, [this](const int &row){
            const int id = ui->cmbEntity->itemData(row).toInt();
            if(id<=0){ clearHighlight(); return; }
            if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntities(2, QList<int>{id}, true);
        });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this, [this](){
            if (auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->clearEntityHoverHighlight();
            onSelectionChanged();
        });
    }
}

MeshFaceControlWidget::~MeshFaceControlWidget(){ beforeUnload(); delete ui; }
void MeshFaceControlWidget::beforeLoad(){ rebuildFaceCandidates(); loadRulesFromService(); if (m_items.isEmpty()) onAddControl(); }
void MeshFaceControlWidget::beforeUnload(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }

void MeshFaceControlWidget::onAddControl(){ FaceControlItem item; m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); const int row = m_items.size() - 1; ui->tableControls->selectRow(row); fillFormData(item); onSelectionChanged(); refreshButtons(); }
void MeshFaceControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size()) return; m_items.removeAt(row); ui->tableControls->removeRow(row); syncRulesToService(); onResetForm(); }
void MeshFaceControlWidget::onResetForm(){ ui->editFaceIds->clear(); ui->spinLocalSizeFactor->setValue(1.0); ui->checkCurvature->setChecked(true); ui->spinCurvatureLevel->setValue(20); ui->tableControls->clearSelection(); clearHighlight(); refreshButtons(); }
void MeshFaceControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); QSet<int> selectedIds; QString err; if(row>=0&&row<m_items.size()) parseEntityIds(m_items[row].faceIds, selectedIds, err); if(!selectedIds.isEmpty()){ QList<int> v=selectedIds.values(); std::sort(v.begin(), v.end()); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntities(2, v, false); } else clearHighlight(); refreshButtons(); }
void MeshFaceControlWidget::onParameterEdited(){ ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked()); int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()){ QString err; if(validateInput(err,row)){ auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); syncRulesToService(); } } refreshButtons(); }

void MeshFaceControlWidget::onAppendSelectedEntity(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size()){ QMessageBox::information(this,tr("面控制"),tr("请先在表格中选择要编辑的规则行。")); return; } int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; QStringList parts=m_items[row].faceIds.trimmed().split(',',QString::SkipEmptyParts); QString idStr=QString::number(id); if(!parts.contains(idStr)){ const QString oldIds = m_items[row].faceIds; parts.append(idStr); m_items[row].faceIds=parts.join(','); ui->editFaceIds->setText(m_items[row].faceIds); QString err; if(!validateInput(err, row)){ m_items[row].faceIds=oldIds; ui->editFaceIds->setText(oldIds); QMessageBox::warning(this,tr("面控制"),err); return; } appendOrUpdateTableRow(row,m_items[row]); syncRulesToService(); onSelectionChanged(); } }

void MeshFaceControlWidget::setupUiState(){ ui->tableControls->setColumnCount(3); ui->tableControls->setHorizontalHeaderLabels({tr("面 ID"), tr("尺寸因子"), tr("曲率控制")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildFaceCandidates(); }
bool MeshFaceControlWidget::validateInput(QString &err, int ignoreRow) const{ if (ui->editFaceIds->text().trimmed().isEmpty()) { err = tr("请先输入或选择面 ID。"); return false; } QSet<int> currentIds; if(!parseEntityIds(ui->editFaceIds->text().trimmed(), currentIds, err)) return false; for(int i=0;i<m_items.size();++i){ if(i==ignoreRow) continue; QSet<int> existingIds; QString parseErr; if(!parseEntityIds(m_items[i].faceIds, existingIds, parseErr)) continue; QSet<int> overlap = currentIds & existingIds; if(!overlap.isEmpty()){ QList<int> ids = overlap.values(); std::sort(ids.begin(), ids.end()); QStringList overlapStr; for(int id:ids) overlapStr << QString::number(id); err = tr("面 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。").arg(i+1).arg(overlapStr.join(',')); return false; }} return true; }
MeshFaceControlWidget::FaceControlItem MeshFaceControlWidget::collectFormData() const{ FaceControlItem item; item.faceIds=ui->editFaceIds->text().trimmed(); item.localSizeFactor=ui->spinLocalSizeFactor->value(); item.curvatureRefine=ui->checkCurvature->isChecked(); item.curvatureElementsPer2Pi=ui->spinCurvatureLevel->value(); return item; }
void MeshFaceControlWidget::fillFormData(const FaceControlItem &item){ ui->editFaceIds->setText(item.faceIds); ui->spinLocalSizeFactor->setValue(item.localSizeFactor); ui->checkCurvature->setChecked(item.curvatureRefine); ui->spinCurvatureLevel->setValue(item.curvatureElementsPer2Pi); }
void MeshFaceControlWidget::appendOrUpdateTableRow(int row, const FaceControlItem &item){ const QString factor=QString::number(item.localSizeFactor,'f',2); const QString curvature=item.curvatureRefine?tr("启用(2π:%1)").arg(item.curvatureElementsPer2Pi):tr("关闭"); if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(item.faceIds); ui->tableControls->item(row,1)->setText(factor); ui->tableControls->item(row,2)->setText(curvature); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(item.faceIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(factor)); ui->tableControls->setItem(r,2,new QTableWidgetItem(curvature)); }
void MeshFaceControlWidget::refreshButtons(){ bool hasSelection=ui->tableControls->currentRow()>=0; ui->btnAdd->setEnabled(true); ui->btnDelete->setEnabled(hasSelection); }

void MeshFaceControlWidget::rebuildFaceCandidates(){ ui->cmbEntity->clear(); m_entityIds.clear(); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) m_entityIds=svc->listEntityIds(2); for(const auto& id:m_entityIds) ui->cmbEntity->addItem(tr("面 #%1").arg(id), id.toInt()); updateEntityUiState(); onSelectionChanged(); }
void MeshFaceControlWidget::clearHighlight(){ if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->clearEntityHighlight(); }
void MeshFaceControlWidget::updateEntityUiState(){ const bool hasEntities=!m_entityIds.isEmpty(); ui->cmbEntity->setEnabled(hasEntities); ui->btnAppendEntity->setEnabled(hasEntities); }
void MeshFaceControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    QVector<FaceControlRule> rules;
    for (const auto& it : m_items) { if(it.faceIds.trimmed().isEmpty()) continue; FaceControlRule r; r.faceIds=it.faceIds; r.localSizeFactor=it.localSizeFactor; r.curvatureRefine=it.curvatureRefine; r.curvatureElementsPer2Pi=it.curvatureElementsPer2Pi; rules.push_back(r); }
    svc->setFaceControlRules(rules);
}

void MeshFaceControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const auto rules = svc->faceControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules) { FaceControlItem i; i.faceIds=r.faceIds; i.localSizeFactor=r.localSizeFactor; i.curvatureRefine=r.curvatureRefine; i.curvatureElementsPer2Pi=r.curvatureElementsPer2Pi; m_items.push_back(i); appendOrUpdateTableRow(ui->tableControls->rowCount(), i); }
}
