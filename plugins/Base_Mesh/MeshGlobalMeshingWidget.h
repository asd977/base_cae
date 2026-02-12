#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <gmsh.h>
#include <BasePage.h>
#include <QThread>

class QThread;

namespace Ui {
class MeshGlobalMeshingWidget;
}

namespace {

struct Params
{
    QString inputStepPath = "./tmp/geom.step";           // useCurrentGeometry=false 时使用

    int dimension = 3;               // 2 or 3
    int elemType = 4;                // 2D: 1 Tri / 2 Quad; 3D: 3 Tet / 4 Hex
    double lenMin = 0.5;
    double lenMax = 5.0;
    double sizeFactor = 1.0;
    int elementOrder = 1;            // 1 or 2
    int algorithm = 1;               // 2D/3D算法号（gmsh option）
    int smoothing = 5;

    bool fixShape = true;
    double tolerance = 1e-6;

    bool exportMsh = true;
    bool exportVtk = true;
    bool exportNas = true;
};

}
// ---------------- Worker ----------------
class MeshGlobalWorker : public QObject
{
    Q_OBJECT
public:
    explicit MeshGlobalWorker(Params p, QObject* parent=nullptr)
        : QObject(parent), m_p(std::move(p)) {}

signals:
    void log(const QString& s);
    void finishedOK(const QString& msh, const QString& vtk, const QString& nas);
    void finishedFail(const QString& err);

public slots:
    void run()
    {
        try
        {
            emit log("开始整体剖分...");

            ensureGmshInitialized();

            // 1) 准备 STEP 输入
            QString stepPath = m_p.inputStepPath;
            // if (m_p.useCurrentGeometry)
            // {
            //     stepPath = m_tempStepPath;
            //     QString err;
            //     if (!exportCurrentGeometryToStep(stepPath, err))
            //     {
            //         emit finishedFail(QString("STEP 导出失败：%1").arg(err));
            //         return;
            //     }
            //     emit log(QString("STEP 已导出：%1").arg(stepPath));
            // }
            // else
            // {
            //     if (stepPath.isEmpty() || !QFileInfo::exists(stepPath))
            //     {
            //         emit finishedFail(QString("STEP 文件不存在：%1").arg(stepPath));
            //         return;
            //     }
            // }

            // 2) 清理/建模
            gmsh::clear();
            gmsh::model::add("global_mesh");

            // 几何修复相关选项必须在 importShapes 前设置；
            // 这些选项是全局状态，若放在后面会在“下一次剖分”才生效，导致跨次串扰。
            gmsh::option::setNumber("Geometry.OCCFixDegenerated", m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCFixSmallEdges",  m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCFixSmallFaces",  m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCSewFaces",       m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.Tolerance", m_p.tolerance);

            std::vector<std::pair<int,int>> outDimTags;
            gmsh::model::occ::importShapes(stepPath.toStdString(), outDimTags, true);
            // Ensure touching domains share one interface entity after STEP import.
            // 单体模型在某些 OCC 情况下会抛出 “Boolean fragments failed”，这里容错处理。
            if (outDimTags.size() > 1) {
                try {
                    gmsh::model::occ::removeAllDuplicates();
                } catch (const std::exception& e) {
                    emit log(QString("removeAllDuplicates 跳过：%1").arg(e.what()));
                } catch (...) {
                    emit log("removeAllDuplicates 跳过：unknown error");
                }
            }
            gmsh::model::occ::synchronize();

            emit log(QString("导入几何完成，实体数：%1").arg((int)outDimTags.size()));

            // 3) 物理组（建议：否则你 set SaveAll=0 时会输出为空）
            createPhysicalGroupsAllDims();

            // 4) 设置网格参数
            gmsh::option::setNumber("Mesh.CharacteristicLengthMin", m_p.lenMin);
            gmsh::option::setNumber("Mesh.CharacteristicLengthMax", m_p.lenMax);
            gmsh::option::setNumber("Mesh.MeshSizeFactor",          m_p.sizeFactor);
            gmsh::option::setNumber("Mesh.ElementOrder",            m_p.elementOrder);
            gmsh::option::setNumber("Mesh.Smoothing",               m_p.smoothing);

            // 2D/3D 单元类型
            if (m_p.dimension == 2)
            {
                // Quad（重组）
                if (m_p.elemType == 2)
                {
                    gmsh::option::setNumber("Mesh.RecombineAll", 1);
                    gmsh::option::setNumber("Mesh.SubdivisionAlgorithm", 1);
                }
                else
                {
                    gmsh::option::setNumber("Mesh.RecombineAll", 0);
                }

                gmsh::option::setNumber("Mesh.Algorithm", m_p.algorithm);
                emit log("开始生成 2D 网格...");
                gmsh::model::mesh::generate(2);
            }
            else
            {
                auto count3dCells = []() -> std::size_t {
                    std::vector<int> elemTypes3d;
                    std::vector<std::vector<std::size_t>> elemTags3d;
                    std::vector<std::vector<std::size_t>> nodeTags3d;
                    gmsh::model::mesh::getElements(elemTypes3d, elemTags3d, nodeTags3d, 3);
                    std::size_t total3d = 0;
                    for (const auto &v : elemTags3d) total3d += v.size();
                    return total3d;
                };

                // 首轮：按用户选择（tet/hex）
                if (m_p.elemType == 4)
                {
                    gmsh::option::setNumber("Mesh.Recombine3DAll", 1);
                    gmsh::option::setNumber("Mesh.SubdivisionAlgorithm", 2);
                }
                else
                {
                    gmsh::option::setNumber("Mesh.Recombine3DAll", 0);
                }

                gmsh::option::setNumber("Mesh.Algorithm3D", m_p.algorithm);
                emit log("开始生成 3D 网格...");
                gmsh::model::mesh::generate(3);

                std::size_t total3d = count3dCells();

                // 回退：若首轮没有3D单元，自动使用稳定tet方案再试一次
                if (total3d == 0)
                {
                    emit log("首轮3D生成未得到体单元，自动回退到稳定四面体方案重试...");
                    gmsh::model::mesh::clear();
                    gmsh::option::setNumber("Mesh.Recombine3DAll", 0);
                    gmsh::option::setNumber("Mesh.Algorithm3D", 1); // Delaunay
                    gmsh::model::mesh::generate(3);
                    total3d = count3dCells();
                }

                if (total3d == 0)
                {
                    emit finishedFail("3D网格生成后仍未检测到体单元（tet/hex/wedge/pyramid）。请检查几何是否为封闭体，或尝试放宽尺寸参数。\n"
                                     "若看到 no volume cells imported，通常就是这里没有生成可识别的体单元。");
                    return;
                }

                emit log(QString("3D体单元数量：%1").arg((qlonglong)total3d));
            }

            emit log("网格生成完成。");

            // 6) 输出
            // 为避免物理组过滤导致二次划分时导出缺失体单元，这里强制导出全部网格元素。
            gmsh::option::setNumber("Mesh.SaveAll", 1);

            QString msh = m_tempMshPath;
            QString vtk = m_tempVtkPath;
            QString nas = m_tempNasPath;

            if (m_p.exportNas)
            {
                gmsh::write(nas.toStdString());
                emit log(QString("已输出 NAS：%1").arg(nas));
            }
            if (m_p.exportVtk)
            {
                gmsh::write(vtk.toStdString());
                emit log(QString("已输出 VTK：%1").arg(vtk));
            }
            if (m_p.exportMsh)
            {
                gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
                gmsh::write(msh.toStdString());
                emit log(QString("已输出 MSH：%1").arg(msh));
            }

            emit finishedOK(msh, vtk, nas);
        }
        catch (const std::exception& e)
        {
            emit finishedFail(QString("异常：%1").arg(e.what()));
        }
        catch (...)
        {
            emit finishedFail("未知异常。");
        }
    }

public:
    QString m_tempDirPath;
    QString m_tempStepPath;
    QString m_tempMshPath;
    QString m_tempVtkPath;
    QString m_tempNasPath;

private:
    Params m_p;

