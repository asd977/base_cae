#include "BarElementPlacementWidget.h"

#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellType.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <QStringList>

namespace
{
constexpr int kLegacyDirectionData = 3;
constexpr double kDirectionEpsilon = 1.0e-12;
constexpr int kPreviewBarLimit = 300;
constexpr int kMaxSemiAutoBars = 5000;

QColor barDisplayColor()
{
    // 锚杆统一使用一种显示颜色；MATYPE 是计算属性，不承担显示分类。
    return QColor(232, 126, 48);
}

double pointDistance(const double a[3], const double b[3])
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::array<double, 3> normalized(const std::array<double, 3>& v)
{
    const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n <= kDirectionEpsilon)
        return {{0.0, 0.0, 0.0}};
    return {{v[0] / n, v[1] / n, v[2] / n}};
}

}

BarElementPlacementWidget::BarElementPlacementWidget(QWidget* parent)
    : BasePage(QStringLiteral("BarElementPlacement"))
{
    Q_UNUSED(parent);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto* geometryBox = new QGroupBox(QStringLiteral("锚杆布置"), this);
    auto* geometryLayout = new QVBoxLayout(geometryBox);
    geometryLayout->setSpacing(8);

    auto* placementForm = new QFormLayout();
    m_placementMode = new QComboBox(geometryBox);
    m_placementMode->addItem(QStringLiteral("手动布置"),
                             static_cast<int>(PlacementMode::Manual));
    m_placementMode->addItem(QStringLiteral("区域布置"),
                             static_cast<int>(PlacementMode::SurfaceSemiAuto));
    m_placementMode->setMaximumWidth(180);
    placementForm->addRow(QStringLiteral("布置方式"), m_placementMode);
    geometryLayout->addLayout(placementForm);

    // ---------------- 手动布置 ----------------
    // 手动布置以“两个端点”为最直接的工作流；所有坐标输入改为纵向紧凑布局，
    // 避免 X/Y/Z + 按钮横向铺开把整个属性面板撑得过宽。
    m_manualPanel = new QWidget(geometryBox);
    auto* manualLayout = new QVBoxLayout(m_manualPanel);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(8);

    auto* manualHint = new QLabel(
        QStringLiteral("确定锚头与锚固端后添加锚杆。"),
        m_manualPanel);
    manualHint->setWordWrap(true);
    manualLayout->addWidget(manualHint);

    auto* definitionForm = new QFormLayout();
    definitionForm->setHorizontalSpacing(8);
    definitionForm->setVerticalSpacing(6);
    m_definitionMode = new QComboBox(m_manualPanel);
    // 新建节点默认“两点定义”；旧工程仍按保存的 data 恢复，不受条目顺序影响。
    m_definitionMode->addItem(QStringLiteral("两点定义（推荐）"),
                              static_cast<int>(DefinitionMode::TwoPoints));
    m_definitionMode->addItem(QStringLiteral("锚头 + 长度 + 方向"),
                              static_cast<int>(DefinitionMode::HeadLengthDirection));
    m_definitionMode->setMaximumWidth(220);
    definitionForm->addRow(QStringLiteral("定义方式"), m_definitionMode);
    manualLayout->addLayout(definitionForm);

    auto* endpointBox = new QGroupBox(QStringLiteral("端点"), m_manualPanel);
    auto* coordinateGrid = new QGridLayout(endpointBox);
    coordinateGrid->setContentsMargins(10, 10, 10, 10);
    coordinateGrid->setHorizontalSpacing(8);
    coordinateGrid->setVerticalSpacing(6);
    coordinateGrid->setColumnStretch(1, 1);

    auto* startTitle = new QLabel(QStringLiteral("锚头"), endpointBox);
    m_pickStartButton = new QPushButton(QStringLiteral("取点"), endpointBox);
    m_pickStartButton->setMaximumWidth(86);
    m_pickStartButton->setToolTip(QStringLiteral("从模型中读取一个实体网格节点作为锚头。"));
    coordinateGrid->addWidget(startTitle, 0, 0);
    coordinateGrid->addWidget(m_pickStartButton, 0, 1, 1, 1, Qt::AlignRight);

    const QStringList axisNames = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis)
    {
        m_start[axis] = coordinateSpin();
        coordinateGrid->addWidget(new QLabel(axisNames.at(axis), endpointBox), axis + 1, 0);
        coordinateGrid->addWidget(m_start[axis], axis + 1, 1);

        connect(m_start[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double)
                {
                    if (definitionMode() == DefinitionMode::HeadLengthDirection)
                        updateEndFromLengthDirection(false);
                    updateLengthLabel();
                });
    }

    m_endRowLabel = new QLabel(QStringLiteral("锚固端"), endpointBox);
    m_pickEndButton = new QPushButton(QStringLiteral("取点"), endpointBox);
    m_pickEndButton->setMaximumWidth(86);
    m_pickEndButton->setToolTip(QStringLiteral("从模型中读取一个实体网格节点作为锚固端。"));
    coordinateGrid->addWidget(m_endRowLabel, 4, 0);
    coordinateGrid->addWidget(m_pickEndButton, 4, 1, 1, 1, Qt::AlignRight);

    for (int axis = 0; axis < 3; ++axis)
    {
        m_end[axis] = coordinateSpin(axis == 0 ? 1.0 : 0.0);
        coordinateGrid->addWidget(new QLabel(axisNames.at(axis), endpointBox), axis + 5, 0);
        coordinateGrid->addWidget(m_end[axis], axis + 5, 1);
        connect(m_end[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { updateLengthLabel(); });
    }
    manualLayout->addWidget(endpointBox);

    m_lengthDirectionPanel = new QGroupBox(QStringLiteral("长度与方向"), m_manualPanel);
    auto* directionForm = new QFormLayout(m_lengthDirectionPanel);
    directionForm->setContentsMargins(10, 10, 10, 10);
    directionForm->setHorizontalSpacing(8);
    directionForm->setVerticalSpacing(6);

    m_length = new QDoubleSpinBox(m_lengthDirectionPanel);
    m_length->setRange(0.0, 1.0e12);
    m_length->setDecimals(8);
    m_length->setValue(1.0);
    m_length->setMinimumWidth(112);
    m_length->setMaximumWidth(150);
    m_length->setToolTip(QStringLiteral("锚头到锚固端的实际长度。"));
    directionForm->addRow(QStringLiteral("锚杆长度"), m_length);

    auto* directionRow = new QWidget(m_lengthDirectionPanel);
    auto* directionGrid = new QGridLayout(directionRow);
    directionGrid->setContentsMargins(0, 0, 0, 0);
    directionGrid->setHorizontalSpacing(8);
    directionGrid->setVerticalSpacing(6);
    directionGrid->setColumnStretch(1, 1);
    for (int axis = 0; axis < 3; ++axis)
    {
        directionGrid->addWidget(new QLabel(axisNames.at(axis), directionRow), axis, 0);
        m_direction[axis] = directionSpin(axis == 0 ? 1.0 : 0.0);
        directionGrid->addWidget(m_direction[axis], axis, 1);
        connect(m_direction[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double)
                {
                    if (definitionMode() == DefinitionMode::HeadLengthDirection)
                        updateEndFromLengthDirection(false);
                });
    }
    m_pickDirectionButton = new QPushButton(QStringLiteral("从模型取方向"), directionRow);
    m_pickDirectionButton->setMaximumWidth(118);
    m_pickDirectionButton->setToolTip(QStringLiteral("点击模型节点，用“锚头 → 该节点”确定方向。"));
    directionGrid->addWidget(m_pickDirectionButton, 3, 0, 1, 2, Qt::AlignLeft);
    directionForm->addRow(QStringLiteral("方向向量"), directionRow);
    manualLayout->addWidget(m_lengthDirectionPanel);

    m_lengthLabel = new QLabel(m_manualPanel);
    m_pickStatus = new QLabel(QStringLiteral("先确定两个端点。"), m_manualPanel);
    m_pickStatus->setWordWrap(true);
    manualLayout->addWidget(m_lengthLabel);
    manualLayout->addWidget(m_pickStatus);
    geometryLayout->addWidget(m_manualPanel);

    // ---------------- 区域布置 ----------------
    m_surfacePanel = new QWidget(geometryBox);
    auto* surfaceLayout = new QVBoxLayout(m_surfacePanel);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(10);

    auto* semiHint = new QLabel(
        QStringLiteral("选择施工面并设置长度、间隔后生成。"),
        m_surfacePanel);
    semiHint->setWordWrap(true);
    surfaceLayout->addWidget(semiHint);

    // 1. 选择施工面：进入区域布置后拾取始终有效，不再要求额外点击“开始选择”。
    auto* surfaceSelectBox = new QGroupBox(QStringLiteral("1. 选择施工面"), m_surfacePanel);
    auto* surfaceSelectLayout = new QVBoxLayout(surfaceSelectBox);
    surfaceSelectLayout->setContentsMargins(10, 10, 10, 10);
    surfaceSelectLayout->setSpacing(8);

    m_surfaceWholeFace = new QCheckBox(QStringLiteral("自动选择整片平面"), surfaceSelectBox);
    m_surfaceWholeFace->setChecked(true);
    m_surfaceWholeFace->setToolTip(QStringLiteral("推荐开启。单击一个网格面后，只扩展到同一连续平面，不跨越折角。点击面同时作为网格间隔的计算基准。"));
    surfaceSelectLayout->addWidget(m_surfaceWholeFace);

    auto* surfaceStatusRow = new QHBoxLayout();
    surfaceStatusRow->setContentsMargins(0, 0, 0, 0);
    surfaceStatusRow->setSpacing(8);
    m_surfaceStatus = new QLabel(QStringLiteral("在模型中单击一个开挖面。"), surfaceSelectBox);
    m_surfaceStatus->setWordWrap(true);
    m_clearSurfaceButton = new QPushButton(QStringLiteral("重新选择"), surfaceSelectBox);
    m_clearSurfaceButton->setEnabled(false);
    m_clearSurfaceButton->setToolTip(QStringLiteral("清除当前施工面选择，然后重新点选。"));
    surfaceStatusRow->addWidget(m_surfaceStatus, 1);
    surfaceStatusRow->addWidget(m_clearSurfaceButton);
    surfaceSelectLayout->addLayout(surfaceStatusRow);
    surfaceLayout->addWidget(surfaceSelectBox);

    // 2. 布置参数：方向 1/2 直接对应模型中显示的 1/2 指示线。
    auto* surfaceParamBox = new QGroupBox(QStringLiteral("2. 布置参数"), m_surfacePanel);
    auto* surfaceForm = new QFormLayout(surfaceParamBox);
    surfaceForm->setContentsMargins(10, 10, 10, 10);
    surfaceForm->setHorizontalSpacing(10);
    surfaceForm->setVerticalSpacing(8);

    m_surfaceLength = new QDoubleSpinBox(surfaceParamBox);
    m_surfaceLength->setRange(1.0e-8, 1.0e12);
    m_surfaceLength->setDecimals(4);
    m_surfaceLength->setSingleStep(0.1);
    m_surfaceLength->setValue(1.0);
    m_surfaceLength->setToolTip(QStringLiteral("锚头端到锚固端的实际长度。应用层只生成这两个端点。"));
    surfaceForm->addRow(QStringLiteral("锚杆长度"), m_surfaceLength);

    m_surfaceGridIntervalU = new QSpinBox(surfaceParamBox);
    m_surfaceGridIntervalU->setRange(1, 1000);
    m_surfaceGridIntervalU->setValue(1);
    m_surfaceGridIntervalU->setSuffix(QStringLiteral(" 格"));
    m_surfaceGridIntervalU->setToolTip(QStringLiteral("沿模型中标记“1”的方向，从本次点击面开始计算。1 格表示每个网格面都布置，2 格表示隔 1 格布置。"));
    surfaceForm->addRow(QStringLiteral("方向 1 间隔"), m_surfaceGridIntervalU);

    m_surfaceGridIntervalV = new QSpinBox(surfaceParamBox);
    m_surfaceGridIntervalV->setRange(1, 1000);
    m_surfaceGridIntervalV->setValue(1);
    m_surfaceGridIntervalV->setSuffix(QStringLiteral(" 格"));
    m_surfaceGridIntervalV->setToolTip(QStringLiteral("沿模型中标记“2”的方向，从本次点击面开始计算。1 格表示每个网格面都布置，2 格表示隔 1 格布置。"));
    surfaceForm->addRow(QStringLiteral("方向 2 间隔"), m_surfaceGridIntervalV);

    m_surfaceDirectionMode = new QComboBox(surfaceParamBox);
    m_surfaceDirectionMode->addItem(QStringLiteral("朝围岩侧"), 0);
    m_surfaceDirectionMode->addItem(QStringLiteral("反向（朝开挖侧）"), 1);
    m_surfaceDirectionMode->setToolTip(QStringLiteral("通常选择开挖体表面，因此默认锚杆背离所选开挖体、朝围岩侧生成。请以模型中的锚杆预览为准；方向不对时直接切换为反向。"));
    surfaceForm->addRow(QStringLiteral("锚杆方向"), m_surfaceDirectionMode);

    auto* directionHint = new QLabel(
        QStringLiteral("方向 1 / 方向 2 会显示在模型的点击位置；修改长度、间隔或方向后，预览会自动更新。"),
        surfaceParamBox);
    directionHint->setWordWrap(true);
    surfaceForm->addRow(QString(), directionHint);
    surfaceLayout->addWidget(surfaceParamBox);

    geometryLayout->addWidget(m_surfacePanel);

    connect(m_placementMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { handlePlacementModeChanged(); });
    connect(m_definitionMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateDefinitionUi(); });
    connect(m_length, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double)
            {
                if (definitionMode() == DefinitionMode::HeadLengthDirection)
                    updateEndFromLengthDirection(false);
            });
    connect(m_pickStartButton, &QPushButton::clicked, this,
            [this]() { beginCoordinatePick(PickTarget::Start); });
    connect(m_pickEndButton, &QPushButton::clicked, this,
            [this]() { beginCoordinatePick(PickTarget::End); });
    connect(m_pickDirectionButton, &QPushButton::clicked, this,
            [this]() { beginCoordinatePick(PickTarget::DirectionPoint); });
    m_surfacePreviewTimer = new QTimer(this);
    m_surfacePreviewTimer->setSingleShot(true);
    m_surfacePreviewTimer->setInterval(80);
    connect(m_surfacePreviewTimer, &QTimer::timeout, this, [this]()
    {
        if (m_pageActive && isSurfaceSemiAuto())
            updatePreview();
    });

    connect(m_surfaceLength, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { scheduleSurfacePreview(); });
    connect(m_surfaceGridIntervalU, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { scheduleSurfacePreview(); });
    connect(m_surfaceGridIntervalV, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { scheduleSurfacePreview(); });
    connect(m_surfaceDirectionMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { scheduleSurfacePreview(); });
    connect(m_surfaceWholeFace, &QCheckBox::toggled, this, [this](bool)
    {
        updateSurfacePickStatus();
        scheduleSurfacePreview();
    });
    connect(m_clearSurfaceButton, &QPushButton::clicked,
            this, &BarElementPlacementWidget::clearSurfaceSelection);
    connect(&PickManager::instance(), &PickManager::nodePicked, this,
            [this](const QString& domain, const QString& activeUuid, GridKind kind,
                   vtkIdType, int nodeId, double x, double y, double z,
                   const QString&)
            {
                if (!isSurfaceSemiAuto() && m_pickTarget != PickTarget::None &&
                    domain == getPageType() && activeUuid == m_uuid &&
                    kind == GridKind::Solid)
                {
                    applyPickedCoordinate(nodeId, x, y, z);
                }
            });
    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& activeUuid,
                   PickMode mode, GridKind kind)
            {
                if (!m_pageActive || !isSurfaceSemiAuto() ||
                    mode != PickMode::Face || kind != GridKind::Solid ||
                    domain != getPageType() || activeUuid != m_uuid)
                    return;

                // 面高亮立即反馈；连续点选时只合并刷新一次区域预览。
                updateSurfacePickStatus();
                scheduleSurfacePreview();
            });
    connect(&PickManager::instance(), &PickManager::facePicked, this,
            [this](const QString& domain, const QString& activeUuid, GridKind kind,
                   vtkIdType faceKey, bool selected)
            {
                if (!m_pageActive || !isSurfaceSemiAuto() || kind != GridKind::Solid ||
                    domain != getPageType() || activeUuid != m_uuid)
                    return;

                // 每次点击的 faceKey 都是本次布置的基准格。整片模式下再由这个 seed 扩展整片。
                QTimer::singleShot(0, this, [this, faceKey, selected]()
                {
                    if (m_surfaceWholeFace && m_surfaceWholeFace->isChecked())
                    {
                        applyWholeSurfacePick(faceKey, selected);
                        return;
                    }

                    if (selected)
                    {
                        if (!m_surfaceLayoutSeedFaces.contains(faceKey))
                            m_surfaceLayoutSeedFaces.push_back(faceKey);
                    }
                    else
                    {
                        m_surfaceLayoutSeedFaces.removeAll(faceKey);
                    }
                    scheduleSurfacePreview();
                });
            });

    root->addWidget(geometryBox);

    auto* propertyBox = new QGroupBox(QStringLiteral("施工属性与生成"), this);
    auto* form = new QFormLayout(propertyBox);
    form->setContentsMargins(10, 10, 10, 10);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);

    // 面向客户显示“第几级”，不暴露洞室求解器内部使用的负层号。
    // 项目网格中仍保存正数安装级；Base_Solver 在洞室计算临时 IIIN.DAT 中
    // 将 KETYPE=1 的杆单元转换为 LAYER=-1/-2/...，避免污染其他分析类型。
    auto* stageMaterialRow = new QWidget(propertyBox);
    auto* stageMaterialLayout = new QHBoxLayout(stageMaterialRow);
    stageMaterialLayout->setContentsMargins(0, 0, 0, 0);
    stageMaterialLayout->setSpacing(12);

    auto* stageLabel = new QLabel(QStringLiteral("安装级"), stageMaterialRow);
    m_installationLayer = new QSpinBox(stageMaterialRow);
    m_installationLayer->setRange(1, 999);
    m_installationLayer->setValue(1);
    m_installationLayer->setPrefix(QStringLiteral("第 "));
    m_installationLayer->setSuffix(QStringLiteral(" 级"));
    m_installationLayer->setMaximumWidth(116);
    m_installationLayer->setToolTip(
        QStringLiteral("锚杆从该开挖级开始参与计算。界面按第 1、2、3…级填写；洞室求解时会自动转换为 LAYER=-1、-2、-3…。"));

    auto* materialLabel = new QLabel(QStringLiteral("材料"), stageMaterialRow);
    m_materialStatus = new QLabel(stageMaterialRow);
    m_materialStatus->setToolTip(QStringLiteral("对应求解器锚杆材料号；材料参数在材料模块中设置。"));

    stageMaterialLayout->addWidget(stageLabel);
    stageMaterialLayout->addWidget(m_installationLayer);
    stageMaterialLayout->addSpacing(6);
    stageMaterialLayout->addWidget(materialLabel);
    stageMaterialLayout->addWidget(m_materialStatus);
    stageMaterialLayout->addStretch(1);
    form->addRow(stageMaterialRow);

    m_manualArrayPanel = new QWidget(propertyBox);
    auto* manualArrayForm = new QFormLayout(m_manualArrayPanel);
    manualArrayForm->setContentsMargins(0, 0, 0, 0);
    manualArrayForm->setHorizontalSpacing(8);
    manualArrayForm->setVerticalSpacing(6);
    m_arrayMode = new QComboBox(m_manualArrayPanel);
    m_arrayMode->addItem(QStringLiteral("单根"), 0);
    m_arrayMode->addItem(QStringLiteral("阵列"), 1);
    m_arrayMode->setMaximumWidth(150);
    manualArrayForm->addRow(QStringLiteral("排列方式"), m_arrayMode);

    m_arrayRow = new QWidget(m_manualArrayPanel);
    auto* arrayLayout = new QFormLayout(m_arrayRow);
    arrayLayout->setContentsMargins(0, 0, 0, 0);
    arrayLayout->setHorizontalSpacing(8);
    arrayLayout->setVerticalSpacing(6);
    m_arrayCount = new QSpinBox(m_arrayRow);
    m_arrayCount->setRange(2, 10000);
    m_arrayCount->setValue(2);
    m_arrayCount->setMaximumWidth(150);
    m_arraySpacing = new QDoubleSpinBox(m_arrayRow);
    m_arraySpacing->setRange(0.0, 1.0e12);
    m_arraySpacing->setDecimals(8);
    m_arraySpacing->setValue(1.0);
    m_arraySpacing->setMinimumWidth(112);
    m_arraySpacing->setMaximumWidth(150);
    m_arrayAxis = new QComboBox(m_arrayRow);
    m_arrayAxis->addItem(QStringLiteral("X 方向"), 0);
    m_arrayAxis->addItem(QStringLiteral("Y 方向"), 1);
    m_arrayAxis->addItem(QStringLiteral("Z 方向"), 2);
    m_arrayAxis->setMaximumWidth(150);
    arrayLayout->addRow(QStringLiteral("数量"), m_arrayCount);
    arrayLayout->addRow(QStringLiteral("间距"), m_arraySpacing);
    arrayLayout->addRow(QStringLiteral("偏移方向"), m_arrayAxis);
    manualArrayForm->addRow(m_arrayRow);
    form->addRow(m_manualArrayPanel);

    connect(m_arrayMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateArrayUi(); });
    connect(m_arrayAxis, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int)
            {
                if (m_arrayAxis->currentData().toInt() != kLegacyDirectionData)
                    m_legacyArrayDirection = {{0.0, 0.0, 0.0}};
            });
    connect(m_arrayCount, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { updatePreview(); });
    connect(m_arraySpacing, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updatePreview(); });
    connect(m_arrayAxis, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updatePreview(); });

    m_createButton = new QPushButton(QStringLiteral("生成锚杆"), propertyBox);
    m_createButton->setMaximumWidth(104);
    auto* createRow = new QWidget(propertyBox);
    auto* createLayout = new QHBoxLayout(createRow);
    createLayout->setContentsMargins(0, 0, 0, 0);
    createLayout->addStretch(1);
    createLayout->addWidget(m_createButton);
    form->addRow(createRow);
    m_actionStatus = new QLabel(propertyBox);
    m_actionStatus->setWordWrap(true);
    form->addRow(QString(), m_actionStatus);
    connect(m_createButton, &QPushButton::clicked, this, &BarElementPlacementWidget::createBars);
    root->addWidget(propertyBox);

    auto* recordBox = new QGroupBox(QStringLiteral("已生成锚杆"), this);
    auto* recordLayout = new QVBoxLayout(recordBox);
    m_records = new QTableWidget(0, 2, recordBox);
    m_records->setHorizontalHeaderLabels(
        {QStringLiteral("锚杆数"), QStringLiteral("操作")});
    m_records->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_records->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_records->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_records->setSelectionMode(QAbstractItemView::SingleSelection);
    m_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_records->setMinimumHeight(96);
    m_records->setMaximumHeight(132);
    m_records->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    recordLayout->addWidget(m_records);
    root->addWidget(recordBox);
    root->addStretch(1);

    updateDefinitionUi();
    updateArrayUi();
    updateMaterialStatus();
    updatePlacementUi();
    refreshData();
}

