#include <CompactDoubleSpinBox.h>
#include "TerrainSurfacePanel.h"
#include "../builders/GeometryBuildUtils.h"
#include <PublicConstants.h>
#include <GeometryDisplayManager.h>
#include <GeometryCleanup.h>
#include <GeomShapeObject.h>
#include "../workbench/GeometryWorkbench.h"
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontMetrics>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QCheckBox>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <cmath>
#include <algorithm>

using namespace GeometryTerrain;

namespace
{
QStringList sourceRoles()
{
    return {QObject::tr("等高线"), QObject::tr("范围边界"), QObject::tr("控制线"), QObject::tr("高程点")};
}

// Create a combo box only while editing, rather than one QWidget per DXF entity.
class SourceRoleDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* combo = new QComboBox(parent);
        combo->addItems(sourceRoles());
        return combo;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<QComboBox*>(editor)->setCurrentText(index.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        model->setData(index, static_cast<QComboBox*>(editor)->currentText(), Qt::EditRole);
    }
};
}

TerrainSurfacePanel::TerrainSurfacePanel(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomTerrainSurface)
{
    Q_UNUSED(parent);
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("等高线生成地形面"), this);
    title->setStyleSheet("font-weight:600; font-size: 13px;");
    layout->addWidget(title);

    auto* import = new QPushButton(tr("导入等高线 DXF"), this);
    import->setObjectName("importContoursButton");
    layout->addWidget(import);

    m_table = new QTableWidget(this);
    m_table->setObjectName("terrainSourceTable");
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("参与"), tr("图层 / 对象"), tr("用途"), tr("高程"), tr("检查")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const QFontMetrics tableMetrics(m_table->font());
    m_table->setColumnWidth(1, tableMetrics.horizontalAdvance(tr("图层 / 对象 000000"))+tableMetrics.height());
    m_table->horizontalHeader()->setMinimumSectionSize(tableMetrics.horizontalAdvance(tr("高程"))+tableMetrics.height());
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(140);
    m_table->setMaximumHeight(240);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(3, tableMetrics.horizontalAdvance("-000000.00")+tableMetrics.height());
    m_table->setColumnWidth(4, tableMetrics.horizontalAdvance(tr("100 点，闭合"))+tableMetrics.height());
    m_table->setItemDelegateForColumn(2, new SourceRoleDelegate(m_table));
    m_table->setToolTip(tr("勾选参与插值的对象；双击用途或高程可修改。等高线应保留真实高程。"));
    layout->addWidget(m_table);

    // 参数设置组
    auto* paramGroup = new QGroupBox(tr("方网插值参数"), this);
    auto* paramLayout = new QVBoxLayout(paramGroup);

    auto* form = new QFormLayout;

    m_domainMode = new QComboBox(this);
    m_domainMode->setObjectName("terrainDomainModeCombo");
    m_domainMode->addItem(tr("方网插值"), static_cast<int>(DomainMode::GridInterpolation));
    m_domainMode->addItem(tr("按等高线分域"), static_cast<int>(DomainMode::ContourRegions));
    m_domainMode->addItem(tr("整体连续地形面"), static_cast<int>(DomainMode::WholeSurface));
    m_domainMode->setToolTip(tr("一般地形使用方网插值；另外两种模式适用于互相嵌套的闭合等高线。"));
    form->addRow(tr("生成模式"), m_domainMode);

    m_algorithm = new QComboBox(this);
    m_algorithm->setObjectName("terrainAlgorithmCombo");
    m_algorithm->addItem(tr("反距离权重 (IDW)"), 0);
    m_algorithm->addItem(tr("线性内插 (Linear Delaunay)"), 2);
    m_algorithm->addItem(tr("最近邻 (Nearest Neighbour)"), 1);
    form->addRow(tr("插值算法"), m_algorithm);

    m_spacing = new CompactDoubleSpinBox(this);
    m_spacing->setObjectName("terrainSpacingSpin");
    m_spacing->setDecimals(2);
    m_spacing->setRange(0.01, 100000.0);
    m_spacing->setSingleStep(1.0);
    m_spacing->setValue(5.0);
    m_spacingLabel = new QLabel(tr("方网间距 (米)"), this);
    form->addRow(m_spacingLabel, m_spacing);

    paramLayout->addLayout(form);

    // 平面范围设置
    auto* boundsLayout = new QFormLayout;
    m_xmin = new CompactDoubleSpinBox(this);
    m_xmin->setDecimals(2);
    m_xmin->setRange(-1e9, 1e9);
    m_xmin->setSingleStep(10.0);

    m_xmax = new CompactDoubleSpinBox(this);
    m_xmax->setDecimals(2);
    m_xmax->setRange(-1e9, 1e9);
    m_xmax->setSingleStep(10.0);

    m_ymin = new CompactDoubleSpinBox(this);
    m_ymin->setDecimals(2);
    m_ymin->setRange(-1e9, 1e9);
    m_ymin->setSingleStep(10.0);

    m_ymax = new CompactDoubleSpinBox(this);
    m_ymax->setDecimals(2);
    m_ymax->setRange(-1e9, 1e9);
    m_ymax->setSingleStep(10.0);

    boundsLayout->addRow(tr("Xmin"), m_xmin);
    boundsLayout->addRow(tr("Xmax"), m_xmax);
    boundsLayout->addRow(tr("Ymin"), m_ymin);
    boundsLayout->addRow(tr("Ymax"), m_ymax);

    paramLayout->addLayout(boundsLayout);

    m_autoBoundsBtn = new QPushButton(tr("自适应 DXF 范围"), this);
    m_autoBoundsBtn->setObjectName("terrainAutoBoundsBtn");
    paramLayout->addWidget(m_autoBoundsBtn);

    m_gridStatsLabel = new QLabel(tr("方网规格: 未计算"), this);
    m_gridStatsLabel->setObjectName("terrainGridStatsLabel");
    paramLayout->addWidget(m_gridStatsLabel);

    m_createSolid = new QCheckBox(tr("生成闭合地质体"), this);
    m_createSolid->setObjectName("terrainCreateSolidCheck");
    m_createSolid->setChecked(true);
    m_createSolid->setToolTip(tr("勾选后将地形面与基底水平面及4个侧面缝合成闭合三维实体，可直接参与有限元体网格剖分与开挖布尔。"));
    paramLayout->addWidget(m_createSolid);

    auto* baseHLayout = new QFormLayout;
    m_baseElevationLabel = new QLabel(tr("基底高程 (米):"), this);
    m_baseElevation = new CompactDoubleSpinBox(this);
    m_baseElevation->setObjectName("terrainBaseElevationSpin");
    m_baseElevation->setDecimals(2);
    m_baseElevation->setRange(-1e9, 1e9);
    m_baseElevation->setSingleStep(10.0);
    m_baseElevation->setValue(0.0);
    m_baseElevation->setToolTip(tr("闭合地质体底部水平面标高。设为 0 时系统自动依据地形最低点向下延伸计算。"));
    baseHLayout->addRow(m_baseElevationLabel, m_baseElevation);
    paramLayout->addLayout(baseHLayout);

    connect(m_createSolid, &QCheckBox::toggled, this, [this](bool on) {
        m_baseElevation->setEnabled(on);
        m_baseElevationLabel->setEnabled(on);
    });

    layout->addWidget(paramGroup);

    auto* generateButton = new QPushButton(tr("构建三维地质体 / 地形面"), this);
    generateButton->setObjectName("buildTerrainButton");
    layout->addWidget(generateButton);

    auto* displayRow = new QHBoxLayout;
    m_showContours = new QCheckBox(tr("显示等高线"), this);
    m_showContours->setObjectName("showTerrainContoursCheck");
    m_showContours->setChecked(true);
    displayRow->addWidget(m_showContours);

    m_showSurface = new QCheckBox(tr("显示高程伪彩地表"), this);
    m_showSurface->setObjectName("showTerrainSurfaceCheck");
    m_showSurface->setChecked(true);
    displayRow->addWidget(m_showSurface);
    layout->addLayout(displayRow);

    m_status = new QLabel(tr("未导入等高线"), this);
    m_status->setObjectName("terrainStatusLabel");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();

    connect(import, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("导入等高线"), {}, tr("DXF 文件 (*.dxf)"));
        if (path.isEmpty()) return;
        QString diagnostic;
        if (!importFile(path, diagnostic))
            QMessageBox::warning(this, tr("导入失败"), diagnostic);
    });

    connect(m_autoBoundsBtn, &QPushButton::clicked, this, [this] {
        updateBoundsFromSources();
    });

    connect(generateButton, &QPushButton::clicked, this, [this] {
        QString diagnostic;
        if (!generate(diagnostic))
            QMessageBox::warning(this, tr("无法构建地形面"), diagnostic);
    });

    connect(m_showContours, &QCheckBox::toggled, this, [this] { updatePreview(); });
    connect(m_showSurface, &QCheckBox::toggled, this, [this] { updatePreview(); });

    auto onParamChanged = [this] {
        updateGridStats();
        invalidate();
    };

    connect(m_spacing, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onParamChanged);
    connect(m_xmin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onParamChanged);
    connect(m_xmax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onParamChanged);
    connect(m_ymin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onParamChanged);
    connect(m_ymax, qOverload<double>(&QDoubleSpinBox::valueChanged), this, onParamChanged);
    connect(m_algorithm, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { invalidate(); });
    connect(m_domainMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateModeControls();
        invalidate();
    });

    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (m_refreshing || row < 0 || row >= m_sources.size()) return;
        if (column == 0)
        {
            m_sources[row].enabled = (m_table->item(row, 0)->checkState() == Qt::Checked);
        }
        else if (column == 2)
        {
            const int role = sourceRoles().indexOf(m_table->item(row, 2)->text());
            if (role < 0) return;
            m_sources[row].role = Role(role);
        }
        else if (column == 3)
        {
            bool ok = false;
            double z = m_table->item(row, 3)->text().toDouble(&ok);
            if (!ok || !std::isfinite(z))
            {
                m_status->setText(tr("高程必须是有效数字。"));
                refreshRow(row);
                return;
            }
            for (auto& p : m_sources[row].points)
                p.SetZ(z);
        }
        else return;

        invalidate();
        refreshRow(row);
        m_sourceActor = nullptr;
        updatePreview();
    });

    updateGridStats();
    updateModeControls();
}

