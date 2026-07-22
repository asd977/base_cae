#include "DisplacementConstraintWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhySicalFieldService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

#include "ui_DisplacementConstraintWidget.h"

// VTK
#include <PickManager.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 0: return QStringLiteral("点边界");
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}

static MeshTagKey comboTagKey(QComboBox* combo, int row)
{
    if (!combo || row < 0)
        return {};
    return combo->itemData(row).value<MeshTagKey>();
}

static QString boundaryDisplayText(MeshModuleControllerService* meshSvc, const MeshTagSet& set, bool showDimPrefix)
{
    QString text = meshSvc ? meshSvc->displayTextOfTag(set.key) : set.name.trimmed();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("%1%2").arg(boundaryDimText(set.key.dim)).arg(set.key.id);
    return showDimPrefix ? QStringLiteral("%1：%2").arg(boundaryDimText(set.key.dim), text) : text;
}

static void initConstraintCombo(QComboBox* combo)
{
    combo->clear();
    combo->addItem(QStringLiteral("固定"), QStringLiteral("0"));
    combo->addItem(QStringLiteral("自由"), QStringLiteral("1"));
    combo->addItem(QStringLiteral("悬空杆件节点（专用）"), QStringLiteral("-10000"));
    combo->setCurrentIndex(0);
}

static void setConstraintValue(QComboBox* combo, const QString& value)
{
    const int index = combo->findData(value.trimmed());
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

struct CoordinateRangeFilter
{
    bool useX = false;
    bool useY = false;
    bool useZ = false;
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
    double tolerance = 0.0;
};

static bool coordinateMatches(double value, double minimum, double maximum, double tolerance)
{
    return value >= minimum - tolerance && value <= maximum + tolerance;
}

static int maximumCellDimension(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return -1;

    int maxDimension = -1;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (cell)
            maxDimension = std::max(maxDimension, cell->GetCellDimension());
    }
    return maxDimension;
}