QDoubleSpinBox* BarElementPlacementWidget::coordinateSpin(double value)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(-1.0e12, 1.0e12);
    spin->setDecimals(8);
    spin->setValue(value);
    spin->setMinimumWidth(112);
    spin->setMaximumWidth(150);
    spin->setKeyboardTracking(false);
    return spin;
}

QDoubleSpinBox* BarElementPlacementWidget::directionSpin(double value)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(-1.0e9, 1.0e9);
    spin->setDecimals(8);
    spin->setValue(value);
    spin->setMinimumWidth(112);
    spin->setMaximumWidth(150);
    spin->setKeyboardTracking(false);
    return spin;
}

BarElementPlacementWidget::PlacementMode BarElementPlacementWidget::placementMode() const
{
    return static_cast<PlacementMode>(m_placementMode->currentData().toInt());
}

BarElementPlacementWidget::DefinitionMode BarElementPlacementWidget::definitionMode() const
{
    return static_cast<DefinitionMode>(m_definitionMode->currentData().toInt());
}

bool BarElementPlacementWidget::isSurfaceSemiAuto() const
{
    return placementMode() == PlacementMode::SurfaceSemiAuto;
}

void BarElementPlacementWidget::beforeLoad()
{
    m_pageActive = true;
    m_pickTarget = PickTarget::None;

    initializeMaterialNumber();
    refreshData();
    updateDefinitionUi();
    ensurePreviewActors();
    updatePlacementUi();

    if (isSurfaceSemiAuto())
        activateSurfacePick();
}