void TerrainSurfacePanel::updateModeControls()
{
    const bool grid = m_domainMode->currentData().toInt() == int(DomainMode::GridInterpolation);
    for (QWidget* widget : {static_cast<QWidget*>(m_algorithm), static_cast<QWidget*>(m_xmin),
                           static_cast<QWidget*>(m_xmax), static_cast<QWidget*>(m_ymin),
                           static_cast<QWidget*>(m_ymax), static_cast<QWidget*>(m_autoBoundsBtn)})
        widget->setEnabled(grid);
    m_gridStatsLabel->setVisible(grid);
    m_spacingLabel->setText(grid ? tr("方网间距 (米)") : tr("预览采样间距 (米)"));
}

void TerrainSurfacePanel::updateBoundsFromSources()
{
    GridBounds bounds;
    if (TerrainSurfaceBuilder::computeSourcesBounds(m_sources, bounds))
    {
        const QSignalBlocker b1(m_xmin);
        const QSignalBlocker b2(m_xmax);
        const QSignalBlocker b3(m_ymin);
        const QSignalBlocker b4(m_ymax);
        m_xmin->setValue(bounds.xmin);
        m_xmax->setValue(bounds.xmax);
        m_ymin->setValue(bounds.ymin);
        m_ymax->setValue(bounds.ymax);
        updateGridStats();
        invalidate();
    }
}

