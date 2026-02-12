#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QSignalBlocker>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include "MeshControlRuleStore.h"

#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkCellArray.h>
#include <vtkVertexGlyphFilter.h>

static const QString kFaceHighlightPropId = "mesh.ctrl.face.highlight";

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(6);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "面组ID" << "组名" << "最小尺寸" << "最大尺寸" << "增长率" << "边界层");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshFaceControlWidget::onDimChanged);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshFaceControlWidget::onGroupChanged);
    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshFaceControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshFaceControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshFaceControlWidget::onResetDefaults);
    connect(ui->btnApplyControls, &QPushButton::clicked, this, &MeshFaceControlWidget::onApplyControls);

    m_highlightMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_highlightMapper->ScalarVisibilityOff();
    m_highlightActor = vtkSmartPointer<vtkActor>::New();
    m_highlightActor->SetMapper(m_highlightMapper);
    m_highlightActor->PickableOff();
    m_highlightActor->GetProperty()->SetColor(0.12, 0.58, 1.0);
    m_highlightActor->GetProperty()->SetPointSize(9.0);

    onResetDefaults();
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    clearGroupHighlight(false);
    delete ui;
}

void MeshFaceControlWidget::beforeLoad()
{
    {
        QSignalBlocker blocker(ui->comboBoxDim);
        ui->comboBoxDim->clear();
        ui->comboBoxDim->addItem("dim2 面", 2);
        ui->comboBoxDim->setCurrentIndex(0);
    }
    refreshGroupCombo();
    updateSummary();
    ui->labelApplyStatus->setText("未执行。请先添加规则，然后点击【执行面控制】。");
}

void MeshFaceControlWidget::beforeUnload()
{
    clearGroupHighlight(true);
}

void MeshFaceControlWidget::onDimChanged()
{
    refreshGroupCombo();
}

void MeshFaceControlWidget::onGroupChanged()
{
    updateGroupHighlight();
}

void MeshFaceControlWidget::refreshGroupCombo()
{
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        ui->labelPickStatus->setText("未找到网格服务，无法读取面组。");
        return;
    }

    m_cachedGrid = meshSvc->getVtkUnstructuredGridSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap &map = meshSvc->mshPickMapSolid();

    QSignalBlocker blocker(ui->comboBoxGroup);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it) {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dimFilter >= 0 && dim != dimFilter) continue;
        if (dim != 2) continue;
        ui->comboBoxGroup->addItem(QString("%1 (id=%2)").arg(it.value()).arg(physId), physId);
    }

    if (ui->comboBoxGroup->count() > 0) {
        ui->comboBoxGroup->setCurrentIndex(0);
        blocker.unblock();
        updateGroupHighlight();
    } else {
        ui->labelPickStatus->setText("暂无可用面组，请先完成整体剖分/导入带分组网格。");
        clearGroupHighlight(true);
    }
}

void MeshFaceControlWidget::updateGroupHighlight()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        clearGroupHighlight(true);
        return;
    }

    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!meshSvc || !visual) return;

    if (!m_cachedGrid) m_cachedGrid = meshSvc->getVtkUnstructuredGridSolid();
    if (!m_cachedGrid || m_cachedGrid->GetNumberOfPoints() <= 0) {
        ui->labelPickStatus->setText("无可高亮点。");
        clearGroupHighlight(true);
        return;
    }

    const MshPickMap &map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(idx).toInt();
    const QVector<int> pids = map.bndFacePhysToPointIds.value(physId);
    if (pids.isEmpty()) {
        ui->labelPickStatus->setText("当前面组没有关联点。");
        clearGroupHighlight(true);
        return;
    }

    auto pts = vtkSmartPointer<vtkPoints>::New();
    auto verts = vtkSmartPointer<vtkCellArray>::New();
    for (int pid : pids) {
        if (pid < 0 || pid >= m_cachedGrid->GetNumberOfPoints()) continue;
        double p[3];
        m_cachedGrid->GetPoint(pid, p);
        vtkIdType id = pts->InsertNextPoint(p);
        verts->InsertNextCell(1);
        verts->InsertCellPoint(id);
    }

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(pts);
    poly->SetVerts(verts);

    auto glyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    glyph->SetInputData(poly);
    glyph->Update();

    m_highlightMapper->SetInputData(glyph->GetOutput());
    visual->removeProp(kFaceHighlightPropId, false);
    visual->addProp(kFaceHighlightPropId, m_highlightActor, "网格", "面控制高亮", false, true);

    ui->labelPickStatus->setText(QString("当前高亮面组：%1（点数 %2）").arg(ui->comboBoxGroup->currentText()).arg(pids.size()));
}