void BarElementPlacementWidget::beforeUnload()
{
    if (m_manualPickActive)
        finishCoordinatePick();

    if (m_surfacePreviewTimer)
        m_surfacePreviewTimer->stop();

    // 先退出页面状态，再清理选择，避免 clearSelection 触发新的预览任务。
    m_pageActive = false;
    if (m_surfacePickActive)
        PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Face);
    m_surfaceLayoutSeedFaces.clear();
    deactivateSurfacePick();

    m_pickTarget = PickTarget::None;
    removePreviewActors();
}

void BarElementPlacementWidget::activateSurfacePick()
{
    if (!m_pageActive || !isSurfaceSemiAuto() || m_surfacePickActive)
        return;

    PickManager& picker = PickManager::instance();
    picker.activate(getPageType(), m_uuid, PickMode::Face);
    picker.setClickPickEnabled(getPageType(), m_uuid, true);
    picker.setBoxSelectEnabled(getPageType(), m_uuid, false);
    m_surfacePickActive = true;
}

void BarElementPlacementWidget::deactivateSurfacePick()
{
    if (!m_surfacePickActive)
        return;

    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
    m_surfacePickActive = false;
}

void BarElementPlacementWidget::handlePlacementModeChanged()
{
    if (m_manualPickActive)
        finishCoordinatePick();

    if (m_pageActive)
    {
        if (isSurfaceSemiAuto())
        {
            activateSurfacePick();
        }
        else
        {
            if (m_surfacePreviewTimer)
                m_surfacePreviewTimer->stop();
            if (m_surfacePickActive)
                PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Face);
            m_surfaceLayoutSeedFaces.clear();
            deactivateSurfacePick();
        }
    }

    updatePlacementUi();
}

void BarElementPlacementWidget::updatePlacementUi()
{
    const bool semi = isSurfaceSemiAuto();
    if (m_manualPanel)
        m_manualPanel->setVisible(!semi);
    if (m_surfacePanel)
        m_surfacePanel->setVisible(semi);
    if (m_manualArrayPanel)
        m_manualArrayPanel->setVisible(!semi);
    if (m_createButton)
        m_createButton->setText(semi ? QStringLiteral("生成锚杆")
                                     : QStringLiteral("添加锚杆"));
    if (m_actionStatus)
        m_actionStatus->clear();

    updateSurfacePickStatus();
    if (semi)
        scheduleSurfacePreview();
    else
        updatePreview();
}

void BarElementPlacementWidget::updateSurfacePickStatus()
{
    if (!m_surfaceStatus)
        return;

    const int selectedCount =
        (m_pageActive && isSurfaceSemiAuto())
            ? PickManager::instance()
                  .getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid)
                  .size()
            : 0;

    if (m_clearSurfaceButton)
        m_clearSurfaceButton->setEnabled(selectedCount > 0);
    if (m_createButton && isSurfaceSemiAuto())
    {
        m_createButton->setEnabled(false);
        m_createButton->setText(QStringLiteral("生成锚杆"));
    }

    if (selectedCount > 0)
    {
        m_surfaceStatus->setText(
            QStringLiteral("已选 %1 个网格面 · 正在更新预览……")
                .arg(selectedCount));
    }
    else
    {
        m_surfaceStatus->setText(
            QStringLiteral("在模型中单击一个开挖面。选中后会立即显示锚杆预览。"));
    }
}

void BarElementPlacementWidget::applyWholeSurfacePick(vtkIdType seedFaceKey, bool selected)
{
    if (!m_pageActive || !isSurfaceSemiAuto() || !m_surfacePickActive ||
        !m_surfaceWholeFace || !m_surfaceWholeFace->isChecked())
        return;

    PickManager& picker = PickManager::instance();
    const QVector<vtkIdType> candidates = picker.connectedVisibleFacePatch(seedFaceKey, 8.0);
    if (candidates.size() <= 1)
        return;

    // “整片”只表示同一个连续平面，不能越过块体的棱边跑到相邻侧面。
    // PickManager 仍只负责给出候选面；Bar 页面在这里按 seed 面的真实几何平面再严格过滤一次。
    vtkUnstructuredGrid* grid = picker.solidGrid();
    if (!grid || !grid->GetPoints())
        return;

    auto faceGeometry = [&](vtkIdType faceKey,
                            std::array<double, 3>& center,
                            std::array<double, 3>& normal) -> bool
    {
        const vtkIdType cellId = picker.unpackFaceCellId(faceKey);
        const int localFaceId = picker.unpackFaceLocalId(faceKey);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            return false;
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 3 ||
            localFaceId < 0 || localFaceId >= cell->GetNumberOfFaces())
            return false;
        vtkCell* face = cell->GetFace(localFaceId);
        if (!face || face->GetNumberOfPoints() < 3)
            return false;

        center = {{0.0, 0.0, 0.0}};
        const vtkIdType npts = face->GetNumberOfPoints();
        QVector<std::array<double, 3>> pts;
        pts.reserve(int(npts));
        for (vtkIdType i = 0; i < npts; ++i)
        {
            double q[3]{};
            grid->GetPoint(face->GetPointId(i), q);
            pts.push_back({{q[0], q[1], q[2]}});
            center[0] += q[0] / double(npts);
            center[1] += q[1] / double(npts);
            center[2] += q[2] / double(npts);
        }

        normal = {{0.0, 0.0, 0.0}};
        for (int i = 0; i < pts.size(); ++i)
        {
            const auto& a = pts[i];
            const auto& b = pts[(i + 1) % pts.size()];
            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
        }
        const double n = std::sqrt(normal[0] * normal[0]
                                   + normal[1] * normal[1]
                                   + normal[2] * normal[2]);
        if (n <= kDirectionEpsilon)
            return false;
        normal[0] /= n;
        normal[1] /= n;
        normal[2] /= n;
        return true;
    };

    std::array<double, 3> seedCenter{};
    std::array<double, 3> seedNormal{};
    if (!faceGeometry(seedFaceKey, seedCenter, seedNormal))
        return;

    double bounds[6]{};
    grid->GetBounds(bounds);
    const double diagonal = std::sqrt(
        (bounds[1] - bounds[0]) * (bounds[1] - bounds[0])
        + (bounds[3] - bounds[2]) * (bounds[3] - bounds[2])
        + (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));
    const double planeTolerance = qMax(1.0e-8, diagonal * 1.0e-7);
    const double cosTolerance = std::cos(3.0 * 3.14159265358979323846 / 180.0);

    QVector<vtkIdType> patch;
    patch.reserve(candidates.size());
    for (vtkIdType key : candidates)
    {
        std::array<double, 3> center{};
        std::array<double, 3> normal{};
        if (!faceGeometry(key, center, normal))
            continue;

        const double ndot = std::abs(seedNormal[0] * normal[0]
                                     + seedNormal[1] * normal[1]
                                     + seedNormal[2] * normal[2]);
        if (ndot < cosTolerance)
            continue;

        const double dx = center[0] - seedCenter[0];
        const double dy = center[1] - seedCenter[1];
        const double dz = center[2] - seedCenter[2];
        const double planeDistance = std::abs(seedNormal[0] * dx
                                              + seedNormal[1] * dy
                                              + seedNormal[2] * dz);
        if (planeDistance > planeTolerance)
            continue;
        patch.push_back(key);
    }

    if (patch.size() <= 1)
        return;

    if (selected)
    {
        // 这次点击的面就是该区域的布置基准格。若同一区域已有旧 seed，先替换掉。
        QSet<vtkIdType> patchKeys;
        for (vtkIdType key : patch)
            patchKeys.insert(key);
        for (int i = m_surfaceLayoutSeedFaces.size() - 1; i >= 0; --i)
        {
            if (patchKeys.contains(m_surfaceLayoutSeedFaces[i]))
                m_surfaceLayoutSeedFaces.removeAt(i);
        }
        m_surfaceLayoutSeedFaces.push_back(seedFaceKey);

        // 保留之前已经选好的其它区域，把当前同一平面整片追加进去。
        picker.setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, patch, false);
    }
    else
    {
        // 用户在已选区域上再次单击：取消这一整片，同时删除该区域的基准格。
        QSet<vtkIdType> removeKeys;
        for (vtkIdType key : patch)
            removeKeys.insert(key);
        for (int i = m_surfaceLayoutSeedFaces.size() - 1; i >= 0; --i)
        {
            if (removeKeys.contains(m_surfaceLayoutSeedFaces[i]))
                m_surfaceLayoutSeedFaces.removeAt(i);
        }

        const QVector<vtkIdType> current = picker.getIdList(
            getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
        QVector<vtkIdType> remaining;
        remaining.reserve(current.size());
        for (vtkIdType key : current)
        {
            if (!removeKeys.contains(key))
                remaining.push_back(key);
        }
        picker.setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, remaining, true);
    }
}

void BarElementPlacementWidget::scheduleSurfacePreview()
{
    if (!m_pageActive || !isSurfaceSemiAuto() || !m_surfacePreviewTimer)
        return;
    m_surfacePreviewTimer->start();
}

void BarElementPlacementWidget::clearSurfaceSelection()
{
    m_surfaceLayoutSeedFaces.clear();
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Face);
    if (m_surfacePreviewTimer)
        m_surfacePreviewTimer->stop();
    updateSurfacePickStatus();
    updatePreview();
}

