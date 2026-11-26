#include "MeshGlobalMeshingWidget.h"
#include <MeshWorkingPaths.h>

#include "SmeshMeshGenerator.h"
#include "MeshModuleController.h"
#include "ui_MeshGlobalMeshingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>

#include <QCoreApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QJsonArray>
#include <QEvent>
#include <QSignalBlocker>
#include "CompactDoubleSpinBox.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace
{
constexpr int kElemTypeHex8FromTet = 4;

std::set<int> parseEntityIdSet(const QString& text)
{
    std::set<int> ids;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString part : parts)
    {
        part = part.trimmed();
        if (part.isEmpty())
            continue;

        const int dash = part.indexOf(QLatin1Char('-'));
        if (dash > 0)
        {
            bool okBegin = false;
            bool okEnd = false;
            int begin = part.left(dash).trimmed().toInt(&okBegin);
            int end = part.mid(dash + 1).trimmed().toInt(&okEnd);
            if (!okBegin || !okEnd || begin <= 0 || end <= 0)
                continue;
            if (begin > end)
                std::swap(begin, end);
            for (int id = begin; id <= end; ++id)
                ids.insert(id);
        }
        else
        {
            bool ok = false;
            const int id = part.toInt(&ok);
            if (ok && id > 0)
                ids.insert(id);
        }
    }
    return ids;
}



}

MeshGlobalWorker::MeshGlobalWorker(Params p, QObject* parent)
    : QObject(parent), m_p(std::move(p))
{
}

void MeshGlobalWorker::run()
{
    SmeshMeshingRequest request;
    request.inputGeometryPath = m_p.inputGeometryPath;
    request.workflow = m_p.workflow;
    request.allGeometry = m_p.allGeometry;
    request.geometryScope = m_p.geometryScope;
    request.conformalizeRegions = m_p.autoPartition;
    request.vtkPath = m_tempVtkPath;
    request.nasPath = m_tempNasPath;
    request.dimension = m_p.dimension;
    request.elemType = m_p.elemType;
    request.lenMin = m_p.lenMin;
    request.lenMax = m_p.lenMax;
    request.fineness = m_p.fineness;
    request.nativeFineness = m_p.nativeFineness;
    request.growthRate = m_p.growthRate;
    request.segmentsPerEdge = m_p.segmentsPerEdge;
    request.segmentsPerRadius = m_p.segmentsPerRadius;
    request.elementOrder = m_p.elementOrder;
    request.optimize = m_p.optimize;
    request.localSizeControls = m_p.localSizeControls;
    request.surfaceCurvature = m_p.surfaceCurvature;
    request.progress = [this](const QString& message) { emit log(message); };
    request.tolerance = m_p.tolerance;
    request.exportVtk = m_p.exportVtk;
    request.exportNas = m_p.exportNas;

    if (!m_p.localSizeControls.isEmpty())
    {
        emit log(QStringLiteral("应用局部加密：%1 个几何实体").arg(m_p.localSizeControls.size()));
    }
    if (m_p.skippedLocalControlCount > 0)
    {
        emit log(QStringLiteral("有 %1 个局部控制因实体信息无效或不会细化而跳过。")
                     .arg(m_p.skippedLocalControlCount));
    }
    if (m_p.clampedLocalControlCount > 0)
    {
        emit log(QStringLiteral(
                     "有 %1 个局部最大尺寸小于整体最小尺寸，实际按整体最小尺寸 %2 处理。")
                     .arg(m_p.clampedLocalControlCount)
                     .arg(m_p.lenMin, 0, 'g', 8));
    }

    if (m_p.elemType == kElemTypeHex8FromTet)
        emit log(QStringLiteral("开始生成六面体网格..."));
    else
        emit log(QStringLiteral("开始生成网格..."));

    QString err;
    SmeshMeshingResult result;
    if (!SmeshMeshGenerator::generate(request, &result, &err))
    {
        emit finishedFail(err.isEmpty() ? QStringLiteral("网格生成失败") : err);
        return;
    }

    if (result.appliedLocalControlCount > 0)
    {
        emit log(QStringLiteral("局部尺寸已应用：%1 个实体，建立 %2 个局部网格设置")
                     .arg(result.appliedLocalControlCount)
                     .arg(result.nativeSubmeshCount));
    }

    if (result.conformalized)
    {
        emit log(QStringLiteral(
                     "几何分域共形处理完成：原始面域 %1，处理后面域 %2，共享界面 %3；相邻面域共用节点。")
                     .arg(result.sourceRegionCount)
                     .arg(result.conformalRegionCount)
                     .arg(result.sharedInterfaceCount));
    }

    if (m_p.elemType == kElemTypeHex8FromTet)
    {
        emit log(QStringLiteral(
                     "网格生成完成：节点 %1，边界面 %2，六面体单元 %3，几何区域 %4，最小质量 %5")
                     .arg(result.pointCount)
                     .arg(result.surfaceElementCount)
                     .arg(result.volumeElementCount)
                     .arg(result.regionCount)
                     .arg(result.minScaledJacobian, 0, 'g', 6));
    }
    else
    {
        emit log(QStringLiteral("网格生成完成：节点 %1，表面单元 %2，体单元 %3，几何区域 %4")
                     .arg(result.pointCount)
                     .arg(result.surfaceElementCount)
                     .arg(result.volumeElementCount)
                     .arg(result.regionCount));
    }
    emit finishedOK(m_tempVtkPath, m_tempNasPath);
}

