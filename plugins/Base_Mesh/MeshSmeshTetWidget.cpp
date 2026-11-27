#include "MeshCompactDoubleSpinBox.h"
#include "MeshSmeshTetWidget.h"
#include "core/MeshSettingsUi.h"
#include "core/MeshSmeshWorkflowState.h"
#include "core/MeshAutoSizing.h"
#include "core/MeshLengthUnit.h"

#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/MeshModuleControllerService.h>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QTimer>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QVBoxLayout>

MeshSmeshTetWidget::MeshSmeshTetWidget(QWidget* parent)
    : BasePage("MeshSmeshTetWidget")
{
    Q_UNUSED(parent);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);
    // The Mesh 1 and Mesh N pages use the same basic parameter vocabulary.
    // This is one scoped Tet operation, not an independent parent Mesh.
    auto* group = new QGroupBox(tr("基础网格"), this);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* groupLayout = new QVBoxLayout(group);
    auto* options = new QGroupBox(tr("网格控制"), group);
    auto* params = new QFormLayout(options);
    m_method = new QComboBox(options);
    m_method->addItem(tr("自动四面体（NETGEN）"), 0);
    m_method->setToolTip(tr("当前分域操作使用 SMESH/NETGEN 四面体算法。扫掠需要使用对应的扫掠操作。"));
    params->addRow(tr("网格方法"), m_method);
    m_density = new QComboBox(options);
    m_density->addItem(tr("自定义参数"), QStringLiteral("custom"));
    m_density->addItem(tr("较少"), QStringLiteral("few"));
    m_density->addItem(tr("标准"), QStringLiteral("normal"));
    m_density->addItem(tr("精细"), QStringLiteral("fine"));
    params->addRow(tr("尺寸预设"), m_density);
    m_density->setToolTip(tr("根据所选实体尺度设置尺寸上限和 NETGEN 精细度，最小尺寸保持原生自动；不保证固定单元数量。"));
    m_sizingScope = new QComboBox(options);
    m_sizingScope->setObjectName(QStringLiteral("smeshSizingScope"));
    m_sizingScope->addItem(tr("整个域：表面与内部"), 1);
    m_sizingScope->addItem(tr("仅体内部（表面沿用父级）"), 0);
    m_sizingScope->setToolTip(tr("整个域：本节点的最大、最小尺寸、精细度及曲率作用于所选域表面和体内部；局部面/边规则优先。仅体内部：表面使用父 Mesh 或面/边规则，本节点不修改表面参数。"));
    params->addRow(tr("尺寸作用范围"), m_sizingScope);
    // Surface-vs-volume scope changes actual meshing semantics; do not
    // hide it behind Advanced settings where a user cannot see its effect.
    m_targetSize = new MeshCompactDoubleSpinBox(options);
    m_targetSize->setRange(1.0e-6, 1.0e9);
    m_targetSize->setDecimals(6);
    m_targetSize->setValue(5.0);
    params->addRow(tr("域尺寸上限"), m_targetSize);
    m_targetSize->setToolTip(tr("整个域模式下同时控制该域的表面与内部。较大的值允许较粗网格；曲率、短边和显式局部规则仍可能要求更小的单元。共享面按原生子网格优先级计算一次。"));
    m_fineness = new QComboBox(options);
    m_fineness->addItem(tr("很粗"), 0);
    m_fineness->addItem(tr("粗"), 1);
    m_fineness->addItem(tr("标准"), 2);
    m_fineness->addItem(tr("细"), 3);
    m_fineness->addItem(tr("很细"), 4);
    m_fineness->addItem(tr("自定义（独立参数）"), 5);
    m_fineness->setToolTip(tr("使用 NETGEN 原生精细度；整个域模式下同时用于表面和体网格。选自定义可修改增长率及边、曲率密度。"));
    params->addRow(tr("划分精细度"), m_fineness);
    groupLayout->addWidget(options);
    m_surfaceSizingHint = new QLabel(group);
    m_surfaceSizingHint->setObjectName(QStringLiteral("smeshSurfaceSizingScope"));
    m_surfaceSizingHint->setWordWrap(true);
    groupLayout->addWidget(m_surfaceSizingHint);
    updateSurfaceSizingHint();
    m_showAdvanced = new QCheckBox(tr("显示高级参数"), group);
    auto* showAdvanced = m_showAdvanced;
    groupLayout->addWidget(showAdvanced);
    root->addWidget(group);

    auto* advanced = new QGroupBox(tr("高级参数"), this);
    advanced->setVisible(false);
    connect(showAdvanced, &QCheckBox::toggled, advanced, &QWidget::setVisible);
    auto* advancedParams = new QFormLayout(advanced);
    m_curvature = new QCheckBox(tr("按曲率和边长限制尺寸"), advanced);
    m_curvature->setChecked(true);
    m_curvature->setToolTip(tr("启用 NETGEN 原生几何局部尺寸限制。整个域模式下作用于本域表面及内部；关闭可能降低小孔和曲面的保形精度。"));
    advancedParams->addRow(tr("局部尺寸自适应"),m_curvature);
    m_chordalMode = new QComboBox(advanced);
    m_chordalMode->setObjectName(QStringLiteral("tetChordalMode"));
    m_chordalMode->addItem(tr("继承父 Mesh"), -1);
    m_chordalMode->addItem(tr("关闭"), 0);
    m_chordalMode->addItem(tr("本域指定"), 1);
    advancedParams->addRow(tr("曲面弦高约束"), m_chordalMode);
    m_chordalError = new MeshCompactDoubleSpinBox(advanced);
    m_chordalError->setObjectName(QStringLiteral("tetChordalError"));
    m_chordalError->setDecimals(6);
    m_chordalError->setRange(0.000001, 1.0e12);
    m_chordalError->setValue(1.0);
    m_chordalError->setEnabled(false);
    m_chordalError->setToolTip(tr("使用工程长度单位；越小通常越密。独立于曲率开关，仍受最小尺寸限制。"));
    advancedParams->addRow(tr("弦高误差（工程长度单位）"), m_chordalError);
    m_chordalHint = new QLabel(advanced);
    m_chordalHint->setObjectName(QStringLiteral("tetChordalHint"));
    m_chordalHint->setWordWrap(true);
    advancedParams->addRow(m_chordalHint);
    connect(m_chordalMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateSurfaceSizingHint(); queueCommit();
    });
    connect(m_chordalError, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updateSurfaceSizingHint(); queueCommit();
    });
    m_minMode = new QComboBox(advanced);
    m_minMode->addItem(tr("自动"), true);
    m_minMode->addItem(tr("手动"), false);
    advancedParams->addRow(tr("最小尺寸方式"), m_minMode);
    m_minMode->setToolTip(tr("整个域：下限同时用于本域表面和体内部；仅体内部：只用于 3D 网格，二维表面仍继承父 Mesh。自动向 NETGEN 传 Min=0（预设档可能按几何估算有效下限）；手动下限过大可能损失小特征精度。"));
    m_minSize = new MeshCompactDoubleSpinBox(advanced);
    // A saved legacy lower bound of 1e-6 mm becomes 1e-9 m. Preserve it
    // through page load/save even while automatic sizing disables this field.
    m_minSize->setObjectName(QStringLiteral("tetManualMinSize"));
    m_minSize->setDecimals(12);
    m_minSize->setRange(1.0e-12, 1.0e9);
    m_minSize->setValue(0.5);
    m_minSize->setEnabled(false);
    advancedParams->addRow(tr("手动最小尺寸"), m_minSize);
    m_growthRate = new MeshCompactDoubleSpinBox(advanced);
    m_growthRate->setRange(0.01, 1.0);
    m_growthRate->setDecimals(3);
    m_growthRate->setSingleStep(0.05);
    m_growthRate->setValue(0.3);
    m_growthRate->setToolTip(tr("控制局部细网格向较粗区域过渡的速度；越大过渡通常越快。仅自定义时可修改。"));
    advancedParams->addRow(tr("尺寸增长率"), m_growthRate);
    m_segPerEdge = new MeshCompactDoubleSpinBox(advanced);
    m_segPerEdge->setRange(0.05, 20.0);
    m_segPerEdge->setDecimals(2);
    m_segPerEdge->setSingleStep(0.25);
    m_segPerEdge->setValue(1.0);
    m_segPerEdge->setToolTip(tr("NETGEN 原生边尺寸参考，不是固定分段数。整个域模式下用于该域的边与面；显式边规则优先。"));
    advancedParams->addRow(tr("每边分段密度"), m_segPerEdge);
    m_segPerRadius = new MeshCompactDoubleSpinBox(advanced);
    m_segPerRadius->setRange(0.1, 50.0);
    m_segPerRadius->setDecimals(2);
    m_segPerRadius->setSingleStep(0.5);
    m_segPerRadius->setValue(2.0);
    m_segPerRadius->setToolTip(tr("NETGEN 曲率尺寸参考，越大通常越细。整个域模式下直接用于本域曲面；显式面规则优先。"));
    advancedParams->addRow(tr("每曲率半径分段"), m_segPerRadius);
    auto* curvatureHint=new QLabel(tr("当前曲率参考低于 NETGEN 内置“很粗”的值 1。数量可能减少，但曲面可能失真或表面相交；请先检查表面网格。"),advanced);
    MeshSettingsUi::hideExplanation(curvatureHint);
    curvatureHint->setWordWrap(true);advancedParams->addRow(curvatureHint);
    const auto updateCurvatureHint=[this,curvatureHint]() {
        curvatureHint->setVisible(m_curvature->isChecked() && m_segPerRadius->value()<1.);
    };
    connect(m_segPerRadius,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[updateCurvatureHint](double){updateCurvatureHint();});
    connect(m_curvature,&QCheckBox::toggled,this,[updateCurvatureHint](bool){updateCurvatureHint();});
    // Display the approximate native preset values, but never pass these
    // display values when a non-Custom fineness is selected.
    const auto syncNativePreset = [this, showAdvanced, updateCurvatureHint]() {
        const int f = m_fineness->currentData().toInt();
        const bool custom = f == 5;
        if (!custom && f >= 0 && f <= 4) {
            static const double growth[] = {0.7, 0.5, 0.3, 0.2, 0.1};
            static const double edge[] = {0.3, 0.5, 1.0, 2.0, 3.0};
            static const double radius[] = {1.0, 1.5, 2.0, 3.0, 5.0};
            QSignalBlocker b1(m_growthRate), b2(m_segPerEdge), b3(m_segPerRadius);
            m_growthRate->setValue(growth[f]);
            m_segPerEdge->setValue(edge[f]);
            m_segPerRadius->setValue(radius[f]);
        }
        m_growthRate->setEnabled(custom);
        m_segPerEdge->setEnabled(custom && m_curvature->isChecked());
        m_segPerRadius->setEnabled(m_curvature->isChecked() && custom);
        updateCurvatureHint();
        if (custom && !m_reloading) showAdvanced->setChecked(true);
    };
    connect(m_fineness, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [syncNativePreset](int) { syncNativePreset(); });
    syncNativePreset();
    m_optimize = new QCheckBox(tr("生成后优化网格"), advanced);
    m_optimize->setChecked(true);
    advancedParams->addRow(tr("网格优化"), m_optimize);
    m_surfaceLimit=new QSpinBox(advanced);
    m_surfaceLimit->setObjectName(QStringLiteral("smeshSurfaceElementLimit"));
    m_surfaceLimit->setRange(0,2000000000);
    m_surfaceLimit->setSpecialValueText(tr("不限"));
    m_surfaceLimit->setSingleStep(10000);
    m_surfaceLimit->setToolTip(tr("当前工程共用的表面数量上限。先完成原生表面划分并输出实际数量，超过此值则在体填充前停止；0 表示不限。"));
    advancedParams->addRow(tr("表面单元上限（工程共用）"),m_surfaceLimit);
    connect(m_surfaceLimit,QOverload<int>::of(&QSpinBox::valueChanged),this,[this](int value) {
        if(!m_reloading)PublicVariables::instance().set(QStringLiteral("mesh.smesh.surfaceElementLimit"),value,"mesh");
    });
    root->addWidget(advanced);
    connect(m_minMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) {
            m_minSize->setEnabled(!m_minMode->currentData().toBool());
            updateSurfaceSizingHint();
        });
    connect(m_density, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) {
            if (m_reloading) return;
            const QString value = m_density->currentData().toString();
            if (value == QStringLiteral("custom")) return;
            QVector<int> ids;
            if (m_scopeMode->currentData().toBool()) {
                const auto main = MeshSmeshWorkflowState::mainGeometryScopeForPlan(m_planUuid);
                const QVariantMap occupied = occupiedDomains();
                auto* geo = ServiceRegistry::instance().getService<PreProcessingService>();
                if (geo) for (const auto& entry : geo->entityBindings(3)) {
                    const int id = entry.toObject().value(QStringLiteral("id")).toInt();
                    if (id > 0 && (main.all || main.volumeUiIds.contains(id))
                        && !occupied.contains(QString::number(id))) ids.push_back(id);
                }
            } else {
                for (int i=0; i<m_list->count(); ++i)
                    if (m_list->item(i)->checkState() == Qt::Checked)
                        ids.push_back(m_list->item(i)->data(Qt::UserRole).toInt());
            }
            if (ids.isEmpty()) {
                m_summary->setText(tr("请先选择要划分的实体，再使用自动预设。"));
                const QSignalBlocker block(m_density);
                m_density->setCurrentIndex(0);
                return;
            }
            auto* geo = ServiceRegistry::instance().getService<PreProcessingService>();
            const auto cad = geo ? geo->nativeCadSnapshot() : nullptr;
            const auto scale = cad ? MeshAutoSizing::measure(*cad, ids) : MeshAutoSizing::Scale{};
            if (!scale.valid) {
                m_summary->setText(tr("暂无法读取当前实体的 CAD 尺寸；未修改设置。"));
                const QSignalBlocker block(m_density);
                m_density->setCurrentIndex(0);
                return;
            }
            const auto preset = MeshAutoSizing::infer(scale, value);
            m_reloading = true;
            m_targetSize->setValue(preset.maxSize);
            m_minMode->setCurrentIndex(m_minMode->findData(true));
            m_fineness->setCurrentIndex(m_fineness->findData(preset.fineness));
            m_presetBaseSize = preset.maxSize;
            m_summary->clear();
            m_reloading = false;
        });
    connect(m_targetSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double value) {
            if (m_reloading) return;
            m_presetBaseSize = value;
            m_density->setCurrentIndex(0);
        });
    connect(m_fineness, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { if (!m_reloading) m_density->setCurrentIndex(0); });
    connect(m_minMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { if (!m_reloading) m_density->setCurrentIndex(0); });
    connect(m_minSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { if (!m_reloading) m_density->setCurrentIndex(0); });
    connect(m_segPerRadius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) {
            if (m_reloading) return;
            if (m_fineness->currentData().toInt() != 5)
                m_fineness->setCurrentIndex(m_fineness->findData(5));
            queueCommit();
        });

    auto* scope = new QGroupBox(tr("作用域"), this);
    scope->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* scopeLayout = new QVBoxLayout(scope);
    auto* help = new QLabel(tr("修改后自动应用到当前 SubMesh；保存工程时才写入工程文件。每个实体只能归属一个网格节点。"), scope);
    MeshSettingsUi::hideExplanation(help);
    m_scopeMode = new QComboBox(scope);
    m_scopeMode->setObjectName(QStringLiteral("meshSmeshScopeMode"));
    m_scopeMode->addItem(tr("全部未分配的实体"), true);
    m_scopeMode->addItem(tr("指定实体"), false);
    scopeLayout->addWidget(m_scopeMode);
    help->setWordWrap(true);
    scopeLayout->addWidget(help);
    m_filter = new QLineEdit(scope);
    m_filter->setObjectName(QStringLiteral("meshSmeshDomainFilter"));
    m_filter->setPlaceholderText(tr("搜索实体名称或编号"));
    scopeLayout->addWidget(m_filter);
    m_list = new QListWidget(scope);
    m_list->setObjectName(QStringLiteral("meshSmeshDomainList"));
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setMinimumHeight(190);
    m_list->setMaximumHeight(260); // do not turn a scope list into a full-height blank pane
    scopeLayout->addWidget(m_list);
    connect(m_scopeMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const bool specified = !m_scopeMode->currentData().toBool();
        m_filter->setVisible(specified);
        m_list->setVisible(specified);
        queueCommit();
    });
    auto* row = new QHBoxLayout();
    auto* selectRemaining = new QPushButton(tr("选择未分配实体"), scope);
    row->addWidget(selectRemaining);
    connect(m_scopeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            selectRemaining, [this, selectRemaining](int) {
                selectRemaining->setVisible(!m_scopeMode->currentData().toBool());
            });
    scopeLayout->addLayout(row);
    auto* filterActions = new QWidget(scope);
    auto* filterRow = new QHBoxLayout(filterActions);filterRow->setContentsMargins(0,0,0,0);
    auto* checkVisible = new QPushButton(tr("勾选搜索结果"), filterActions);
    auto* clearVisible = new QPushButton(tr("取消勾选搜索结果"), filterActions);
    checkVisible->setObjectName(QStringLiteral("smeshCheckVisibleDomains"));
    clearVisible->setObjectName(QStringLiteral("smeshClearVisibleDomains"));
    checkVisible->setToolTip(tr("批量勾选当前显示且可用的实体；保留其他实体的勾选状态，不抢占其他节点。"));
    clearVisible->setToolTip(tr("只取消当前显示且可用的实体；保留未显示实体的勾选状态。"));
    filterRow->addWidget(checkVisible);filterRow->addWidget(clearVisible);
    scopeLayout->addWidget(filterActions);
    connect(checkVisible,&QPushButton::clicked,this,[this](){selectVisible(true);});
    connect(clearVisible,&QPushButton::clicked,this,[this](){selectVisible(false);});
    connect(m_scopeMode,QOverload<int>::of(&QComboBox::currentIndexChanged),filterActions,
        [this,filterActions](int){filterActions->setVisible(!m_scopeMode->currentData().toBool());});
    filterActions->setVisible(false);
    selectRemaining->setVisible(false); // Initial "all remaining" selection
    root->addWidget(scope);
    root->insertWidget(0, m_selectionPresentation.createViewSelector(this));
    auto* preview = new QPushButton(tr("仅预览当前区域（高级）"), this);
    preview->setToolTip(tr("使用临时原生 Mesh 计算本区域，可以反复修改参数；不影响正式增量会话与求解网格。"));
    root->addWidget(preview);
    preview->setVisible(false);
    connect(showAdvanced, &QCheckBox::toggled, preview, &QWidget::setVisible);
    auto* closePreview = new QPushButton(tr("结束预览，恢复正式网格"), this);
    closePreview->setVisible(false);
    root->addWidget(closePreview);
    connect(showAdvanced, &QCheckBox::toggled, closePreview, &QWidget::setVisible);
    connect(closePreview, &QPushButton::clicked, this, []() {
        if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            if (auto* obj = dynamic_cast<QObject*>(svc))
                QMetaObject::invokeMethod(obj, "restoreOfficialMeshDisplay", Qt::DirectConnection);
    });
    auto* generateCurrent = new QPushButton(tr("生成完整网格"), this);
    generateCurrent->setObjectName(QStringLiteral("smeshGenerateCurrent"));
    generateCurrent->setToolTip(tr("保存当前区域参数，由父 Mesh 统一重建完整网格：包含全部已配置四面体、扫掠和边界层。共享接口由同一原生 Mesh 管理。"));
    root->addWidget(generateCurrent);
    auto* stop = new QPushButton(tr("终止当前生成"), this);
    stop->setToolTip(tr("请求终止正在执行的 NETGEN 原生计算；通常不必等整个实体完成。进程内计算不能保证立即退出。"));
    root->addWidget(stop);
    connect(stop, &QPushButton::clicked, this, [this]() {
        if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            if (auto* obj = dynamic_cast<QObject*>(svc))
                QMetaObject::invokeMethod(obj, "requestStopCurrentMesh", Qt::DirectConnection);
    });
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);
    auto* details = new QLabel(tr("本页设置所选实体的网格尺寸。面/边局部控制单独设置；点击“生成完整网格”将统一重建，确保共享接口一致。独立预览仅用于检查，不替换计算网格。"), this);
    details->setWordWrap(true);
    root->addWidget(details);
    MeshSettingsUi::hideExplanation(details);
    MeshSettingsUi::hideExplanation(m_surfaceSizingHint);
    MeshSettingsUi::hideExplanation(m_chordalHint);
    // Keep selector, parameters and actions together.  Only the area below
    // them grows when the settings dock is resized.
    root->addStretch(1);
    connect(m_sizingScope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if(!m_reloading && m_sizingScope->currentData().toInt()==1) {
            m_summary->clear();
        }
        updateSurfaceSizingHint(); queueCommit();
    });
    // One event-loop turn coalesces a series of changes (e.g. select all).
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { queueCommit(); });
    connect(m_targetSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { updateSurfaceSizingHint(); queueCommit(); });
    connect(m_fineness, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { queueCommit(); });
    connect(m_density, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { queueCommit(); });
    connect(m_curvature, &QCheckBox::toggled, this, [this](bool enabled) {
        m_segPerRadius->setEnabled(enabled);
        m_segPerEdge->setEnabled(enabled && m_fineness->currentData().toInt()==5);
        queueCommit();
    });
    connect(m_optimize, &QCheckBox::toggled, this, [this](bool) { queueCommit(); });
    connect(m_minMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { updateSurfaceSizingHint(); queueCommit(); });
    connect(m_minSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { updateSurfaceSizingHint(); queueCommit(); });
    connect(m_growthRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { queueCommit(); });
    connect(m_segPerEdge, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
        [this](double) { queueCommit(); });

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& filter) {
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setHidden(!m_list->item(i)->text().contains(filter, Qt::CaseInsensitive));
    });
    connect(selectRemaining, &QPushButton::clicked, this, &MeshSmeshTetWidget::selectUnassigned);
    connect(preview, &QPushButton::clicked, this, &MeshSmeshTetWidget::previewSelected);
    connect(generateCurrent, &QPushButton::clicked, this, &MeshSmeshTetWidget::generateSelected);
    MeshSettingsUi::simplify(this);
}

