#include "MeshVolumeControlWidget.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>

#include <QColor>
#include <QMessageBox>
#include <QSet>
#include <QTableWidgetItem>
#include <algorithm>

#include "ui_MeshVolumeControlWidget.h"

namespace
{

static bool parseEntityIds(const QString& text, QSet<int>& ids, QString& err)
{
    ids.clear();
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (QString part : parts)
    {
        part = part.trimmed();
        if (part.isEmpty())
            continue;
        if (part.contains('-'))
        {
            const QStringList bounds = part.split('-');
            if (bounds.size() != 2)
            {
                err = QObject::tr("ID 段 \"%1\" 格式错误，应为 a-b。").arg(part);
                return false;
            }
            bool okStart = false, okEnd = false;
            const int start = bounds[0].trimmed().toInt(&okStart);
            const int end = bounds[1].trimmed().toInt(&okEnd);
            if (!okStart || !okEnd || start <= 0 || end <= 0 || start > end)
            {
                err = QObject::tr("ID 段 \"%1\" 非法，请使用正整数且起止满足 a<=b。").arg(part);
                return false;
            }
            for (int id = start; id <= end; ++id)
                ids.insert(id);
        }
        else
        {
            bool ok = false;
            const int id = part.toInt(&ok);
            if (!ok || id <= 0)
            {
                err = QObject::tr("ID \"%1\" 非法，请输入正整数。").arg(part);
                return false;
            }
            ids.insert(id);
        }
    }
    return true;
}

} // namespace

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget* parent)
    : BasePage("MeshVolumeControlWidget"), ui(new Ui::MeshVolumeControlWidget),
      m_highlightPropId("mesh.volume.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshVolumeControlWidget::onAddControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshVolumeControlWidget::onDeleteControl);
    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this,
            &MeshVolumeControlWidget::onSelectionChanged);
    connect(ui->editVolumeIds, &QLineEdit::textChanged, this,
            &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->spinLocalSizeFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MeshVolumeControlWidget::onParameterEdited);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this,
            &MeshVolumeControlWidget::onAppendSelectedEntity);
    auto* entityCombo = qobject_cast<CustomComboBox*>(ui->cmbEntity);
    if (entityCombo)
    {
        connect(entityCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    const int id = ui->cmbEntity->itemData(row).toInt();
                    if (id <= 0)
                    {
                        clearHighlight();
                        return;
                    }
                    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
                        svc->highlightEntities(3, QList<int>{id}, true);
                });
        connect(entityCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
                        svc->clearEntityHoverHighlight();
                    onSelectionChanged();
                });
    }
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    beforeUnload();
    delete ui;
}
void MeshVolumeControlWidget::beforeLoad()
{
    rebuildVolumeCandidates();
    loadRulesFromService();
    if (m_items.isEmpty())
        onAddControl();
}
void MeshVolumeControlWidget::beforeUnload()
{
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
}

void MeshVolumeControlWidget::onAddControl()
{
    VolumeControlItem item;
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    const int row = m_items.size() - 1;
    ui->tableControls->selectRow(row);
    fillFormData(item);
    onSelectionChanged();
    refreshButtons();
}
void MeshVolumeControlWidget::onDeleteControl()
{
    int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    syncRulesToService();
    onResetForm();
}
void MeshVolumeControlWidget::onResetForm()
{
    ui->editVolumeIds->clear();
    ui->spinLocalSizeFactor->setValue(1.0);
    ui->tableControls->clearSelection();
    clearHighlight();
    refreshButtons();
}
void MeshVolumeControlWidget::onSelectionChanged()
{
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
        fillFormData(m_items[row]);
    QSet<int> selectedIds;
    QString err;
    if (row >= 0 && row < m_items.size())
        parseEntityIds(m_items[row].volumeIds, selectedIds, err);
    if (!selectedIds.isEmpty())
    {
        QList<int> v = selectedIds.values();
        std::sort(v.begin(), v.end());
        if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
            svc->highlightEntities(3, v, false);
    }
    else
        clearHighlight();
    refreshButtons();
}
void MeshVolumeControlWidget::onParameterEdited()
{
    int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        QString err;
        if (validateInput(err, row))
        {
            auto item = collectFormData();
            m_items[row] = item;
            appendOrUpdateTableRow(row, item);
            syncRulesToService();
        }
    }
    refreshButtons();
}

void MeshVolumeControlWidget::onAppendSelectedEntity()
{
    int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        QMessageBox::information(this, tr("体控制"), tr("请先在表格中选择要编辑的规则行。"));
        return;
    }
    int id = ui->cmbEntity->currentData().toInt();
    if (id <= 0)
        return;
    QStringList p = m_items[row].volumeIds.trimmed().split(',', Qt::SkipEmptyParts);
    QString s = QString::number(id);
    if (!p.contains(s))
    {
        const QString oldIds = m_items[row].volumeIds;
        p << s;
        m_items[row].volumeIds = p.join(',');
        ui->editVolumeIds->setText(m_items[row].volumeIds);
        QString err;
        if (!validateInput(err, row))
        {
            m_items[row].volumeIds = oldIds;
            ui->editVolumeIds->setText(oldIds);
            QMessageBox::warning(this, tr("体控制"), err);
            return;
        }
        appendOrUpdateTableRow(row, m_items[row]);
        syncRulesToService();
        onSelectionChanged();
    }
}

void MeshVolumeControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(2);
    ui->tableControls->setHorizontalHeaderLabels({tr("体 ID"), tr("尺寸因子")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    onResetForm();
    rebuildVolumeCandidates();
}
bool MeshVolumeControlWidget::validateInput(QString& err, int ignoreRow) const
{
    if (ui->editVolumeIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入或选择体 ID。");
        return false;
    }
    QSet<int> currentIds;
    if (!parseEntityIds(ui->editVolumeIds->text().trimmed(), currentIds, err))
        return false;
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (i == ignoreRow)
            continue;
        QSet<int> existingIds;
        QString parseErr;
        if (!parseEntityIds(m_items[i].volumeIds, existingIds, parseErr))
            continue;
        QSet<int> overlap = currentIds & existingIds;
        if (!overlap.isEmpty())
        {
            QList<int> ids = overlap.values();
            std::sort(ids.begin(), ids.end());
            QStringList overlapStr;
            for (int id : ids)
                overlapStr << QString::number(id);
            err = tr("体 ID 不能重复配置：与第 %1 条规则冲突（重复 ID：%2）。")
                      .arg(i + 1)
                      .arg(overlapStr.join(','));
            return false;
        }
    }
    return true;
}
MeshVolumeControlWidget::VolumeControlItem MeshVolumeControlWidget::collectFormData() const
{
    VolumeControlItem i;
    i.volumeIds = ui->editVolumeIds->text().trimmed();
    i.localSizeFactor = ui->spinLocalSizeFactor->value();
    return i;
}
void MeshVolumeControlWidget::fillFormData(const VolumeControlItem& i)
{
    ui->editVolumeIds->setText(i.volumeIds);
    ui->spinLocalSizeFactor->setValue(i.localSizeFactor);
}
void MeshVolumeControlWidget::appendOrUpdateTableRow(int row, const VolumeControlItem& i)
{
    const QString factor = QString::number(i.localSizeFactor, 'f', 2);
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(i.volumeIds);
        ui->tableControls->item(row, 1)->setText(factor);
        return;
    }
    int r = ui->tableControls->rowCount();
    ui->tableControls->insertRow(r);
    ui->tableControls->setItem(r, 0, new QTableWidgetItem(i.volumeIds));
    ui->tableControls->setItem(r, 1, new QTableWidgetItem(factor));
}
void MeshVolumeControlWidget::refreshButtons()
{
    bool sel = ui->tableControls->currentRow() >= 0;
    ui->btnAdd->setEnabled(true);
    ui->btnDelete->setEnabled(sel);
}
void MeshVolumeControlWidget::rebuildVolumeCandidates()
{
    ui->cmbEntity->clear();
    m_entityIds.clear();
    if (auto* svc = ServiceRegistry::instance().getService<PreProcessingService>())
        m_entityIds = svc->listEntityIds(3);
    for (const auto& id : m_entityIds)
        ui->cmbEntity->addItem(tr("体 #%1").arg(id), id.toInt());
    updateEntityUiState();
    onSelectionChanged();
}
void MeshVolumeControlWidget::clearHighlight()
{
    if (auto* s = ServiceRegistry::instance().getService<PreProcessingService>())
        s->clearEntityHighlight();
}
void MeshVolumeControlWidget::updateEntityUiState()
{
    bool has = !m_entityIds.isEmpty();
    ui->cmbEntity->setEnabled(has);
    ui->btnAppendEntity->setEnabled(has);
}

void MeshVolumeControlWidget::syncRulesToService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc)
        return;
    QVector<VolumeControlRule> rules;
    for (const auto& it : m_items)
    {
        if (it.volumeIds.trimmed().isEmpty())
            continue;
        VolumeControlRule r;
        r.volumeIds = it.volumeIds;
        r.localSizeFactor = it.localSizeFactor;
        rules.push_back(r);
    }
    svc->setVolumeControlRules(rules);
}

void MeshVolumeControlWidget::loadRulesFromService()
{
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!svc)
        return;
    const auto rules = svc->volumeControlRules();
    m_items.clear();
    ui->tableControls->setRowCount(0);
    for (const auto& r : rules)
    {
        VolumeControlItem i;
        i.volumeIds = r.volumeIds;
        i.localSizeFactor = r.localSizeFactor;
        m_items.push_back(i);
        appendOrUpdateTableRow(ui->tableControls->rowCount(), i);
    }
}