void TerrainSurfacePanel::updateGridStats()
{
    if (!m_gridStatsLabel || !m_spacing || !m_xmin || !m_xmax || !m_ymin || !m_ymax)
        return;

    const double spacing = m_spacing->value();
    const double dx = m_xmax->value() - m_xmin->value();
    const double dy = m_ymax->value() - m_ymin->value();

    if (spacing <= 0.001 || dx <= 0 || dy <= 0)
    {
        m_gridStatsLabel->setText(tr("方网规格: 范围或间距无效"));
        m_gridStatsLabel->setStyleSheet("color: #d9534f;");
        return;
    }

    // Check the scale before converting to an integer, including very small spacing.
    const double nx = std::max(2.0, std::floor(dx / spacing) + 1);
    const double ny = std::max(2.0, std::floor(dy / spacing) + 1);
    const double total = nx * ny;
    const QString dimensions = tr("%1 × %2 = %3 点")
        .arg(nx, 0, 'f', 0).arg(ny, 0, 'f', 0).arg(total, 0, 'f', 0);

    if (total > 2500000)
    {
        m_gridStatsLabel->setText(tr("方网规格: %1 (超出 250 万限制，请增大间距)").arg(dimensions));
        m_gridStatsLabel->setStyleSheet("color: #d9534f; font-weight: bold;");
    }
    else
    {
        m_gridStatsLabel->setText(tr("方网规格: %1").arg(dimensions));
        m_gridStatsLabel->setStyleSheet("color: #2e7d32;");
    }
}