MeshGlobalMeshingWidget::MeshGlobalMeshingWidget(QWidget* parent)
    : BasePage("MeshGlobalMeshingWidget"), ui(new Ui::MeshGlobalMeshingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setupGeometryScope();

    ui->labelFactor->setText(tr("精细度"));
    // Only one meshing algorithm is currently exposed; do not occupy a row with a fixed option.
    ui->labelAlgo->hide();
    ui->cmbAlgo->hide();
    ui->dsMin->setSpecialValueText(tr("自动"));
    ui->dsFactor->setDecimals(2);
    ui->dsFactor->setRange(0.0, 1.0);
    ui->dsFactor->setSingleStep(0.25);
    ui->dsFactor->setValue(0.5);

    initCombos();

    // Keep existing designer fields/IDs, and arrange the complete SMESH sizing
    // settings together. The hidden factor remains readable by older projects.
    ui->dsFactor->hide();
    m_fineness=new QComboBox(this);m_fineness->setObjectName("smeshFineness");
    const QStringList levels{tr("很粗"),tr("粗"),tr("标准"),tr("细"),tr("很细"),tr("自定义")};
    for(int i=0;i<levels.size();++i)m_fineness->addItem(levels[i],i);
    m_fineness->setCurrentIndex(2);
    m_automaticMin=new QCheckBox(tr("自动确定最小尺寸"),this);
    m_automaticMin->setObjectName("smeshAutomaticMinSize");m_automaticMin->setChecked(true);
    auto spin=[this](const char* name,double minimum,double maximum,int decimals,double step,double value){
        auto* field=new CompactDoubleSpinBox(this);field->setObjectName(name);
        field->setDecimals(decimals);field->setRange(minimum,maximum);field->setSingleStep(step);field->setValue(value);
        return field;
    };
    m_growthRate=spin("smeshGrowthRate",0.01,1.0,3,0.05,0.3);
    m_segmentsPerEdge=spin("smeshSegmentsPerEdge",0.05,20,2,0.25,1);
    m_segmentsPerRadius=spin("smeshSegmentsPerRadius",0.1,50,2,0.5,2);
    auto* grid=ui->gridLayout;
    for(QWidget* field:QList<QWidget*>{ui->labelMin,ui->dsMin,ui->labelMax,ui->dsMax,
        ui->labelFactor,ui->dsFactor,ui->labelOrder,ui->spOrder,ui->labelSmooth,ui->chkOptimize,
        ui->labelTol,ui->dsTol,ui->labelCurvature,ui->chkCurvature,ui->labelAlgo,ui->cmbAlgo})grid->removeWidget(field);
    auto row=[&](QWidget* label,QWidget* field,int index){grid->addWidget(label,index,0);grid->addWidget(field,index,1);};
    row(ui->labelMax,ui->dsMax,2);
    grid->addWidget(m_automaticMin,3,1);
    ui->labelMin->setText(tr("手动最小尺寸"));row(ui->labelMin,ui->dsMin,4);
    row(ui->labelFactor,m_fineness,5);
    row(ui->labelCurvature,ui->chkCurvature,6);row(ui->labelSmooth,ui->chkOptimize,7);
    row(new QLabel(tr("尺寸增长率"),this),m_growthRate,8);
    row(new QLabel(tr("每边分段密度"),this),m_segmentsPerEdge,9);
    row(new QLabel(tr("每曲率半径分段"),this),m_segmentsPerRadius,10);
    row(ui->labelOrder,ui->spOrder,11);row(ui->labelTol,ui->dsTol,12);
    connect(m_automaticMin,&QCheckBox::toggled,ui->dsMin,&QWidget::setDisabled);
    ui->dsMin->setDisabled(true);
    connect(m_fineness,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MeshGlobalMeshingWidget::updateFinenessControls);
    for(auto* field:{m_growthRate,m_segmentsPerEdge,m_segmentsPerRadius})
        connect(field,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{m_fineness->setCurrentIndex(5);});
    updateFinenessControls();

    connect(ui->cmbDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onDimChanged);
    connect(ui->cmbElemType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onElemTypeChanged);

    onDimChanged(ui->cmbDim->currentIndex());
}