MeshSmeshTetWidget::~MeshSmeshTetWidget()
{
    m_selectionPresentation.leave();
}

void MeshSmeshTetWidget::setOperation(const QString& planUuid, const QString& taskUuid)
{
    m_planUuid = planUuid;
    m_taskUuid = taskUuid;
    reload();
}
void MeshSmeshTetWidget::beforeLoad()
{
    m_selectionPresentation.enter();
    reload();
    if (auto* button = findChild<QPushButton*>(QStringLiteral("smeshGenerateCurrent"))) {
        button->setEnabled(true);
        button->setText(tr("生成完整网格"));
    }
    const QString unitSuffix = QStringLiteral(" ") + MeshLengthUnit::symbol();
    m_targetSize->setSuffix(unitSuffix);
    m_minSize->setSuffix(unitSuffix);
    m_targetSize->setToolTip(tr("所选域 NETGEN 的最大尺寸，默认同时用于本域表面与内部。单位：%1。局部面、边的显式规则优先。")
        .arg(MeshLengthUnit::interpretation()));
    MeshSettingsUi::simplify(this);
}
void MeshSmeshTetWidget::beforeUnload() { save(false); m_selectionPresentation.leave(); }
bool MeshSmeshTetWidget::commitSettings() { return save(true); }

void MeshSmeshTetWidget::queueCommit()
{
    if (m_reloading || m_planUuid.isEmpty() || m_taskUuid.isEmpty()) return;
    if (m_commitQueued) return;
    m_commitQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_commitQueued = false;
        if (!m_reloading) { save(false); syncSelectionHighlight(); }
    });
}

