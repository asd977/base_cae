#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

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
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshFaceControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshFaceControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshFaceControlWidget::onSelectionChanged);
    connect(ui->editFaceIds, &QLineEdit::textChanged, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->checkCurvature, &QCheckBox::toggled, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinCurvatureLevel, qOverload<int>(&QSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshFaceControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo) {
        connect(entityCombo, &CustomComboBox::itemHovered, this, [this](const int &row){
            const int id = ui->cmbEntity->itemData(row).toInt();
            if(id<=0){ clearHighlight(); return; }
            if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntity(2,id);
        });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this, [this](){ clearHighlight(); });
    }
}

MeshFaceControlWidget::~MeshFaceControlWidget(){ beforeUnload(); delete ui; }
void MeshFaceControlWidget::beforeLoad(){ rebuildFaceCandidates(); loadRulesFromService(); }
void MeshFaceControlWidget::beforeUnload(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }

void MeshFaceControlWidget::onAddControl(){ QString err; if(!validateInput(err, -1)){ QMessageBox::warning(this,tr("面控制"),err); return;} auto item=collectFormData(); m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); syncRulesToService(); onResetForm(); }
void MeshFaceControlWidget::onUpdateControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size()) return; QString err; if(!validateInput(err, row)){ QMessageBox::warning(this,tr("面控制"),err); return;} auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); syncRulesToService(); }
void MeshFaceControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size()) return; m_items.removeAt(row); ui->tableControls->removeRow(row); syncRulesToService(); onResetForm(); }
void MeshFaceControlWidget::onResetForm(){ ui->editFaceIds->clear(); ui->spinMinSize->setValue(0.2); ui->spinMaxSize->setValue(1.0); ui->checkCurvature->setChecked(true); ui->spinCurvatureLevel->setValue(2); ui->tableControls->clearSelection(); refreshButtons(); }
void MeshFaceControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); refreshButtons(); }
void MeshFaceControlWidget::onParameterEdited(){ ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked()); refreshButtons(); }

void MeshFaceControlWidget::onAppendSelectedEntity(){ int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; appendEntityIdToEdit(id); }

void MeshFaceControlWidget::setupUiState(){ ui->tableControls->setColumnCount(5); ui->tableControls->setHorizontalHeaderLabels({tr("面 ID"), tr("最小尺寸"), tr("最大尺寸"), tr("曲率加密"), tr("加密等级")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildFaceCandidates(); }
bool MeshFaceControlWidget::validateInput(QString &err, int ignoreRow) const{ if (ui->editFaceIds->text().trimmed().isEmpty()) { err = tr("请先输入或选择面 ID。"); return false; } if (ui->spinMinSize->value() > ui->spinMaxSize->value()) { err = tr("最小尺寸不能大于最大尺寸。"); return false; } QSet<int> currentIds; if(!parseEntityIds(ui->editFaceIds->text().trimmed(), currentIds, err)) return false; for(int i=0;i<m_items.size();++i){ if(i==ignoreRow) continue; QSet<int> existingIds; QString parseErr; if(!parseEntityIds(m_items[i].faceIds, existingIds, parseErr)) continue; QSet<int> overlap = currentIds & existingIds; if(!overlap.isEmpty()){ QList<int> ids = overlap.values(); std::sort(ids.begin(), ids.end()); QStringList overlapStr; for(int id:ids) overlapStr << QString::number(id); err = tr("面 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。").arg(i+1).arg(overlapStr.join(',')); return false; }} return true; }
MeshFaceControlWidget::FaceControlItem MeshFaceControlWidget::collectFormData() const{ FaceControlItem item; item.faceIds=ui->editFaceIds->text().trimmed(); item.minSize=ui->spinMinSize->value(); item.maxSize=ui->spinMaxSize->value(); item.curvatureRefine=ui->checkCurvature->isChecked(); item.curvatureLevel=ui->spinCurvatureLevel->value(); return item; }
void MeshFaceControlWidget::fillFormData(const FaceControlItem &item){ ui->editFaceIds->setText(item.faceIds); ui->spinMinSize->setValue(item.minSize); ui->spinMaxSize->setValue(item.maxSize); ui->checkCurvature->setChecked(item.curvatureRefine); ui->spinCurvatureLevel->setValue(item.curvatureLevel); }
void MeshFaceControlWidget::appendOrUpdateTableRow(int row, const FaceControlItem &item){ if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(item.faceIds); ui->tableControls->item(row,1)->setText(QString::number(item.minSize,'f',3)); ui->tableControls->item(row,2)->setText(QString::number(item.maxSize,'f',3)); ui->tableControls->item(row,3)->setText(item.curvatureRefine?tr("启用"):tr("关闭")); ui->tableControls->item(row,4)->setText(item.curvatureRefine?QString::number(item.curvatureLevel):"-"); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(item.faceIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(QString::number(item.minSize,'f',3))); ui->tableControls->setItem(r,2,new QTableWidgetItem(QString::number(item.maxSize,'f',3))); ui->tableControls->setItem(r,3,new QTableWidgetItem(item.curvatureRefine?tr("启用"):tr("关闭"))); ui->tableControls->setItem(r,4,new QTableWidgetItem(item.curvatureRefine?QString::number(item.curvatureLevel):"-")); }
void MeshFaceControlWidget::refreshButtons(){ bool hasSelection=ui->tableControls->currentRow()>=0; bool hasInput=!ui->editFaceIds->text().trimmed().isEmpty(); ui->btnAdd->setEnabled(hasInput); ui->btnUpdate->setEnabled(hasSelection&&hasInput); ui->btnDelete->setEnabled(hasSelection); }

void MeshFaceControlWidget::rebuildFaceCandidates(){ ui->cmbEntity->clear(); m_entityIds.clear(); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) m_entityIds=svc->listEntityIds(2); for(const auto& id:m_entityIds) ui->cmbEntity->addItem(tr("面 #%1").arg(id), id.toInt()); updateEntityUiState(); }
void MeshFaceControlWidget::clearHighlight(){ if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->clearEntityHighlight(); }
void MeshFaceControlWidget::updateEntityUiState(){ const bool hasEntities=!m_entityIds.isEmpty(); ui->cmbEntity->setEnabled(hasEntities); ui->btnAppendEntity->setEnabled(hasEntities); }
bool MeshFaceControlWidget::appendEntityIdToEdit(int id){ QStringList parts=ui->editFaceIds->text().trimmed().split(',',QString::SkipEmptyParts); QString idStr=QString::number(id); if(!parts.contains(idStr)){ parts.append(idStr); ui->editFaceIds->setText(parts.join(',')); } return true; }

void MeshFaceControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    QVector<FaceControlRule> rules;
    for (const auto& it : m_items) { FaceControlRule r; r.faceIds=it.faceIds; r.minSize=it.minSize; r.maxSize=it.maxSize; r.curvatureRefine=it.curvatureRefine; r.curvatureLevel=it.curvatureLevel; rules.push_back(r); }
    svc->setFaceControlRules(rules);
}

void MeshFaceControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const auto rules = svc->faceControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules) { FaceControlItem i; i.faceIds=r.faceIds; i.minSize=r.minSize; i.maxSize=r.maxSize; i.curvatureRefine=r.curvatureRefine; i.curvatureLevel=r.curvatureLevel; m_items.push_back(i); appendOrUpdateTableRow(ui->tableControls->rowCount(), i); }
}
