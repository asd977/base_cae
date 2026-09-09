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
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

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
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <QStringList>

namespace
{
constexpr char kPickDomain[] = "BarElementPlacement";
constexpr int kLegacyDirectionData = 3;
constexpr double kDirectionEpsilon = 1.0e-12;
constexpr int kPreviewBarLimit = 300;
constexpr int kMaxSemiAutoBars = 5000;
constexpr double kSurfaceGrowLocalAngleDeg = 50.0;

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
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* geometryBox = new QGroupBox(QStringLiteral("锚杆布置"), this);
    auto* geometryLayout = new QVBoxLayout(geometryBox);
    geometryLayout->setSpacing(10);

    auto* placementForm = new QFormLayout();
    m_placementMode = new QComboBox(geometryBox);
    m_placementMode->addItem(QStringLiteral("单根布置"),
                             static_cast<int>(PlacementMode::Manual));
    m_placementMode->addItem(QStringLiteral("区域布置"),
                             static_cast<int>(PlacementMode::SurfaceSemiAuto));
    placementForm->addRow(QStringLiteral("布置方式"), m_placementMode);
    geometryLayout->addLayout(placementForm);

    // ---------------- 手动布置 ----------------
    m_manualPanel = new QWidget(geometryBox);
    auto* manualLayout = new QVBoxLayout(m_manualPanel);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(10);

    auto* definitionForm = new QFormLayout();
    m_definitionMode = new QComboBox(m_manualPanel);
    m_definitionMode->addItem(QStringLiteral("位置 + 长度 + 方向"),
                              static_cast<int>(DefinitionMode::HeadLengthDirection));
    m_definitionMode->addItem(QStringLiteral("两点坐标"),
                              static_cast<int>(DefinitionMode::TwoPoints));
    definitionForm->addRow(QStringLiteral("定义方式"), m_definitionMode);
    manualLayout->addLayout(definitionForm);

    auto* coordinateGrid = new QGridLayout();
    coordinateGrid->setHorizontalSpacing(8);
    coordinateGrid->setVerticalSpacing(8);
    coordinateGrid->addWidget(new QLabel(QStringLiteral("坐标"), m_manualPanel), 0, 0);
    coordinateGrid->addWidget(new QLabel(QStringLiteral("X"), m_manualPanel), 0, 1);
    coordinateGrid->addWidget(new QLabel(QStringLiteral("Y"), m_manualPanel), 0, 2);
    coordinateGrid->addWidget(new QLabel(QStringLiteral("Z"), m_manualPanel), 0, 3);

    coordinateGrid->addWidget(new QLabel(QStringLiteral("锚头位置"), m_manualPanel), 1, 0);
    m_endRowLabel = new QLabel(QStringLiteral("锚固端（计算）"), m_manualPanel);
    coordinateGrid->addWidget(m_endRowLabel, 2, 0);