static QSet<vtkIdType> boundaryPointIds(vtkUnstructuredGrid* grid)
{
    QSet<vtkIdType> pointIds;
    if (!grid || grid->GetNumberOfPoints() <= 0 || grid->GetNumberOfCells() <= 0)
        return pointIds;

    const int maxDimension = maximumCellDimension(grid);
    if (maxDimension <= 1)
    {
        // 杆、梁等线网格没有体内部节点，坐标约束仍允许按位置选择其线节点。
        pointIds.reserve(static_cast<int>(grid->GetNumberOfPoints()));
        for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
            pointIds.insert(pointId);
        return pointIds;
    }

    // 对最高维单元的面/边进行拓扑计数：只出现一次的子单元属于真实网格边界。
    // 三维网格统计面，二维网格统计边；材料内部共享界面出现两次，不会被当成外边界。
    using FacetKey = std::vector<vtkIdType>;
    struct FacetKeyHash
    {
        std::size_t operator()(const FacetKey& key) const noexcept
        {
            std::size_t seed = key.size();
            for (vtkIdType id : key)
            {
                const std::size_t value = std::hash<vtkIdType>{}(id);
                seed ^= value + std::size_t(0x9e3779b9) + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    std::unordered_map<FacetKey, int, FacetKeyHash> facetUseCount;
    const bool useFaces = (maxDimension >= 3);

    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != maxDimension)
            continue;

        const int facetCount = useFaces ? cell->GetNumberOfFaces() : cell->GetNumberOfEdges();
        for (int facetIndex = 0; facetIndex < facetCount; ++facetIndex)
        {
            vtkCell* facet = useFaces ? cell->GetFace(facetIndex) : cell->GetEdge(facetIndex);
            if (!facet || facet->GetNumberOfPoints() <= 0)
                continue;

            FacetKey key;
            key.reserve(static_cast<std::size_t>(facet->GetNumberOfPoints()));
            for (vtkIdType i = 0; i < facet->GetNumberOfPoints(); ++i)
                key.push_back(facet->GetPointId(i));
            std::sort(key.begin(), key.end());
            ++facetUseCount[key];
        }
    }

    for (const auto& entry : facetUseCount)
    {
        if (entry.second != 1)
            continue;
        for (vtkIdType pointId : entry.first)
            pointIds.insert(pointId);
    }
    return pointIds;
}

static QVector<vtkIdType> boundaryPointsInCoordinateRange(vtkUnstructuredGrid* grid,
                                                           const CoordinateRangeFilter& filter)
{
    QVector<vtkIdType> pointIds;
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return pointIds;

    const QSet<vtkIdType> boundaryIds = boundaryPointIds(grid);
    pointIds.reserve(boundaryIds.size());
    double point[3] = {0.0, 0.0, 0.0};
    for (vtkIdType pointId : boundaryIds)
    {
        if (pointId < 0 || pointId >= grid->GetNumberOfPoints())
            continue;

        grid->GetPoint(pointId, point);
        if (filter.useX && !coordinateMatches(point[0], filter.minX, filter.maxX, filter.tolerance))
            continue;
        if (filter.useY && !coordinateMatches(point[1], filter.minY, filter.maxY, filter.tolerance))
            continue;
        if (filter.useZ && !coordinateMatches(point[2], filter.minZ, filter.maxZ, filter.tolerance))
            continue;
        pointIds.push_back(pointId);
    }
    std::sort(pointIds.begin(), pointIds.end());
    return pointIds;
}

static bool appendGridBounds(vtkUnstructuredGrid* grid, double bounds[6], bool& initialized)
{
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return false;

    double current[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    grid->GetBounds(current);
    if (!initialized)
    {
        std::copy(current, current + 6, bounds);
        initialized = true;
        return true;
    }

    bounds[0] = std::min(bounds[0], current[0]);
    bounds[1] = std::max(bounds[1], current[1]);
    bounds[2] = std::min(bounds[2], current[2]);
    bounds[3] = std::max(bounds[3], current[3]);
    bounds[4] = std::min(bounds[4], current[4]);
    bounds[5] = std::max(bounds[5], current[5]);
    return true;
}

static void normalizeRange(QDoubleSpinBox* minimumSpin, QDoubleSpinBox* maximumSpin,
                           double& minimum, double& maximum)
{
    minimum = minimumSpin->value();
    maximum = maximumSpin->value();
    if (minimum <= maximum)
        return;

    std::swap(minimum, maximum);
    minimumSpin->setValue(minimum);
    maximumSpin->setValue(maximum);
}
}

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget* parent)
    : BasePage("DisplacementConstraintWidget"), ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
    initConstraintCombo(ui->UXLineEdit);
    initConstraintCombo(ui->UYLineEdit);
    initConstraintCombo(ui->UZLineEdit);

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);
    ui->UZLineEdit->setEnabled(!is2D);
    ui->UZLineEdit->setToolTip(is2D ? QStringLiteral("二维模型不使用 Z 方向") : QString());

    connect(ui->btnSetAllBarsSuspended, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onSetAllBarsSuspended);
    connect(ui->btnSelectByCoordinate, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onSelectByCoordinateRange);
    connect(ui->btnUseMeshBounds, &QPushButton::clicked, this,
            &DisplacementConstraintWidget::onUseMeshBounds);

    const auto coordinateStateChanged = [this](bool)
    {
        updateCoordinateControlState();
    };
    connect(ui->coordXCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);
    connect(ui->coordYCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);
    connect(ui->coordZCheckBox, &QCheckBox::toggled, this, coordinateStateChanged);
    updateCoordinateControlState();
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
}

void DisplacementConstraintWidget::beforeLoad()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    pickManager.activate(getPageType(), m_uuid, PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &DisplacementConstraintWidget::onNodePicked, Qt::UniqueConnection);

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());

    // 防止页面来回切换后重复连接，造成批量选逻辑多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    // 维度变化时仅刷新候选组
    QObject::connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DisplacementConstraintWidget::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            });

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    if (row < 0)
                        return;

                    PickManager& pickManager = PickManager::instance();
                    auto* meshSvc =
                        ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    pickManager.setPreviewPointIds(
                        getPageType(), m_uuid, GridKind::Solid,
                        toVtkIds(meshSvc->pointIdsOfTag(key)));
                    pickManager.setPreviewPointIds(getPageType(), m_uuid, GridKind::Line, {});
                });
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                });
    }

    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager& pickManager = PickManager::instance();
                if (on)
                {
                    // Netgen 网格重新导入/刷新后，显示 actor 和数据集可能已经更换。
                    // 开启点选时重新绑定当前网格，并确保节点拾取观察器处于激活状态。
                    pickManager.updateStructure();
                    pickManager.activate(getPageType(), m_uuid, PickMode::Node);
                }
                pickManager.setClickPickEnabled(getPageType(), m_uuid, on);
            });

    // 进入页面不自动开启点选，避免单击模型时误选节点。
    {
        const QSignalBlocker blocker(ui->pickEnableCheckBox);
        ui->pickEnableCheckBox->setChecked(false);
    }
    pickManager.setClickPickEnabled(getPageType(), m_uuid, false);

    if (!m_coordinateRangeLoaded)
        onUseMeshBounds();
    updateCoordinateControlState();
}