    static void ensureGmshInitialized()
    {
        static bool inited = false;
        if (!inited)
        {
            int argc = 0;
            char** argv = nullptr;
            gmsh::initialize(argc, argv);
            // gmsh::option::setNumber("General.Terminal", 1); // 需要时打开
            inited = true;
        }
    }

    static void createPhysicalGroupsAllDims()
    {
        for (int dim = 0; dim <= 3; ++dim)
        {
            std::vector<std::pair<int,int>> ents;
            gmsh::model::getEntities(ents, dim);
            for (auto& e : ents)
            {
                const int tag = e.second;
                const int phys = gmsh::model::addPhysicalGroup(dim, std::vector<int>{tag});
                gmsh::model::setPhysicalName(dim, phys,
                                             std::string("dim") + std::to_string(dim) + "_tag" + std::to_string(tag));
            }
        }
    }

};

class MeshGlobalMeshingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit MeshGlobalMeshingWidget(QWidget* parent = nullptr);
    ~MeshGlobalMeshingWidget()
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
    void beforeLoad() override;
    void beforeUnload() override;

signals:
    void meshGenerated(const QString& mshPath, const QString& vtkPath, const QString& nasPath);
    void logMessage(const QString& msg);

private slots:
    void onGenerate();
    void onExportAs();
    void onOpenTempDir();
    void onDimChanged(int idx);
    void onElemTypeChanged(int idx);

    void on_generateButton_clicked();

private:


private:
    void initCombos();
    void refreshAlgoCombo(int dim);
    void refreshElemCombo(int dim);
    Params collectParams() const;

    QString tempDirPath() const;
    QString tempStepPath() const;
    QString tempMshPath() const;
    QString tempVtkPath() const;
    QString tempNasPath() const;

    void appendLog(const QString& s);

private:
    Ui::MeshGlobalMeshingWidget* ui = nullptr;
    QThread* m_thread = nullptr;
    void showMeshVtk(const QString &vtkPath);
};