    for (int axis = 0; axis < 3; ++axis)
    {
        m_start[axis] = coordinateSpin();
        m_end[axis] = coordinateSpin(axis == 0 ? 1.0 : 0.0);
        coordinateGrid->addWidget(m_start[axis], 1, axis + 1);
        coordinateGrid->addWidget(m_end[axis], 2, axis + 1);

        connect(m_start[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double)
                {
                    if (definitionMode() == DefinitionMode::HeadLengthDirection)
                        updateEndFromLengthDirection(false);
                    updateLengthLabel();
                });
        connect(m_end[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { updateLengthLabel(); });
    }

    m_pickStartButton = new QPushButton(QStringLiteral("模型取点"), m_manualPanel);
    m_pickEndButton = new QPushButton(QStringLiteral("模型取点"), m_manualPanel);
    coordinateGrid->addWidget(m_pickStartButton, 1, 4);
    coordinateGrid->addWidget(m_pickEndButton, 2, 4);
    manualLayout->addLayout(coordinateGrid);

    m_lengthDirectionPanel = new QWidget(m_manualPanel);
    auto* directionForm = new QFormLayout(m_lengthDirectionPanel);
    directionForm->setContentsMargins(0, 0, 0, 0);
    m_length = new QDoubleSpinBox(m_lengthDirectionPanel);
    m_length->setRange(0.0, 1.0e12);
    m_length->setDecimals(8);
    m_length->setValue(1.0);
    directionForm->addRow(QStringLiteral("长度"), m_length);

    auto* directionRow = new QWidget(m_lengthDirectionPanel);
    auto* directionLayout = new QHBoxLayout(directionRow);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    directionLayout->setSpacing(6);
    const QStringList axisNames = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis)
    {
        directionLayout->addWidget(new QLabel(axisNames.at(axis), directionRow));
        m_direction[axis] = directionSpin(axis == 0 ? 1.0 : 0.0);
        directionLayout->addWidget(m_direction[axis]);
        connect(m_direction[axis], qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double)
                {
                    if (definitionMode() == DefinitionMode::HeadLengthDirection)
                        updateEndFromLengthDirection(false);
                });
    }
    m_pickDirectionButton = new QPushButton(QStringLiteral("模型取方向"), directionRow);
    directionLayout->addWidget(m_pickDirectionButton);
    directionForm->addRow(QStringLiteral("方向向量"), directionRow);
    manualLayout->addWidget(m_lengthDirectionPanel);

    m_lengthLabel = new QLabel(m_manualPanel);
    m_pickStatus = new QLabel(
        QStringLiteral("坐标可直接输入；模型取点只负责读取坐标。"), m_manualPanel);
    m_pickStatus->setWordWrap(true);
    manualLayout->addWidget(m_lengthLabel);
    manualLayout->addWidget(m_pickStatus);
    geometryLayout->addWidget(m_manualPanel);

    // ---------------- 区域半自动布置 ----------------
    m_surfacePanel = new QWidget(geometryBox);
    auto* surfaceLayout = new QVBoxLayout(m_surfacePanel);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(8);

    auto* semiHint = new QLabel(
        QStringLiteral("直接点一个当前可见表面，软件会自动扩展整片连续区域。洞室/开挖面请先在“显示控制”中隐藏开挖体，再点击暴露出来的内部表面。"),
        m_surfacePanel);
    semiHint->setWordWrap(true);
    surfaceLayout->addWidget(semiHint);

    auto* surfacePickRow = new QHBoxLayout();
    m_pickSurfaceButton = new QPushButton(QStringLiteral("选择区域"), m_surfacePanel);
    m_clearSurfaceButton = new QPushButton(QStringLiteral("清除区域"), m_surfacePanel);
    m_clearSurfaceButton->setEnabled(false);
    surfacePickRow->addWidget(m_pickSurfaceButton);
    surfacePickRow->addWidget(m_clearSurfaceButton);
    surfacePickRow->addStretch(1);
    surfaceLayout->addLayout(surfacePickRow);

    auto* surfaceForm = new QFormLayout();
    m_surfaceLength = new QDoubleSpinBox(m_surfacePanel);
    m_surfaceLength->setRange(1.0e-8, 1.0e12);
    m_surfaceLength->setDecimals(8);
    m_surfaceLength->setValue(1.0);
    surfaceForm->addRow(QStringLiteral("锚杆长度"), m_surfaceLength);

    m_surfaceSpacing = new QDoubleSpinBox(m_surfacePanel);
    m_surfaceSpacing->setRange(1.0e-8, 1.0e12);
    m_surfaceSpacing->setDecimals(8);
    m_surfaceSpacing->setValue(1.0);
    surfaceForm->addRow(QStringLiteral("布置间距"), m_surfaceSpacing);

    auto* directionLabel = new QLabel(QStringLiteral("自动朝围岩内部"), m_surfacePanel);
    surfaceForm->addRow(QStringLiteral("锚杆方向"), directionLabel);
    surfaceLayout->addLayout(surfaceForm);

    m_surfaceStatus = new QLabel(QStringLiteral("尚未选择区域。"), m_surfacePanel);
    m_surfaceStatus->setWordWrap(true);
    surfaceLayout->addWidget(m_surfaceStatus);
    geometryLayout->addWidget(m_surfacePanel);

    connect(m_placementMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updatePlacementUi(); });
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
    connect(m_surfaceLength, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updatePreview(); });
    connect(m_surfaceSpacing, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { updatePreview(); });
    connect(m_pickSurfaceButton, &QPushButton::clicked,
            this, &BarElementPlacementWidget::beginSurfaceRegionPick);
    connect(m_clearSurfaceButton, &QPushButton::clicked,
            this, &BarElementPlacementWidget::clearSurfaceSelection);

    connect(&PickManager::instance(), &PickManager::nodePicked, this,
            [this](const QString& domain, const QString& activeUuid, GridKind kind,
                   vtkIdType, int nodeId, double x, double y, double z,
                   const QString&)
            {
                if (!isSurfaceSemiAuto() && m_pickTarget != PickTarget::None &&
                    domain == QString::fromLatin1(kPickDomain) && activeUuid == m_uuid &&
                    kind == GridKind::Solid)
                {
                    applyPickedCoordinate(nodeId, x, y, z);
                }
            });
    // 区域布置只认本次真实点到的一个种子面，直接监听 facePicked。
    // 这样不再依赖 selectionChanged 后从选择集合反查，避免高亮重建时漏响应。
    connect(&PickManager::instance(), &PickManager::facePicked, this,
            [this](const QString& domain, const QString& activeUuid, GridKind kind,
                   vtkIdType faceKey)
            {
                if (!isSurfaceSemiAuto() || !m_surfacePickArmed ||
                    kind != GridKind::Solid || domain != QString::fromLatin1(kPickDomain) ||
                    activeUuid != m_uuid || m_surfaceSelectionUpdating)
                {
                    return;
                }
                applySurfaceSeedFace(faceKey);
            });

    connect(&PickManager::instance(), &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& activeUuid,
                   PickMode mode, GridKind kind)
            {
                if (!isSurfaceSemiAuto() || mode != PickMode::Face || kind != GridKind::Solid ||
                    domain != QString::fromLatin1(kPickDomain) || activeUuid != m_uuid ||
                    m_surfaceSelectionUpdating || m_surfacePickArmed)
                {
                    return;
                }
                updatePreview();
            });

    root->addWidget(geometryBox);

    auto* propertyBox = new QGroupBox(QStringLiteral("布置设置"), this);
    auto* form = new QFormLayout(propertyBox);

    m_materialStatus = new QLabel(propertyBox);
    m_materialStatus->setToolTip(QStringLiteral("该编号只用于标记本锚杆布置；添加后请在材料模块设置对应杆单元材料。"));
    form->addRow(QStringLiteral("材料编号"), m_materialStatus);

    m_manualArrayPanel = new QWidget(propertyBox);
    auto* manualArrayForm = new QFormLayout(m_manualArrayPanel);
    manualArrayForm->setContentsMargins(0, 0, 0, 0);
    m_arrayMode = new QComboBox(m_manualArrayPanel);
    m_arrayMode->addItem(QStringLiteral("单根"), 0);
    m_arrayMode->addItem(QStringLiteral("阵列"), 1);
    manualArrayForm->addRow(QStringLiteral("手动排列"), m_arrayMode);

    m_arrayRow = new QWidget(m_manualArrayPanel);
    auto* arrayLayout = new QHBoxLayout(m_arrayRow);
    arrayLayout->setContentsMargins(0, 0, 0, 0);
    arrayLayout->setSpacing(8);
    m_arrayCount = new QSpinBox(m_arrayRow);
    m_arrayCount->setRange(2, 10000);
    m_arrayCount->setValue(2);
    m_arraySpacing = new QDoubleSpinBox(m_arrayRow);
    m_arraySpacing->setRange(0.0, 1.0e12);
    m_arraySpacing->setDecimals(8);
    m_arraySpacing->setValue(1.0);
    m_arrayAxis = new QComboBox(m_arrayRow);
    m_arrayAxis->addItem(QStringLiteral("X 方向"), 0);
    m_arrayAxis->addItem(QStringLiteral("Y 方向"), 1);
    m_arrayAxis->addItem(QStringLiteral("Z 方向"), 2);
    arrayLayout->addWidget(new QLabel(QStringLiteral("数量"), m_arrayRow));
    arrayLayout->addWidget(m_arrayCount);
    arrayLayout->addWidget(new QLabel(QStringLiteral("间距"), m_arrayRow));
    arrayLayout->addWidget(m_arraySpacing);
    arrayLayout->addWidget(m_arrayAxis, 1);
    manualArrayForm->addRow(QStringLiteral("阵列参数"), m_arrayRow);
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

    m_createButton = new QPushButton(QStringLiteral("添加锚杆"), propertyBox);
    form->addRow(m_createButton);
    m_actionStatus = new QLabel(propertyBox);
    m_actionStatus->setWordWrap(true);
    form->addRow(QString(), m_actionStatus);
    connect(m_createButton, &QPushButton::clicked, this, &BarElementPlacementWidget::createBars);
    root->addWidget(propertyBox);

    auto* recordBox = new QGroupBox(QStringLiteral("当前布置已生成"), this);
    auto* recordLayout = new QVBoxLayout(recordBox);
    auto* recordHint = new QLabel(
        QStringLiteral("当前布置节点生成的杆单元使用同一材料编号。"), recordBox);
    recordHint->setWordWrap(true);
    recordLayout->addWidget(recordHint);

    m_records = new QTableWidget(0, 2, recordBox);
    m_records->setHorizontalHeaderLabels(
        {QStringLiteral("杆单元数"), QStringLiteral("操作")});
    m_records->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_records->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_records->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_records->setSelectionMode(QAbstractItemView::SingleSelection);
    m_records->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordLayout->addWidget(m_records);
    root->addWidget(recordBox, 1);

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
    return spin;
}

