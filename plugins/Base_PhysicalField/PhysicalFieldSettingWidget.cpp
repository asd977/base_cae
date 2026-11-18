#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/PhysicalFieldService.h>
#include <services/VisualWidgetService.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <PickManager.h>

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QGridLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHash>
#include <QItemSelectionModel>
#include <QStringList>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <utility>

#include "ui_PhysicalFieldSettingWidget.h"

namespace {
const QString kScope = QStringLiteral("physical.displacement.overview");
const QString kNodes = kScope + QStringLiteral(".nodes");
const QString kDirections = kScope + QStringLiteral(".directions");
const QString kFocus = kScope + QStringLiteral(".focus");  // 仅用于清理旧版显示层

QString directionLabel(int ux, int uy, int uz)
{
    const bool is2d = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2;
    QStringList fixed;
    QStringList special;
    const int flags[] = {ux, uy, uz};
    const char* axes[] = {"X", "Y", "Z"};
    for (int i = 0; i < (is2d ? 2 : 3); ++i)
    {
        if (flags[i] == 0) fixed.append(QString::fromLatin1(axes[i]));
        else if (flags[i] == -10000) special.append(QString::fromLatin1(axes[i]));
    }
    QStringList labels;
    if (!fixed.isEmpty()) labels << fixed.join(QStringLiteral("、")) + QStringLiteral(" 固定");
    if (!special.isEmpty()) labels << special.join(QStringLiteral("、")) + QStringLiteral(" 杆件特殊标记");
    return labels.isEmpty() ? QStringLiteral("未限制") : labels.join(QStringLiteral("；"));
}

// 从真实 VTK 节点表建立原始 node_id 到坐标的映射；不可用时不推测编号。
void appendMeshNodes(vtkUnstructuredGrid* grid, const QSet<int>& wanted,
                     QHash<int, std::array<double, 3>>& points)
{
    if (!grid || !grid->GetPointData()) return;
    vtkDataArray* ids = grid->GetPointData()->GetArray("node_id");
    if (!ids) ids = grid->GetPointData()->GetArray("NodeId");
    if (!ids) return;
    const vtkIdType count = std::min(grid->GetNumberOfPoints(), ids->GetNumberOfTuples());
    for (vtkIdType i = 0; i < count; ++i)
    {
        const int id = static_cast<int>(ids->GetComponent(i, 0));
        if (!wanted.contains(id) || points.contains(id)) continue;
        double v[3];
        grid->GetPoint(i, v);
        points.insert(id, {v[0], v[1], v[2]});
    }
}

// 三个二进制位分别表示 X、Y、Z 的位移被固定（0=自由，1=固定）。
// 0b000 仅用于显示属于约束节点集、却没有任何普通固定自由度的节点。
struct CombinedDirections
{
    quint8 mask = 0;
    bool hasSpecial = false; // -10000 杆件标记仅保留状态，绝不当作固定方向
};

struct ConstraintColor
{
    const char* name;
    unsigned char rgb[3];
};

// 0..7：按实际合并后的方向组合着色；不是按约束条目/材料号着色。
// 高区分度分类色：灰/红/绿/黄/蓝/紫/青/橙。
// 与下方颜色筛选图例共用此表，修改颜色不会影响约束方向及导出数据。
constexpr ConstraintColor kConstraintColors[8] = {
    {"未固定", {138, 141, 145}},  // #8A8D91 中性灰
    {"X 固定", {222, 23, 56}},    // #DE1738 红
    {"Y 固定", {0, 136, 62}},    // #00883E 绿
    {"XY 固定", {255, 228, 0}},  // #FFE400 黄
    {"Z 固定", {19, 100, 189}},  // #1364BD 蓝
    {"XZ 固定", {202, 0, 223}},  // #CA00DF 紫
    {"YZ 固定", {0, 208, 240}},  // #00D0F0 青
    {"XYZ 固定", {245, 126, 5}}  // #F57E05 橙
};

QString rgbHex(const unsigned char color[3])
{
    return QStringLiteral("#%1%2%3")
        .arg(color[0], 2, 16, QLatin1Char('0'))
        .arg(color[1], 2, 16, QLatin1Char('0'))
        .arg(color[2], 2, 16, QLatin1Char('0'))
        .toUpper();
}

// 同一份 vtkPolyData 渲染全部可见节点；各节点 RGB 独立，不复制重合网格面。
// 不按数量取样，颜色过滤只影响渲染数据，不改变 PickManager 中的约束。
vtkSmartPointer<vtkActor> buildColoredPointActor(
    const QVector<int>& orderedIds,
    const QHash<int, CombinedDirections>& combined,
    const QHash<int, std::array<double, 3>>& nodePositions,
    const std::array<bool, 8>& enabled,
    int& drawnCount)
{
    drawnCount = 0;
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto vertices = vtkSmartPointer<vtkCellArray>::New();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetNumberOfComponents(3);
    colors->SetName("displacement_combination_rgb");
    points->Allocate(orderedIds.size());
    for (int id : orderedIds)
    {
        auto dirIt = combined.constFind(id);
        auto posIt = nodePositions.constFind(id);
        if (dirIt == combined.cend() || posIt == nodePositions.cend()) continue;
        const unsigned int mask = dirIt->mask & 0x07u;
        if (!enabled[mask]) continue;
        const vtkIdType pt = points->InsertNextPoint(posIt.value().data());
        vertices->InsertNextCell(1);
        vertices->InsertCellPoint(pt);
        colors->InsertNextTypedTuple(kConstraintColors[mask].rgb);
        ++drawnCount;
    }
    if (!drawnCount) return {};
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);
    poly->SetVerts(vertices);
    poly->GetPointData()->SetScalars(colors);
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(poly);
    mapper->SetScalarModeToUsePointData();
    mapper->SetColorModeToDirectScalars();
    mapper->ScalarVisibilityOn();
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetPointSize(5.0);
    actor->GetProperty()->SetRenderPointsAsSpheres(true);
    return actor;
}
}

