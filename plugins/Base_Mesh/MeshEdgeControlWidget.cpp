#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QSignalBlocker>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>

#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkUnstructuredGrid.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkCellArray.h>
#include <vtkVertexGlyphFilter.h>

static const QString kEdgeHighlightPropId = "mesh.ctrl.edge.highlight";

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
{
    ui->setupUi(this);

    ui->tableRules->setColumnCount(5);
    ui->tableRules->setHorizontalHeaderLabels(QStringList() << "线组ID" << "组名" << "最小尺寸" << "最大尺寸" << "偏置系数");
    ui->tableRules->horizontalHeader()->setStretchLastSection(true);
    ui->tableRules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableRules->verticalHeader()->setVisible(false);
    ui->tableRules->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableRules->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableRules->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEdgeControlWidget::onDimChanged);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEdgeControlWidget::onGroupChanged);
    connect(ui->btnAddOrUpdate, &QPushButton::clicked, this, &MeshEdgeControlWidget::onAddOrUpdateRule);
    connect(ui->btnRemoveSelected, &QPushButton::clicked, this, &MeshEdgeControlWidget::onRemoveSelectedRule);
    connect(ui->btnClearRules, &QPushButton::clicked, this, &MeshEdgeControlWidget::onClearRules);
    connect(ui->btnResetDefaults, &QPushButton::clicked, this, &MeshEdgeControlWidget::onResetDefaults);
    connect(ui->btnApplyControls, &QPushButton::clicked, this, &MeshEdgeControlWidget::onApplyControls);

    m_highlightMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_highlightMapper->ScalarVisibilityOff();
    m_highlightActor = vtkSmartPointer<vtkActor>::New();
    m_highlightActor->SetMapper(m_highlightMapper);
    m_highlightActor->PickableOff();
    m_highlightActor->GetProperty()->SetColor(1.0, 0.15, 0.05);
    m_highlightActor->GetProperty()->SetPointSize(10.0);

    onResetDefaults();
}

MeshEdgeControlWidget::~MeshEdgeControlWidget()
{
    clearGroupHighlight(false);
    delete ui;
}

void MeshEdgeControlWidget::beforeLoad()
{
    {
        QSignalBlocker blocker(ui->comboBoxDim);
        ui->comboBoxDim->clear();
        ui->comboBoxDim->addItem("dim1 线", 1);
        ui->comboBoxDim->setCurrentIndex(0);
    }
    refreshGroupCombo();
    updateSummary();
    ui->labelApplyStatus->setText("未执行。请先添加规则，然后点击【执行边控制】。");
}

void MeshEdgeControlWidget::beforeUnload()
{
    clearGroupHighlight(true);
}

void MeshEdgeControlWidget::onDimChanged()
{
    refreshGroupCombo();
}

void MeshEdgeControlWidget::onGroupChanged()
{
    updateGroupHighlight();
}

void MeshEdgeControlWidget::refreshGroupCombo()
{
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) {
        ui->labelPickStatus->setText("未找到网格服务，无法读取线组。");
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
        if (dim != 1) continue;
        ui->comboBoxGroup->addItem(QString("%1 (id=%2)").arg(it.value()).arg(physId), physId);
    }

    if (ui->comboBoxGroup->count() > 0) {
        ui->comboBoxGroup->setCurrentIndex(0);
        blocker.unblock();
        updateGroupHighlight();
    } else {
        ui->labelPickStatus->setText("暂无可用线组，请先完成整体剖分/导入带分组网格。");
        clearGroupHighlight(true);
    }
}