QVector<BarElementPlacementWidget::PreviewBar>
BarElementPlacementWidget::buildSurfaceBars(QString* errMsg, int* selectedFaceCount,
                                             int* selectedRegionCount)
{
    if (errMsg)
        errMsg->clear();
    if (selectedFaceCount)
        *selectedFaceCount = 0;
    if (selectedRegionCount)
        *selectedRegionCount = 0;

    QVector<PreviewBar> bars;
    m_surfaceGuideValid = false;
    PickManager& picker = PickManager::instance();
    vtkUnstructuredGrid* grid = picker.solidGrid();
    if (!grid || !grid->GetPoints())
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前没有可用于区域布置的实体网格。");
        return bars;
    }

    QVector<vtkIdType> faceKeys = picker.getIdList(
        getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    std::sort(faceKeys.begin(), faceKeys.end());
    if (selectedFaceCount)
        *selectedFaceCount = faceKeys.size();
    if (faceKeys.isEmpty())
        return bars;

    const double length = m_surfaceLength ? m_surfaceLength->value() : 0.0;
    const int intervalU = m_surfaceGridIntervalU ? m_surfaceGridIntervalU->value() : 1;
    const int intervalV = m_surfaceGridIntervalV ? m_surfaceGridIntervalV->value() : 1;
    if (length <= kDirectionEpsilon)
    {
        if (errMsg)
            *errMsg = QStringLiteral("锚杆长度必须大于 0。");
        return bars;
    }

    struct FaceInfo
    {
        vtkIdType faceKey = -1;
        vtkIdType cellId = -1;
        int faceLocalId = -1;
        double p[4][3]{};
        std::array<double, 3> center{{0.0, 0.0, 0.0}};
        std::array<double, 3> inward{{0.0, 0.0, 0.0}};
        double areaWeight = 1.0;
    };

    QVector<FaceInfo> faces;
    faces.reserve(faceKeys.size());

    for (vtkIdType faceKey : faceKeys)
    {
        const vtkIdType cellId = picker.unpackFaceCellId(faceKey);
        const int faceLocalId = picker.unpackFaceLocalId(faceKey);
        if (cellId < 0 || cellId >= grid->GetNumberOfCells())
            continue;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || cell->GetCellDimension() != 3 ||
            cell->GetCellType() != VTK_HEXAHEDRON || cell->GetNumberOfPoints() != 8)
        {
            if (errMsg)
            {
                *errMsg = QStringLiteral(
                    "所选区域包含非 8 节点六面体实体单元。当前求解器的锚杆 INOUSE 耦合只支持 8 节点六面体宿主，请重新选择。");
            }
            return {};
        }

        if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
            continue;
        vtkCell* face = cell->GetFace(faceLocalId);
        if (!face || face->GetNumberOfPoints() != 4)
            continue;

        FaceInfo info;
        info.faceKey = faceKey;
        info.cellId = cellId;
        info.faceLocalId = faceLocalId;

        for (int i = 0; i < 4; ++i)
        {
            grid->GetPoint(face->GetPointId(i), info.p[i]);
            for (int axis = 0; axis < 3; ++axis)
                info.center[axis] += info.p[i][axis] * 0.25;
        }

        double cellCenter[3]{};
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
        {
            double q[3]{};
            grid->GetPoint(cell->GetPointId(i), q);
            for (int axis = 0; axis < 3; ++axis)
                cellCenter[axis] += q[axis] / cell->GetNumberOfPoints();
        }

        const std::array<double, 3> e1{{info.p[1][0] - info.p[0][0],
                                        info.p[1][1] - info.p[0][1],
                                        info.p[1][2] - info.p[0][2]}};
        const std::array<double, 3> e2{{info.p[3][0] - info.p[0][0],
                                        info.p[3][1] - info.p[0][1],
                                        info.p[3][2] - info.p[0][2]}};
        std::array<double, 3> rawNormal{{e1[1] * e2[2] - e1[2] * e2[1],
                                         e1[2] * e2[0] - e1[0] * e2[2],
                                         e1[0] * e2[1] - e1[1] * e2[0]}};
        const double rawNorm = std::sqrt(rawNormal[0] * rawNormal[0]
                                         + rawNormal[1] * rawNormal[1]
                                         + rawNormal[2] * rawNormal[2]);
        if (rawNorm <= kDirectionEpsilon)
            continue;

        info.inward = normalized(rawNormal);
        info.areaWeight = rawNorm;
        const std::array<double, 3> towardCell{{cellCenter[0] - info.center[0],
                                                cellCenter[1] - info.center[1],
                                                cellCenter[2] - info.center[2]}};
        const double directionDot = info.inward[0] * towardCell[0]
                                    + info.inward[1] * towardCell[1]
                                    + info.inward[2] * towardCell[2];
        if (directionDot < 0.0)
        {
            for (double& value : info.inward)
                value = -value;
        }
        faces.push_back(info);
    }

    if (faces.isEmpty())
    {
        if (errMsg)
            *errMsg = QStringLiteral("没有找到可用于区域布置的有效四边形表面。");
        return {};
    }

    // 只按共享边把选择结果分成区域。网格间隔只决定“隔几个面取一个”，
    // 不再建立独立点阵，也不再把点投影回表面。
    double gridBounds[6]{};
    grid->GetBounds(gridBounds);
    const double gridDiagonal = std::sqrt(
        (gridBounds[1] - gridBounds[0]) * (gridBounds[1] - gridBounds[0])
        + (gridBounds[3] - gridBounds[2]) * (gridBounds[3] - gridBounds[2])
        + (gridBounds[5] - gridBounds[4]) * (gridBounds[5] - gridBounds[4]));
    const double edgeTolerance = qMax(1.0e-8, gridDiagonal * 1.0e-10);
    const auto pointKey = [edgeTolerance](const double p[3])
    {
        return QStringLiteral("%1,%2,%3")
            .arg(qRound64(p[0] / edgeTolerance))
            .arg(qRound64(p[1] / edgeTolerance))
            .arg(qRound64(p[2] / edgeTolerance));
    };
    const auto edgeKey = [&pointKey](const double a[3], const double b[3])
    {
        QString first = pointKey(a);
        QString second = pointKey(b);
        if (second < first)
            std::swap(first, second);
        return first + QLatin1Char('|') + second;
    };

    QHash<QString, QVector<int>> edgeOwners;
    for (int faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
    {
        for (int edge = 0; edge < 4; ++edge)
        {
            edgeOwners[edgeKey(faces[faceIndex].p[edge],
                               faces[faceIndex].p[(edge + 1) % 4])]
                .push_back(faceIndex);
        }
    }

    QVector<QVector<int>> adjacency(faces.size());
    for (auto it = edgeOwners.constBegin(); it != edgeOwners.constEnd(); ++it)
    {
        const QVector<int>& owners = it.value();
        for (int i = 0; i < owners.size(); ++i)
        {
            for (int j = i + 1; j < owners.size(); ++j)
            {
                // 共享边还不够：90 度折角的两个侧面不能算成同一个布置区域。
                const FaceInfo& a = faces[owners[i]];
                const FaceInfo& b = faces[owners[j]];
                const double ndot = std::abs(a.inward[0] * b.inward[0]
                                             + a.inward[1] * b.inward[1]
                                             + a.inward[2] * b.inward[2]);
                constexpr double kRegionCosTolerance = 0.9961946980917455; // cos(5 deg)
                if (ndot < kRegionCosTolerance)
                    continue;
                adjacency[owners[i]].push_back(owners[j]);
                adjacency[owners[j]].push_back(owners[i]);
            }
        }
    }

    QVector<QVector<int>> regions;
    QVector<bool> visited(faces.size(), false);
    for (int seed = 0; seed < faces.size(); ++seed)
    {
        if (visited[seed])
            continue;
        QVector<int> region;
        QVector<int> stack{seed};
        visited[seed] = true;
        while (!stack.isEmpty())
        {
            const int current = stack.takeLast();
            region.push_back(current);
            for (int next : adjacency[current])
            {
                if (!visited[next])
                {
                    visited[next] = true;
                    stack.push_back(next);
                }
            }
        }
        regions.push_back(region);
    }
    if (selectedRegionCount)
        *selectedRegionCount = regions.size();

    const auto dot3 = [](const std::array<double, 3>& a,
                         const std::array<double, 3>& b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const auto buildLevels = [](QVector<double> values, double tolerance)
    {
        QVector<double> levels;
        if (values.isEmpty())
            return levels;
        std::sort(values.begin(), values.end());
        double sum = values.front();
        int count = 1;
        double center = values.front();
        for (int i = 1; i < values.size(); ++i)
        {
            if (std::abs(values[i] - center) <= tolerance)
            {
                sum += values[i];
                ++count;
                center = sum / count;
            }
            else
            {
                levels.push_back(center);
                sum = values[i];
                count = 1;
                center = values[i];
            }
        }
        levels.push_back(center);
        return levels;
    };
    const auto nearestLevel = [](double value, const QVector<double>& levels)
    {
        int best = 0;
        double bestDistance = std::numeric_limits<double>::max();
        for (int i = 0; i < levels.size(); ++i)
        {
            const double distance = std::abs(value - levels[i]);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    };

    for (const QVector<int>& region : regions)
    {
        if (region.isEmpty())
            continue;

        std::array<double, 3> regionNormal{{0.0, 0.0, 0.0}};
        for (int faceIndex : region)
        {
            const FaceInfo& face = faces[faceIndex];
            for (int axis = 0; axis < 3; ++axis)
                regionNormal[axis] += face.inward[axis] * face.areaWeight;
        }
        regionNormal = normalized(regionNormal);
        if (std::abs(regionNormal[0]) + std::abs(regionNormal[1])
                + std::abs(regionNormal[2]) <= kDirectionEpsilon)
        {
            if (errMsg)
                *errMsg = QStringLiteral("所选整片表面弯曲过大，请分成多个区域选择。");
            return {};
        }

        // 每个区域的两个网格方向直接来自“这次点击的 seed 面”的两组相邻边。
        // 不再用全局 Z 猜“横向/纵向”；用户在视图里直接看方向 1 / 方向 2。
        vtkIdType regionSeedFaceKey = -1;
        int regionSeedFaceIndex = -1;
        for (int seedIndex = m_surfaceLayoutSeedFaces.size() - 1; seedIndex >= 0; --seedIndex)
        {
            const vtkIdType seedFaceKey = m_surfaceLayoutSeedFaces[seedIndex];
            for (int faceIndex : region)
            {
                if (faces[faceIndex].faceKey == seedFaceKey)
                {
                    regionSeedFaceKey = seedFaceKey;
                    regionSeedFaceIndex = faceIndex;
                    break;
                }
            }
            if (regionSeedFaceIndex >= 0)
                break;
        }
        if (regionSeedFaceIndex < 0)
        {
            if (errMsg)
                *errMsg = QStringLiteral("当前区域缺少点击基准面，请清除选择后重新点选该区域。");
            return {};
        }

        const FaceInfo& seedFace = faces[regionSeedFaceIndex];
        std::array<double, 3> edgeA{{seedFace.p[1][0] - seedFace.p[0][0],
                                     seedFace.p[1][1] - seedFace.p[0][1],
                                     seedFace.p[1][2] - seedFace.p[0][2]}};
        std::array<double, 3> edgeB{{seedFace.p[3][0] - seedFace.p[0][0],
                                     seedFace.p[3][1] - seedFace.p[0][1],
                                     seedFace.p[3][2] - seedFace.p[0][2]}};
        edgeA = normalized(edgeA);
        edgeB = normalized(edgeB);
        if (std::abs(edgeA[0]) + std::abs(edgeA[1]) + std::abs(edgeA[2]) <= kDirectionEpsilon ||
            std::abs(edgeB[0]) + std::abs(edgeB[1]) + std::abs(edgeB[2]) <= kDirectionEpsilon)
        {
            if (errMsg)
                *errMsg = QStringLiteral("无法识别点击网格面的两个网格方向。");
            return {};
        }

        // 为了让同一片面上不同点击位置的“方向 1/2”尽量稳定：
        // 有明显竖向分量时，方向 2 取更接近全局 Z 的那组边；水平面则方向 1 取更接近全局 X 的那组边。
        const std::array<double, 3> globalZ{{0.0, 0.0, 1.0}};
        const std::array<double, 3> globalX{{1.0, 0.0, 0.0}};
        std::array<double, 3> horizontal{}; // UI：方向 1
        std::array<double, 3> vertical{};   // UI：方向 2
        const double zA = std::abs(dot3(edgeA, globalZ));
        const double zB = std::abs(dot3(edgeB, globalZ));
        if (qMax(zA, zB) > 0.25)
        {
            if (zA >= zB)
            {
                vertical = edgeA;
                horizontal = edgeB;
            }
            else
            {
                vertical = edgeB;
                horizontal = edgeA;
            }
        }
        else
        {
            const double xA = std::abs(dot3(edgeA, globalX));
            const double xB = std::abs(dot3(edgeB, globalX));
            if (xA >= xB)
            {
                horizontal = edgeA;
                vertical = edgeB;
            }
            else
            {
                horizontal = edgeB;
                vertical = edgeA;
            }
        }

        QVector<double> uValues;
        QVector<double> vValues;
        uValues.reserve(region.size());
        vValues.reserve(region.size());
        double avgSpanU = 0.0;
        double avgSpanV = 0.0;

        struct IndexedFace
        {
            int faceIndex = -1;
            double u = 0.0;
            double v = 0.0;
        };
        QVector<IndexedFace> indexedFaces;
        indexedFaces.reserve(region.size());

        for (int faceIndex : region)
        {
            const FaceInfo& face = faces[faceIndex];
            const std::array<double, 3> c = face.center;
            const double u = dot3(c, horizontal);
            const double v = dot3(c, vertical);
            uValues.push_back(u);
            vValues.push_back(v);

            double minU = std::numeric_limits<double>::max();
            double maxU = -std::numeric_limits<double>::max();
            double minV = std::numeric_limits<double>::max();
            double maxV = -std::numeric_limits<double>::max();
            for (int p = 0; p < 4; ++p)
            {
                const std::array<double, 3> q{{face.p[p][0], face.p[p][1], face.p[p][2]}};
                const double pu = dot3(q, horizontal);
                const double pv = dot3(q, vertical);
                minU = qMin(minU, pu);
                maxU = qMax(maxU, pu);
                minV = qMin(minV, pv);
                maxV = qMax(maxV, pv);
            }
            avgSpanU += qMax(0.0, maxU - minU);
            avgSpanV += qMax(0.0, maxV - minV);
            indexedFaces.push_back(IndexedFace{faceIndex, u, v});
        }

        const double regionFaceCount = static_cast<double>(region.size());
        avgSpanU /= regionFaceCount;
        avgSpanV /= regionFaceCount;
        const double toleranceU = qMax(gridDiagonal * 1.0e-9, avgSpanU * 0.25);
        const double toleranceV = qMax(gridDiagonal * 1.0e-9, avgSpanV * 0.25);
        const QVector<double> columns = buildLevels(uValues, toleranceU);
        const QVector<double> rows = buildLevels(vValues, toleranceV);

        // 网格间隔从本区域这次实际点击的 seed 面起算。
        int seedColumn = -1;
        int seedRow = -1;
        for (const IndexedFace& indexed : indexedFaces)
        {
            if (faces[indexed.faceIndex].faceKey != regionSeedFaceKey)
                continue;
            seedColumn = nearestLevel(indexed.u, columns);
            seedRow = nearestLevel(indexed.v, rows);
            break;
        }
        if (seedColumn < 0 || seedRow < 0)
        {
            if (errMsg)
                *errMsg = QStringLiteral("无法定位本次点击的基准网格面。");
            return {};
        }

        if (!m_surfaceLayoutSeedFaces.isEmpty() &&
            regionSeedFaceKey == m_surfaceLayoutSeedFaces.last())
        {
            m_surfaceGuideValid = true;
            m_surfaceGuideOrigin = seedFace.center;
            m_surfaceGuideNormal = seedFace.inward;
            m_surfaceGuideU = horizontal;
            m_surfaceGuideV = vertical;
            m_surfaceGuideScale = qMax(avgSpanU, avgSpanV) * 2.4;
        }

        for (const IndexedFace& indexed : indexedFaces)
        {
            const int column = nearestLevel(indexed.u, columns);
            const int row = nearestLevel(indexed.v, rows);
            const int deltaColumn = std::abs(column - seedColumn);
            const int deltaRow = std::abs(row - seedRow);
            if (deltaColumn % intervalU != 0 || deltaRow % intervalV != 0)
                continue;

            const FaceInfo& face = faces[indexed.faceIndex];
            PreviewBar bar;
            bar.start = face.center;

            // 区域布置的业务语义：所选面属于开挖体。face.inward 指向该面所属
            // Hex（开挖侧）内部，因此默认锚杆方向取反，背离开挖体、朝围岩侧。
            // “反向”仅用于用户明确需要相反施工方向时。
            const bool reverseDirection = m_surfaceDirectionMode &&
                                          m_surfaceDirectionMode->currentData().toInt() == 1;
            for (int axis = 0; axis < 3; ++axis)
                bar.direction[axis] = reverseDirection ? face.inward[axis] : -face.inward[axis];
            for (int axis = 0; axis < 3; ++axis)
                bar.end[axis] = bar.start[axis] + length * bar.direction[axis];
            bars.push_back(bar);

            if (bars.size() > kMaxSemiAutoBars)
            {
                if (errMsg)
                    *errMsg = QStringLiteral("当前区域将生成超过 %1 根锚杆，请增大横向或纵向网格间隔。")
                                  .arg(kMaxSemiAutoBars);
                return {};
            }
        }
    }

    return bars;
}

void BarElementPlacementWidget::beginCoordinatePick(PickTarget target)
{
    if (isSurfaceSemiAuto() || !m_pageActive)
        return;

    if (m_manualPickActive)
        finishCoordinatePick();

    PickManager& picker = PickManager::instance();
    picker.activate(getPageType(), m_uuid, PickMode::Node);
    picker.setBoxSelectEnabled(getPageType(), m_uuid, false);
    picker.setClickPickEnabled(getPageType(), m_uuid, true);
    picker.clearSelection(getPageType(), m_uuid, PickMode::Node);
    m_manualPickActive = true;

    m_pickTarget = target;
    m_pickStartButton->setEnabled(target != PickTarget::Start);
    m_pickEndButton->setEnabled(target != PickTarget::End);
    m_pickDirectionButton->setEnabled(target != PickTarget::DirectionPoint);
    m_pickStartButton->setText(target == PickTarget::Start ? QStringLiteral("点击模型…")
                                                           : QStringLiteral("取点"));
    m_pickEndButton->setText(target == PickTarget::End ? QStringLiteral("点击模型…")
                                                       : QStringLiteral("取点"));
    m_pickDirectionButton->setText(target == PickTarget::DirectionPoint
                                       ? QStringLiteral("点击模型…")
                                       : QStringLiteral("从模型取方向"));

    if (target == PickTarget::Start)
        m_pickStatus->setText(QStringLiteral("正在取锚头：请点击一个实体网格节点。"));
    else if (target == PickTarget::End)
        m_pickStatus->setText(QStringLiteral("正在取锚固端：请点击一个实体网格节点。"));
    else
        m_pickStatus->setText(QStringLiteral("正在取方向：请点击一个节点，方向为“锚头 → 该节点”。"));
}

void BarElementPlacementWidget::finishCoordinatePick()
{
    if (m_manualPickActive)
    {
        PickManager& picker = PickManager::instance();
        picker.clearSelection(getPageType(), m_uuid, PickMode::Node);
        picker.deactivate(getPageType(), m_uuid, PickMode::Node);
        m_manualPickActive = false;
    }

    m_pickTarget = PickTarget::None;
    if (m_pickStartButton)
    {
        m_pickStartButton->setEnabled(true);
        m_pickStartButton->setText(QStringLiteral("取点"));
    }
    if (m_pickEndButton)
    {
        m_pickEndButton->setEnabled(true);
        m_pickEndButton->setText(QStringLiteral("取点"));
    }
    if (m_pickDirectionButton)
    {
        m_pickDirectionButton->setEnabled(true);
        m_pickDirectionButton->setText(QStringLiteral("从模型取方向"));
    }
}

void BarElementPlacementWidget::applyPickedCoordinate(int nodeId,
                                                       double x, double y, double z)
{
    if (m_pickTarget == PickTarget::None)
        return;

    const PickTarget target = m_pickTarget;
    const double xyz[3] = {x, y, z};
    if (target == PickTarget::DirectionPoint)
    {
        const double dx = x - m_start[0]->value();
        const double dy = y - m_start[1]->value();
        const double dz = z - m_start[2]->value();
        const double norm = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (norm <= kDirectionEpsilon)
        {
            finishCoordinatePick();
            m_pickStatus->setText(QStringLiteral("方向点不能与锚头位置重合。"));
            return;
        }
        const double direction[3] = {dx / norm, dy / norm, dz / norm};
        for (int axis = 0; axis < 3; ++axis)
            m_direction[axis]->setValue(direction[axis]);
        updateEndFromLengthDirection(false);
    }
    else
    {
        QDoubleSpinBox** coordinate = target == PickTarget::Start ? m_start : m_end;
        for (int axis = 0; axis < 3; ++axis)
            coordinate[axis]->setValue(xyz[axis]);
        if (target == PickTarget::Start &&
            definitionMode() == DefinitionMode::HeadLengthDirection)
        {
            updateEndFromLengthDirection(false);
        }
    }

    finishCoordinatePick();

    if (target == PickTarget::Start)
    {
        m_pickStatus->setText(definitionMode() == DefinitionMode::TwoPoints
                                  ? QStringLiteral("锚头已取（节点 %1）。下一步取锚固端。").arg(nodeId)
                                  : QStringLiteral("锚头已取（节点 %1）。继续设置长度与方向。").arg(nodeId));
    }
    else if (target == PickTarget::End)
    {
        m_pickStatus->setText(QStringLiteral("锚固端已取（节点 %1）。预览已更新，可直接添加。").arg(nodeId));
    }
    else
    {
        m_pickStatus->setText(QStringLiteral("方向已更新（节点 %1）。预览已更新。").arg(nodeId));
    }
    updateLengthLabel();
}

void BarElementPlacementWidget::updateDefinitionUi()
{
    if (m_pickTarget != PickTarget::None)
        finishCoordinatePick();
    const bool byDirection = definitionMode() == DefinitionMode::HeadLengthDirection;
    m_lengthDirectionPanel->setVisible(byDirection);
    m_pickEndButton->setVisible(!byDirection);
    m_endRowLabel->setText(byDirection ? QStringLiteral("锚固端（计算）")
                                      : QStringLiteral("锚固端位置"));
    for (auto* spin : m_end)
        spin->setEnabled(!byDirection);

    if (byDirection)
        updateEndFromLengthDirection(false);
    updateLengthLabel();
    m_pickStatus->setText(byDirection
        ? QStringLiteral("先确定锚头，再设置长度和方向；模型会实时预览锚固端。")
        : QStringLiteral("推荐分别点击两个“取点”按钮确定锚头和锚固端，也可直接输入坐标。"));
}

bool BarElementPlacementWidget::updateEndFromLengthDirection(bool showWarning)
{
    if (!m_length || !m_direction[0])
        return false;

    const double vector[3] = {m_direction[0]->value(),
                              m_direction[1]->value(),
                              m_direction[2]->value()};
    const double norm = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1]
                                  + vector[2] * vector[2]);
    if (norm <= kDirectionEpsilon)
    {
        if (showWarning)
            QMessageBox::warning(this, QStringLiteral("锚杆方向"), QStringLiteral("方向向量不能为零。"));
        if (m_lengthLabel)
            m_lengthLabel->setText(QStringLiteral("当前方向无效"));
        return false;
    }

    const double length = m_length->value();
    if (length <= kDirectionEpsilon)
    {
        if (showWarning)
            QMessageBox::warning(this, QStringLiteral("锚杆长度"), QStringLiteral("锚杆长度必须大于 0。"));
        return false;
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        const QSignalBlocker blocker(m_end[axis]);
        m_end[axis]->setValue(m_start[axis]->value() + length * vector[axis] / norm);
    }
    updateLengthLabel();
    return true;
}

