#include "MeshGlobalMeshingWidget.h"

#include "NetgenMeshGenerator.h"
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

double entityCharacteristicLength(int dimension, double measure)
{
    if (!std::isfinite(measure) || measure <= 0.0)
        return 0.0;
    if (dimension == 1)
        return measure;
    if (dimension == 2)
        return std::sqrt(measure);
    return std::cbrt(measure);
}

}

MeshGlobalWorker::MeshGlobalWorker(Params p, QObject* parent)
    : QObject(parent), m_p(std::move(p))
{
}

void MeshGlobalWorker::run()
{
    NetgenMeshingRequest request;
    request.inputGeometryPath = m_p.inputStepPath;
    request.volPath = m_tempVolPath;
    request.vtkPath = m_tempVtkPath;
    request.nasPath = m_tempNasPath;
    request.dimension = m_p.dimension;
    request.elemType = m_p.elemType;
    request.lenMin = m_p.lenMin;
    request.lenMax = m_p.lenMax;
    request.fineness = m_p.fineness;
    request.elementOrder = m_p.elementOrder;
    request.smoothing = m_p.smoothing;
    request.localSizeControls = m_p.localSizeControls;
    request.fixShape = m_p.fixShape;
    request.tolerance = m_p.tolerance;
    request.exportVol = m_p.exportVol && m_p.elemType != kElemTypeHex8FromTet;
    request.exportVtk = m_p.exportVtk;
    request.exportNas = m_p.exportNas;

    if (!m_p.localSizeControls.isEmpty())
    {
        emit log(QStringLiteral("应用局部加密：%1 个几何实体").arg(m_p.localSizeControls.size()));
        if (m_p.legacyRuleCount > 0)
        {
            emit log(QStringLiteral(
                "其中 %1 条旧版 Gmsh 规则已自动换算为 Netgen 局部最大尺寸。")
                         .arg(m_p.legacyRuleCount));
        }
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
    NetgenMeshingResult result;
    if (!NetgenMeshGenerator::generate(request, &result, &err))
    {
        emit finishedFail(err.isEmpty() ? QStringLiteral("网格生成失败") : err);
        return;
    }

    if (result.appliedLocalControlCount > 0)
    {
        emit log(QStringLiteral("局部尺寸已应用：%1 个实体，写入 %2 个 Netgen 尺寸约束点")
                     .arg(result.appliedLocalControlCount)
                     .arg(result.localRestrictionPointCount));
    }

    if (result.conformalized)
    {
        emit log(QStringLiteral(
                     "材料分区共形处理完成：原始区域 %1，处理后区域 %2，共享界面 %3；相邻区域共用节点。")
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
    emit finishedOK(m_tempVolPath, m_tempVtkPath, m_tempNasPath);
}

MeshGlobalMeshingWidget::MeshGlobalMeshingWidget(QWidget* parent)
    : BasePage("MeshGlobalMeshingWidget"), ui(new Ui::MeshGlobalMeshingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    // Use Netgen's own global controls. The previous "size factor" was a
    // Gmsh-style multiplier and made local rules difficult to predict.
    ui->labelFactor->setText(tr("细化程度"));
    ui->labelFactor->setToolTip(tr("Netgen fineness：0 表示偏粗，1 表示偏细。"));
    ui->dsFactor->setDecimals(2);
    ui->dsFactor->setRange(0.0, 1.0);
    ui->dsFactor->setSingleStep(0.05);
    ui->dsFactor->setValue(0.5);
    ui->dsFactor->setToolTip(ui->labelFactor->toolTip());
    ui->labelMin->setToolTip(
        tr("Netgen 的全局尺寸下限；局部最大尺寸小于该值时会按此值处理。"));
    ui->dsMin->setToolTip(ui->labelMin->toolTip());
    ui->labelMax->setToolTip(tr("未设置局部控制区域所使用的全局最大尺寸。"));
    ui->dsMax->setToolTip(ui->labelMax->toolTip());

    initCombos();

    connect(ui->cmbDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onDimChanged);
    connect(ui->cmbElemType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MeshGlobalMeshingWidget::onElemTypeChanged);

    onDimChanged(ui->cmbDim->currentIndex());
}

MeshGlobalMeshingWidget::~MeshGlobalMeshingWidget()
{
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
}

void MeshGlobalMeshingWidget::beforeUnload()
{
}

void MeshGlobalMeshingWidget::appendLog(const QString& s)
{
    emit logMessage(s);
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
    ui->spSmooth->setValue(5);
    ui->chkFix->setChecked(true);
    ui->dsTol->setValue(1e-6);
}

void MeshGlobalMeshingWidget::refreshElemCombo(int dim)
{
    ui->cmbElemType->clear();
    if (dim == 2)
    {
        ui->cmbElemType->addItem(QStringLiteral("三角形单元"), 1);
        ui->cmbElemType->addItem(QStringLiteral("四边形为主"), 2);
        ui->cmbElemType->setCurrentIndex(0);
    }
    else
    {
        ui->cmbElemType->addItem(QStringLiteral("四节点四面体单元"), 3);
        ui->cmbElemType->addItem(QStringLiteral("八节点六面体单元（由四面体转换）"),
                                 kElemTypeHex8FromTet);
        ui->cmbElemType->setCurrentIndex(0);
    }
}

void MeshGlobalMeshingWidget::refreshAlgoCombo(int dim)
{
    ui->cmbAlgo->clear();
    if (dim == 2)
        ui->cmbAlgo->addItem(QStringLiteral("表面网格算法"), 1);
    else
        ui->cmbAlgo->addItem(QStringLiteral("体网格算法"), 1);
    ui->cmbAlgo->setCurrentIndex(0);
}

void MeshGlobalMeshingWidget::onDimChanged(int)
{
    const int dim = ui->cmbDim->currentData().toInt();
    refreshElemCombo(dim);
    refreshAlgoCombo(dim);
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
    p.lenMin = ui->dsMin->value();
    p.lenMax = ui->dsMax->value();
    p.fineness = ui->dsFactor->value();
    p.elementOrder = ui->spOrder->value();
    p.algorithm = ui->cmbAlgo->currentData().toInt();
    p.smoothing = ui->spSmooth->value();
    p.fixShape = ui->chkFix->isChecked();
    p.tolerance = ui->dsTol->value();
    return p;
}

QString MeshGlobalMeshingWidget::tempDirPath() const
{
    return QCoreApplication::applicationDirPath();
}

QString MeshGlobalMeshingWidget::tempStepPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("geom.step"));
}

QString MeshGlobalMeshingWidget::tempVolPath() const
{
    return QDir(tempDirPath()).filePath(QStringLiteral("mesh.vol"));
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
    auto* preService = ServiceRegistry::instance().getService<PreProcessingService>();
    if (!preService)
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("前处理功能尚未就绪，请稍后重试。"));
        return;
    }

    preService->generateStepFile();

    const QString filePath =
        QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data")))
            .filePath(QStringLiteral("ExportedModel.step"));

    Params p = collectParams();
    PublicVariables::instance().set(PublicConstants::kMeshDimension, p.dimension, "mesh");
    p.inputStepPath = filePath;

    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        const QVector<EdgeControlRule> edgeRules = meshService->edgeControlRules();
        const QVector<FaceControlRule> faceRules = meshService->faceControlRules();
        const QVector<VolumeControlRule> volumeRules = meshService->volumeControlRules();

        auto appendControl = [&](int dimension, int entityId, double measure, double targetSize)
        {
            QVector3D center;
            if (!preService->entityCenter(dimension, entityId, center) ||
                !std::isfinite(measure) || measure <= 0.0 ||
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

            NetgenLocalSizeControl control;
            control.dimension = dimension;
            control.entityId = entityId;
            control.center = center;
            control.characteristicLength = entityCharacteristicLength(dimension, measure);
            if (targetSize < p.lenMin)
                ++p.clampedLocalControlCount;
            control.targetSize = std::max(p.lenMin, targetSize);
            p.localSizeControls.push_back(control);
        };

        for (const EdgeControlRule& rule : edgeRules)
        {
            for (int entityId : parseEntityIdSet(rule.edgeIds))
            {
                double length = 0.0;
                if (!preService->entityMeasure(1, entityId, length) || length <= 0.0)
                {
                    ++p.skippedLocalControlCount;
                    continue;
                }

                double target = rule.maxSize;
                if (target <= 0.0)
                {
                    target = length / std::max(1, rule.divisions);
                    ++p.legacyRuleCount;
                }
                appendControl(1, entityId, length, target);
            }
        }

        for (const FaceControlRule& rule : faceRules)
        {
            for (int entityId : parseEntityIdSet(rule.faceIds))
            {
                double area = 0.0;
                if (!preService->entityMeasure(2, entityId, area) || area <= 0.0)
                {
                    ++p.skippedLocalControlCount;
                    continue;
                }

                double target = rule.maxSize;
                if (target <= 0.0)
                {
                    target = p.lenMax * rule.localSizeFactor;
                    ++p.legacyRuleCount;
                }
                appendControl(2, entityId, area, target);
            }
        }

        for (const VolumeControlRule& rule : volumeRules)
        {
            for (int entityId : parseEntityIdSet(rule.volumeIds))
            {
                double volume = 0.0;
                if (!preService->entityMeasure(3, entityId, volume) || volume <= 0.0)
                {
                    ++p.skippedLocalControlCount;
                    continue;
                }

                double target = rule.maxSize;
                if (target <= 0.0)
                {
                    target = p.lenMax * rule.localSizeFactor;
                    ++p.legacyRuleCount;
                }
                appendControl(3, entityId, volume, target);
            }
        }

        const int configuredRules = edgeRules.size() + faceRules.size() + volumeRules.size();
        if (configuredRules > 0)
        {
            appendLog(QStringLiteral("读取 Netgen 局部尺寸：边 %1，面 %2，体 %3；有效实体 %4")
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

    if (p.inputStepPath.isEmpty() || !QFileInfo::exists(p.inputStepPath))
    {
        QMessageBox::warning(this, QStringLiteral("网格划分"),
                             QStringLiteral("几何模型文件不存在，请先完成几何建模。"));
        return;
    }

    m_lastGeneratedHex8 = p.elemType == kElemTypeHex8FromTet;

    QDir dir(tempDirPath());
    dir.mkpath(tempDirPath());
    QFile::remove(tempVolPath());
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
    worker->m_tempDirPath = tempDirPath();
    worker->m_tempStepPath = tempStepPath();
    worker->m_tempVolPath = tempVolPath();
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
            [this](const QString& vol, const QString& vtk, const QString& nas)
            {
                appendLog(QStringLiteral("网格生成完成。"));
                emit meshGenerated(vol, vtk, nas);

                auto* service =
                    ServiceRegistry::instance().getService<MeshModuleControllerService>();
                if (service)
                {
                    QString err;
                    if (!service->importGeneratedNetgenMesh(vtk, &err))
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
                QMessageBox::critical(this, QStringLiteral("网格生成失败"), err);
                if (m_thread)
                    m_thread->quit();
            });

    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    m_thread->start();
}

void MeshGlobalMeshingWidget::onExportAs()
{
    const QString filters =
        m_lastGeneratedHex8
            ? QStringLiteral("VTK 网格文件 (*.vtk);;Nastran 网格文件 (*.nas);;所有文件 (*.*)")
            : QStringLiteral("Netgen 网格文件 (*.vol);;VTK 网格文件 (*.vtk);;Nastran 网格文件 (*.nas);;所有文件 (*.*)");

    QString selectedFilter;
    QString savePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("网格另存为"), QString(), filters, &selectedFilter);
    if (savePath.isEmpty())
        return;

    if (QFileInfo(savePath).suffix().isEmpty())
    {
        if (selectedFilter.contains(QStringLiteral("*.nas")))
            savePath += QStringLiteral(".nas");
        else if (!m_lastGeneratedHex8 && selectedFilter.contains(QStringLiteral("*.vol")))
            savePath += QStringLiteral(".vol");
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
    else if (savePath.endsWith(QStringLiteral(".vol"), Qt::CaseInsensitive))
    {
        if (m_lastGeneratedHex8)
        {
            QMessageBox::warning(
                this, QStringLiteral("导出网格"),
                QStringLiteral("八节点六面体转换结果不能保存为 Netgen 网格格式，"
                               "请选择 VTK 或 Nastran 网格格式。"));
            return;
        }
        src = tempVolPath();
    }
    else
    {
        QMessageBox::warning(
            this, QStringLiteral("导出网格"),
            QStringLiteral("请选择支持的网格文件扩展名：.vtk、.nas%1")
                .arg(m_lastGeneratedHex8 ? QString() : QStringLiteral(" 或 .vol")));
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

    visual->addProp("mesh.global.surf", actorSurf, QStringLiteral("{mesh} Netgen surface"));
    visual->addProp("mesh.global.wire", actorWire, QStringLiteral("{mesh} Netgen wire"));
    visual->setGroupVisible("Geometry", false, false);
    visual->resetCamera();
    visual->render();
}