MeshGlobalMeshingWidget::~MeshGlobalMeshingWidget()
{
    stopGeometryPick();
    m_selectionPresentation.leave();
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    delete ui;
}

void MeshGlobalMeshingWidget::beforeLoad()
{
    m_selectionPresentation.enter();
    refreshGeometryScope();
}

void MeshGlobalMeshingWidget::beforeUnload()
{
    stopGeometryPick();
    m_selectionPresentation.leave();
}

void MeshGlobalMeshingWidget::setupGeometryScope()
{
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (!layout) return;
    layout->insertWidget(0, m_selectionPresentation.createViewSelector(this));
    auto* group = new QGroupBox(tr("几何范围"), this);
    auto* content = new QVBoxLayout(group);
    m_scopeMode = new QComboBox(group);
    m_scopeMode->setObjectName("globalGeometryScopeMode");
    m_scopeMode->addItems({tr("全部几何"), tr("指定几何")});
    content->addWidget(m_scopeMode);
    m_geometryList = new QListWidget(group);
    m_geometryList->setObjectName("globalGeometryList");
    m_geometryList->setMaximumHeight(150);
    content->addWidget(m_geometryList);
    auto* actions = new QHBoxLayout;
    m_pickGeometry = new QPushButton(tr("选取几何"), group);
    m_pickGeometry->setObjectName("pickGlobalGeometry");
    auto* refresh = new QPushButton(tr("刷新"), group);
    auto* all = new QPushButton(tr("全选"), group);
    auto* clear = new QPushButton(tr("清空"), group);
    for (auto* button : {m_pickGeometry, refresh, all, clear}) actions->addWidget(button);
    content->addLayout(actions);
    m_geometryStatus = new QLabel(group);
    content->addWidget(m_geometryStatus);
    m_autoPartition = new QCheckBox(tr("统一共享拓扑（Partition）"), group);
    m_autoPartition->setObjectName("globalAutoPartition");
    m_autoPartition->setChecked(true);
    m_autoPartition->setToolTip(tr("剖分前对本次所选几何建立共享边界，使相邻区域共用网格节点；关闭时使用源几何已有拓扑。"));
    content->addWidget(m_autoPartition);
    layout->insertWidget(1, group);
    connect(refresh, &QPushButton::clicked, this, &MeshGlobalMeshingWidget::refreshGeometryScope);
    connect(m_scopeMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateGeometryScope(); });
    connect(m_geometryList, &QListWidget::itemChanged, this, [this] {
        if (m_refreshingGeometry) return;
        const int dimension = meshingDimension();
        m_geometryIds[dimension].clear();
        for (int i = 0; i < m_geometryList->count(); ++i) {
            const auto* item = m_geometryList->item(i);
            if (item->checkState() == Qt::Checked) m_geometryIds[dimension].append(item->data(Qt::UserRole).toInt());
        }
        updateGeometryScope();
    });
    auto setAll = [this](Qt::CheckState state) {
        m_scopeMode->setCurrentIndex(1);
        const QSignalBlocker block(m_geometryList);
        auto& ids = m_geometryIds[meshingDimension()];
        ids.clear();
        for (int i = 0; i < m_geometryList->count(); ++i) {
            auto* item = m_geometryList->item(i);
            item->setCheckState(state);
            if (state == Qt::Checked) ids.append(item->data(Qt::UserRole).toInt());
        }
        updateGeometryScope();
    };
    connect(all, &QPushButton::clicked, this, [setAll] { setAll(Qt::Checked); });
    connect(clear, &QPushButton::clicked, this, [setAll] { setAll(Qt::Unchecked); });
    connect(m_pickGeometry, &QPushButton::clicked, this, [this] {
        if (m_pickingGeometry) { stopGeometryPick(); return; }
        m_scopeMode->setCurrentIndex(1);
        auto* bridge = dynamic_cast<QObject*>(ServiceRegistry::instance().getService<PreProcessingService>());
        bool ok = false;
        if (bridge) QMetaObject::invokeMethod(bridge, "beginExternalEntityPick", Qt::DirectConnection,
            Q_RETURN_ARG(bool, ok), Q_ARG(QObject*, this), Q_ARG(int, meshingDimension()));
        m_pickingGeometry = ok;
        if (ok) m_pickGeometry->setText(tr("取消选取"));
        else m_geometryStatus->setText(tr("请先创建或导入几何，再选取。"));
    });
}