void BarElementPlacementWidget::updateLengthLabel()
{
    if (!m_lengthLabel || !m_start[0] || !m_end[0])
        return;
    const double dx = m_end[0]->value() - m_start[0]->value();
    const double dy = m_end[1]->value() - m_start[1]->value();
    const double dz = m_end[2]->value() - m_start[2]->value();
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    m_lengthLabel->setText(QStringLiteral("当前长度：%1").arg(QString::number(length, 'g', 10)));
    updatePreview();
}

void BarElementPlacementWidget::updateArrayUi()
{
    if (!m_arrayMode || !m_arrayRow)
        return;
    m_arrayRow->setVisible(m_arrayMode->currentData().toInt() == 1);
    updatePreview();
}

std::array<double, 3> BarElementPlacementWidget::currentArrayOffset() const
{
    std::array<double, 3> offset{{0.0, 0.0, 0.0}};
    if (!m_arrayMode || m_arrayMode->currentData().toInt() == 0)
        return offset;

    const double spacing = m_arraySpacing->value();
    const int axis = m_arrayAxis->currentData().toInt();
    if (axis >= 0 && axis <= 2)
    {
        offset[axis] = spacing;
        return offset;
    }

    if (axis == kLegacyDirectionData)
    {
        for (int i = 0; i < 3; ++i)
            offset[i] = m_legacyArrayDirection[i] * spacing;
    }
    return offset;
}