QDoubleSpinBox* BarElementPlacementWidget::directionSpin(double value)
{
    auto* spin = new QDoubleSpinBox(this);
    spin->setRange(-1.0e9, 1.0e9);
    spin->setDecimals(8);
    spin->setValue(value);
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
    m_surfacePickArmed = false;
    m_surfaceRegionFaceKeys.clear();

    // 先切到锚杆布置显示，再刷新 PickManager。首次进入页面时显示函数可能
    // 新建 mesh.imported.surface actor；如果先 updateStructure()，Picker 会抓到
    // 旧的/空的 prop，表现为“点模型没有反应”。
    if (auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        service->setBarPlacementDisplayMode(true);
    PickManager::instance().updateStructure();

    initializeMaterialNumber();
    refreshData();
    updateDefinitionUi();
    updatePlacementUi();

    ensurePreviewActors();
    updatePreview();
}

void BarElementPlacementWidget::beforeUnload()
{
    finishCoordinatePick();
    auto& picker = PickManager::instance();

    m_surfaceSelectionUpdating = true;
    picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
    m_surfaceSelectionUpdating = false;
    m_surfaceRegionFaceKeys.clear();
    m_surfacePickArmed = false;

    picker.deactivate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
    picker.deactivate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
    m_pageActive = false;
    removePreviewActors();
    if (auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        service->setBarPlacementDisplayMode(false);
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

    finishCoordinatePick();

    if (!semi)
    {
        m_surfacePickArmed = false;
        m_surfaceRegionFaceKeys.clear();
        if (m_pageActive)
        {
            auto& picker = PickManager::instance();
            m_surfaceSelectionUpdating = true;
            picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
            m_surfaceSelectionUpdating = false;
        }
    }
    else if (m_pageActive && m_surfaceRegionFaceKeys.isEmpty())
    {
        // 区域布置的主操作就是“点一个面”。进入该模式后直接进入点选状态，
        // 不再要求用户先点一次“选择区域”按钮。
        m_surfacePickArmed = true;
    }

    if (m_pickSurfaceButton)
    {
        m_pickSurfaceButton->setEnabled(!m_surfacePickArmed);
        m_pickSurfaceButton->setText(
            m_surfacePickArmed ? QStringLiteral("等待点选…")
                               : QStringLiteral("重新选择区域"));
    }
    if (m_clearSurfaceButton)
    {
        m_clearSurfaceButton->setEnabled(m_surfacePickArmed || !m_surfaceRegionFaceKeys.isEmpty());
        m_clearSurfaceButton->setText(m_surfacePickArmed ? QStringLiteral("停止选择")
                                                        : QStringLiteral("清除区域"));
    }
    if (semi && m_surfaceStatus && m_surfacePickArmed)
    {
        m_surfaceStatus->setToolTip(QString());
        m_surfaceStatus->setText(
            QStringLiteral("请直接点击当前可见表面；筛选后暴露出来的洞室/开挖内表面同样可以选择。"));
    }

    updatePickingMode();
    applyEditingSurfaceStyle();
    updatePreview();
}

void BarElementPlacementWidget::updatePickingMode()
{
    if (!m_pageActive)
        return;

    auto& picker = PickManager::instance();
    picker.deactivate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
    picker.deactivate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);

    if (isSurfaceSemiAuto())
    {
        // 重新绑定当前可见的 surface actor，避免 Picker 仍抓着网格刷新前的旧 prop。
        picker.updateStructure();
        picker.activate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
        picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid,
                                   m_surfacePickArmed);
        // 区域布置只需要一个种子面。关闭框选，避免用户又回到逐个选网格小面的操作。
        picker.setBoxSelectEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
    }
    else
    {
        picker.activate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
        picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
        picker.setBoxSelectEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
    }
}

void BarElementPlacementWidget::applyEditingSurfaceStyle()
{
    if (!m_pageActive)
        return;

    if (!m_visual)
        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visual)
        return;

    auto* actor = vtkActor::SafeDownCast(
        m_visual->getProp(QStringLiteral("mesh.imported.surface")));
    if (!actor)
        actor = vtkActor::SafeDownCast(m_visual->getProp(QStringLiteral("mesh.global.surf")));
    if (!actor || !actor->GetProperty())
        return;

    auto* prop = actor->GetProperty();
    prop->SetRepresentationToSurface();
    // 手动布置保留半透明；区域布置必须看清并直接点中网格面，因此保持不透明。
    prop->SetOpacity(isSurfaceSemiAuto() ? 1.0 : 0.30);
    prop->EdgeVisibilityOn();
    prop->SetEdgeColor(0.18, 0.20, 0.22);
    prop->SetLineWidth(1.0);
    actor->PickableOn();
    if (auto* mapper = actor->GetMapper())
        mapper->ScalarVisibilityOff();

    m_visual->render();
}

void BarElementPlacementWidget::beginSurfaceRegionPick()
{
    if (!m_pageActive || !isSurfaceSemiAuto())
        return;

    auto& picker = PickManager::instance();
    m_surfaceSelectionUpdating = true;
    picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
    m_surfaceSelectionUpdating = false;

    m_surfaceRegionFaceKeys.clear();
    m_surfacePickArmed = true;
    applyEditingSurfaceStyle();
    picker.updateStructure();
    picker.activate(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
    picker.setBoxSelectEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
    picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, true);

    if (m_pickSurfaceButton)
    {
        m_pickSurfaceButton->setEnabled(false);
        m_pickSurfaceButton->setText(QStringLiteral("等待点选…"));
    }
    if (m_clearSurfaceButton)
    {
        m_clearSurfaceButton->setEnabled(true);
        m_clearSurfaceButton->setText(QStringLiteral("停止选择"));
    }
    if (m_surfaceStatus)
    {
        m_surfaceStatus->setToolTip(QString());
        m_surfaceStatus->setText(
            QStringLiteral("请直接点击当前可见表面；筛选后暴露出来的洞室/开挖内表面同样可以选择。"));
    }
    updatePreview();
}

void BarElementPlacementWidget::handleSurfaceFaceSelection()
{
    if (!m_surfacePickArmed || m_surfaceSelectionUpdating)
        return;

    auto& picker = PickManager::instance();
    const QVector<vtkIdType> faceKeys = picker.getIdList(
        QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face, GridKind::Solid);
    if (faceKeys.isEmpty())
        return;

    // 框选已关闭，正常情况下这里只有刚刚点击的一个种子面。
    applySurfaceSeedFace(faceKeys.first());
}