QVariantMap MeshSmeshTetWidget::occupiedDomains() const
{
    QVariantMap owners;
    auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (auto* obj = dynamic_cast<QObject*>(svc))
        QMetaObject::invokeMethod(obj, "subMeshOccupancyForPlan", Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap, owners), Q_ARG(QString, m_planUuid), Q_ARG(QString, m_taskUuid));
    return owners;
}

void MeshSmeshTetWidget::reload()
{
    m_reloading = true;
    updateSurfaceSizingHint();
    m_list->clear();
    if (m_planUuid.isEmpty() || m_taskUuid.isEmpty()) { m_reloading = false; return; }
    auto* geometry = ServiceRegistry::instance().getService<PreProcessingService>();
    if (!geometry)
    {
        m_summary->setText(tr("几何模块尚不可用。"));
        m_reloading = false;
        return;
    }
    QSet<int> saved;
    MeshSmeshWorkflowState::TetTask selectedTask;
    for (const auto& task : MeshSmeshWorkflowState::tetTasksForPlan(m_planUuid))
        if (task.taskUuid == m_taskUuid) {
            selectedTask = task;
            for (int id : task.volumeUiIds) saved.insert(id);
            break;
        }
    m_scopeMode->setCurrentIndex(selectedTask.allVolumes ? 0 : 1);
    m_sizingScope->setCurrentIndex(m_sizingScope->findData(selectedTask.sizingScope));
    updateSurfaceSizingHint();
    m_filter->setVisible(!selectedTask.allVolumes);
    m_list->setVisible(!selectedTask.allVolumes);
    const auto globalValue = [](const QString& key, const QVariant& fallback) {
        const QVariant value = PublicVariables::instance().get(key);
        return value.isValid() ? value : fallback;
    };
    const double rootSize = std::max(1.0e-6,
        globalValue(QStringLiteral("mesh.global.lenMax"), 5.0).toDouble() *
        globalValue(QStringLiteral("mesh.global.sizeFactor"), 1.0).toDouble());
    m_targetSize->setValue(selectedTask.targetSize > 0.0 && std::isfinite(selectedTask.targetSize)
        ? selectedTask.targetSize : rootSize);
    m_presetBaseSize = m_targetSize->value();
    const int f = selectedTask.fineness >= 0 ? selectedTask.fineness
        : globalValue(QStringLiteral("mesh.smesh.netgen.fineness"), 2).toInt();
    const int fi = m_fineness->findData(std::clamp(f, 0, 5));
    m_fineness->setCurrentIndex(fi >= 0 ? fi : 2);
    m_showAdvanced->setChecked(f == 5 || selectedTask.chordalErrorEnabled >= 0);
    m_chordalMode->setCurrentIndex(m_chordalMode->findData(selectedTask.chordalErrorEnabled));
    m_chordalError->setValue(selectedTask.chordalError);
    if (f == 5) {
        // Old projects had no per-domain Custom values: inherit global defaults.
        const double growth = selectedTask.growthRate > 0.0 ? selectedTask.growthRate
            : globalValue(QStringLiteral("mesh.smesh.netgen.growthRate"), 0.3).toDouble();
        const double edge = selectedTask.nbSegPerEdge > 0.0 ? selectedTask.nbSegPerEdge
            : globalValue(QStringLiteral("mesh.smesh.netgen.nbSegPerEdge"), 1.0).toDouble();
        const double radius = selectedTask.nbSegPerRadius > 0.0 ? selectedTask.nbSegPerRadius
            : globalValue(QStringLiteral("mesh.smesh.netgen.nbSegPerRadius"), 2.0).toDouble();
        m_growthRate->setValue(std::clamp(growth, 0.01, 1.0));
        m_segPerEdge->setValue(std::clamp(edge, 0.05, 20.0));
        m_segPerRadius->setValue(std::clamp(radius, 0.1, 50.0));
    }
    m_curvature->setChecked(selectedTask.surfaceCurvature >= 0 ? selectedTask.surfaceCurvature != 0
        : globalValue(QStringLiteral("mesh.smesh.netgen.surfaceCurvature"),true).toBool());
    m_optimize->setChecked(selectedTask.optimize >= 0 ? selectedTask.optimize != 0
        : globalValue(QStringLiteral("mesh.smesh.netgen.optimize"), true).toBool());
    const bool autoMin = selectedTask.automaticMinSize >= 0 ? selectedTask.automaticMinSize != 0
        : selectedTask.sizingScope==1 || globalValue(QStringLiteral("mesh.smesh.netgen.automaticMinSize"), true).toBool();
    m_minMode->setCurrentIndex(autoMin ? 0 : 1);
    m_minSize->setValue(selectedTask.manualMinSize > 0.0 ? selectedTask.manualMinSize
        : globalValue(QStringLiteral("mesh.global.lenMin"), 0.5).toDouble());
    m_minSize->setEnabled(!autoMin);
    m_surfaceLimit->setValue(globalValue(QStringLiteral("mesh.smesh.surfaceElementLimit"),0).toInt());
    const int savedPresetIndex = m_density->findData(selectedTask.densityPreset);
    m_density->setCurrentIndex(savedPresetIndex >= 0 ? savedPresetIndex : 0);
    int count = 0;
    const auto parentScope = MeshSmeshWorkflowState::mainGeometryScopeForPlan(m_planUuid);
    QSet<int> parentAllowed;
    for (int id : parentScope.volumeUiIds) parentAllowed.insert(id);
    const QVariantMap occupied = occupiedDomains();
    for (const auto& value : geometry->entityBindings(3))
    {
        const auto obj = value.toObject();
        const int id = obj.value(QStringLiteral("id")).toInt();
        if (id <= 0 || (!parentScope.all && !parentAllowed.contains(id))) continue;
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        auto* item = new QListWidgetItem(name.isEmpty()
            ? tr("实体 %1").arg(id) : tr("%1（实体 %2）").arg(name).arg(id), m_list);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(saved.contains(id) ? Qt::Checked : Qt::Unchecked);
        const QString owner = occupied.value(QString::number(id)).toString();
        if (!owner.isEmpty()) {
            item->setToolTip(tr("当前由 %1 管理").arg(owner));
            if (!saved.contains(id))
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        ++count;
    }
    // Returning to this page rebuilds the list without changing filter text.
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setHidden(!m_list->item(i)->text().contains(m_filter->text(), Qt::CaseInsensitive));
    m_summary->clear();
    m_reloading = false;
    updateSurfaceSizingHint();
    syncSelectionHighlight();
}

void MeshSmeshTetWidget::syncSelectionHighlight()
{
    if (m_reloading || !m_list || !m_scopeMode) return;
    const bool remaining = m_scopeMode->currentData().toBool();
    const auto occupied = remaining ? occupiedDomains() : QVariantMap{};
    QList<int> ids;
    for (int i=0;i<m_list->count();++i) {
        const auto* item=m_list->item(i);
        const int id=item->data(Qt::UserRole).toInt();
        if ((remaining && !occupied.contains(QString::number(id))) ||
            (!remaining && item->checkState()==Qt::Checked)) ids.append(id);
    }
    m_selectionPresentation.setSelectedEntities(3, ids);
}

void MeshSmeshTetWidget::updateSurfaceSizingHint()
{
    if (!m_surfaceSizingHint || !m_chordalHint) return;
    const bool wholeDomain = m_sizingScope && m_sizingScope->currentData().toInt() == 1;
    m_chordalMode->setEnabled(wholeDomain);
    m_chordalError->setEnabled(wholeDomain && m_chordalMode->currentData().toInt() == 1);
    auto& pv = PublicVariables::instance();
    const bool parentChordal = pv.get(QStringLiteral("mesh.smesh.netgen.chordalErrorEnabled")).toBool();
    const double parentError = pv.get(QStringLiteral("mesh.smesh.netgen.chordalError")).isValid()
        ? pv.get(QStringLiteral("mesh.smesh.netgen.chordalError")).toDouble() : 1.0;
    const int mode = wholeDomain ? m_chordalMode->currentData().toInt() : -1;
    m_chordalHint->setText(!wholeDomain
        ? tr("仅体内部：表面弦高沿用父 Mesh，本域覆盖不生效；改为整个域后可设置。")
        : mode < 0
        ? (parentChordal ? tr("继承父级：弦高误差 %1 工程长度单位。").arg(parentError, 0, 'g', 6) : tr("继承父级：弦高约束关闭。"))
        : mode == 0 ? tr("本域不使用弦高约束；曲率及面/边规则仍独立生效。")
        : tr("本域表面弦高误差 %1 工程长度单位；共享面由 SMESH 统一解析，不能拥有两套网格。").arg(m_chordalError->value(), 0, 'g', 6));
    const double domainMax = m_targetSize ? m_targetSize->value() : 0.0;
    const QString minText = m_minMode && m_minMode->currentData().toBool()
        ? tr("自动（原生 Min=0）")
        : QString::number(m_minSize ? m_minSize->value() : 0.0, 'g', 6);
    if (m_sizingScope && m_sizingScope->currentData().toInt() == 1) {
        m_surfaceSizingHint->setText(tr("整个域：表面和内部 Max=%1，Min=%2（工程长度单位）。显式面/边规则优先；共享接口统一生成。")
            .arg(domainMax, 0, 'g', 6).arg(minText) + QStringLiteral("\n") + m_chordalHint->text());
        return;
    }
    const bool parentEnabled = PublicVariables::instance()
        .get(QStringLiteral("mesh.smesh.parentSizingEnabled")).toBool();
    const QVariant parentValue = PublicVariables::instance().get(QStringLiteral("mesh.global.lenMax"));
    const double parentMax = parentValue.isValid() ? parentValue.toDouble() : 5.0;
    m_surfaceSizingHint->setText(parentEnabled
        ? tr("仅体内部：3D Max=%1，Min=%2；二维表面沿用父 Mesh Max=%3 和面/边局部规则（工程长度单位）。\n"
             "调大体尺寸不会合并已有的表面三角形。若要减少整体网格量，请用“整个域”调整表面尺寸并先预览表面；不要仅靠提高最小尺寸压低数量。")
            .arg(domainMax, 0, 'g', 6).arg(minText).arg(parentMax, 0, 'g', 6)
        : tr("仅体内部：3D Max=%1，Min=%2；父 Mesh 尚未启用公共表面尺寸，请先设置父级或改为整个域。")
            .arg(domainMax, 0, 'g', 6).arg(minText));
}

void MeshSmeshTetWidget::selectUnassigned()
{
    const QVariantMap occupied = occupiedDomains();
    m_reloading = true;
    int count = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        const int id = item->data(Qt::UserRole).toInt();
        const bool select = !occupied.contains(QString::number(id));
        item->setCheckState(select ? Qt::Checked : Qt::Unchecked);
        if (select) ++count;
    }
    m_reloading = false;
    m_summary->clear();
    queueCommit();
}

