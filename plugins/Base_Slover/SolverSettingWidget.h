#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>
#include <mesh/BoundaryConditions.h>
#include "mfem.hpp"
namespace Ui {
class SolverSettingWidget;
}

// 按边界 attribute 取 Dirichlet 电位 φ 的系数
class BdrAttrPhiCoefficient : public mfem::Coefficient
{
public:
    BdrAttrPhiCoefficient(const mfem::Mesh &mesh,
                          const std::map<int,double> &bdrAttrPhi)
        : mesh_(mesh), bdrAttrPhi_(bdrAttrPhi) {}

    virtual double Eval(mfem::ElementTransformation &T,
                        const mfem::IntegrationPoint &ip)
    {
        // 这里很关键：T 对于边界单元时有 BdrElementNo 信息
        int be = T.ElementNo; // 边界单元编号
        int battr = mesh_.GetBdrAttribute(be);

        auto it = bdrAttrPhi_.find(battr);
        if (it != bdrAttrPhi_.end())
            return it->second;
        else
            return 0.0; // 或者其它默认
    }

private:
    const mfem::Mesh &mesh_;
    const std::map<int,double> &bdrAttrPhi_;
};

// 计算电解质电流密度 J = -sigma * grad(phi_e) 的 VectorCoefficient
class ElectrolyteCurrentCoefficient : public mfem::VectorCoefficient
{
public:
    ElectrolyteCurrentCoefficient(const mfem::Mesh &mesh,
                                  const mfem::GridFunction &phi,
                                  const std::map<int,double> &attrSigma)
        : mfem::VectorCoefficient(mesh.Dimension())
        , m_mesh(mesh)
        , m_phi(phi)
        , m_attrSigma(attrSigma)
    {}

    virtual void Eval(mfem::Vector &J,
                      mfem::ElementTransformation &T,
                      const mfem::IntegrationPoint &ip) override
    {
        using namespace mfem;
        const int dim = m_mesh.Dimension();

        J.SetSize(dim);

        // 在当前单元 / 积分点上求 grad(phi_e)
        Vector grad(dim);
        T.SetIntPoint(&ip);
        m_phi.GetGradient(T, grad);  // 注意：T 已经带有 int point

        // 查这个单元的 attribute -> sigma
        int elem = T.ElementNo;
        int attr = m_mesh.GetAttribute(elem);
        double sigma = 0.0;
        auto it = m_attrSigma.find(attr);
        if (it != m_attrSigma.end())
            sigma = it->second;

        // J = -sigma * grad(phi_e)
        for (int d = 0; d < dim; ++d)
            J[d] = -sigma * grad[d];
    }

private:
    const mfem::Mesh &m_mesh;
    const mfem::GridFunction &m_phi;
    const std::map<int,double> &m_attrSigma;
};


class SolverSettingWidget : public BasePage
{
    Q_OBJECT
public:
    explicit SolverSettingWidget(QWidget *parent = nullptr);
    ~SolverSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    // 一次电流分布
    void on_caculateButton_clicked();

    // 二次电流分布
    void on_secondarySolveButton_clicked();

private:
    Ui::SolverSettingWidget *ui;

    // 一次电流分布求解器（你原来的实现）
    void solvePrimaryCurrentWithMfem(
        mfem::Mesh& mesh,
        const std::map<int,double>& attrSigma,
        const std::map<int,double>& bdrAttrPhi,
        int order,
        int refLevels,
        double relTol,
        int maxIter,
        bool useAMG,
        bool printLog,
        const std::string& outputPrefix);

    // 二次电流分布求解器骨架
    void solveSecondaryCurrentWithMfem(
        mfem::Mesh& mesh,
        const std::map<int,double>& attrSigma,
        const std::vector<BCSolverEntry>& electrodeBCs,
        int order,
        int refLevels,
        double linRelTol,
        int linMaxIter,
        bool useAMG,
        bool printLog,
        double newtonRelTol,
        int newtonMaxIter,
        double newtonDamping,
        const std::string &outputPrefix,
        bool usePrimaryAsInitial);
};

#endif // SOLVERSETTINGWIDGET_H