void BarElementPlacementWidget::applySurfaceSeedFace(vtkIdType seedFaceKey)
{
    QString error;
    const QVector<vtkIdType> region = growSurfaceRegion(seedFaceKey, &error);
    auto& picker = PickManager::instance();

    if (region.isEmpty())
    {
        m_surfaceSelectionUpdating = true;
        picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
        m_surfaceSelectionUpdating = false;
        m_surfaceRegionFaceKeys.clear();

        // 保持点选状态，用户可以直接再点另一个面。先清空旧预览，再保留具体错误信息。
        updatePreview();
        if (m_surfaceStatus)
            m_surfaceStatus->setText(error.isEmpty()
                                         ? QStringLiteral("没有找到可用于布置锚杆的连续区域，请重新点选。")
                                         : error);
        return;
    }

    m_surfaceSelectionUpdating = true;
    picker.setSelectedFaceKeys(QString::fromLatin1(kPickDomain), m_uuid, GridKind::Solid,
                               region, true);
    m_surfaceSelectionUpdating = false;

    m_surfaceRegionFaceKeys.clear();
    for (vtkIdType key : region)
        m_surfaceRegionFaceKeys.insert(key);

    m_surfacePickArmed = false;
    picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);

    if (m_pickSurfaceButton)
    {
        m_pickSurfaceButton->setEnabled(true);
        m_pickSurfaceButton->setText(QStringLiteral("重新选择区域"));
    }
    if (m_clearSurfaceButton)
    {
        m_clearSurfaceButton->setEnabled(true);
        m_clearSurfaceButton->setText(QStringLiteral("清除区域"));
    }

    updatePreview();
}

void BarElementPlacementWidget::clearSurfaceSelection()
{
    auto& picker = PickManager::instance();
    const bool wasPicking = m_surfacePickArmed;

    m_surfaceSelectionUpdating = true;
    picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face);
    m_surfaceSelectionUpdating = false;
    m_surfaceRegionFaceKeys.clear();

    if (wasPicking)
    {
        // “停止选择”只退出当前点选状态，不马上再次自动进入，避免按钮看起来无效。
        m_surfacePickArmed = false;
        picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
        if (m_pickSurfaceButton)
        {
            m_pickSurfaceButton->setEnabled(true);
            m_pickSurfaceButton->setText(QStringLiteral("选择区域"));
        }
        if (m_clearSurfaceButton)
        {
            m_clearSurfaceButton->setEnabled(false);
            m_clearSurfaceButton->setText(QStringLiteral("清除区域"));
        }
        if (m_surfaceStatus)
        {
            m_surfaceStatus->setToolTip(QString());
            m_surfaceStatus->setText(QStringLiteral("已停止选择。点击“选择区域”可重新开始。"));
        }
    }
    else
    {
        // 已经有区域时，“清除区域”后直接等待新点选，便于快速换一片表面。
        m_surfacePickArmed = m_pageActive && isSurfaceSemiAuto();
        picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, m_surfacePickArmed);
        if (m_pickSurfaceButton)
        {
            m_pickSurfaceButton->setEnabled(!m_surfacePickArmed);
            m_pickSurfaceButton->setText(m_surfacePickArmed ? QStringLiteral("等待点选…")
                                                           : QStringLiteral("选择区域"));
        }
        if (m_clearSurfaceButton)
        {
            m_clearSurfaceButton->setEnabled(m_surfacePickArmed);
            m_clearSurfaceButton->setText(m_surfacePickArmed ? QStringLiteral("停止选择")
                                                            : QStringLiteral("清除区域"));
        }
        if (m_surfaceStatus)
        {
            m_surfaceStatus->setToolTip(QString());
            m_surfaceStatus->setText(m_surfacePickArmed
                                         ? QStringLiteral("请直接点击当前可见表面；筛选后暴露出来的洞室/开挖内表面同样可以选择。")
                                         : QStringLiteral("尚未选择区域。"));
        }
    }
    updatePreview();
}