void MeshGlobalMeshingWidget::refreshGeometryScope()
{
    if (!m_geometryList) return;
    m_refreshingGeometry = true;
    m_geometryList->clear();
    const int dimension = meshingDimension();
    QList<int> ids;
    if (auto* geometry = ServiceRegistry::instance().getService<PreProcessingService>())
        for (const auto& text : geometry->listEntityIds(dimension)) {
            bool ok = false; const int id = text.toInt(&ok);
            if (ok && id > 0 && !ids.contains(id)) ids.append(id);
        }
    for (int id : m_geometryIds[dimension]) if (!ids.contains(id)) ids.append(id);
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        auto* item = new QListWidgetItem(dimension == 3 ? tr("体 %1").arg(id) : tr("面 %1").arg(id), m_geometryList);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_geometryIds[dimension].contains(id) ? Qt::Checked : Qt::Unchecked);
    }
    m_refreshingGeometry = false;
    updateGeometryScope();
}

void MeshGlobalMeshingWidget::updateGeometryScope()
{
    if (!m_scopeMode) return;
    const bool all = m_scopeMode->currentIndex() == 0;
    m_geometryList->setEnabled(!all);
    m_geometryStatus->setText(all ? tr("全部几何：%1 项").arg(m_geometryList->count())
        : tr("已选 %1 / %2 项").arg(m_geometryIds[meshingDimension()].size()).arg(m_geometryList->count()));
    QList<int> ids = m_geometryIds[meshingDimension()];
    if (all) { ids.clear(); for (int i = 0; i < m_geometryList->count(); ++i) ids.append(m_geometryList->item(i)->data(Qt::UserRole).toInt()); }
    m_selectionPresentation.setSelectedEntities(meshingDimension(), ids);
}

void MeshGlobalMeshingWidget::stopGeometryPick()
{
    if (m_pickingGeometry)
        if (auto* bridge = dynamic_cast<QObject*>(ServiceRegistry::instance().getService<PreProcessingService>()))
            QMetaObject::invokeMethod(bridge, "stopExternalEntityPick", Qt::DirectConnection, Q_ARG(QObject*, this));
    m_pickingGeometry = false;
    if (m_pickGeometry) m_pickGeometry->setText(tr("选取几何"));
}

bool MeshGlobalMeshingWidget::event(QEvent* event)
{
    if (event->type() == static_cast<QEvent::Type>(QEvent::User + 421) && m_pickingGeometry) {
        const int dimension = property("_geometryExternalPickDim").toInt();
        const int id = property("_geometryExternalPickId").toInt();
        if (dimension == meshingDimension() && id > 0) {
            if (!m_geometryIds[dimension].contains(id)) m_geometryIds[dimension].append(id);
            refreshGeometryScope();
        }
        stopGeometryPick();
        return true;
    }
    return BasePage::event(event);
}

void MeshGlobalMeshingWidget::appendLog(const QString& s)
{
    // Backend names are implementation details; keep user-facing logs concise.
    QString message=s;
    message.replace(QStringLiteral("SMESH"), QStringLiteral("网格"), Qt::CaseInsensitive);
    emit logMessage(message);
}

void MeshGlobalMeshingWidget::initCombos()
{
    ui->cmbDim->clear();
    ui->cmbDim->addItem(QStringLiteral("二维表面网格"), 2);
    ui->cmbDim->addItem(QStringLiteral("三维体网格"), 3);
    ui->cmbDim->setCurrentIndex(1);

    ui->dsMin->setValue(0.5);
    ui->dsMax->setValue(5.0);
    ui->dsFactor->setValue(0.5);
    ui->spOrder->setValue(1);
    ui->chkOptimize->setChecked(true);
    ui->chkCurvature->setChecked(true);
    ui->dsTol->setValue(1e-6);
}

void MeshGlobalMeshingWidget::refreshElemCombo(int dim)
{
    const int previousType = ui->cmbElemType->currentData().toInt();
    const QSignalBlocker blocker(ui->cmbElemType);
    ui->cmbElemType->clear();
    if (dim == 2)
    {
        ui->cmbElemType->addItem(QStringLiteral("三角形单元"), 1);
        ui->cmbElemType->addItem(QStringLiteral("四边形为主的面网格"), 2);
        ui->cmbElemType->setCurrentIndex(0);
    }
    else
    {
        ui->cmbElemType->addItem(QStringLiteral("四节点四面体单元"), 3);
        ui->cmbElemType->addItem(QStringLiteral("八节点六面体单元（由四面体转换）"),
                                 kElemTypeHex8FromTet);
        ui->cmbElemType->setCurrentIndex(0);
    }
    const int index = ui->cmbElemType->findData(
        SmeshMeshGenerator::elementTypeForDimension(dim, previousType));
    if (index >= 0)
        ui->cmbElemType->setCurrentIndex(index);
    onElemTypeChanged(ui->cmbElemType->currentIndex());
}