PhysicalFieldSettingWidget::PhysicalFieldSettingWidget(QWidget* parent)
    : BasePage(QStringLiteral("PhysicalFieldSettingWidget")),
      ui(new Ui::PhysicalFieldSettingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("约束与荷载"));

    auto* panel = new QGroupBox(QStringLiteral("位移约束总览"), this);
    auto* layout = new QVBoxLayout(panel);
    m_showAll = new QCheckBox(QStringLiteral("显示全部位移约束"), panel);
    m_showAll->setChecked(true);
    layout->addWidget(m_showAll);

    auto* hint = new QLabel(QStringLiteral("勾选约束后按最终固定方向着色；单击选择，双击编辑。"), panel);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_table = new QTableWidget(panel);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QStringLiteral("显示"), QStringLiteral("名称与约束方向"), QStringLiteral("节点数")});
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMinimumHeight(150);
    layout->addWidget(m_table);

    // 颜色是最终约束方向的组合，而非某一条约束的独占颜色。
    auto* filterGroup = new QGroupBox(QStringLiteral("按约束组合筛选颜色"), panel);
    auto* filterGrid = new QGridLayout(filterGroup);
    filterGrid->setHorizontalSpacing(12);
    filterGrid->setVerticalSpacing(5);
    for (int mask = 0; mask < 8; ++mask)
    {
        auto* cell = new QWidget(filterGroup);
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(1, 1, 1, 1);
        cellLayout->setSpacing(5);
        auto* swatch = new QFrame(cell);
        swatch->setFixedSize(16, 16);
        swatch->setStyleSheet(QStringLiteral("background-color:%1; border:1px solid #686868; border-radius:2px;")
                              .arg(rgbHex(kConstraintColors[mask].rgb)));
        cellLayout->addWidget(swatch);
        auto* check = new QCheckBox(QString::fromUtf8(kConstraintColors[mask].name), cell);
        check->setChecked(true);
        cellLayout->addWidget(check);
        auto* count = new QLabel(QStringLiteral("0"), cell);
        cellLayout->addWidget(count);
        cellLayout->addStretch();
        filterGrid->addWidget(cell, mask / 2, mask % 2);
        m_colorChecks[mask] = check;
        m_colorCounts[mask] = count;
        m_colorCells[mask] = cell;
        connect(check, &QCheckBox::toggled, this, [this](bool) { updateOverviewOverlay(); });
    }
    layout->addWidget(filterGroup);

    m_info = new QLabel(panel);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
    auto* bottom = new QHBoxLayout();
    auto* refreshButton = new QPushButton(QStringLiteral("刷新列表"), panel);
    bottom->addWidget(refreshButton);
    bottom->addStretch();
    layout->addLayout(bottom);
    auto* note = new QLabel(QStringLiteral("颜色按同一节点叠加后的 XYZ 固定组合计算；未固定不代表网格中所有自由节点。"), panel);
    note->setWordWrap(true);
    layout->addWidget(note);

    // 原有“导入已有约束”保持不变；总览放在它前面。
    ui->contentLayout->insertWidget(0, panel);

    connect(m_showAll, &QCheckBox::toggled, this, [this](bool) { updateOverviewOverlay(); });
    connect(refreshButton, &QPushButton::clicked, this, [this]() { refreshOverview(); });
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int column)
    {
        if (!m_updatingTable && column == 0 && row >= 0) updateOverviewOverlay();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this]() { if (!m_updatingTable) updateOverviewOverlay(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int)
    {
        if (row >= 0 && row < m_items.size() && m_openPage)
            m_openPage(m_items[row].uuid);
    });
}