void MeshSmeshTetWidget::selectVisible(bool checked)
{
    m_reloading = true;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        if (!item->isHidden() && (item->flags() & Qt::ItemIsEnabled))
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    m_reloading = false;
    queueCommit();
}

bool MeshSmeshTetWidget::save(bool interactive)
{
    if (m_planUuid.isEmpty() || m_taskUuid.isEmpty()) return false;
    const QVariantMap occupied = occupiedDomains();
    const auto parentScope = MeshSmeshWorkflowState::mainGeometryScopeForPlan(m_planUuid);
    QSet<int> parentAllowed;
    for (int id : parentScope.volumeUiIds) parentAllowed.insert(id);
    MeshSmeshWorkflowState::TetTask task;
    task.allVolumes = m_scopeMode->currentData().toBool();
    if (task.allVolumes) {
        const QString owner = occupied.value(QStringLiteral("__catch_all_owner__")).toString();
        if (!owner.isEmpty()) {
            const QString msg = tr("%1 已选择全部未分配实体，一个 Mesh 只能有一个此类 SubMesh。")
                                    .arg(owner);
            m_summary->setText(msg);
            if (interactive) QMessageBox::warning(this, tr("作用域冲突"), msg);
            return false;
        }
    }
    task.planUuid = m_planUuid;
    task.taskUuid = m_taskUuid;
    for (const auto& previous : MeshSmeshWorkflowState::tetTasksForPlan(m_planUuid))
        if (previous.taskUuid == m_taskUuid) task.title = previous.title;

    // In catch-all mode any old check marks are irrelevant. Previously we
    // validated them anyway, reporting bogus "occupied" errors on mode change.
    if (!task.allVolumes) for (int i = 0; i < m_list->count(); ++i) {
        const auto* item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int id = item->data(Qt::UserRole).toInt();
        const QString owner = occupied.value(QString::number(id)).toString();
        if (!owner.isEmpty()) {
            const QString msg = tr("实体 %1 当前属于 %2，请先在该节点取消选择。")
                                    .arg(id).arg(owner);
            m_summary->setText(msg);
            if (interactive) QMessageBox::warning(this, tr("作用域冲突"), msg);
            return false;
        }
        if (id > 0 && (parentScope.all || parentAllowed.contains(id)))
            task.volumeUiIds.push_back(id);
    }
    // Empty SubMesh is a valid draft configuration: the user may clear its
    // selection and assign solids later. Compute validates nonempty scope.
    task.targetSize = m_targetSize->value();
    task.sizingScope = m_sizingScope->currentData().toInt();
    task.densityPreset = m_density->currentData().toString();
    task.fineness = m_fineness->currentData().toInt();
    task.surfaceCurvature = m_curvature->isChecked() ? 1 : 0;
    task.chordalErrorEnabled = m_chordalMode->currentData().toInt();
    task.chordalError = m_chordalError->value();
    task.optimize = m_optimize->isChecked() ? 1 : 0;
    task.automaticMinSize = m_minMode->currentData().toBool() ? 1 : 0;
    task.manualMinSize = m_minSize->value();
    task.growthRate = task.fineness == 5 ? m_growthRate->value() : -1.0;
    task.nbSegPerEdge = task.fineness == 5 ? m_segPerEdge->value() : -1.0;
    task.nbSegPerRadius = task.fineness == 5 ? m_segPerRadius->value() : -1.0;
    if (!task.automaticMinSize && task.manualMinSize > task.targetSize) {
        m_summary->setText(tr("手动最小尺寸不能大于目标尺寸（当前修改尚未应用）。"));
        if (interactive)
            QMessageBox::warning(this, tr("尺寸参数"), tr("手动最小尺寸不能大于目标尺寸。"));
        return false;
    }
    bool changed = true;
    for (const auto& previous : MeshSmeshWorkflowState::tetTasksForPlan(m_planUuid))
        if (previous.taskUuid == task.taskUuid) {
            changed = previous.allVolumes != task.allVolumes ||
                      previous.volumeUiIds != task.volumeUiIds ||
                      previous.sizingScope != task.sizingScope ||
                      !qFuzzyCompare(previous.targetSize + 1.0, task.targetSize + 1.0) ||
                      previous.densityPreset != task.densityPreset ||
                      previous.fineness != task.fineness ||
                      previous.surfaceCurvature != task.surfaceCurvature ||
                      previous.chordalErrorEnabled != task.chordalErrorEnabled ||
                      previous.chordalError != task.chordalError ||
                      previous.optimize != task.optimize ||
                      previous.automaticMinSize != task.automaticMinSize ||
                      !qFuzzyCompare(previous.manualMinSize + 1.0, task.manualMinSize + 1.0) ||
                      !qFuzzyCompare(previous.growthRate + 1.0, task.growthRate + 1.0) ||
                      !qFuzzyCompare(previous.nbSegPerEdge + 1.0, task.nbSegPerEdge + 1.0) ||
                      !qFuzzyCompare(previous.nbSegPerRadius + 1.0, task.nbSegPerRadius + 1.0);
            break;
        }
    if (changed) {
        MeshSmeshWorkflowState::upsertTetTask(task);
        if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            if (auto* obj = dynamic_cast<QObject*>(svc))
                QMetaObject::invokeMethod(obj, "markMeshControlsChangedForPlan", Qt::DirectConnection,
                    Q_ARG(QString, m_planUuid), Q_ARG(QString, tr("SubMesh 参数或作用域已修改")));
    }
    m_summary->clear();
    return true;
}

void MeshSmeshTetWidget::generateSelected()
{
    if (!save(true)) return;
    // Normal user action always computes the *whole* owning parent Mesh.
    // Do not append a partial result to a stale native session after edits.
    if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        if (auto* obj = dynamic_cast<QObject*>(svc))
            QMetaObject::invokeMethod(obj, "generateCurrentMeshConfiguration", Qt::DirectConnection);
}

void MeshSmeshTetWidget::previewSelected()
{
    if (!save(true)) return;
    if (auto* svc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
        if (auto* obj = dynamic_cast<QObject*>(svc))
            QMetaObject::invokeMethod(obj, "generateSmeshTetPreview", Qt::DirectConnection,
                Q_ARG(QString, m_planUuid), Q_ARG(QString, m_taskUuid));
}