void MeshGlobalMeshingWidget::refreshAlgoCombo(int dim)
{
    ui->cmbAlgo->clear();
    if (dim == 2)
        ui->cmbAlgo->addItem(QStringLiteral("自动表面网格"), 1);
    else
        ui->cmbAlgo->addItem(QStringLiteral("自动四面体网格"), 1);
    ui->cmbAlgo->setCurrentIndex(0);
}

void MeshGlobalMeshingWidget::onDimChanged(int)
{
    const int dim = ui->cmbDim->currentData().toInt();
    refreshElemCombo(dim);
    refreshAlgoCombo(dim);
    stopGeometryPick();
    refreshGeometryScope();
}

void MeshGlobalMeshingWidget::onElemTypeChanged(int)
{
    const bool hexFromTet = ui->cmbElemType->currentData().toInt() == kElemTypeHex8FromTet;
    if (hexFromTet)
        ui->spOrder->setValue(1);
    ui->spOrder->setMaximum(hexFromTet ? 1 : 2);
}

Params MeshGlobalMeshingWidget::collectParams() const
{
    Params p;
    p.dimension = ui->cmbDim->currentData().toInt();
    p.elemType = ui->cmbElemType->currentData().toInt();
    p.lenMin = m_automaticMin->isChecked()?0.0:ui->dsMin->value();
    p.lenMax = ui->dsMax->value();
    p.nativeFineness = m_fineness->currentData().toInt();
    p.fineness = p.nativeFineness<=4?p.nativeFineness/4.0:0.5;
    p.growthRate=m_growthRate->value();p.segmentsPerEdge=m_segmentsPerEdge->value();p.segmentsPerRadius=m_segmentsPerRadius->value();
    p.elementOrder = ui->spOrder->value();
    p.optimize = ui->chkOptimize->isChecked();
    p.surfaceCurvature = ui->chkCurvature->isChecked();
    p.tolerance = ui->dsTol->value();
    return p;
}

void MeshGlobalMeshingWidget::updateFinenessControls()
{
    const int level=m_fineness->currentData().toInt();
    if(level>=0&&level<5) {
        static const double growth[]{.7,.5,.3,.2,.1},edge[]{.3,.5,1.,2.,3.},radius[]{1.,1.5,2.,3.,5.};
        const QSignalBlocker a(m_growthRate),b(m_segmentsPerEdge),c(m_segmentsPerRadius);
        m_growthRate->setValue(growth[level]);m_segmentsPerEdge->setValue(edge[level]);m_segmentsPerRadius->setValue(radius[level]);
    }
    m_growthRate->setEnabled(level==5);m_segmentsPerEdge->setEnabled(level==5);
    // Radius can be adjusted directly and promotes the preset to Custom.
    m_segmentsPerRadius->setEnabled(true);
}

QString MeshGlobalMeshingWidget::tempDirPath() const
{
    return MeshWorkingPaths::tempGridDir();
}

QString MeshGlobalMeshingWidget::tempVtkPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.vtk"));
}

QString MeshGlobalMeshingWidget::tempNasPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.nas"));
}