void DisplacementConstraintWidget::refreshGroupCombo(int dimFilter)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    const bool showDimPrefix = (dimFilter < 0);

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, showDimPrefix),
                                   QVariant::fromValue(set.key));

    const bool available = ui->comboBoxGroup->count() > 0;
    ui->comboBoxGroup->setEnabled(available);
    ui->btnBatchAdd->setEnabled(available);
    ui->comboBoxGroup->setToolTip(
        available ? QStringLiteral("选择边界后点击“添加”")
                  : QStringLiteral("当前维度没有可用边界组"));
    if (available)
        ui->comboBoxGroup->setCurrentIndex(0);

    ui->comboBoxGroup->blockSignals(false);
}

void DisplacementConstraintWidget::onDimChanged(int)
{
    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());
}


void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0)
        return;

    PickManager& pickManager = PickManager::instance();
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                    toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}


void DisplacementConstraintWidget::updateCoordinateControlState()
{
    const int dimension = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (dimension == 2);

    ui->xMinSpinBox->setEnabled(ui->coordXCheckBox->isChecked());
    ui->xMaxSpinBox->setEnabled(ui->coordXCheckBox->isChecked());
    ui->yMinSpinBox->setEnabled(ui->coordYCheckBox->isChecked());
    ui->yMaxSpinBox->setEnabled(ui->coordYCheckBox->isChecked());

    if (is2D)
        ui->coordZCheckBox->setChecked(false);
    ui->coordZCheckBox->setEnabled(!is2D);
    ui->zMinSpinBox->setEnabled(!is2D && ui->coordZCheckBox->isChecked());
    ui->zMaxSpinBox->setEnabled(!is2D && ui->coordZCheckBox->isChecked());
}

void DisplacementConstraintWidget::onUseMeshBounds()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();

    double bounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool initialized = false;
    appendGridBounds(pickManager.solidGrid(), bounds, initialized);
    appendGridBounds(pickManager.lineGrid(), bounds, initialized);

    if (!initialized)
    {
        ui->coordinateResultLabel->setText(QStringLiteral("当前没有可用网格。"));
        return;
    }

    ui->xMinSpinBox->setValue(bounds[0]);
    ui->xMaxSpinBox->setValue(bounds[1]);
    ui->yMinSpinBox->setValue(bounds[2]);
    ui->yMaxSpinBox->setValue(bounds[3]);
    ui->zMinSpinBox->setValue(bounds[4]);
    ui->zMaxSpinBox->setValue(bounds[5]);

    if (!m_coordinateRangeLoaded)
    {
        const double maxSpan = std::max({bounds[1] - bounds[0], bounds[3] - bounds[2],
                                         bounds[5] - bounds[4]});
        ui->coordToleranceSpinBox->setValue(std::max(1.0e-9, maxSpan * 1.0e-8));
    }

    ui->coordinateResultLabel->setText(QStringLiteral("已读取模型坐标范围。"));
}