QVector<vtkIdType> BarElementPlacementWidget::growSurfaceRegion(vtkIdType seedFaceKey,
                                                                 QString* errMsg) const
{
    if (errMsg)
        errMsg->clear();

    QVector<vtkIdType> region;
    auto& picker = PickManager::instance();
    vtkUnstructuredGrid* grid = picker.solidGrid();
    if (!grid || !grid->GetPoints())
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前没有可用于区域布置的实体网格。");
        return region;
    }

    if (!m_visual)
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前没有可用于区域布置的显示网格。");
        return region;
    }

    // 区域布置必须以“当前屏幕上真正可见的表面”为准，而不是以完整体网格的
    // 几何外边界为准。这样用户在显示控制里隐藏开挖体/隧洞材料后，暴露出来的
    // 洞壁虽然在完整网格中是内部材料界面，也能正常作为锚杆布置面。
    auto* surfaceActor = vtkActor::SafeDownCast(
        m_visual->getProp(QStringLiteral("mesh.imported.surface")));
    if (!surfaceActor)
        surfaceActor =
            vtkActor::SafeDownCast(m_visual->getProp(QStringLiteral("mesh.global.surf")));

    auto* surfaceMapper =
        surfaceActor ? vtkPolyDataMapper::SafeDownCast(surfaceActor->GetMapper()) : nullptr;
    vtkPolyData* visibleSurface = surfaceMapper ? surfaceMapper->GetInput() : nullptr;
    auto* visibleFaceKeys =
        visibleSurface && visibleSurface->GetCellData()
            ? visibleSurface->GetCellData()->GetArray("vtkOriginalFaceKeys")
            : nullptr;

    if (!visibleSurface || visibleSurface->GetNumberOfCells() <= 0 || !visibleFaceKeys)
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前可见网格表面没有有效的面映射，请刷新网格显示后重试。");
        return region;
    }

    grid->BuildLinks();

    // 将显示面的 faceKey 统一解析到真正的三维宿主单元面。
    // 对显式二维边界单元也做一次向三维 Hex 宿主的映射，保证后续生成杆件时
    // 使用的仍然是 INOUSE 所需要的实体宿主单元。
    const auto resolveHostFace =
        [grid, &picker](vtkIdType rawFaceKey,
                        vtkIdType& outCellId,
                        int& outFaceLocalId,
                        vtkIdType& outHostFaceKey) -> bool
    {
        outCellId = picker.unpackFaceCellId(rawFaceKey);
        outFaceLocalId = picker.unpackFaceLocalId(rawFaceKey);
        outHostFaceKey = rawFaceKey;

        if (outCellId < 0 || outCellId >= grid->GetNumberOfCells())
            return false;

        vtkCell* sourceCell = grid->GetCell(outCellId);
        if (!sourceCell)
            return false;

        if (sourceCell->GetCellDimension() == 3 && outFaceLocalId >= 0 &&
            outFaceLocalId < sourceCell->GetNumberOfFaces())
        {
            outHostFaceKey = picker.packFaceKey(outCellId, outFaceLocalId);
            return true;
        }

        if (sourceCell->GetCellDimension() != 2 && outFaceLocalId != 15)
            return false;

        vtkIdList* sourcePids = sourceCell->GetPointIds();
        if (!sourcePids || sourcePids->GetNumberOfIds() < 3)
            return false;

        auto neighbors = vtkSmartPointer<vtkIdList>::New();
        grid->GetCellNeighbors(outCellId, sourcePids, neighbors);

        for (vtkIdType i = 0; i < neighbors->GetNumberOfIds(); ++i)
        {
            const vtkIdType candidateCellId = neighbors->GetId(i);
            vtkCell* candidateCell = grid->GetCell(candidateCellId);
            if (!candidateCell || candidateCell->GetCellDimension() != 3)
                continue;

            for (int fi = 0; fi < candidateCell->GetNumberOfFaces(); ++fi)
            {
                vtkCell* candidateFace = candidateCell->GetFace(fi);
                if (!candidateFace || !candidateFace->GetPointIds() ||
                    candidateFace->GetPointIds()->GetNumberOfIds() !=
                        sourcePids->GetNumberOfIds())
                {
                    continue;
                }

                bool samePoints = true;
                for (vtkIdType p = 0; p < sourcePids->GetNumberOfIds(); ++p)
                {
                    const vtkIdType pid = sourcePids->GetId(p);
                    bool found = false;
                    for (vtkIdType q = 0;
                         q < candidateFace->GetPointIds()->GetNumberOfIds(); ++q)
                    {
                        if (candidateFace->GetPointIds()->GetId(q) == pid)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        samePoints = false;
                        break;
                    }
                }

                if (samePoints)
                {
                    outCellId = candidateCellId;
                    outFaceLocalId = fi;
                    outHostFaceKey = picker.packFaceKey(outCellId, outFaceLocalId);
                    return true;
                }
            }
        }

        return false;
    };

    vtkIdType seedCellId = -1;
    int seedFaceLocalId = -1;
    vtkIdType resolvedSeedFaceKey = -1;
    if (!resolveHostFace(seedFaceKey, seedCellId, seedFaceLocalId, resolvedSeedFaceKey))
    {
        if (errMsg)
            *errMsg = QStringLiteral("没有识别到有效的三维宿主表面，请重新点选。");
        return region;
    }

    vtkCell* seedCell = grid->GetCell(seedCellId);
    if (!seedCell || seedCell->GetCellDimension() != 3 ||
        seedCell->GetCellType() != VTK_HEXAHEDRON || seedCell->GetNumberOfPoints() != 8)
    {
        if (errMsg)
            *errMsg = QStringLiteral(
                "当前区域布置只支持 8 节点六面体宿主单元。");
        return region;
    }

    const auto inwardNormal = [grid](vtkIdType cellId, int faceLocalId,
                                     std::array<double, 3>& outNormal) -> bool
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell || faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
            return false;

        vtkCell* face = cell->GetFace(faceLocalId);
        if (!face || !face->GetPoints() || face->GetNumberOfPoints() < 3)
            return false;

        double p0[3]{}, p1[3]{}, p2[3]{};
        face->GetPoints()->GetPoint(0, p0);
        face->GetPoints()->GetPoint(1, p1);
        face->GetPoints()->GetPoint(2, p2);

        const std::array<double, 3> e1{{p1[0] - p0[0], p1[1] - p0[1],
                                        p1[2] - p0[2]}};
        const std::array<double, 3> e2{{p2[0] - p0[0], p2[1] - p0[1],
                                        p2[2] - p0[2]}};
        outNormal = normalized(
            {{e1[1] * e2[2] - e1[2] * e2[1],
              e1[2] * e2[0] - e1[0] * e2[2],
              e1[0] * e2[1] - e1[1] * e2[0]}});

        if (std::abs(outNormal[0]) + std::abs(outNormal[1]) + std::abs(outNormal[2])
            <= kDirectionEpsilon)
        {
            return false;
        }

        double faceCenter[3]{};
        for (vtkIdType i = 0; i < face->GetNumberOfPoints(); ++i)
        {
            double p[3]{};
            face->GetPoints()->GetPoint(i, p);
            for (int axis = 0; axis < 3; ++axis)
                faceCenter[axis] += p[axis] / face->GetNumberOfPoints();
        }

        double cellCenter[3]{};
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
        {
            double p[3]{};
            cell->GetPoints()->GetPoint(i, p);
            for (int axis = 0; axis < 3; ++axis)
                cellCenter[axis] += p[axis] / cell->GetNumberOfPoints();
        }

        const double towardCell[3] = {
            cellCenter[0] - faceCenter[0],
            cellCenter[1] - faceCenter[1],
            cellCenter[2] - faceCenter[2]};
        const double dot = outNormal[0] * towardCell[0]
                           + outNormal[1] * towardCell[1]
                           + outNormal[2] * towardCell[2];
        if (dot < 0.0)
        {
            outNormal[0] = -outNormal[0];
            outNormal[1] = -outNormal[1];
            outNormal[2] = -outNormal[2];
        }
        return true;
    };

    // 当前可见 surface 就是区域生长的边界集合。
    // 这比在完整体网格里调用 GetCellNeighbors 判断“几何外表面”更符合锚杆场景：
    // 隐藏开挖体后，岩体-开挖体的内部界面会成为可见表面，也应当允许布置锚杆。
    QSet<vtkIdType> visibleHostFaces;
    QHash<quint64, QVector<vtkIdType>> facesByEdge;

    const auto packEdge = [](vtkIdType a, vtkIdType b) -> quint64
    {
        quint64 ua = static_cast<quint64>(a);
        quint64 ub = static_cast<quint64>(b);
        if (ua > ub)
            std::swap(ua, ub);
        // 当前网格点数远小于 2^32；用紧凑 key 可避免为区域邻接建立大量 QString。
        return (ua << 32) | (ub & 0xffffffffULL);
    };

    const vtkIdType visibleCellCount = visibleSurface->GetNumberOfCells();
    for (vtkIdType displayCellId = 0; displayCellId < visibleCellCount; ++displayCellId)
    {
        if (displayCellId >= visibleFaceKeys->GetNumberOfTuples())
            break;

        const vtkIdType rawKey =
            static_cast<vtkIdType>(visibleFaceKeys->GetTuple1(displayCellId));

        vtkIdType hostCellId = -1;
        int hostFaceLocalId = -1;
        vtkIdType hostFaceKey = -1;
        if (!resolveHostFace(rawKey, hostCellId, hostFaceLocalId, hostFaceKey))
            continue;

        vtkCell* hostCell = grid->GetCell(hostCellId);
        if (!hostCell || hostCell->GetCellDimension() != 3 ||
            hostCell->GetCellType() != VTK_HEXAHEDRON ||
            hostCell->GetNumberOfPoints() != 8 ||
            hostFaceLocalId < 0 ||
            hostFaceLocalId >= hostCell->GetNumberOfFaces())
        {
            continue;
        }

        if (visibleHostFaces.contains(hostFaceKey))
            continue;

        vtkCell* hostFace = hostCell->GetFace(hostFaceLocalId);
        if (!hostFace)
            continue;

        visibleHostFaces.insert(hostFaceKey);

        for (int ei = 0; ei < hostFace->GetNumberOfEdges(); ++ei)
        {
            vtkCell* edge = hostFace->GetEdge(ei);
            if (!edge || !edge->GetPointIds() ||
                edge->GetPointIds()->GetNumberOfIds() < 2)
            {
                continue;
            }

            const vtkIdType a = edge->GetPointIds()->GetId(0);
            const vtkIdType b = edge->GetPointIds()->GetId(1);
            facesByEdge[packEdge(a, b)].push_back(hostFaceKey);
        }
    }

    if (!visibleHostFaces.contains(resolvedSeedFaceKey))
    {
        if (errMsg)
            *errMsg = QStringLiteral(
                "所点表面当前不可用于区域布置。请先在“显示控制”中把需要支护的开挖面显示出来，再直接点该面。");
        return region;
    }

    const double rad = 3.14159265358979323846 / 180.0;
    const double minLocalDot = std::cos(kSurfaceGrowLocalAngleDeg * rad);

    QVector<vtkIdType> queue;
    queue.push_back(resolvedSeedFaceKey);
    QSet<vtkIdType> accepted;
    accepted.insert(resolvedSeedFaceKey);

    int head = 0;
    while (head < queue.size())
    {
        const vtkIdType currentKey = queue.at(head++);
        region.push_back(currentKey);

        const vtkIdType currentCellId = picker.unpackFaceCellId(currentKey);
        const int currentFaceLocalId = picker.unpackFaceLocalId(currentKey);
        vtkCell* currentCell = grid->GetCell(currentCellId);
        if (!currentCell || currentFaceLocalId < 0 ||
            currentFaceLocalId >= currentCell->GetNumberOfFaces())
        {
            continue;
        }

        vtkCell* currentFace = currentCell->GetFace(currentFaceLocalId);
        if (!currentFace)
            continue;

        std::array<double, 3> currentNormal{};
        if (!inwardNormal(currentCellId, currentFaceLocalId, currentNormal))
            continue;

        for (int edgeIndex = 0; edgeIndex < currentFace->GetNumberOfEdges(); ++edgeIndex)
        {
            vtkCell* edge = currentFace->GetEdge(edgeIndex);
            if (!edge || !edge->GetPointIds() ||
                edge->GetPointIds()->GetNumberOfIds() < 2)
            {
                continue;
            }

            const quint64 edgeKey =
                packEdge(edge->GetPointIds()->GetId(0), edge->GetPointIds()->GetId(1));
            const QVector<vtkIdType> neighbors = facesByEdge.value(edgeKey);
            for (vtkIdType candidateKey : neighbors)
            {
                if (candidateKey == currentKey || accepted.contains(candidateKey))
                    continue;

                const vtkIdType candidateCellId = picker.unpackFaceCellId(candidateKey);
                const int candidateFaceLocalId = picker.unpackFaceLocalId(candidateKey);
                vtkCell* candidateCell = grid->GetCell(candidateCellId);
                if (!candidateCell || candidateCell->GetCellDimension() != 3 ||
                    candidateCell->GetCellType() != VTK_HEXAHEDRON ||
                    candidateCell->GetNumberOfPoints() != 8)
                {
                    continue;
                }

                std::array<double, 3> candidateNormal{};
                if (!inwardNormal(candidateCellId, candidateFaceLocalId, candidateNormal))
                    continue;

                const double localDot =
                    currentNormal[0] * candidateNormal[0]
                    + currentNormal[1] * candidateNormal[1]
                    + currentNormal[2] * candidateNormal[2];

                // 只根据相邻面的局部转角判断是否属于同一连续表面。
                // 不再限制“相对种子面的累计角度”，否则圆形洞室会被切成一条
                // 不规则的带状区域；连续圆弧应当沿洞壁自然扩展，明显棱角处再停止。
                if (localDot + 1.0e-12 < minLocalDot)
                    continue;

                accepted.insert(candidateKey);
                queue.push_back(candidateKey);
            }
        }
    }

    std::sort(region.begin(), region.end());
    return region;
}