void TerrainSurfacePanel::invalidate()
{
    if (m_generated)
    {
        GeometryCleanup::removeExistingGeometry(getUuid(), false);
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
        {
            visual->removeProp(getUuid() + "_terrain_surface", false);
            visual->render();
        }
        if (auto* project = ServiceRegistry::instance().getService<ProjectManagerService>())
            project->setNodeDimmed(getUuid(), true);
    }
    m_generated = false;
    m_result = {};
    m_status->setText(tr("地形数据已更新"));
}

void TerrainSurfacePanel::setSources(const QVector<Source>& sources)
{
    invalidate();
    m_sources = sources;
    m_sourceActor = nullptr;
    refreshTable();
    updateBoundsFromSources();
    updatePreview(true);
}

bool TerrainSurfacePanel::importFile(const QString& path, QString& diagnostic)
{
    QVector<Source> sources;
    if (!TerrainSurfaceBuilder::readDxf(path, sources, diagnostic))
        return false;
    setSources(sources);
    m_sourcePath = QFileInfo(path).fileName();
    m_status->setText(diagnostic);
    return true;
}

void TerrainSurfacePanel::refreshTable()
{
    const QSignalBlocker guard(m_table);
    m_table->setUpdatesEnabled(false);
    m_refreshing = true;
    m_table->setRowCount(m_sources.size());
    for (int row = 0; row < m_sources.size(); ++row)
        refreshRow(row);
    m_refreshing = false;
    m_table->setUpdatesEnabled(true);
}