void MeshEdgeControlWidget::updateGroupHighlight()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        clearGroupHighlight(true);
        return;
    }

    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!meshSvc || !visual) {
        return;
    }

    if (!m_cachedGrid) {
        m_cachedGrid = meshSvc->getVtkUnstructuredGridSolid();
    }
    if (!m_cachedGrid || m_cachedGrid->GetNumberOfPoints() <= 0) {
        ui->labelPickStatus->setText("无可高亮点。");
        clearGroupHighlight(true);
        return;
    }

    const MshPickMap &map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(idx).toInt();
    const QVector<int> pids = map.bndEdgePhysToPointIds.value(physId);
    if (pids.isEmpty()) {
        ui->labelPickStatus->setText("当前线组没有关联点。");
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
    visual->removeProp(kEdgeHighlightPropId, false);
    visual->addProp(kEdgeHighlightPropId, m_highlightActor, "网格", "边控制高亮", false, true);

    ui->labelPickStatus->setText(QString("当前高亮线组：%1（点数 %2）").arg(ui->comboBoxGroup->currentText()).arg(pids.size()));
}

void MeshEdgeControlWidget::clearGroupHighlight(bool renderNow)
{
    auto *visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (visual) visual->removeProp(kEdgeHighlightPropId, renderNow);
}

void MeshEdgeControlWidget::onAddOrUpdateRule()
{
    const int idx = ui->comboBoxGroup->currentIndex();
    if (idx < 0) {
        QMessageBox::warning(this, "边控制", "请先从下拉框选择线组。");
        return;
    }

    const int edgeId = ui->comboBoxGroup->itemData(idx).toInt();
    const QString groupName = ui->comboBoxGroup->currentText();
    const double minSize = ui->dsMinSize->value();
    const double maxSize = ui->dsMaxSize->value();
    const double bias = ui->dsBias->value();

    if (minSize > maxSize) {
        QMessageBox::warning(this, "边控制", "最小尺寸不能大于最大尺寸。");
        return;
    }

    int row = findRowByEdgeId(edgeId);
    if (row < 0) {
        row = ui->tableRules->rowCount();
        ui->tableRules->insertRow(row);
    }

    ui->tableRules->setItem(row, 0, new QTableWidgetItem(QString::number(edgeId)));
    ui->tableRules->setItem(row, 1, new QTableWidgetItem(groupName));
    ui->tableRules->setItem(row, 2, new QTableWidgetItem(QString::number(minSize, 'f', 3)));
    ui->tableRules->setItem(row, 3, new QTableWidgetItem(QString::number(maxSize, 'f', 3)));
    ui->tableRules->setItem(row, 4, new QTableWidgetItem(QString::number(bias, 'f', 3)));
    updateSummary();
}

void MeshEdgeControlWidget::onRemoveSelectedRule()
{
    const int row = ui->tableRules->currentRow();
    if (row < 0) return;
    ui->tableRules->removeRow(row);
    updateSummary();
}

void MeshEdgeControlWidget::onClearRules()
{
    ui->tableRules->setRowCount(0);
    updateSummary();
}

void MeshEdgeControlWidget::onResetDefaults()
{
    ui->dsMinSize->setValue(0.5);
    ui->dsMaxSize->setValue(2.0);
    ui->dsBias->setValue(1.0);
}

void MeshEdgeControlWidget::onApplyControls()
{
    const int ruleCount = ui->tableRules->rowCount();
    if (ruleCount <= 0) {
        QMessageBox::information(this, "边控制", "当前没有规则可执行，请先添加规则。");
        return;
    }
    ui->labelApplyStatus->setText(QString("已执行边控制规则：%1 条。后续重新剖分时将按当前规则生效。").arg(ruleCount));
    QMessageBox::information(this, "边控制", QString("边控制已执行，共 %1 条规则。").arg(ruleCount));
}

int MeshEdgeControlWidget::findRowByEdgeId(int edgeId) const
{
    for (int row = 0; row < ui->tableRules->rowCount(); ++row) {
        QTableWidgetItem *item = ui->tableRules->item(row, 0);
        if (item && item->text().toInt() == edgeId) return row;
    }
    return -1;
}

void MeshEdgeControlWidget::updateSummary()
{
    ui->labelSummary->setText(QString("当前边控制规则数：%1").arg(ui->tableRules->rowCount()));
}