void MeshGlobalMeshingWidget::onGenerate()
{
    if(m_thread&&m_thread->isRunning())return;
    auto* preService = ServiceRegistry::instance().getService<PreProcessingService>();
    if (!preService)
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("前处理功能尚未就绪，请稍后重试。"));
        return;
    }

    Params p = collectParams();
    p.allGeometry = m_scopeMode->currentIndex() == 0;
    p.autoPartition = m_autoPartition->isChecked();
    if (!p.allGeometry) {
        for (int id : m_geometryIds[p.dimension]) {
            SmeshEntityReference ref; ref.dimension = p.dimension; ref.entityId = id;
            if (!preService->listEntityIds(p.dimension).contains(QString::number(id))) {
                QMessageBox::warning(this, tr("几何范围"), tr("所选几何编号已失效，请重新选择。")); return;
            }
            p.geometryScope.append(ref);
        }
        if (p.geometryScope.isEmpty()) { QMessageBox::warning(this, tr("几何范围"), tr("请至少选择一个几何对象。")); return; }
    }
    stopGeometryPick();
    QString geometryPath;
    QString geometryDiagnostic;
    if (!preService->prepareMeshingGeometry(p.dimension, geometryPath, geometryDiagnostic))
    {
        appendLog(QStringLiteral("网格几何准备失败：%1").arg(geometryDiagnostic));
        QMessageBox::warning(this, tr("网格几何检查"), geometryDiagnostic);
        return;
    }
    if (!QFileInfo::exists(geometryPath) || QFileInfo(geometryPath).size() <= 0)
    {
        appendLog(QStringLiteral("网格几何文件不存在：%1").arg(geometryPath));
        QMessageBox::warning(this, tr("网格几何检查"), tr("网格几何导出文件不存在或为空。"));
        return;
    }
    appendLog(geometryDiagnostic);
    PublicVariables::instance().set(PublicConstants::kMeshDimension, p.dimension, "mesh");
    p.inputGeometryPath = geometryPath;

    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const QVector<EdgeControlRule> edgeRules = meshService->edgeControlRules();
        const QVector<FaceControlRule> faceRules = meshService->faceControlRules();
        const QVector<VolumeControlRule> volumeRules = meshService->volumeControlRules();

        auto appendControl = [&](int dimension, int entityId, double targetSize)
        {
            if (!preService->listEntityIds(dimension).contains(QString::number(entityId)) ||
                !std::isfinite(targetSize) || targetSize <= 0.0)
            {
                ++p.skippedLocalControlCount;
                return;
            }

            // A restriction larger than the global maximum cannot refine anything.
            if (targetSize >= p.lenMax * 0.999999)
            {
                ++p.skippedLocalControlCount;
                return;
            }

            SmeshLocalSizeControl control;
            control.dimension = dimension;
            control.entityId = entityId;
            if (targetSize < p.lenMin)
                ++p.clampedLocalControlCount;
            control.targetSize = std::max(p.lenMin, targetSize);
            p.localSizeControls.push_back(control);
        };

        for (const EdgeControlRule& rule : edgeRules)
        {
            for (int entityId : parseEntityIdSet(rule.edgeIds))
            {
                double target = rule.maxSize;
                appendControl(1, entityId, target);
            }
        }

        for (const FaceControlRule& rule : faceRules)
        {
            for (int entityId : parseEntityIdSet(rule.faceIds))
            {
                double target = rule.maxSize;
                appendControl(2, entityId, target);
            }
        }

        for (const VolumeControlRule& rule : volumeRules)
        {
            for (int entityId : parseEntityIdSet(rule.volumeIds))
            {
                double target = rule.maxSize;
                appendControl(3, entityId, target);
            }
        }

        const int configuredRules = edgeRules.size() + faceRules.size() + volumeRules.size();
        if (configuredRules > 0)
        {
            appendLog(QStringLiteral("局部尺寸：边 %1，面 %2，体 %3；有效实体 %4")
                          .arg(edgeRules.size())
                          .arg(faceRules.size())
                          .arg(volumeRules.size())
                          .arg(p.localSizeControls.size()));
        }
    }

    if (p.lenMin > p.lenMax)
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("最小尺寸不能大于最大尺寸。"));
        return;
    }

    if (p.elemType == kElemTypeHex8FromTet && p.dimension != 3)
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("八节点六面体转换仅适用于三维体网格。"));
        return;
    }

    if (p.inputGeometryPath.isEmpty() || !QFileInfo::exists(p.inputGeometryPath))
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("几何模型文件不存在，请先完成几何建模。"));
        return;
    }


    if(auto* controller=dynamic_cast<MeshModuleController*>(ServiceRegistry::instance().getService<MeshModuleControllerService>())) {
        QString error;
        if(!controller->collectSmeshWorkflow(getUuid(),p.workflow,error)) {
            appendLog(error);QMessageBox::warning(this,tr("网格设置"),error);return;
        }
        if(!m_computeSubmeshUuid.isEmpty()&&!p.workflow.sweeps.isEmpty())
            appendLog(tr("当前包含扫掠区域，将应用体网格设置并生成全部网格，以保持共享界面连接。"));
        else p.workflow.computeSubmeshUuid=m_computeSubmeshUuid;
    }

    QDir dir(tempDirPath());
    if (!MeshWorkingPaths::prepare() || !dir.mkpath(tempDirPath())) {
        QMessageBox::warning(this,tr("网格生成"),tr("无法创建当前工程的网格临时目录。"));return;
    }
    QFile::remove(tempVtkPath());
    QFile::remove(tempNasPath());

    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    m_thread = new QThread(this);
    auto* worker = new MeshGlobalWorker(p);
    worker->m_tempVtkPath = tempVtkPath();
    worker->m_tempNasPath = tempNasPath();
    worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, worker, &MeshGlobalWorker::run);
    connect(worker, &MeshGlobalWorker::log, this,
            [this](const QString& s)
            {
                appendLog(s);
            });

    connect(worker, &MeshGlobalWorker::finishedOK, this,
            [this](const QString& vtk, const QString& nas)
            {
                appendLog(QStringLiteral("网格生成完成。"));
                emit meshGenerated(vtk, nas);

                auto* service =
                    ServiceRegistry::instance().getService<MeshModuleControllerService>();
                if (service)
                {
                    QString err;
                    if (!service->importGeneratedSmeshMesh(vtk, &err))
                    {
                        appendLog(QStringLiteral("生成的网格导入失败：%1").arg(err));
                        QMessageBox::warning(
                            this, QStringLiteral("网格导入失败"),
                            QStringLiteral("网格已经生成，但未能导入：%1").arg(err));
                    }
                }

                if (m_thread)
                    m_thread->quit();
            });

    connect(worker, &MeshGlobalWorker::finishedFail, this,
            [this](const QString& err)
            {
                appendLog(QStringLiteral("网格生成失败：%1").arg(err));
                QString message=err;
                message.replace(QStringLiteral("SMESH"), QStringLiteral("网格"), Qt::CaseInsensitive);
                QMessageBox::critical(this, QStringLiteral("网格生成失败"), message);
                if (m_thread)
                    m_thread->quit();
            });

    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    m_thread->start();
}