void TerrainSurfacePanel::refreshRow(int row)
{
    const QSignalBlocker guard(m_table);
    const auto& source = m_sources[row];
    double z = 0;
    const bool constant = source.constantElevation(&z);

    auto* check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    check->setCheckState(source.enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(row, 0, check);

    auto* name = new QTableWidgetItem(source.layer + " / " + source.name);
    name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_table->setItem(row, 1, name);

    auto* role = new QTableWidgetItem(sourceRoles().value(int(source.role)));
    role->setToolTip(source.role == Role::Breakline
        ? tr("方网模式将控制线的高程点参与插值，不保证沿线形成网格边。")
        : tr("双击修改用途"));
    m_table->setItem(row, 2, role);
    m_table->setItem(row, 3, new QTableWidgetItem(constant ? QString::number(z, 'g', 12) : tr("保留逐点 Z")));

    QString status;
    if (source.role == Role::Contour && !constant)
        status = tr("非等高，请修正");
    else if (source.role == Role::Boundary && !source.closed)
        status = tr("边界未闭合");
    else
        status = tr("%1 点，%2").arg(source.points.size()).arg(source.closed ? tr("闭合") : tr("开放"));
    auto* info = new QTableWidgetItem(status);
    info->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_table->setItem(row, 4, info);
}

bool TerrainSurfacePanel::generate(QString& diagnostic)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);

    GridOptions gridOpts;
    gridOpts.spacing = m_spacing->value();
    gridOpts.bounds.xmin = m_xmin->value();
    gridOpts.bounds.xmax = m_xmax->value();
    gridOpts.bounds.ymin = m_ymin->value();
    gridOpts.bounds.ymax = m_ymax->value();
    gridOpts.customBounds = gridOpts.bounds.isValid();
    gridOpts.algorithm = m_algorithm ? m_algorithm->currentData().toInt() : 0;
    gridOpts.createSolid = m_createSolid ? m_createSolid->isChecked() : true;
    gridOpts.baseElevation = m_baseElevation ? m_baseElevation->value() : 0.0;
    gridOpts.autoBaseElevation = (gridOpts.baseElevation == 0.0);

    DomainMode mode = DomainMode::GridInterpolation;
    if (m_domainMode)
        mode = static_cast<DomainMode>(m_domainMode->currentData().toInt());

    const Result result = TerrainSurfaceBuilder::build(m_sources, m_spacing->value(), mode, gridOpts);
    QApplication::restoreOverrideCursor();

    if (!result.ok())
    {
        invalidate();
        diagnostic = result.error;
        m_status->setText(diagnostic);
        return false;
    }

    m_result = result;
    m_generated = true;

    if (result.isSolid && m_baseElevation)
    {
        const QSignalBlocker guard(m_baseElevation);
        m_baseElevation->setValue(result.baseElevation);
    }

    GeometryDisplayManager::setTerrainPresentation(getUuid(), isSurfaceColorEnabled(), isContoursEnabled());
    GeometryBuildUtils::registerShape(getUuid(), getDisplayName(), new TopoDS_Shape(result.shape));
    if (auto* body = GeomManager::CadDocument::getInstance()->getGeomShapeObject(getUuid()))
    {
        QVector<CadBody::FaceGroup> groups;
        for (const auto& fg : result.faceGroups)
            groups.append({fg.name, fg.role, fg.faceIndices});
        body->setFaceGroups(groups);
    }
    if (auto* workbench = dynamic_cast<GeometryWorkbench*>(ServiceRegistry::instance().getService<PreProcessingService>()))
        workbench->refreshTreePresentation(getUuid());

    diagnostic = result.summary();
    m_status->setText(diagnostic);

    updatePreview(false);

    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        double b[6];
        result.mesh->GetBounds(b);
        fitView(b);
        visual->render();
    }
    return true;
}

QJsonObject TerrainSurfacePanel::saveJson()
{
    GridOptions gridOpts;
    gridOpts.spacing = m_spacing->value();
    gridOpts.bounds.xmin = m_xmin->value();
    gridOpts.bounds.xmax = m_xmax->value();
    gridOpts.bounds.ymin = m_ymin->value();
    gridOpts.bounds.ymax = m_ymax->value();
    gridOpts.customBounds = gridOpts.bounds.isValid();
    gridOpts.algorithm = m_algorithm ? m_algorithm->currentData().toInt() : 0;
    gridOpts.createSolid = m_createSolid ? m_createSolid->isChecked() : true;
    gridOpts.baseElevation = m_baseElevation ? m_baseElevation->value() : 0.0;
    gridOpts.autoBaseElevation = (gridOpts.baseElevation == 0.0);

    DomainMode mode = DomainMode::GridInterpolation;
    if (m_domainMode)
        mode = static_cast<DomainMode>(m_domainMode->currentData().toInt());

    auto state = TerrainSurfaceBuilder::state(m_sources, m_generated, m_spacing->value(), mode, gridOpts);
    state["sourceFileName"] = m_sourcePath;
    state["showContours"] = m_showContours->isChecked();
    state["showSurface"] = m_showSurface->isChecked();
    return state;
}