PhysicalFieldSettingWidget::~PhysicalFieldSettingWidget()
{
    clearOverviewOverlay(false);
    delete ui;
}

void PhysicalFieldSettingWidget::setDisplacementProvider(
       std::function<QVector<DisplacementOverviewItem>()> provider,
       std::function<void(const QString&)> openPage)
{
    m_provider = std::move(provider);
    m_openPage = std::move(openPage);
}

void PhysicalFieldSettingWidget::beforeLoad()
{
    m_active = true;
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    // 更新当前网格引用，不激活任何拾取模式；选择集的拥有者仍是原位移约束页面。
    PickManager::instance().updateStructure();
    refreshOverview();
}

void PhysicalFieldSettingWidget::beforeUnload()
{
    m_active = false;
    clearOverviewOverlay(true);
}

QString PhysicalFieldSettingWidget::selectedUuid() const
{
    const int row = m_table ? m_table->currentRow() : -1;
    return row >= 0 && row < m_items.size() ? m_items[row].uuid : QString();
}

void PhysicalFieldSettingWidget::refreshOverview()
{
    if (!m_active) return;
    const QString oldUuid = selectedUuid();
    QSet<QString> hidden;
    for (int i = 0; i < m_items.size(); ++i)
        if (m_table->item(i, 0) && m_table->item(i, 0)->checkState() == Qt::Unchecked)
            hidden.insert(m_items[i].uuid);
    m_items = m_provider ? m_provider() : QVector<DisplacementOverviewItem>{};
    m_updatingTable = true;
    m_table->setRowCount(m_items.size());
    int selectedRow = -1;
    for (int i = 0; i < m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        auto* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        check->setCheckState(hidden.contains(item.uuid) ? Qt::Unchecked : Qt::Checked);
        m_table->setItem(i, 0, check);
        auto* title = new QTableWidgetItem(item.title + QStringLiteral(" · ") +
                                         directionLabel(item.ux, item.uy, item.uz));
        title->setToolTip(directionLabel(item.ux, item.uy, item.uz));
        m_table->setItem(i, 1, title);
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(item.nodeIds.size())));
        if (item.uuid == oldUuid) selectedRow = i;
    }
    m_table->clearSelection();
    if (selectedRow >= 0)
        m_table->setCurrentCell(selectedRow, 1,
                                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    else
        m_table->setCurrentItem(nullptr);
    m_updatingTable = false;
    updateOverviewOverlay();
}

void PhysicalFieldSettingWidget::clearOverviewOverlay(bool renderNow)
{
    auto* visual = m_visual ? m_visual : ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual) return;
    visual->removeProp(kNodes, false);
    visual->removeProp(kDirections, false);
    visual->removeProp(kFocus, false);
    if (renderNow) visual->render();
}

