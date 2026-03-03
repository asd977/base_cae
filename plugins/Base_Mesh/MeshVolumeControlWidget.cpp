#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

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

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
    , m_highlightPropId("mesh.volume.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshVolumeControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshVolumeControlWidget::onSelectionChanged);
    connect(ui->editVolumeIds, &QLineEdit::textChanged, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinGrowthRate, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->checkBoundaryLayer, &QCheckBox::toggled, this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinLayerCount, qOverload<int>(&QSpinBox::valueChanged), this, &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo) {
        connect(entityCombo, &CustomComboBox::itemHovered, this, [this](const int &row){
            const int id = ui->cmbEntity->itemData(row).toInt();
            if(id<=0){ clearHighlight(); return; }
            if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntities(3, QList<int>{id}, true);
        });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this, [this](){
            if (auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->clearEntityHoverHighlight();
            onSelectionChanged();
        });
    }
}

MeshVolumeControlWidget::~MeshVolumeControlWidget(){ beforeUnload(); delete ui; }
void MeshVolumeControlWidget::beforeLoad(){ rebuildVolumeCandidates(); loadRulesFromService(); }
void MeshVolumeControlWidget::beforeUnload(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }

void MeshVolumeControlWidget::onAddControl(){ VolumeControlItem item; m_items.push_back(item); appendOrUpdateTableRow(ui->tableControls->rowCount(),item); const int row = m_items.size() - 1; ui->tableControls->selectRow(row); fillFormData(item); onSelectionChanged(); refreshButtons(); }
void MeshVolumeControlWidget::onDeleteControl(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size())return; m_items.removeAt(row); ui->tableControls->removeRow(row); syncRulesToService(); onResetForm(); }
void MeshVolumeControlWidget::onResetForm(){ ui->editVolumeIds->clear(); ui->spinMinSize->setValue(0.5); ui->spinMaxSize->setValue(2.0); ui->spinGrowthRate->setValue(1.2); ui->checkBoundaryLayer->setChecked(false); ui->spinLayerCount->setValue(3); ui->tableControls->clearSelection(); clearHighlight(); refreshButtons(); }
void MeshVolumeControlWidget::onSelectionChanged(){ int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()) fillFormData(m_items[row]); QSet<int> selectedIds; QString err; if(row>=0&&row<m_items.size()) parseEntityIds(m_items[row].volumeIds, selectedIds, err); if(!selectedIds.isEmpty()){ QList<int> v=selectedIds.values(); std::sort(v.begin(), v.end()); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) svc->highlightEntities(3, v, false); } else clearHighlight(); refreshButtons(); }
void MeshVolumeControlWidget::onParameterEdited(){ ui->spinLayerCount->setEnabled(ui->checkBoundaryLayer->isChecked()); int row=ui->tableControls->currentRow(); if(row>=0&&row<m_items.size()){ QString err; if(validateInput(err,row)){ auto item=collectFormData(); m_items[row]=item; appendOrUpdateTableRow(row,item); syncRulesToService(); } } refreshButtons(); }

void MeshVolumeControlWidget::onAppendSelectedEntity(){ int row=ui->tableControls->currentRow(); if(row<0||row>=m_items.size()){ QMessageBox::information(this,tr("体控制"),tr("请先在表格中选择要编辑的规则行。")); return; } int id=ui->cmbEntity->currentData().toInt(); if(id<=0)return; QStringList p=m_items[row].volumeIds.trimmed().split(',',QString::SkipEmptyParts); QString s=QString::number(id); if(!p.contains(s)){ const QString oldIds = m_items[row].volumeIds; p<<s; m_items[row].volumeIds=p.join(','); ui->editVolumeIds->setText(m_items[row].volumeIds); QString err; if(!validateInput(err, row)){ m_items[row].volumeIds=oldIds; ui->editVolumeIds->setText(oldIds); QMessageBox::warning(this,tr("体控制"),err); return; } appendOrUpdateTableRow(row,m_items[row]); syncRulesToService(); onSelectionChanged(); }}