void MeshGlobalMeshingWidget::onExportAs()
{
    const QString filters = QStringLiteral("VTK 网格文件 (*.vtk);;Nastran 网格文件 (*.nas)");

    QString selectedFilter;
    QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("网格另存为"), QString(), filters, &selectedFilter);
    if (savePath.isEmpty())
        return;

    if (QFileInfo(savePath).suffix().isEmpty())
    {
        if (selectedFilter.contains(QStringLiteral("*.nas")))
            savePath += QStringLiteral(".nas");
        else
            savePath += QStringLiteral(".vtk");
    }

    QString src;
    if (savePath.endsWith(QStringLiteral(".vtk"), Qt::CaseInsensitive))
    {
        src = tempVtkPath();
    }
    else if (savePath.endsWith(QStringLiteral(".nas"), Qt::CaseInsensitive))
    {
        src = tempNasPath();
    }
    else
    {
        QMessageBox::warning(
            this, QStringLiteral("导出网格"),
            QStringLiteral("请选择支持的网格文件扩展名：.vtk、.nas"));
        return;
    }

    if (!QFileInfo::exists(src))
    {
        QMessageBox::warning(this, QStringLiteral("导出网格"),
                             QStringLiteral("临时网格文件不存在，请先生成网格。"));
        return;
    }

    QFile::remove(savePath);
    if (!QFile::copy(src, savePath))
    {
        QMessageBox::warning(this, QStringLiteral("导出网格"),
                             QStringLiteral("网格文件复制失败，请检查保存位置是否可写。"));
        return;
    }

    appendLog(QStringLiteral("网格已保存至：%1").arg(savePath));
}

void MeshGlobalMeshingWidget::onOpenTempDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(tempDirPath()));
}

void MeshGlobalMeshingWidget::on_generateButton_clicked()
{
    onGenerate();
}

void MeshGlobalMeshingWidget::showMeshVtk(const QString& vtkPath)
{
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
        return;

    auto reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
    reader->SetFileName(vtkPath.toLocal8Bit().constData());
    reader->Update();

    auto grid = reader->GetOutput();
    if (!grid || grid->GetNumberOfCells() == 0)
        return;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(grid);
    mapper->ScalarVisibilityOff();

    auto actorSurf = vtkSmartPointer<vtkActor>::New();
    actorSurf->SetMapper(mapper);
    actorSurf->GetProperty()->SetColor(0.7, 0.7, 0.7);
    actorSurf->GetProperty()->SetOpacity(0.35);
    actorSurf->GetProperty()->SetRepresentationToSurface();

    auto actorWire = vtkSmartPointer<vtkActor>::New();
    actorWire->SetMapper(mapper);
    actorWire->GetProperty()->SetColor(0.35, 0.35, 0.35);
    actorWire->GetProperty()->SetOpacity(1.0);
    actorWire->GetProperty()->SetRepresentationToWireframe();
    actorWire->GetProperty()->SetLineWidth(1.0);
    actorWire->GetProperty()->SetRenderLinesAsTubes(false);

    visual->removeProp("mesh.global.surf", false);
    visual->removeProp("mesh.global.wire", false);
    visual->removeProp("mesh.imported.surface", false);
    visual->removeProp("mesh.imported.line", false);

    visual->addProp("mesh.global.surf", actorSurf, QStringLiteral("{mesh} 表面网格"));
    visual->addProp("mesh.global.wire", actorWire, QStringLiteral("{mesh} 网格线"));
    visual->setGroupVisible("Geometry", false, false);
    visual->resetCamera();
    visual->render();
}

