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
    bool useCurrentGeometry = true;  // true: 导出当前几何到STEP再剖分；false: 用用户选的STEP
    QString inputStepPath;           // useCurrentGeometry=false 时使用

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

            std::vector<std::pair<int,int>> outDimTags;
            gmsh::model::occ::importShapes(stepPath.toStdString(), outDimTags, true);
            gmsh::model::occ::synchronize();

            emit log(QString("导入几何完成，实体数：%1").arg((int)outDimTags.size()));

            // 3) 物理组（建议：否则你 set SaveAll=0 时会输出为空）
            createPhysicalGroupsAllDims();

            // 4) 设置几何修复/容差
            gmsh::option::setNumber("Geometry.OCCFixDegenerated", m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCFixSmallEdges",  m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCFixSmallFaces",  m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.OCCSewFaces",       m_p.fixShape ? 1 : 0);
            gmsh::option::setNumber("Geometry.Tolerance", m_p.tolerance);

            // 5) 设置网格参数
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
                // Hex（重组）
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
            }

            emit log("网格生成完成。");

            // 6) 输出
            // 只输出物理组元素（你旧代码就是这么干的）
            gmsh::option::setNumber("Mesh.SaveAll", 0);

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
};