void BarElementPlacementWidget::loadLegacyArrayOffset(const QJsonArray& values)
{
    std::array<double, 3> offset{{0.0, 0.0, 0.0}};
    for (int axis = 0; axis < 3 && axis < values.size(); ++axis)
        offset[axis] = values.at(axis).toDouble();

    const double length = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1]
                                    + offset[2] * offset[2]);
    if (length <= 1.0e-12)
    {
        m_arraySpacing->setValue(1.0);
        m_arrayAxis->setCurrentIndex(0);
        m_legacyArrayDirection = {{0.0, 0.0, 0.0}};
        return;
    }

    m_arraySpacing->setValue(length);
    int nonZero = 0;
    int exactAxis = -1;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(offset[axis]) > 1.0e-12)
        {
            ++nonZero;
            exactAxis = axis;
        }
    }

    if (nonZero == 1 && offset[exactAxis] > 0.0)
    {
        m_arrayAxis->setCurrentIndex(exactAxis);
        m_legacyArrayDirection = {{0.0, 0.0, 0.0}};
        return;
    }

    m_legacyArrayDirection = {{offset[0] / length, offset[1] / length, offset[2] / length}};
    if (m_arrayAxis->findData(kLegacyDirectionData) < 0)
        m_arrayAxis->addItem(QStringLiteral("原工程偏移方向"), kLegacyDirectionData);
    m_arrayAxis->setCurrentIndex(m_arrayAxis->findData(kLegacyDirectionData));
}

void BarElementPlacementWidget::initializeMaterialNumber()
{
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
        return;

    const auto records = service->barElementRecords();
    for (const BarElementRecordInfo& record : records)
    {
        if (!record.placementUuid.trimmed().isEmpty() && record.placementUuid == m_uuid &&
            record.matype >= 3 && record.matype <= 300)
        {
            m_assignedMatype = record.matype;
            updateMaterialStatus();
            return;
        }
    }

    const QSet<int> usedByGrid = service->getAvailableMatypes();
    if (m_assignedMatype >= 3 && m_assignedMatype <= 300 &&
        !usedByGrid.contains(m_assignedMatype))
    {
        updateMaterialStatus();
        return;
    }

    QSet<int> unavailable = usedByGrid;
    if (auto* materialService = ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        const QMap<int, QString> configured = materialService->calculationMaterialLabels();
        for (auto it = configured.constBegin(); it != configured.constEnd(); ++it)
        {
            if (it.key() >= 3 && it.key() <= 300)
                unavailable.insert(it.key());
        }
    }

    m_assignedMatype = 0;
    for (int candidate = 3; candidate <= 300; ++candidate)
    {
        if (!unavailable.contains(candidate))
        {
            m_assignedMatype = candidate;
            break;
        }
    }
    updateMaterialStatus();
}

int BarElementPlacementWidget::resolveMaterialNumberForCreate()
{
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
        return 0;

    const auto records = service->barElementRecords();
    for (const BarElementRecordInfo& record : records)
    {
        if (!record.placementUuid.trimmed().isEmpty() && record.placementUuid == m_uuid &&
            record.matype >= 3 && record.matype <= 300)
        {
            m_assignedMatype = record.matype;
            return m_assignedMatype;
        }
    }

    const QSet<int> usedByGrid = service->getAvailableMatypes();
    if (m_assignedMatype >= 3 && m_assignedMatype <= 300 &&
        !usedByGrid.contains(m_assignedMatype))
    {
        return m_assignedMatype;
    }

    QSet<int> unavailable = usedByGrid;
    if (auto* materialService = ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        const QMap<int, QString> configured = materialService->calculationMaterialLabels();
        for (auto it = configured.constBegin(); it != configured.constEnd(); ++it)
        {
            if (it.key() >= 3 && it.key() <= 300)
                unavailable.insert(it.key());
        }
    }

    for (int candidate = 3; candidate <= 300; ++candidate)
    {
        if (!unavailable.contains(candidate))
        {
            m_assignedMatype = candidate;
            return candidate;
        }
    }
    return 0;
}

void BarElementPlacementWidget::updateMaterialStatus()
{
    if (!m_materialStatus)
        return;
    m_materialStatus->setStyleSheet(QString());
    m_materialStatus->setText(m_assignedMatype >= 3 && m_assignedMatype <= 300
                                  ? QString::number(m_assignedMatype)
                                  : QStringLiteral("待分配"));
}

QString BarElementPlacementWidget::previewPropId(const QString& suffix) const
{
    return QStringLiteral("mesh.bar-placement.%1.%2").arg(m_uuid, suffix);
}

void BarElementPlacementWidget::ensurePreviewActors()
{
    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visual || m_previewActor)
        return;

    m_previewPolyData = vtkSmartPointer<vtkPolyData>::New();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(m_previewPolyData);
    mapper->ScalarVisibilityOff();

    m_previewActor = vtkSmartPointer<vtkActor>::New();
    m_previewActor->SetMapper(mapper);
    // 预览只负责显示，绝不能参与任何节点/面/框选。
    m_previewActor->PickableOff();
    auto* prop = m_previewActor->GetProperty();
    prop->SetLineWidth(3.0);
    prop->SetPointSize(5.0);
    prop->RenderLinesAsTubesOn();
    prop->RenderPointsAsSpheresOn();
    prop->SetOpacity(1.0);
    const QColor color = barDisplayColor();
    prop->SetColor(color.redF(), color.greenF(), color.blueF());
    m_visual->addProp(previewPropId(QStringLiteral("geometry")), m_previewActor,
                      QStringLiteral("网格"), QStringLiteral("锚杆布置预览"), false, false);

    // 区域布置方向指示：只显示本次最后点击面的两个网格方向。
    m_surfaceGuidePolyData = vtkSmartPointer<vtkPolyData>::New();
    auto guideMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    guideMapper->SetInputData(m_surfaceGuidePolyData);
    guideMapper->ScalarVisibilityOff();
    m_surfaceGuideActor = vtkSmartPointer<vtkActor>::New();
    m_surfaceGuideActor->SetMapper(guideMapper);
    m_surfaceGuideActor->PickableOff();
    m_surfaceGuideActor->SetVisibility(false);
    m_surfaceGuideActor->GetProperty()->SetLineWidth(5.0);
    m_surfaceGuideActor->GetProperty()->SetColor(0.95, 0.82, 0.18);
    m_visual->addProp(previewPropId(QStringLiteral("surface-guide")), m_surfaceGuideActor,
                      QStringLiteral("网格"), QStringLiteral("锚杆布置方向"), false, false);

    m_surfaceULabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_surfaceULabelActor->PickableOff();
    m_surfaceULabelActor->SetVisibility(false);
    m_surfaceULabelActor->GetTextProperty()->SetFontSize(17);
    m_surfaceULabelActor->GetTextProperty()->SetBold(true);
    m_surfaceULabelActor->GetTextProperty()->SetColor(0.95, 0.82, 0.18);
    m_surfaceULabelActor->SetInput("1");
    m_visual->addProp(previewPropId(QStringLiteral("surface-guide-u")), m_surfaceULabelActor,
                      QStringLiteral("网格"), QStringLiteral("锚杆方向1"), false, false);

    m_surfaceVLabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_surfaceVLabelActor->PickableOff();
    m_surfaceVLabelActor->SetVisibility(false);
    m_surfaceVLabelActor->GetTextProperty()->SetFontSize(17);
    m_surfaceVLabelActor->GetTextProperty()->SetBold(true);
    m_surfaceVLabelActor->GetTextProperty()->SetColor(0.95, 0.82, 0.18);
    m_surfaceVLabelActor->SetInput("2");
    m_visual->addProp(previewPropId(QStringLiteral("surface-guide-v")), m_surfaceVLabelActor,
                      QStringLiteral("网格"), QStringLiteral("锚杆方向2"), false, false);

    m_startLabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_startLabelActor->PickableOff();
    m_startLabelActor->GetTextProperty()->SetFontSize(16);
    m_startLabelActor->GetTextProperty()->SetBold(false);
    m_startLabelActor->GetTextProperty()->SetBackgroundOpacity(0.55);
    m_startLabelActor->GetTextProperty()->SetBackgroundColor(0.12, 0.12, 0.12);
    m_startLabelActor->GetTextProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
    m_visual->addProp(previewPropId(QStringLiteral("start-label")), m_startLabelActor,
                      QStringLiteral("网格"), QStringLiteral("锚头坐标"), false, false);

    m_endLabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_endLabelActor->PickableOff();
    m_endLabelActor->GetTextProperty()->SetFontSize(15);
    m_endLabelActor->GetTextProperty()->SetBold(false);
    m_endLabelActor->GetTextProperty()->SetBackgroundOpacity(0.45);
    m_endLabelActor->GetTextProperty()->SetBackgroundColor(0.12, 0.12, 0.12);
    m_endLabelActor->GetTextProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
    m_visual->addProp(previewPropId(QStringLiteral("end-label")), m_endLabelActor,
                      QStringLiteral("网格"), QStringLiteral("锚固端坐标"), false, false);
}

void BarElementPlacementWidget::removePreviewActors()
{
    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (m_visual)
    {
        m_visual->removeProp(previewPropId(QStringLiteral("geometry")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("surface-guide")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("surface-guide-u")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("surface-guide-v")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("start-label")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("end-label")), false);
        m_visual->render();
    }
    m_previewPolyData = nullptr;
    m_previewActor = nullptr;
    m_surfaceGuidePolyData = nullptr;
    m_surfaceGuideActor = nullptr;
    m_surfaceULabelActor = nullptr;
    m_surfaceVLabelActor = nullptr;
    m_startLabelActor = nullptr;
    m_endLabelActor = nullptr;
}