int MeshGlobalMeshingWidget::meshingDimension() const { return ui->cmbDim->currentData().toInt(); }
bool MeshGlobalMeshingWidget::usesAllGeometry() const { return m_scopeMode->currentIndex()==0; }
QList<int> MeshGlobalMeshingWidget::selectedGeometryIds() const { return m_geometryIds[meshingDimension()]; }
void MeshGlobalMeshingWidget::generateSubmesh(const QString& uuid)
{
    if(m_thread&&m_thread->isRunning())return;
    m_computeSubmeshUuid=uuid;onGenerate();m_computeSubmeshUuid.clear();
}
QJsonObject MeshGlobalMeshingWidget::saveJson()
{
    const auto p=collectParams();
    QJsonArray faces, solids;
    for (int id : m_geometryIds[2]) faces.append(id);
    for (int id : m_geometryIds[3]) solids.append(id);
    return {{"dimension",p.dimension},{"element_type",p.elemType},{"min_size",p.lenMin},{"max_size",p.lenMax},
        {"all_geometry",m_scopeMode->currentIndex()==0},{"geometry_faces",faces},{"geometry_solids",solids},
        {"auto_partition",m_autoPartition->isChecked()},
        {"fineness",p.fineness},{"element_order",p.elementOrder},{"optimize",p.optimize},
        {"curvature",p.surfaceCurvature},{"tolerance",p.tolerance},
        {"automatic_min_size",m_automaticMin->isChecked()},{"manual_min_size",ui->dsMin->value()},
        {"smesh_fineness",p.nativeFineness},{"growth_rate",p.growthRate},
        {"segments_per_edge",p.segmentsPerEdge},{"segments_per_radius",p.segmentsPerRadius}};
}
void MeshGlobalMeshingWidget::loadJson(const QJsonObject& o)
{
    if(o.isEmpty())return;
    for (int dimension : {2,3}) {
        m_geometryIds[dimension].clear();
        for (const auto& value : o.value(dimension==3?"geometry_solids":"geometry_faces").toArray())
            if (value.toInt()>0 && !m_geometryIds[dimension].contains(value.toInt())) m_geometryIds[dimension].append(value.toInt());
    }
    m_scopeMode->setCurrentIndex(o.value("all_geometry").toBool(true)?0:1);
    m_autoPartition->setChecked(o.value("auto_partition").toBool(true));
    ui->cmbDim->setCurrentIndex(qMax(0,ui->cmbDim->findData(o.value("dimension").toInt(3))));
    const int elementType = SmeshMeshGenerator::elementTypeForDimension(
        ui->cmbDim->currentData().toInt(), o.value("element_type").toInt(3));
    ui->cmbElemType->setCurrentIndex(qMax(0,ui->cmbElemType->findData(elementType)));
    ui->dsMin->setValue(o.value("min_size").toDouble(.5));ui->dsMax->setValue(o.value("max_size").toDouble(5));
    ui->dsFactor->setValue(o.value("fineness").toDouble(.5));ui->spOrder->setValue(o.value("element_order").toInt(1));
    ui->chkOptimize->setChecked(o.value("optimize").toBool(true));ui->chkCurvature->setChecked(o.value("curvature").toBool(true));
    ui->dsTol->setValue(o.value("tolerance").toDouble(1.e-6));
    const double legacyMin=o.value("min_size").toDouble(.5);
    ui->dsMin->setValue(o.value("manual_min_size").toDouble(legacyMin>0?legacyMin:.5));
    m_automaticMin->setChecked(o.value("automatic_min_size").toBool(legacyMin==0));
    const int legacyFineness=std::clamp(int(std::lround(o.value("fineness").toDouble(.5)*4)),0,4);
    const int level=std::clamp(o.value("smesh_fineness").toInt(legacyFineness),0,5);
    {
        const QSignalBlocker a(m_fineness),b(m_growthRate),c(m_segmentsPerEdge),d(m_segmentsPerRadius);
        m_fineness->setCurrentIndex(level);
        m_growthRate->setValue(o.value("growth_rate").toDouble(.3));
        m_segmentsPerEdge->setValue(o.value("segments_per_edge").toDouble(1.));
        m_segmentsPerRadius->setValue(o.value("segments_per_radius").toDouble(2.));
    }
    updateFinenessControls();
    refreshGeometryScope();
}