QVector<BarElementPlacementWidget::PreviewBar>
BarElementPlacementWidget::buildSurfaceBars(QString* errMsg, int* selectedFaceCount) const
{
    if (errMsg)
        errMsg->clear();
    if (selectedFaceCount)
        *selectedFaceCount = 0;

    QVector<PreviewBar> bars;
    auto& picker = PickManager::instance();
    vtkUnstructuredGrid* grid = picker.solidGrid();
    if (!grid || !grid->GetPoints())
    {
        if (errMsg)
            *errMsg = QStringLiteral("当前没有可用于半自动布置的实体网格。");
        return bars;
    }

    QVector<vtkIdType> faceKeys = picker.getIdList(
        QString::fromLatin1(kPickDomain), m_uuid, PickMode::Face, GridKind::Solid);
    std::sort(faceKeys.begin(), faceKeys.end());
    if (selectedFaceCount)
        *selectedFaceCount = faceKeys.size();
    if (faceKeys.isEmpty())
        return bars;

    const double length = m_surfaceLength ? m_surfaceLength->value() : 0.0;
    const double spacing = m_surfaceSpacing ? m_surfaceSpacing->value() : 0.0;
    if (length <= kDirectionEpsilon || spacing <= kDirectionEpsilon)
    {
        if (errMsg)
            *errMsg = QStringLiteral("锚杆长度和布置间距必须大于 0。");
        return bars;
    }

    struct Candidate
    {
        PreviewBar bar;
    };
    QVector<Candidate> candidates;

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
                    "所选区域包含非 8 节点六面体的实体面。当前求解器 INOUSE 非共节点耦合在三维中使用 SN3D8N，半自动布置只对六面体宿主单元开放。");
            }
            return {};
        }

        if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
            continue;
        vtkCell* face = cell->GetFace(faceLocalId);
        if (!face || face->GetNumberOfPoints() != 4 || !face->GetPoints())
            continue;

        double p[4][3]{};
        double faceCenter[3]{};
        for (int i = 0; i < 4; ++i)
        {
            face->GetPoints()->GetPoint(i, p[i]);
            for (int axis = 0; axis < 3; ++axis)
                faceCenter[axis] += p[i][axis] * 0.25;
        }

        double cellCenter[3]{};
        for (vtkIdType i = 0; i < cell->GetNumberOfPoints(); ++i)
        {
            double q[3]{};
            cell->GetPoints()->GetPoint(i, q);
            for (int axis = 0; axis < 3; ++axis)
                cellCenter[axis] += q[axis] / cell->GetNumberOfPoints();
        }

        // 用面本身的法向作为锚杆方向，再通过单元中心判断正负，确保始终朝实体内部。
        const std::array<double, 3> e1{{p[1][0] - p[0][0],
                                        p[1][1] - p[0][1],
                                        p[1][2] - p[0][2]}};
        const std::array<double, 3> e2{{p[3][0] - p[0][0],
                                        p[3][1] - p[0][1],
                                        p[3][2] - p[0][2]}};
        std::array<double, 3> normal{{e1[1] * e2[2] - e1[2] * e2[1],
                                      e1[2] * e2[0] - e1[0] * e2[2],
                                      e1[0] * e2[1] - e1[1] * e2[0]}};
        normal = normalized(normal);
        if (std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]) <= kDirectionEpsilon)
            continue;
        const std::array<double, 3> towardCell{{cellCenter[0] - faceCenter[0],
                                                cellCenter[1] - faceCenter[1],
                                                cellCenter[2] - faceCenter[2]}};
        const double dot = normal[0] * towardCell[0] + normal[1] * towardCell[1]
                           + normal[2] * towardCell[2];
        if (dot < 0.0)
            for (double& value : normal)
                value = -value;

        const double uLength = 0.5 * (pointDistance(p[0], p[1]) + pointDistance(p[3], p[2]));
        const double vLength = 0.5 * (pointDistance(p[0], p[3]) + pointDistance(p[1], p[2]));
        const int nu = static_cast<int>(
            qMin(200.0, qMax(1.0, std::round(uLength / spacing))));
        const int nv = static_cast<int>(
            qMin(200.0, qMax(1.0, std::round(vLength / spacing))));

        for (int iu = 0; iu < nu; ++iu)
        {
            const double u = (iu + 0.5) / nu;
            for (int iv = 0; iv < nv; ++iv)
            {
                const double v = (iv + 0.5) / nv;
                PreviewBar bar;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double a = (1.0 - u) * p[0][axis] + u * p[1][axis];
                    const double b = (1.0 - u) * p[3][axis] + u * p[2][axis];
                    bar.start[axis] = (1.0 - v) * a + v * b;
                    bar.direction[axis] = normal[axis];
                    bar.end[axis] = bar.start[axis] + length * normal[axis];
                }
                candidates.push_back({bar});
                if (candidates.size() > kMaxSemiAutoBars * 4)
                {
                    if (errMsg)
                        *errMsg = QStringLiteral("当前区域按该间距会产生过多锚杆，请适当增大布置间距。");
                    return {};
                }
            }
        }
    }

    // 相邻网格面会在边界附近产生很近的候选点。只对法向大致一致的候选做最小间距去重，
    // 避免洞室相对两侧因空间距离接近而互相删掉。
    const double minDistance = spacing * 0.72;
    const double binSize = qMax(minDistance, 1.0e-9);
    const auto binCoordinate = [binSize](double value) -> qint64
    {
        return static_cast<qint64>(std::floor(value / binSize));
    };
    const auto binKey = [](qint64 x, qint64 y, qint64 z)
    {
        return QStringLiteral("%1:%2:%3").arg(x).arg(y).arg(z);
    };
    QHash<QString, QVector<int>> acceptedBins;

    for (const Candidate& candidate : candidates)
    {
        const qint64 bx = binCoordinate(candidate.bar.start[0]);
        const qint64 by = binCoordinate(candidate.bar.start[1]);
        const qint64 bz = binCoordinate(candidate.bar.start[2]);
        bool tooClose = false;
        for (qint64 ix = bx - 1; ix <= bx + 1 && !tooClose; ++ix)
        {
            for (qint64 iy = by - 1; iy <= by + 1 && !tooClose; ++iy)
            {
                for (qint64 iz = bz - 1; iz <= bz + 1 && !tooClose; ++iz)
                {
                    const auto it = acceptedBins.constFind(binKey(ix, iy, iz));
                    if (it == acceptedBins.constEnd())
                        continue;
                    for (int acceptedIndex : it.value())
                    {
                        const PreviewBar& existing = bars.at(acceptedIndex);
                        const double normalDot = candidate.bar.direction[0] * existing.direction[0]
                                                 + candidate.bar.direction[1] * existing.direction[1]
                                                 + candidate.bar.direction[2] * existing.direction[2];
                        if (normalDot < 0.5)
                            continue;
                        const double a[3] = {candidate.bar.start[0], candidate.bar.start[1],
                                             candidate.bar.start[2]};
                        const double b[3] = {existing.start[0], existing.start[1], existing.start[2]};
                        if (pointDistance(a, b) < minDistance)
                        {
                            tooClose = true;
                            break;
                        }
                    }
                }
            }
        }
        if (!tooClose)
        {
            const int acceptedIndex = bars.size();
            bars.push_back(candidate.bar);
            acceptedBins[binKey(bx, by, bz)].push_back(acceptedIndex);
        }
        if (bars.size() > kMaxSemiAutoBars)
        {
            if (errMsg)
                *errMsg = QStringLiteral("当前区域将生成超过 %1 根锚杆，请增大布置间距。")
                              .arg(kMaxSemiAutoBars);
            return {};
        }
    }

    return bars;
}