void TerrainSurfacePanel::loadJson(const QJsonObject& state)
{
    m_sources = TerrainSurfaceBuilder::sourcesFromState(state);
    m_sourceActor = nullptr;
    m_sourcePath = state["sourceFileName"].toString();

    const GridOptions gridOpts = TerrainSurfaceBuilder::gridOptions(state);
    const DomainMode mode = TerrainSurfaceBuilder::domainMode(state);

    {
        const QSignalBlocker guard(m_spacing);
        m_spacing->setValue(gridOpts.spacing > 0 ? gridOpts.spacing : state["spacing"].toDouble(5));
    }
    {
        const QSignalBlocker guard(m_domainMode);
        for (int i = 0; i < m_domainMode->count(); ++i)
        {
            if (m_domainMode->itemData(i).toInt() == static_cast<int>(mode))
            {
                m_domainMode->setCurrentIndex(i);
                break;
            }
        }
    }
    if (m_algorithm)
    {
        const QSignalBlocker guard(m_algorithm);
        for (int i = 0; i < m_algorithm->count(); ++i)
        {
            if (m_algorithm->itemData(i).toInt() == gridOpts.algorithm)
            {
                m_algorithm->setCurrentIndex(i);
                break;
            }
        }
    }
    if (gridOpts.bounds.isValid())
    {
        const QSignalBlocker b1(m_xmin);
        const QSignalBlocker b2(m_xmax);
        const QSignalBlocker b3(m_ymin);
        const QSignalBlocker b4(m_ymax);
        m_xmin->setValue(gridOpts.bounds.xmin);
        m_xmax->setValue(gridOpts.bounds.xmax);
        m_ymin->setValue(gridOpts.bounds.ymin);
        m_ymax->setValue(gridOpts.bounds.ymax);
    }
    else
    {
        updateBoundsFromSources();
    }
    updateGridStats();

    if (m_createSolid)
    {
        const QSignalBlocker guard(m_createSolid);
        m_createSolid->setChecked(gridOpts.createSolid);
    }
    if (m_baseElevation)
    {
        const QSignalBlocker guard(m_baseElevation);
        m_baseElevation->setValue(gridOpts.baseElevation);
        m_baseElevation->setEnabled(gridOpts.createSolid);
        if (m_baseElevationLabel)
            m_baseElevationLabel->setEnabled(gridOpts.createSolid);
    }

    {
        const QSignalBlocker a(m_showContours);
        m_showContours->setChecked(state["showContours"].toBool(true));
    }
    {
        const QSignalBlocker b(m_showSurface);
        m_showSurface->setChecked(state["showSurface"].toBool(true));
    }

    GeometryDisplayManager::setTerrainPresentation(getUuid(), isSurfaceColorEnabled(), isContoursEnabled());
    m_generated = state["generated"].toBool();
    m_result = {};
    refreshTable();
    updateModeControls();
    m_status->setText(m_generated ? tr("已恢复地形面") : tr("已恢复等高线"));
}

void TerrainSurfacePanel::beforeLoad()
{
    // The CAD presentation is also the elevation presentation. Opening the page
    // never rebuilds terrain or replaces a valid surface with an empty preview.
    updatePreview(false);
    if (m_generated && GeometryBuildUtils::validShapeObject(getUuid()))
        m_status->setText(tr("已恢复地形面"));
}

TerrainSurfacePanel::~TerrainSurfacePanel()
{
    beforeUnload();
}

void TerrainSurfacePanel::beforeUnload()
{
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        visual->removeProp(getUuid() + "_terrain_sources", false);
        visual->removeProp(getUuid() + "_terrain_surface", false);

        // 退出地形面板时，恢复底层 CAD 实体面的正常显示
        const bool visible = GeometryDisplayManager::isVisible(getUuid());
        visual->setVisible(GeometryDisplayManager::facePropId(getUuid()), visible, false);
        visual->render();
    }
}