void DisplacementConstraintWidget::onSelectByCoordinateRange()
{
    const int dimension = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool useX = ui->coordXCheckBox->isChecked();
    const bool useY = ui->coordYCheckBox->isChecked();
    const bool useZ = dimension != 2 && ui->coordZCheckBox->isChecked();
    if (!useX && !useY && !useZ)
    {
        QMessageBox::information(this, QStringLiteral("坐标范围选择"),
                                 QStringLiteral("请至少勾选 X、Y、Z 中的一个坐标方向。"));
        return;
    }

    CoordinateRangeFilter filter;
    filter.useX = useX;
    filter.useY = useY;
    filter.useZ = useZ;
    filter.tolerance = ui->coordToleranceSpinBox->value();
    normalizeRange(ui->xMinSpinBox, ui->xMaxSpinBox, filter.minX, filter.maxX);
    normalizeRange(ui->yMinSpinBox, ui->yMaxSpinBox, filter.minY, filter.maxY);
    normalizeRange(ui->zMinSpinBox, ui->zMaxSpinBox, filter.minZ, filter.maxZ);

    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    pickManager.setClickPickEnabled(getPageType(), m_uuid, ui->pickEnableCheckBox->isChecked());
    const QVector<vtkIdType> solidPointIds =
        boundaryPointsInCoordinateRange(pickManager.solidGrid(), filter);
    const QVector<vtkIdType> linePointIds =
        boundaryPointsInCoordinateRange(pickManager.lineGrid(), filter);

    if (solidPointIds.isEmpty() && linePointIds.isEmpty())
    {
        ui->coordinateResultLabel->setText(QStringLiteral("未找到范围内的边界节点，当前选择保持不变。"));
        QMessageBox::information(this, QStringLiteral("坐标范围选择"),
                                 QStringLiteral("指定坐标范围内没有找到边界节点。请检查范围和容差。"));
        return;
    }

    const bool append = ui->coordAppendCheckBox->isChecked();
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, solidPointIds, !append);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, linePointIds, !append);

    ui->IDLabel->clear();
    ui->XLabel->clear();
    ui->YLabel->clear();
    ui->ZLabel->clear();
    ui->PrevUuidLabel->clear();
    ui->coordinateResultLabel->setText(
        QStringLiteral("已%1边界节点：实体网格 %2 个，杆网格 %3 个。")
            .arg(append ? QStringLiteral("追加选择") : QStringLiteral("替换选择"))
            .arg(solidPointIds.size())
            .arg(linePointIds.size()));

    qInfo().noquote()
        << QStringLiteral("__RAW__按坐标范围%1边界节点：实体网格 %2，杆网格 %3。")
               .arg(append ? QStringLiteral("追加") : QStringLiteral("选择"))
               .arg(solidPointIds.size())
               .arg(linePointIds.size());
}

QJsonObject DisplacementConstraintWidget::saveJson()
{
    QJsonObject obj;
    obj["ux"] = ui->UXLineEdit->currentData().toString();
    obj["uy"] = ui->UYLineEdit->currentData().toString();
    obj["uz"] = ui->UZLineEdit->currentData().toString();

    QJsonObject coordinateRange;
    coordinateRange["use_x"] = ui->coordXCheckBox->isChecked();
    coordinateRange["use_y"] = ui->coordYCheckBox->isChecked();
    coordinateRange["use_z"] = ui->coordZCheckBox->isChecked();
    coordinateRange["x_min"] = ui->xMinSpinBox->value();
    coordinateRange["x_max"] = ui->xMaxSpinBox->value();
    coordinateRange["y_min"] = ui->yMinSpinBox->value();
    coordinateRange["y_max"] = ui->yMaxSpinBox->value();
    coordinateRange["z_min"] = ui->zMinSpinBox->value();
    coordinateRange["z_max"] = ui->zMaxSpinBox->value();
    coordinateRange["tolerance"] = ui->coordToleranceSpinBox->value();
    coordinateRange["append"] = ui->coordAppendCheckBox->isChecked();
    obj["coordinate_range"] = coordinateRange;
    return obj;
}