void BarElementPlacementWidget::beginCoordinatePick(PickTarget target)
{
    if (isSurfaceSemiAuto())
        return;
    m_pickTarget = target;
    auto& picker = PickManager::instance();
    picker.clearSelection(QString::fromLatin1(kPickDomain), m_uuid, PickMode::Node);
    picker.setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, true);

    m_pickStartButton->setEnabled(target != PickTarget::Start);
    m_pickEndButton->setEnabled(target != PickTarget::End);
    m_pickDirectionButton->setEnabled(target != PickTarget::DirectionPoint);

    if (target == PickTarget::Start)
        m_pickStatus->setText(QStringLiteral("请在模型中点击一个实体网格节点，坐标将写入锚头位置。"));
    else if (target == PickTarget::End)
        m_pickStatus->setText(QStringLiteral("请在模型中点击一个实体网格节点，坐标将写入锚固端位置。"));
    else
        m_pickStatus->setText(QStringLiteral("请点击一个节点，用锚头到该点的方向作为锚杆方向。"));
}

void BarElementPlacementWidget::finishCoordinatePick()
{
    if (m_pageActive && !isSurfaceSemiAuto())
        PickManager::instance().setClickPickEnabled(QString::fromLatin1(kPickDomain), m_uuid, false);
    m_pickTarget = PickTarget::None;
    if (m_pickStartButton)
        m_pickStartButton->setEnabled(true);
    if (m_pickEndButton)
        m_pickEndButton->setEnabled(true);
    if (m_pickDirectionButton)
        m_pickDirectionButton->setEnabled(true);
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
            PickManager::instance().clearSelection(QString::fromLatin1(kPickDomain), m_uuid,
                                                    PickMode::Node);
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
    PickManager::instance().clearSelection(QString::fromLatin1(kPickDomain), m_uuid,
                                            PickMode::Node);

    if (target == PickTarget::Start)
        m_pickStatus->setText(QStringLiteral("锚头位置已取节点 %1 的坐标。").arg(nodeId));
    else if (target == PickTarget::End)
        m_pickStatus->setText(QStringLiteral("锚固端位置已取节点 %1 的坐标。").arg(nodeId));
    else
        m_pickStatus->setText(QStringLiteral("方向已由节点 %1 确定。").arg(nodeId));
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
        ? QStringLiteral("输入锚头、长度和方向；也可从模型取锚头和方向。")
        : QStringLiteral("直接输入锚头与锚固端坐标，也可分别从模型取点。"));
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
    m_lengthLabel->setText(QStringLiteral("长度：%1").arg(QString::number(length, 'g', 10)));
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
    auto* prop = m_previewActor->GetProperty();
    prop->SetLineWidth(3.0);
    prop->SetPointSize(11.0);
    prop->RenderLinesAsTubesOn();
    prop->RenderPointsAsSpheresOn();
    prop->SetOpacity(1.0);
    const QColor color = barDisplayColor();
    prop->SetColor(color.redF(), color.greenF(), color.blueF());
    m_visual->addProp(previewPropId(QStringLiteral("geometry")), m_previewActor,
                      QStringLiteral("网格"), QStringLiteral("锚杆布置预览"), false, false);

    m_startLabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_startLabelActor->GetTextProperty()->SetFontSize(16);
    m_startLabelActor->GetTextProperty()->SetBold(false);
    m_startLabelActor->GetTextProperty()->SetBackgroundOpacity(0.55);
    m_startLabelActor->GetTextProperty()->SetBackgroundColor(0.12, 0.12, 0.12);
    m_startLabelActor->GetTextProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
    m_visual->addProp(previewPropId(QStringLiteral("start-label")), m_startLabelActor,
                      QStringLiteral("网格"), QStringLiteral("锚头坐标"), false, false);

    m_endLabelActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
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
        m_visual->removeProp(previewPropId(QStringLiteral("start-label")), false);
        m_visual->removeProp(previewPropId(QStringLiteral("end-label")), false);
        m_visual->render();
    }
    m_previewPolyData = nullptr;
    m_previewActor = nullptr;
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

    if (isSurfaceSemiAuto())
    {
        int selectedFaces = 0;
        QString error;
        const QVector<PreviewBar> bars = buildSurfaceBars(&error, &selectedFaces);
        if (m_surfaceStatus)
        {
            if (!error.isEmpty())
            {
                m_surfaceStatus->setToolTip(QString());
                m_surfaceStatus->setText(error);
            }
            else if (selectedFaces <= 0)
            {
                m_surfaceStatus->setToolTip(QString());
                m_surfaceStatus->setText(
                    m_surfacePickArmed
                        ? QStringLiteral("正在选择区域：请在模型上点击一个需要支护的表面，只需点一个面。")
                        : QStringLiteral("尚未选择区域。"));
            }
            else
            {
                m_surfaceStatus->setText(
                    QStringLiteral("已选择连续区域，预计生成 %1 根锚杆。").arg(bars.size()));
                m_surfaceStatus->setToolTip(
                    QStringLiteral("该区域底层包含 %1 个网格面；区域由所点击的种子面自动扩展得到。")
                        .arg(selectedFaces));
            }
        }

        const int previewCount = qMin(bars.size(), kPreviewBarLimit);
        for (int i = 0; i < previewCount; ++i)
        {
            const PreviewBar& bar = bars.at(i);
            const vtkIdType p0 = points->InsertNextPoint(bar.start.data());
            const vtkIdType p1 = points->InsertNextPoint(bar.end.data());
            vtkIdType ids[2] = {p0, p1};
            lines->InsertNextCell(2, ids);
            verts->InsertNextCell(1, &ids[0]);
        }

        if (m_startLabelActor)
        {
            if (!bars.isEmpty())
            {
                const PreviewBar& first = bars.first();
                QString label = QStringLiteral("区域预览：%1 根锚杆").arg(bars.size());
                if (bars.size() > kPreviewBarLimit)
                    label += QStringLiteral("（显示前 %1 根）").arg(kPreviewBarLimit);
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
        if (!m_start[0] || !m_end[0])
            return;
        const int requestedCount =
            (m_arrayMode && m_arrayMode->currentData().toInt() == 1 && m_arrayCount)
                ? m_arrayCount->value()
                : 1;
        const int previewCount = qMin(requestedCount, kPreviewBarLimit);
        const std::array<double, 3> offset = currentArrayOffset();

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
    obj[QStringLiteral("surface_spacing")] = m_surfaceSpacing->value();
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
    m_surfaceSpacing->setValue(obj.value(QStringLiteral("surface_spacing")).toDouble(1.0));

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

    if (isSurfaceSemiAuto())
    {
        int selectedFaces = 0;
        QString buildError;
        const QVector<PreviewBar> bars = buildSurfaceBars(&buildError, &selectedFaces);
        if (!buildError.isEmpty())
        {
            QMessageBox::warning(this, QStringLiteral("半自动布置"), buildError);
            return;
        }
        if (bars.isEmpty())
        {
            QMessageBox::information(this, QStringLiteral("半自动布置"),
                                     QStringLiteral("请先点击“选择区域”，再在模型上点一个需要支护的表面。"));
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
            placement.installationLayer = 1;
            placement.matype = matype;
            placement.placementUuid = m_uuid;
            placements.push_back(placement);
        }

        if (!service->appendBarElementsBatch(placements, &resultMessage))
        {
            QMessageBox::warning(this, QStringLiteral("生成锚杆失败"),
                                 resultMessage.isEmpty() ? QStringLiteral("无法修改当前网格。")
                                                         : resultMessage);
            return;
        }
        barCount = placements.size();
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
        placement.installationLayer = 1;
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
    }

    if (isSurfaceSemiAuto())
        clearSurfaceSelection();
    else
        PickManager::instance().clearSelection(QString::fromLatin1(kPickDomain), m_uuid,
                                                PickMode::Node);
    service->setBarPlacementDisplayMode(true);
    applyEditingSurfaceStyle();
    refreshRecords();
    updateMaterialStatus();
    updatePreview();
    if (m_actionStatus)
    {
        QString status = QStringLiteral("已添加 %1 根锚杆，材料编号 %2。")
                             .arg(barCount).arg(matype);
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

    service->setBarPlacementDisplayMode(true);
    applyEditingSurfaceStyle();
    refreshRecords();
    updatePreview();
    if (m_actionStatus)
        m_actionStatus->setText(QStringLiteral("选中的锚杆已删除。"));
}