void BarElementPlacementWidget::updatePreview()
{
    if (!m_previewActor || !m_previewPolyData)
        return;

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    auto verts = vtkSmartPointer<vtkCellArray>::New();

    if (m_surfaceGuideActor)
        m_surfaceGuideActor->SetVisibility(false);
    if (m_surfaceULabelActor)
        m_surfaceULabelActor->SetVisibility(false);
    if (m_surfaceVLabelActor)
        m_surfaceVLabelActor->SetVisibility(false);

    if (isSurfaceSemiAuto())
    {
        int selectedFaces = 0;
        int selectedRegions = 0;
        QString error;
        const QVector<PreviewBar> bars = buildSurfaceBars(&error, &selectedFaces, &selectedRegions);

        if (m_surfaceGuideActor && m_surfaceGuidePolyData &&
            m_surfaceULabelActor && m_surfaceVLabelActor)
        {
            const bool showGuide = error.isEmpty() && m_surfaceGuideValid && m_surfaceGuideScale > 0.0;
            m_surfaceGuideActor->SetVisibility(showGuide);
            m_surfaceULabelActor->SetVisibility(showGuide);
            m_surfaceVLabelActor->SetVisibility(showGuide);
            if (showGuide)
            {
                auto guidePoints = vtkSmartPointer<vtkPoints>::New();
                auto guideLines = vtkSmartPointer<vtkCellArray>::New();
                std::array<double, 3> origin = m_surfaceGuideOrigin;
                // 略微向表面外侧抬起，避免与网格线重合闪烁。
                for (int axis = 0; axis < 3; ++axis)
                    origin[axis] -= m_surfaceGuideNormal[axis] * m_surfaceGuideScale * 0.05;
                std::array<double, 3> endU = origin;
                std::array<double, 3> endV = origin;
                for (int axis = 0; axis < 3; ++axis)
                {
                    endU[axis] += m_surfaceGuideU[axis] * m_surfaceGuideScale;
                    endV[axis] += m_surfaceGuideV[axis] * m_surfaceGuideScale;
                }
                const vtkIdType p0 = guidePoints->InsertNextPoint(origin.data());
                const vtkIdType p1 = guidePoints->InsertNextPoint(endU.data());
                const vtkIdType p2 = guidePoints->InsertNextPoint(endV.data());
                vtkIdType uIds[2] = {p0, p1};
                vtkIdType vIds[2] = {p0, p2};
                guideLines->InsertNextCell(2, uIds);
                guideLines->InsertNextCell(2, vIds);
                m_surfaceGuidePolyData->SetPoints(guidePoints);
                m_surfaceGuidePolyData->SetLines(guideLines);
                m_surfaceGuidePolyData->Modified();
                m_surfaceULabelActor->SetPosition(endU.data());
                m_surfaceVLabelActor->SetPosition(endV.data());
            }
        }

        const bool canCreate = error.isEmpty() && selectedFaces > 0 && !bars.isEmpty();
        if (m_createButton)
        {
            m_createButton->setEnabled(canCreate);
            m_createButton->setText(canCreate
                                        ? QStringLiteral("生成 %1 根锚杆").arg(bars.size())
                                        : QStringLiteral("生成锚杆"));
        }
        if (m_clearSurfaceButton)
            m_clearSurfaceButton->setEnabled(selectedFaces > 0);

        if (m_surfaceStatus)
        {
            if (!error.isEmpty())
            {
                m_surfaceStatus->setText(error);
            }
            else if (selectedFaces <= 0)
            {
                m_surfaceStatus->setText(
                    QStringLiteral("在模型中单击一个开挖面。选中后会立即显示锚杆预览。"));
            }
            else
            {
                const int intervalU = m_surfaceGridIntervalU ? m_surfaceGridIntervalU->value() : 1;
                const int intervalV = m_surfaceGridIntervalV ? m_surfaceGridIntervalV->value() : 1;
                const QString directionText = (m_surfaceDirectionMode &&
                                               m_surfaceDirectionMode->currentData().toInt() == 1)
                                                  ? QStringLiteral("反向（朝开挖侧）")
                                                  : QStringLiteral("朝围岩侧");
                QString status = QStringLiteral("已选：%1 个区域 / %2 个网格面\n预计：%3 根锚杆    方向：%4\n间隔：方向 1 每 %5 格，方向 2 每 %6 格\n继续点击可追加；点击已选区域可取消。")
                                     .arg(selectedRegions).arg(selectedFaces)
                                     .arg(bars.size()).arg(directionText)
                                     .arg(intervalU).arg(intervalV);
                m_surfaceStatus->setText(status);
            }
        }

        // 区域布置最多允许 5000 根，全部预览。不能只显示“前 300 根”，
        // 否则 faceKey/遍历顺序会让预览看起来像随机缺一块。
        const int previewCount = bars.size();
        for (int i = 0; i < previewCount; ++i)
        {
            const PreviewBar& bar = bars.at(i);
            const vtkIdType p0 = points->InsertNextPoint(bar.start.data());
            const vtkIdType p1 = points->InsertNextPoint(bar.end.data());
            vtkIdType ids[2] = {p0, p1};
            lines->InsertNextCell(2, ids);
            // 区域预览同时显示锚头 P1 和锚固端 P2。
            verts->InsertNextCell(1, &ids[0]);
            verts->InsertNextCell(1, &ids[1]);
        }

        if (m_startLabelActor)
        {
            if (!bars.isEmpty())
            {
                const PreviewBar& first = bars.first();
                QString label = QStringLiteral("预览 %1 根").arg(bars.size());
                m_startLabelActor->SetInput(label.toUtf8().constData());
                m_startLabelActor->SetPosition(first.start[0], first.start[1], first.start[2]);
                m_startLabelActor->SetVisibility(1);
            }
            else
            {
                m_startLabelActor->SetVisibility(0);
            }
        }
        if (m_endLabelActor)
            m_endLabelActor->SetVisibility(0);
    }
    else
    {
        if (m_createButton)
            m_createButton->setEnabled(true);
        if (!m_start[0] || !m_end[0])
            return;
        const int requestedCount =
            (m_arrayMode && m_arrayMode->currentData().toInt() == 1 && m_arrayCount)
                ? m_arrayCount->value()
                : 1;
        const int previewCount = qMin(requestedCount, kPreviewBarLimit);
        const std::array<double, 3> offset = currentArrayOffset();

        const double dx0 = m_end[0]->value() - m_start[0]->value();
        const double dy0 = m_end[1]->value() - m_start[1]->value();
        const double dz0 = m_end[2]->value() - m_start[2]->value();
        const bool baseBarValid = std::sqrt(dx0 * dx0 + dy0 * dy0 + dz0 * dz0) > kDirectionEpsilon;
        const bool arrayValid = requestedCount <= 1 || !m_arraySpacing || m_arraySpacing->value() > kDirectionEpsilon;
        if (m_createButton)
        {
            m_createButton->setEnabled(baseBarValid && arrayValid);
            m_createButton->setText(requestedCount > 1
                                        ? QStringLiteral("添加 %1 根锚杆").arg(requestedCount)
                                        : QStringLiteral("添加锚杆"));
        }

        for (int copy = 0; copy < previewCount; ++copy)
        {
            double first[3]{};
            double second[3]{};
            for (int axis = 0; axis < 3; ++axis)
            {
                first[axis] = m_start[axis]->value() + copy * offset[axis];
                second[axis] = m_end[axis]->value() + copy * offset[axis];
            }
            const vtkIdType p0 = points->InsertNextPoint(first);
            const vtkIdType p1 = points->InsertNextPoint(second);
            vtkIdType ids[2] = {p0, p1};
            lines->InsertNextCell(2, ids);
            verts->InsertNextCell(1, &ids[0]);
            verts->InsertNextCell(1, &ids[1]);
        }

        if (m_startLabelActor)
        {
            const double x = m_start[0]->value();
            const double y = m_start[1]->value();
            const double z = m_start[2]->value();
            QString label = QStringLiteral("锚头  X=%1  Y=%2  Z=%3")
                                .arg(QString::number(x, 'g', 9),
                                     QString::number(y, 'g', 9),
                                     QString::number(z, 'g', 9));
            if (requestedCount > kPreviewBarLimit)
                label += QStringLiteral("  （预览前 %1 根）").arg(kPreviewBarLimit);
            m_startLabelActor->SetInput(label.toUtf8().constData());
            m_startLabelActor->SetPosition(x, y, z);
            m_startLabelActor->SetVisibility(1);
        }

        if (m_endLabelActor)
        {
            const double x = m_end[0]->value();
            const double y = m_end[1]->value();
            const double z = m_end[2]->value();
            const QString label = QStringLiteral("锚固端  X=%1  Y=%2  Z=%3")
                                      .arg(QString::number(x, 'g', 9),
                                           QString::number(y, 'g', 9),
                                           QString::number(z, 'g', 9));
            m_endLabelActor->SetInput(label.toUtf8().constData());
            m_endLabelActor->SetPosition(x, y, z);
            m_endLabelActor->SetVisibility(1);
        }
    }

    m_previewPolyData->SetPoints(points);
    m_previewPolyData->SetLines(lines);
    m_previewPolyData->SetVerts(verts);
    m_previewPolyData->Modified();

    if (m_visual)
        m_visual->render();
}

QJsonObject BarElementPlacementWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray start;
    QJsonArray end;
    QJsonArray direction;
    QJsonArray offset;
    for (int axis = 0; axis < 3; ++axis)
    {
        start.append(m_start[axis]->value());
        end.append(m_end[axis]->value());
        direction.append(m_direction[axis]->value());
    }
    const auto arrayOffset = currentArrayOffset();
    for (double value : arrayOffset)
        offset.append(value);

    obj[QStringLiteral("placement_mode")] = m_placementMode->currentData().toInt();
    obj[QStringLiteral("definition_mode")] = m_definitionMode->currentData().toInt();
    obj[QStringLiteral("start")] = start;
    obj[QStringLiteral("end")] = end;
    obj[QStringLiteral("length")] = m_length->value();
    obj[QStringLiteral("direction")] = direction;
    obj[QStringLiteral("surface_length")] = m_surfaceLength->value();
    obj[QStringLiteral("surface_grid_interval_u")] = m_surfaceGridIntervalU->value();
    obj[QStringLiteral("surface_grid_interval_v")] = m_surfaceGridIntervalV->value();
    obj[QStringLiteral("surface_whole_face")] = m_surfaceWholeFace->isChecked();
    obj[QStringLiteral("surface_direction_mode")] = m_surfaceDirectionMode->currentData().toInt();
    // installation_stage 是面向 UI 的正数级次。保留 installation_layer 兼容旧工程；
    // 二者在项目中均保存正数，洞室求解时再由 Base_Solver 转成负 LATYPE。
    obj[QStringLiteral("installation_stage")] = m_installationLayer->value();
    obj[QStringLiteral("installation_layer")] = m_installationLayer->value();
    obj[QStringLiteral("array_mode")] = m_arrayMode->currentData().toInt();
    obj[QStringLiteral("array_offset")] = offset;
    obj[QStringLiteral("array_spacing")] = m_arraySpacing->value();
    obj[QStringLiteral("array_axis")] = m_arrayAxis->currentData().toInt();
    obj[QStringLiteral("matype")] = m_assignedMatype;
    obj[QStringLiteral("array_count")] =
        m_arrayMode->currentData().toInt() == 1 ? m_arrayCount->value() : 1;
    return obj;
}

