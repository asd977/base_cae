#include "MeshSweepMeshingWidget.h"

#include <ServiceRegistry.h>
#include <gmsh.h>
#include <services/MeshModuleControllerService.h>
#include <services/PreProcessingService.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
void ensureGmshInitialized()
{
    static bool s_initialized = false;
    if (!s_initialized)
    {
        gmsh::initialize();
        s_initialized = true;
    }
}

QString stepExportPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return appDir + "/data/ExportedModel.step";
}

std::vector<int> boundaryEntityTags(int dim, int tag, int boundaryDim)
{
    std::vector<int> ret;
    std::vector<std::pair<int, int>> bnd;
    gmsh::model::getBoundary({{dim, tag}}, bnd, false, false, false);
    for (const auto& dt : bnd)
    {
        if (dt.first == boundaryDim)
            ret.push_back(dt.second);
    }
    std::sort(ret.begin(), ret.end());
    ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
    return ret;
}

bool canUseMappedSurface(int surfaceTag)
{
    const auto curves = boundaryEntityTags(2, surfaceTag, 1);
    // Gmsh transfinite surface稳定工作在 3/4 边界曲线情形
    return curves.size() == 3 || curves.size() == 4;
}

bool canUseSweepVolume(int volumeTag)
{
    const auto faces = boundaryEntityTags(3, volumeTag, 2);
    // Gmsh transfinite volume 通常要求 5/6 面拓扑（楔体/六面体逻辑）
    return faces.size() == 5 || faces.size() == 6;
}

} // namespace

MeshSweepMeshingWidget::MeshSweepMeshingWidget(WorkMode mode, QWidget* parent)
    : BasePage(mode == WorkMode::Mapped2D ? "MeshMappedMeshingWidget" : "MeshSweepMeshingWidget"),
      m_mode(mode)
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);

    auto* title = new QGroupBox(mode == WorkMode::Mapped2D ? "映射剖分设置" : "扫掠剖分设置", this);
    auto* form = new QFormLayout(title);

    m_curveDivisions = new QSpinBox(this);
    m_curveDivisions->setRange(2, 500);
    m_curveDivisions->setValue(mode == WorkMode::Mapped2D ? 12 : 20);

    m_sweepLayers = new QSpinBox(this);
    m_sweepLayers->setRange(2, 500);
    m_sweepLayers->setValue(20);
    m_sweepLayers->setEnabled(mode == WorkMode::Sweep3D);

    m_curveBias = new QDoubleSpinBox(this);
    m_curveBias->setRange(0.1, 50.0);
    m_curveBias->setDecimals(3);
    m_curveBias->setSingleStep(0.1);
    m_curveBias->setValue(1.0);

    m_recombineFaces = new QCheckBox("启用映射面（Quad）", this);
    m_recombineFaces->setChecked(true);

    m_recombineVolumes = new QCheckBox("启用扫掠体（Hex）", this);
    m_recombineVolumes->setChecked(mode == WorkMode::Sweep3D);
    m_recombineVolumes->setEnabled(mode == WorkMode::Sweep3D);

    form->addRow("边划分数", m_curveDivisions);
    if (mode == WorkMode::Sweep3D)
        form->addRow("扫掠层数（优先）", m_sweepLayers);
    form->addRow("偏置系数", m_curveBias);
    form->addRow(m_recombineFaces);
    if (mode == WorkMode::Sweep3D)
        form->addRow(m_recombineVolumes);

    root->addWidget(title);

    m_generateButton = new QPushButton(mode == WorkMode::Mapped2D ? "执行映射剖分" : "执行扫掠剖分", this);
    root->addWidget(m_generateButton);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    root->addWidget(m_log, 1);

    connect(m_generateButton, &QPushButton::clicked, this, &MeshSweepMeshingWidget::onGenerate);
}

void MeshSweepMeshingWidget::beforeLoad()
{
}

void MeshSweepMeshingWidget::beforeUnload()
{
}

void MeshSweepMeshingWidget::appendLog(const QString& text)
{
    if (m_log)
        m_log->appendPlainText(text);
}

bool MeshSweepMeshingWidget::prepareModel(const QString& stepPath, QString* errMsg)
{
    if (!QFileInfo::exists(stepPath))
    {
        if (errMsg)
            *errMsg = QString("STEP 文件不存在：%1").arg(stepPath);
        return false;
    }

    try
    {
        ensureGmshInitialized();
        gmsh::clear();
        gmsh::model::add(m_mode == WorkMode::Mapped2D ? "mapped_mesh" : "sweep_mesh");

        gmsh::option::setNumber("Geometry.OCCFixDegenerated", 1);
        gmsh::option::setNumber("Geometry.OCCFixSmallEdges", 1);
        gmsh::option::setNumber("Geometry.OCCFixSmallFaces", 1);
        gmsh::option::setNumber("Geometry.OCCSewFaces", 0);

        std::vector<std::pair<int, int>> outDimTags;
        gmsh::model::occ::importShapes(stepPath.toStdString(), outDimTags, true);
        try
        {
            gmsh::model::occ::removeAllDuplicates();
        }
        catch (...)
        {
        }
        gmsh::model::occ::synchronize();

        appendLog(QString("几何导入完成，实体数：%1").arg((int)outDimTags.size()));
        return true;
    }
    catch (const std::exception& e)
    {
        if (errMsg)
            *errMsg = e.what();
        return false;
    }
}

