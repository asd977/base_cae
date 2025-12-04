#ifndef SOLVERSETTINGWIDGET_H
#define SOLVERSETTINGWIDGET_H

#include <BasePage.h>
#include <QWidget>
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



class SolverSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit SolverSettingWidget(QWidget *parent = nullptr);
    ~SolverSettingWidget();

    void beforeLoad();
    void beforeUnload();

private slots:
    void on_caculateButton_clicked();

    void solvePrimaryCurrentWithMfem(mfem::Mesh& mesh,
                                     const std::map<int,double>& attrSigma,
                                     const std::map<int,double>& bdrAttrPhi,
                                     int order,
                                     int refLevels,
                                     double relTol,
                                     int maxIter,
                                     bool useAMG,
                                     bool printLog,
                                     const std::string& outputPrefix);

private:
    Ui::SolverSettingWidget *ui;
};

#endif // SOLVERSETTINGWIDGET_H