void DisplacementConstraintWidget::onSetAllBarsSuspended()
{
    PickManager& pickManager = PickManager::instance();
    pickManager.updateStructure();
    vtkUnstructuredGrid* lineGrid = pickManager.lineGrid();
    if (!lineGrid || lineGrid->GetNumberOfCells() == 0)
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QSet<vtkIdType> usedPointIds;
    for (vtkIdType cellId = 0; cellId < lineGrid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = lineGrid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 1)
            continue;
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
            usedPointIds.insert(cell->GetPointId(i));
    }

    if (usedPointIds.isEmpty())
    {
        QMessageBox::information(this, QStringLiteral("没有杆单元"),
                                 QStringLiteral("当前网格中没有可设置的杆单元。"));
        return;
    }

    QVector<vtkIdType> pointIds;
    pointIds.reserve(usedPointIds.size());
    for (vtkIdType pointId : usedPointIds)
        pointIds.push_back(pointId);
    std::sort(pointIds.begin(), pointIds.end());

    setConstraintValue(ui->UXLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UYLineEdit, QStringLiteral("-10000"));
    setConstraintValue(ui->UZLineEdit, QStringLiteral("-10000"));

    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, {}, true);
    pickManager.setSelectedPointIds(getPageType(), m_uuid, GridKind::Line, pointIds, true);

    qInfo().noquote()
        << QStringLiteral("__RAW__已自动选择 %1 个杆件节点并设置为悬空。")
               .arg(pointIds.size());
}

void DisplacementConstraintWidget::loadJson(const QJsonObject& obj)
{
    setConstraintValue(ui->UXLineEdit,
                       obj.value("ux").toString(ui->UXLineEdit->currentData().toString()));
    setConstraintValue(ui->UYLineEdit,
                       obj.value("uy").toString(ui->UYLineEdit->currentData().toString()));
    setConstraintValue(ui->UZLineEdit,
                       obj.value("uz").toString(ui->UZLineEdit->currentData().toString()));

    const QJsonObject coordinateRange = obj.value("coordinate_range").toObject();
    if (!coordinateRange.isEmpty())
    {
        m_coordinateRangeLoaded = true;
        ui->coordXCheckBox->setChecked(coordinateRange.value("use_x").toBool(false));
        ui->coordYCheckBox->setChecked(coordinateRange.value("use_y").toBool(false));
        ui->coordZCheckBox->setChecked(coordinateRange.value("use_z").toBool(false));
        ui->xMinSpinBox->setValue(coordinateRange.value("x_min").toDouble(0.0));
        ui->xMaxSpinBox->setValue(coordinateRange.value("x_max").toDouble(0.0));
        ui->yMinSpinBox->setValue(coordinateRange.value("y_min").toDouble(0.0));
        ui->yMaxSpinBox->setValue(coordinateRange.value("y_max").toDouble(0.0));
        ui->zMinSpinBox->setValue(coordinateRange.value("z_min").toDouble(0.0));
        ui->zMaxSpinBox->setValue(coordinateRange.value("z_max").toDouble(0.0));
        ui->coordToleranceSpinBox->setValue(
            std::max(0.0, coordinateRange.value("tolerance").toDouble(1.0e-6)));
        ui->coordAppendCheckBox->setChecked(coordinateRange.value("append").toBool(false));
    }
    updateCoordinateControlState();
}

void DisplacementConstraintWidget::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
                        &DisplacementConstraintWidget::onNodePicked);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

int DisplacementConstraintWidget::uxFlag()
{
    return ui->UXLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uyFlag()
{
    return ui->UYLineEdit->currentData().toInt();
}

int DisplacementConstraintWidget::uzFlag()
{
    return ui->UZLineEdit->currentData().toInt();
}

void DisplacementConstraintWidget::onNodePicked(const QString& domain, const QString& activeUuid,
                                                GridKind, vtkIdType, int nodeId, double x, double y,
                                                double z, const QString& prevUuid)
{
    if (domain != getPageType())
        return;
    if (activeUuid != m_uuid)
        return;

    // 更新 UI：node_id + 坐标
    ui->IDLabel->setText(QString::number(nodeId));
    ui->XLabel->setText(QString::number(x, 'g', 12));
    ui->YLabel->setText(QString::number(y, 'g', 12));
    ui->ZLabel->setText(QString::number(z, 'g', 12));

    if (!prevUuid.isEmpty())
    {
        auto* pf = ServiceRegistry::instance().getService<PhysicalFieldService>();
        const QString prevName = pf ? pf->pageTitleByUuid(prevUuid) : QString();
        ui->PrevUuidLabel->setText(prevName.isEmpty() ? prevUuid : prevName);
    }
    else
    {
        ui->PrevUuidLabel->clear();
    }
}