void TerrainSurfacePanel::fitView(const double b[6])
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* renderer = visual ? visual->renderer() : nullptr;
    if (!renderer) return;

    auto* camera = renderer->GetActiveCamera();
    const double x = (b[0] + b[1]) / 2, y = (b[2] + b[3]) / 2, z = (b[4] + b[5]) / 2;
    const double span = std::max({b[1] - b[0], b[3] - b[2], b[5] - b[4], 1.0});
    camera->SetFocalPoint(x, y, z);
    camera->SetPosition(x + span, y - span, z + span);
    camera->SetViewUp(0, 0, 1);
    renderer->ResetCamera(b);
    renderer->ResetCameraClippingRange();
}

bool TerrainSurfacePanel::isSurfaceColorEnabled() const
{
    return m_showSurface && m_showSurface->isChecked();
}

bool TerrainSurfacePanel::isContoursEnabled() const
{
    return m_showContours && m_showContours->isChecked();
}

void TerrainSurfacePanel::invalidatePreview()
{
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        visual->removeProp(getUuid() + "_terrain_surface", false);
    }
}

void TerrainSurfacePanel::updatePreview(bool fitCamera)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;

    const QString contourId = getUuid() + "_terrain_sources";
    const bool visible = GeometryDisplayManager::isVisible(getUuid());
    GeometryDisplayManager::setTerrainPresentation(getUuid(), isSurfaceColorEnabled(), isContoursEnabled());

    if (!m_showContours->isChecked())
    {
        visual->removeProp(contourId, false);
    }
    else
    {
        if (!m_sourceActor)
        {
            vtkNew<vtkPoints> points;
            points->SetDataTypeToDouble();
            vtkNew<vtkCellArray> lines;
            vtkNew<vtkCellArray> vertices;
            for (const auto& source : m_sources)
            {
                if (!source.enabled || source.points.isEmpty()) continue;
                const vtkIdType start = points->GetNumberOfPoints();
                for (const auto& p : source.points)
                    points->InsertNextPoint(p.X(), p.Y(), p.Z() + 0.05);
                if (source.points.size() == 1)
                {
                    vertices->InsertNextCell(1);
                    vertices->InsertCellPoint(start);
                }
                else
                {
                    lines->InsertNextCell(source.points.size() + (source.closed ? 1 : 0));
                    for (vtkIdType i = 0; i < source.points.size(); ++i) lines->InsertCellPoint(start + i);
                    if (source.closed) lines->InsertCellPoint(start);
                }
            }
            if (points->GetNumberOfPoints())
            {
                vtkNew<vtkPolyData> data;
                data->SetPoints(points);
                data->SetLines(lines);
                if (vertices->GetNumberOfCells() > 0)
                    data->SetVerts(vertices);
                vtkNew<vtkPolyDataMapper> mapper;
                mapper->SetInputData(data);
                mapper->ScalarVisibilityOff();
                mapper->SetResolveCoincidentTopologyToPolygonOffset();
                mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0, -66000);
                m_sourceActor = vtkSmartPointer<vtkActor>::New();
                m_sourceActor->SetMapper(mapper);
                m_sourceActor->PickableOff();
                m_sourceActor->GetProperty()->SetColor(.05, .25, .65);
                m_sourceActor->GetProperty()->SetLineWidth(1.8);
                m_sourceActor->GetProperty()->LightingOff();
            }
        }
        if (m_sourceActor)
        {
            if (visual->getProp(contourId) != m_sourceActor.GetPointer())
            {
                visual->removeProp(contourId, false);
                visual->addProp(contourId, m_sourceActor, "GeometryPreview", tr("地形等高线预览"), false, false);
            }
            visual->setVisible(contourId, visible, false);
        }
    }

    if (fitCamera)
    {
        double bounds[6];
        if (m_result.ok() && m_result.mesh)
            m_result.mesh->GetBounds(bounds);
        else if (m_sourceActor)
            m_sourceActor->GetBounds(bounds);
        else
            return;
        fitView(bounds);
    }
    visual->render();
}