void MeshFaceControlWidget::clearGroupHighlight(bool renderNow)
{
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual) visual->removeProp(kFaceHighlightPropId, renderNow);
}

void MeshFaceControlWidget::onAddOrUpdateRule()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, "面控制", "请先从下拉框选择面组。");
        return;
    }

    const int faceId = ui->comboBoxGroup->itemData(idx).toInt();
    const QString groupName = ui->comboBoxGroup->currentText();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double growth = ui->dsGrowthRate->value();
    const QString boundaryLayer = ui->chkBoundaryLayer->isChecked() ? "启用" : "禁用";

    if (minSize > maxSize) {
        QMessageBox::warning(this, "面控制", "最小尺寸不能大于最大尺寸。");
        return;
    }

    int row = findRowByFaceId(faceId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(faceId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(groupName));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(QString::number(growth, 'f', 3)));
    ui->tableRules->setItem(row, 5, new QTableWidgetItem(boundaryLayer));
    updateSummary();
}

void MeshFaceControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) return;
    ui->tableRules->removeRow(row);
    updateSummary();
}

void MeshFaceControlWidget::onClearRules()
{
    ui->tableRules->setRowCount(0);
    updateSummary();
}

void MeshFaceControlWidget::onResetDefaults()
{
    ui->dsMinSize->setValue(0.8);
    ui->dsMaxSize->setValue(3.0);
    ui->dsGrowthRate->setValue(1.2);
    ui->chkBoundaryLayer->setChecked(false);
}

void MeshFaceControlWidget::onApplyControls()
{
    const int ruleCount = ui->tableRules->rowCount();
    if (ruleCount <= 0) {
        QMessageBox::information(this, "面控制", "当前没有规则可执行，请先添加规则。");
        return;
    }

    QVector<MeshControlRule> rules;
    rules.reserve(ruleCount);
    for (int row = 0; row < ruleCount; ++row) {
        MeshControlRule rule;
        rule.physId = ui->tableRules->item(row, 0) ? ui->tableRules->item(row, 0)->text().toInt() : -1;
        rule.minSize = ui->tableRules->item(row, 2) ? ui->tableRules->item(row, 2)->text().toDouble() : 0.0;
        rule.maxSize = ui->tableRules->item(row, 3) ? ui->tableRules->item(row, 3)->text().toDouble() : 0.0;
        rules.push_back(rule);
    }
    MeshControlRuleStore::instance().setFaceRules(rules);

    ui->labelApplyStatus->setText(QString("已执行面控制规则：%1 条。后续整体剖分将读取这些规则并影响尺寸范围。").arg(ruleCount));
    QMessageBox::information(this, "面控制", QString("面控制已执行，共 %1 条规则。请再到整体剖分点击划分。\n若同时设置了体控制，请使用3D划分。").arg(ruleCount));
}

int MeshFaceControlWidget::findRowByFaceId(int faceId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == faceId) return row;
    }
    return -1;
}

void MeshFaceControlWidget::updateSummary()
{
    ui->labelSummary->setText(QString("当前面控制规则数：%1").arg(ui->tableRules->rowCount()));
}