void MeshVolumeControlWidget::setupUiState(){ ui->tableControls->setColumnCount(4); ui->tableControls->setHorizontalHeaderLabels({tr("体 ID"),tr("尺寸范围"),tr("增长率"),tr("边界层")}); ui->tableControls->horizontalHeader()->setStretchLastSection(true); ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows); ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection); ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers); onResetForm(); rebuildVolumeCandidates(); }
bool MeshVolumeControlWidget::validateInput(QString &err, int ignoreRow) const{ if(ui->editVolumeIds->text().trimmed().isEmpty()){ err=tr("请先输入或选择体 ID。"); return false;} if(ui->spinMinSize->value()>ui->spinMaxSize->value()){ err=tr("最小尺寸不能大于最大尺寸。"); return false;} QSet<int> currentIds; if(!parseEntityIds(ui->editVolumeIds->text().trimmed(), currentIds, err)) return false; for(int i=0;i<m_items.size();++i){ if(i==ignoreRow) continue; QSet<int> existingIds; QString parseErr; if(!parseEntityIds(m_items[i].volumeIds, existingIds, parseErr)) continue; QSet<int> overlap = currentIds & existingIds; if(!overlap.isEmpty()){ QList<int> ids = overlap.values(); std::sort(ids.begin(), ids.end()); QStringList overlapStr; for(int id:ids) overlapStr << QString::number(id); err = tr("体 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。").arg(i+1).arg(overlapStr.join(',')); return false; }} return true; }
MeshVolumeControlWidget::VolumeControlItem MeshVolumeControlWidget::collectFormData() const{ VolumeControlItem i; i.volumeIds=ui->editVolumeIds->text().trimmed(); i.minSize=ui->spinMinSize->value(); i.maxSize=ui->spinMaxSize->value(); i.growthRate=ui->spinGrowthRate->value(); i.boundaryLayer=ui->checkBoundaryLayer->isChecked(); i.layerCount=ui->spinLayerCount->value(); return i; }
void MeshVolumeControlWidget::fillFormData(const VolumeControlItem &i){ ui->editVolumeIds->setText(i.volumeIds); ui->spinMinSize->setValue(i.minSize); ui->spinMaxSize->setValue(i.maxSize); ui->spinGrowthRate->setValue(i.growthRate); ui->checkBoundaryLayer->setChecked(i.boundaryLayer); ui->spinLayerCount->setValue(i.layerCount); }
void MeshVolumeControlWidget::appendOrUpdateTableRow(int row, const VolumeControlItem &i){ const QString sizeRange=tr("%1 ~ %2").arg(i.minSize,0,'f',3).arg(i.maxSize,0,'f',3); const QString layer=i.boundaryLayer?tr("启用(%1层)").arg(i.layerCount):tr("关闭"); const QString displayIds = i.volumeIds.trimmed().isEmpty() ? tr("（空规则，待添加ID）") : i.volumeIds; if(row<ui->tableControls->rowCount()){ ui->tableControls->item(row,0)->setText(displayIds); ui->tableControls->item(row,1)->setText(sizeRange); ui->tableControls->item(row,2)->setText(QString::number(i.growthRate,'f',2)); ui->tableControls->item(row,3)->setText(layer); return;} int r=ui->tableControls->rowCount(); ui->tableControls->insertRow(r); ui->tableControls->setItem(r,0,new QTableWidgetItem(displayIds)); ui->tableControls->setItem(r,1,new QTableWidgetItem(sizeRange)); ui->tableControls->setItem(r,2,new QTableWidgetItem(QString::number(i.growthRate,'f',2))); ui->tableControls->setItem(r,3,new QTableWidgetItem(layer)); }
void MeshVolumeControlWidget::refreshButtons(){ bool sel=ui->tableControls->currentRow()>=0; ui->btnAdd->setEnabled(true); ui->btnDelete->setEnabled(sel);}
void MeshVolumeControlWidget::rebuildVolumeCandidates(){ ui->cmbEntity->clear(); m_entityIds.clear(); if(auto* svc=ServiceRegistry::instance().getService<PreProcessingService>()) m_entityIds=svc->listEntityIds(3); for(const auto& id:m_entityIds) ui->cmbEntity->addItem(tr("体 #%1").arg(id), id.toInt()); updateEntityUiState(); onSelectionChanged(); }
void MeshVolumeControlWidget::clearHighlight(){ if(auto* s=ServiceRegistry::instance().getService<PreProcessingService>()) s->clearEntityHighlight(); }
void MeshVolumeControlWidget::updateEntityUiState(){ bool has=!m_entityIds.isEmpty(); ui->cmbEntity->setEnabled(has); ui->btnAppendEntity->setEnabled(has); }

void MeshVolumeControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    QVector<VolumeControlRule> rules;
    for (const auto& it : m_items) { if(it.volumeIds.trimmed().isEmpty()) continue; VolumeControlRule r; r.volumeIds=it.volumeIds; r.minSize=it.minSize; r.maxSize=it.maxSize; r.growthRate=it.growthRate; r.boundaryLayer=it.boundaryLayer; r.layerCount=it.layerCount; rules.push_back(r); }
    svc->setVolumeControlRules(rules);
}

void MeshVolumeControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc) return;
    const auto rules = svc->volumeControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules) { VolumeControlItem i; i.volumeIds=r.volumeIds; i.minSize=r.minSize; i.maxSize=r.maxSize; i.growthRate=r.growthRate; i.boundaryLayer=r.boundaryLayer; i.layerCount=r.layerCount; m_items.push_back(i); appendOrUpdateTableRow(ui->tableControls->rowCount(), i); }
}