bool MeshSweepMeshingWidget::runMapped(const QString& stepPath, QString* errMsg)
{
    if (!prepareModel(stepPath, errMsg))
        return false;

    const int divs = m_curveDivisions->value();
    const double bias = m_curveBias->value();

    try
    {
        std::vector<std::pair<int, int>> curves;
        std::vector<std::pair<int, int>> faces;
        gmsh::model::getEntities(curves, 1);
        gmsh::model::getEntities(faces, 2);

        for (const auto& c : curves)
            gmsh::model::mesh::setTransfiniteCurve(c.second, divs, "Progression", bias);

        int mappedCount = 0;
        int skippedCount = 0;
        for (const auto& s : faces)
        {
            if (!canUseMappedSurface(s.second))
            {
                ++skippedCount;
                continue;
            }
            try
            {
                gmsh::model::mesh::setTransfiniteSurface(s.second);
                if (m_recombineFaces->isChecked())
                    gmsh::model::mesh::setRecombine(2, s.second);
                ++mappedCount;
            }
            catch (...)
            {
                ++skippedCount;
            }
        }

        if (mappedCount == 0)
        {
            if (errMsg)
                *errMsg = "没有可映射的面（需要 3/4 边拓扑）。请先在几何里分块后再试。";
            return false;
        }

        gmsh::option::setNumber("Mesh.RecombineAll", m_recombineFaces->isChecked() ? 1 : 0);
        gmsh::model::mesh::generate(2);
        appendLog(QString("映射剖分生成完成（2D）：成功 %1，跳过 %2").arg(mappedCount).arg(skippedCount));
        return true;
    }
    catch (const std::exception& e)
    {
        if (errMsg)
            *errMsg = e.what();
        return false;
    }
}

bool MeshSweepMeshingWidget::runSweep(const QString& stepPath, QString* errMsg)
{
    if (!prepareModel(stepPath, errMsg))
        return false;

    const int divs = std::max(m_curveDivisions->value(), m_sweepLayers->value());
    const double bias = m_curveBias->value();

    try
    {
        std::vector<std::pair<int, int>> curves;
        std::vector<std::pair<int, int>> faces;
        std::vector<std::pair<int, int>> volumes;
        gmsh::model::getEntities(curves, 1);
        gmsh::model::getEntities(faces, 2);
        gmsh::model::getEntities(volumes, 3);

        for (const auto& c : curves)
            gmsh::model::mesh::setTransfiniteCurve(c.second, divs, "Progression", bias);

        int mappedFaceCount = 0;
        int skippedFaceCount = 0;
        for (const auto& s : faces)
        {
            if (!canUseMappedSurface(s.second))
            {
                ++skippedFaceCount;
                continue;
            }
            try
            {
                gmsh::model::mesh::setTransfiniteSurface(s.second);
                if (m_recombineFaces->isChecked())
                    gmsh::model::mesh::setRecombine(2, s.second);
                ++mappedFaceCount;
            }
            catch (...)
            {
                ++skippedFaceCount;
            }
        }

        int sweepVolumeCount = 0;
        int skippedVolumeCount = 0;
        for (const auto& v : volumes)
        {
            if (!canUseSweepVolume(v.second))
            {
                ++skippedVolumeCount;
                continue;
            }
            try
            {
                gmsh::model::mesh::setTransfiniteVolume(v.second);
                ++sweepVolumeCount;
            }
            catch (...)
            {
                ++skippedVolumeCount;
            }
        }

        if (sweepVolumeCount == 0)
        {
            if (errMsg)
                *errMsg = "没有可扫掠的体（需要 5/6 面拓扑且源/目标面可对应）。请先分块。";
            return false;
        }

        gmsh::option::setNumber("Mesh.RecombineAll", m_recombineFaces->isChecked() ? 1 : 0);
        gmsh::option::setNumber("Mesh.Recombine3DAll", m_recombineVolumes->isChecked() ? 1 : 0);

        gmsh::model::mesh::generate(3);
        appendLog(QString("扫掠剖分生成完成（3D）：体成功 %1，体跳过 %2，面映射 %3，面跳过 %4")
                      .arg(sweepVolumeCount)
                      .arg(skippedVolumeCount)
                      .arg(mappedFaceCount)
                      .arg(skippedFaceCount));
        return true;
    }
    catch (const std::exception& e)
    {
        if (errMsg)
            *errMsg = e.what();
        return false;
    }
}

void MeshSweepMeshingWidget::onGenerate()
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    if (!pre)
    {
        QMessageBox::warning(this, "网格", "PreProcessingService 不可用。");
        return;
    }

    pre->generateStepFile();
    const QString stepPath = stepExportPath();
    QString err;

    appendLog(QString("开始处理：%1").arg(stepPath));

    const bool ok = (m_mode == WorkMode::Mapped2D) ? runMapped(stepPath, &err)
                                                    : runSweep(stepPath, &err);
    if (!ok)
    {
        QMessageBox::critical(this, "网格", QString("执行失败：%1").arg(err));
        appendLog(QString("执行失败：%1").arg(err));
        return;
    }

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
    {
        QMessageBox::warning(this, "网格", "网格已生成，但 MeshModuleControllerService 不可用。");
        return;
    }

    if (!meshSvc->importCurrentGmshMesh(&err))
    {
        QMessageBox::warning(this, "网格", QString("网格生成成功，但导入失败：%1").arg(err));
        appendLog(QString("导入失败：%1").arg(err));
        return;
    }

    QMessageBox::information(this, "网格", m_mode == WorkMode::Mapped2D ? "映射剖分成功。"
                                                                          : "扫掠剖分成功。");
}