void PhysicalFieldSettingWidget::updateOverviewOverlay()
{
    if (!m_active) return;
    if (!m_visual) m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visual) return;
    clearOverviewOverlay(false);

    const bool is2d = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2;
    const int axisCount = is2d ? 2 : 3;
    const QString focusUuid = selectedUuid();
    QHash<int, CombinedDirections> combined;
    int visibleGroups = 0;
    for (int row = 0; row < m_items.size(); ++row)
    {
        const auto& item = m_items[row];
        if (!m_table->item(row, 0) || m_table->item(row, 0)->checkState() != Qt::Checked)
            continue;
        if (!m_showAll->isChecked() && item.uuid != focusUuid)
            continue;
        ++visibleGroups;
        const int flags[] = {item.ux, item.uy, item.uz};
        for (int id : item.nodeIds)
        {
            auto& directions = combined[id];
            for (int axis = 0; axis < axisCount; ++axis)
            {
                if (flags[axis] == 0) directions.mask |= quint8(1u << axis);
                else if (flags[axis] == -10000) directions.hasSpecial = true;
            }
        }
    }

    // 某些节点仅含 -10000 杆件特殊标记、没有普通固定方向；归入“未固定”颜色，
    // 不能错误地显示为 XYZ 全固定。保留总数提示便于用户辨认。
    std::array<int, 8> counts = {};
    int specialCount = 0;
    for (auto it = combined.cbegin(); it != combined.cend(); ++it)
    {
        ++counts[it.value().mask & 0x07u];
        if (it.value().hasSpecial) ++specialCount;
    }
    std::array<bool, 8> enabled = {};
    for (int mask = 0; mask < 8; ++mask)
    {
        enabled[mask] = m_colorChecks[mask] && m_colorChecks[mask]->isChecked();
        if (m_colorCounts[mask]) m_colorCounts[mask]->setText(QString::number(counts[mask]));
        // 二维不出现含 Z 固定的组合，无须展示它们。
        if (m_colorCells[mask]) m_colorCells[mask]->setVisible(!is2d || ((mask & 4) == 0));
    }

    if (combined.isEmpty())
    {
        m_info->setText(QStringLiteral("共 %1 项位移约束，当前显示 %2 项；0 个约束节点。")
                        .arg(m_items.size()).arg(visibleGroups));
        m_visual->render();
        return;
    }

    // 读取一次网格节点坐标；颜色组合绝不生成第二套网格面。
    PickManager& pick = PickManager::instance();
    QSet<int> wantedIds;
    wantedIds.reserve(combined.size());
    for (auto it = combined.cbegin(); it != combined.cend(); ++it)
        wantedIds.insert(it.key());
    QHash<int, std::array<double, 3>> nodePositions;
    appendMeshNodes(pick.solidGrid(), wantedIds, nodePositions);
    appendMeshNodes(pick.lineGrid(), wantedIds, nodePositions);
    if (nodePositions.isEmpty())
    {
        m_info->setText(QStringLiteral("共 %1 项，%2 个约束节点；未找到网格 node_id 坐标，无法显示。")
                        .arg(visibleGroups).arg(combined.size()));
        m_visual->render();
        return;
    }

    QVector<int> orderedIds;
    orderedIds.reserve(combined.size());
    for (auto it = combined.cbegin(); it != combined.cend(); ++it)
        if (nodePositions.contains(it.key())) orderedIds.push_back(it.key());
    std::sort(orderedIds.begin(), orderedIds.end());
    int displayedNodes = 0;
    auto actor = buildColoredPointActor(orderedIds, combined, nodePositions, enabled, displayedNodes);
    if (actor)
        m_visual->addProp(kNodes, actor, QStringLiteral("{constraint} 位移约束总览"),
                          QStringLiteral("位移约束组合着色"), false, false);

    m_info->setText(QStringLiteral("显示 %1/%2 项，约束节点 %3 个；按颜色筛选后可见 %4 个。")
                    .arg(visibleGroups).arg(m_items.size())
                    .arg(combined.size()).arg(displayedNodes));
    if (specialCount)
        m_info->setText(m_info->text() +
                        QStringLiteral(" 其中 %1 个节点含杆件特殊标记（不作为固定方向）。")
                        .arg(specialCount));
    if (nodePositions.size() != combined.size())
        m_info->setText(m_info->text() +
                        QStringLiteral(" 有 %1 个节点缺少网格坐标。")
                        .arg(combined.size() - nodePositions.size()));
    m_visual->render();
}

void PhysicalFieldSettingWidget::on_importIIIDButton_clicked()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString defaultDir = projectDir.isEmpty()
                                   ? QDir::homePath()
                                   : SolverFilePaths::projectInputDirectory(projectDir);
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入已有约束"), defaultDir,
        QStringLiteral("约束数据 (*.DAT *.dat);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    PhysicalFieldService* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (!service)
    {
        QMessageBox::warning(this, QStringLiteral("导入已有约束"),
                             QStringLiteral("约束与荷载服务不可用。"));
        return;
    }

    QString errorMessage;
    if (!service->importIIIDFile(filePath, &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("导入已有约束"), errorMessage);
        return;
    }

    refreshOverview();
    QMessageBox::information(this, QStringLiteral("导入已有约束"),
                             QStringLiteral("约束数据导入完成，已自动创建位移约束。"));
}