void BarElementPlacementWidget::loadJson(const QJsonObject& obj)
{
    const auto loadCoordinates = [](const QJsonArray& values, QDoubleSpinBox* spins[3])
    {
        for (int axis = 0; axis < 3 && axis < values.size(); ++axis)
            spins[axis]->setValue(values.at(axis).toDouble());
    };
    loadCoordinates(obj.value(QStringLiteral("start")).toArray(), m_start);
    loadCoordinates(obj.value(QStringLiteral("end")).toArray(), m_end);

    if (obj.contains(QStringLiteral("matype")))
    {
        const int storedMatype = obj.value(QStringLiteral("matype")).toInt(0);
        m_assignedMatype = (storedMatype >= 3 && storedMatype <= 300) ? storedMatype : 0;
    }
    else
    {
        m_assignedMatype = 0;
    }

    const int storedArrayCount = obj.value(QStringLiteral("array_count")).toInt(1);
    m_arrayCount->setValue(qMax(2, storedArrayCount));
    const int arrayMode = obj.contains(QStringLiteral("array_mode"))
                              ? obj.value(QStringLiteral("array_mode")).toInt(0)
                              : (storedArrayCount > 1 ? 1 : 0);
    const int arrayModeIndex = m_arrayMode->findData(arrayMode);
    if (arrayModeIndex >= 0)
        m_arrayMode->setCurrentIndex(arrayModeIndex);

    if (obj.contains(QStringLiteral("array_spacing")) &&
        obj.contains(QStringLiteral("array_axis")))
    {
        m_arraySpacing->setValue(obj.value(QStringLiteral("array_spacing")).toDouble(1.0));
        const int axis = obj.value(QStringLiteral("array_axis")).toInt(0);
        const int index = m_arrayAxis->findData(axis);
        if (index >= 0)
            m_arrayAxis->setCurrentIndex(index);
        else
            loadLegacyArrayOffset(obj.value(QStringLiteral("array_offset")).toArray());
    }
    else
    {
        loadLegacyArrayOffset(obj.value(QStringLiteral("array_offset")).toArray());
    }

    const int definition = obj.value(QStringLiteral("definition_mode"))
                               .toInt(static_cast<int>(DefinitionMode::TwoPoints));
    const int definitionIndex = m_definitionMode->findData(definition);
    if (definitionIndex >= 0)
        m_definitionMode->setCurrentIndex(definitionIndex);

    if (obj.contains(QStringLiteral("length")))
        m_length->setValue(obj.value(QStringLiteral("length")).toDouble(1.0));
    else
    {
        const double dx = m_end[0]->value() - m_start[0]->value();
        const double dy = m_end[1]->value() - m_start[1]->value();
        const double dz = m_end[2]->value() - m_start[2]->value();
        m_length->setValue(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    const QJsonArray storedDirection = obj.value(QStringLiteral("direction")).toArray();
    if (storedDirection.size() >= 3)
    {
        for (int axis = 0; axis < 3; ++axis)
            m_direction[axis]->setValue(storedDirection.at(axis).toDouble());
    }
    else
    {
        const double dx = m_end[0]->value() - m_start[0]->value();
        const double dy = m_end[1]->value() - m_start[1]->value();
        const double dz = m_end[2]->value() - m_start[2]->value();
        const double norm = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (norm > kDirectionEpsilon)
        {
            m_direction[0]->setValue(dx / norm);
            m_direction[1]->setValue(dy / norm);
            m_direction[2]->setValue(dz / norm);
        }
    }

    m_surfaceLength->setValue(obj.value(QStringLiteral("surface_length")).toDouble(m_length->value()));
    m_surfaceGridIntervalU->setValue(obj.value(QStringLiteral("surface_grid_interval_u")).toInt(1));
    m_surfaceGridIntervalV->setValue(obj.value(QStringLiteral("surface_grid_interval_v")).toInt(1));
    m_surfaceWholeFace->setChecked(obj.value(QStringLiteral("surface_whole_face")).toBool(true));
    const int surfaceDirection = obj.value(QStringLiteral("surface_direction_mode")).toInt(0);
    const int surfaceDirectionIndex = m_surfaceDirectionMode->findData(surfaceDirection);
    if (surfaceDirectionIndex >= 0)
        m_surfaceDirectionMode->setCurrentIndex(surfaceDirectionIndex);
    int installationStage = obj.value(QStringLiteral("installation_stage")).toInt(0);
    if (installationStage <= 0)
    {
        // 兼容旧工程：早期可能直接保存 LAYER（正数或负数）。界面统一显示为正数级次。
        installationStage = std::abs(obj.value(QStringLiteral("installation_layer")).toInt(1));
    }
    m_installationLayer->setValue(std::clamp(installationStage, 1, 999));
    const int placement = obj.value(QStringLiteral("placement_mode"))
                              .toInt(static_cast<int>(PlacementMode::Manual));
    const int placementIndex = m_placementMode->findData(placement);
    if (placementIndex >= 0)
        m_placementMode->setCurrentIndex(placementIndex);

    updateDefinitionUi();
    updateArrayUi();
    updateMaterialStatus();
    updatePlacementUi();
    refreshRecords();
}

void BarElementPlacementWidget::refreshData()
{
    initializeMaterialNumber();
    updateMaterialStatus();
    refreshRecords();
    updatePreview();
}

void BarElementPlacementWidget::refreshRecords()
{
    m_records->setRowCount(0);
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
        return;

    const auto records = service->barElementRecords();
    const bool hasScopedRecords = std::any_of(
        records.cbegin(), records.cend(),
        [](const BarElementRecordInfo& record)
        {
            return !record.placementUuid.trimmed().isEmpty();
        });

    bool foundMatype = false;
    for (const BarElementRecordInfo& record : records)
    {
        if (hasScopedRecords && record.placementUuid != m_uuid)
            continue;

        if (!foundMatype && record.matype >= 3 && record.matype <= 300)
        {
            m_assignedMatype = record.matype;
            foundMatype = true;
        }

        const int row = m_records->rowCount();
        m_records->insertRow(row);
        auto* countItem = new QTableWidgetItem(QString::number(record.cellCount));
        countItem->setData(Qt::UserRole, record.id);
        countItem->setTextAlignment(Qt::AlignCenter);
        m_records->setItem(row, 0, countItem);

        auto* removeButton = new QPushButton(QStringLiteral("删除"), m_records);
        m_records->setCellWidget(row, 1, removeButton);
        connect(removeButton, &QPushButton::clicked, this, [this, row]()
        {
            m_records->setCurrentCell(row, 0);
            removeSelectedRecord();
        });
    }

    updateMaterialStatus();
}

void BarElementPlacementWidget::createBars()
{
    finishCoordinatePick();

    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
    {
        QMessageBox::warning(this, QStringLiteral("添加锚杆失败"), QStringLiteral("无法访问当前网格。"));
        return;
    }

    const int matype = resolveMaterialNumberForCreate();
    if (matype < 3 || matype > 300)
    {
        QMessageBox::warning(this, QStringLiteral("锚杆材料编号"),
                             QStringLiteral("材料编号 3 至 300 已全部使用，无法为当前锚杆布置分配材料编号。"));
        return;
    }

    QString resultMessage;
    int barCount = 0;
    int barElementCount = 0;

    if (isSurfaceSemiAuto())
    {
        int selectedFaces = 0;
        int selectedRegions = 0;
        QString buildError;
        const QVector<PreviewBar> bars = buildSurfaceBars(&buildError, &selectedFaces, &selectedRegions);
        if (!buildError.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("区域布置"), buildError);
            return;
        }
        if (bars.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("区域布置"),
                                     QStringLiteral("请先点选需要布置锚杆的网格面。"));
            return;
        }

        QVector<BarElementPlacement> placements;
        placements.reserve(bars.size());
        for (const PreviewBar& bar : bars)
        {
            BarElementPlacement placement;
            placement.start = bar.start;
            placement.end = bar.end;
            placement.segmentCount = 1;
            placement.arrayCount = 1;
            placement.arrayOffset = {{0.0, 0.0, 0.0}};
            placement.installationLayer = m_installationLayer ? m_installationLayer->value() : 1;
            placement.matype = matype;
            placement.placementUuid = m_uuid;
            placements.push_back(placement);
        }

        if (!service->appendBarElementsBatch(placements, &resultMessage))
        {
            QMessageBox::warning(this, QStringLiteral("批量生成锚杆失败"),
                                 resultMessage.isEmpty() ? QStringLiteral("无法修改当前网格。")
                                                         : resultMessage);
            return;
        }
        barCount = placements.size();
        barElementCount = barCount;
    }
    else
    {
        if (definitionMode() == DefinitionMode::HeadLengthDirection &&
            !updateEndFromLengthDirection(true))
            return;

        BarElementPlacement placement;
        for (int axis = 0; axis < 3; ++axis)
        {
            placement.start[axis] = m_start[axis]->value();
            placement.end[axis] = m_end[axis]->value();
        }
        placement.segmentCount = 1;
        placement.installationLayer = m_installationLayer ? m_installationLayer->value() : 1;
        placement.matype = matype;
        placement.placementUuid = m_uuid;
        if (m_arrayMode->currentData().toInt() == 1)
        {
            placement.arrayCount = m_arrayCount->value();
            placement.arrayOffset = currentArrayOffset();
            if (m_arraySpacing->value() <= kDirectionEpsilon)
            {
                QMessageBox::warning(this, QStringLiteral("锚杆阵列"),
                                     QStringLiteral("阵列间距必须大于 0。"));
                return;
            }
        }
        else
        {
            placement.arrayCount = 1;
            placement.arrayOffset = {{0.0, 0.0, 0.0}};
        }

        const double dx = placement.end[0] - placement.start[0];
        const double dy = placement.end[1] - placement.start[1];
        const double dz = placement.end[2] - placement.start[2];
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= 1.0e-12)
        {
            QMessageBox::warning(this, QStringLiteral("锚杆位置"),
                                 QStringLiteral("锚头和锚固端不能重合。"));
            return;
        }

        if (!service->appendBarElements(placement, &resultMessage))
        {
            QMessageBox::warning(this, QStringLiteral("添加锚杆失败"),
                                 resultMessage.isEmpty() ? QStringLiteral("无法修改当前网格。")
                                                         : resultMessage);
            return;
        }
        barCount = placement.arrayCount;
        barElementCount = barCount;
    }

    auto& picker = PickManager::instance();
    if (isSurfaceSemiAuto())
    {
        if (m_surfacePreviewTimer)
            m_surfacePreviewTimer->stop();
        picker.clearSelection(getPageType(), m_uuid, PickMode::Face);
        m_surfaceLayoutSeedFaces.clear();
    }
    else
    {
        picker.clearSelection(getPageType(), m_uuid, PickMode::Node);
    }

    // 网格显示刷新由 MeshModuleController 统一同步 PickManager；当前 Face 会话保持不变。
    updatePlacementUi();
    refreshRecords();
    updateMaterialStatus();
    if (isSurfaceSemiAuto())
    {
        // 生成后立即清掉区域预览；不再额外排队一次延迟刷新。
        if (m_surfacePreviewTimer)
            m_surfacePreviewTimer->stop();
        updatePreview();
    }
    if (m_actionStatus)
    {
        QString status = QStringLiteral("生成完成：%1 根锚杆 · 材料 %2 · 第 %3 级安装")
                             .arg(barCount).arg(matype)
                             .arg(m_installationLayer ? m_installationLayer->value() : 1);
        if (!resultMessage.trimmed().isEmpty())
            status += QLatin1Char('\n') + resultMessage.trimmed();
        m_actionStatus->setText(status);
    }
}

void BarElementPlacementWidget::removeSelectedRecord()
{
    const int row = m_records->currentRow();
    if (row < 0 || !m_records->item(row, 0))
        return;

    const int recordId = m_records->item(row, 0)->data(Qt::UserRole).toInt();
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    QString error;
    if (!service || !service->removeBarElementRecord(recordId, &error))
    {
        QMessageBox::warning(this, QStringLiteral("删除锚杆失败"),
                             error.isEmpty() ? QStringLiteral("无法修改当前网格。") : error);
        return;
    }

    updatePlacementUi();
    refreshRecords();
    if (m_actionStatus)
        m_actionStatus->setText(QStringLiteral("选中的锚杆已删除。"));
}
