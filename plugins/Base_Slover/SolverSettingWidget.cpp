#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QDir>
#include <QMessageBox>

#include <mesh/BoundaryConditions.h>

namespace
{
inline double GetScalar(const std::map<std::string,double> &s,
                        const std::string &key, double def = 0.0)
{
    auto it = s.find(key);
    return (it == s.end()) ? def : it->second;
}

inline int GetIntScalar(const std::map<std::string,double> &s,
                        const std::string &key, int def = 0)
{
    auto it = s.find(key);
    return (it == s.end()) ? def : static_cast<int>(it->second);
}

// 统一的动力学：给 eta -> j 以及 dj/dphi_e（注意 phi_e 在 eta 里是负号，所以 dj/dphi_e = -dj/deta）
inline void EvalJandDeriv(double eta,
                          const std::map<std::string,double> &s,
                          double &j, double &dj_dphi)
{
    const double ln10 = 2.302585092994046;

    const int model_type = GetIntScalar(s, "model_type", 1);

    j = 0.0;
    dj_dphi = 0.0;

    if (model_type == 1) // Tafel
    {
        const bool useA = (GetScalar(s, "use_anodic", 0.0) != 0.0);
        const bool useC = (GetScalar(s, "use_cathodic", 0.0) != 0.0);

        double djd_eta = 0.0;

        if (useA)
        {
            double beta_a = std::fabs(GetScalar(s, "beta_a", 0.1));
            if (beta_a <= 0.0) beta_a = 1e-12;
            const double i0_a = GetScalar(s, "i0_a", 0.0);

            const double j_a = i0_a * std::pow(10.0, eta / beta_a);
            j += j_a;
            djd_eta += (ln10 / beta_a) * j_a;
        }

        if (useC)
        {
            double beta_c = std::fabs(GetScalar(s, "beta_c", 0.1));
            if (beta_c <= 0.0) beta_c = 1e-12;
            const double i0_c = GetScalar(s, "i0_c", 0.0);

            const double t   = std::pow(10.0, -eta / beta_c);
            const double j_c = -i0_c * t;
            j += j_c;
            djd_eta += (ln10 / beta_c) * i0_c * t; // derivative of (-i0*10^(-eta/beta))
        }

        dj_dphi = -djd_eta;
    }
    else // Butler-Volmer
    {
        const double F  = 96485.0;
        const double Rg = 8.314;
        const double T  = GetScalar(s, "temp", 298.15);

        const double i0      = GetScalar(s, "i0", 0.0);
        const double alpha_a = GetScalar(s, "alpha_a", 0.5);
        const double alpha_c = GetScalar(s, "alpha_c", 0.5);

        const double k_a = alpha_a * F / (Rg * T);
        const double k_c = alpha_c * F / (Rg * T);

        const double exp_a = std::exp( k_a * eta);
        const double exp_c = std::exp(-k_c * eta);

        j = i0 * (exp_a - exp_c);
        const double djd_eta = i0 * (k_a * exp_a + k_c * exp_c);
        dj_dphi = -djd_eta;
    }
}

// 在电极边界输出 itot = j(eta)
class ItotOnBdrCoefficient : public mfem::Coefficient
{
public:
    ItotOnBdrCoefficient(const mfem::GridFunction &phil,
                         const mfem::Mesh &mesh,
                         const std::map<int, const BCSolverEntry*> &bcMap)
        : phil_(phil), mesh_(mesh), bcMap_(bcMap) {}

    double Eval(mfem::ElementTransformation &T,
                const mfem::IntegrationPoint &ip) override
    {
        const int be = T.ElementNo;
        const int battr = mesh_.GetBdrAttribute(be);

        auto it = bcMap_.find(battr);
        if (it == bcMap_.end()) return 0.0;

        const auto &s = it->second->scalars;

        const double phi_m = GetScalar(s, "phi", 0.0);
        const double Eeq   = GetScalar(s, "Eeq", 0.0);

        const double phi_e = phil_.GetValue(T, ip);
        const double eta   = phi_m - phi_e - Eeq;

        double j = 0.0, dj = 0.0;
        EvalJandDeriv(eta, s, j, dj);
        return j;
    }

private:
    const mfem::GridFunction &phil_;
    const mfem::Mesh &mesh_;
    const std::map<int, const BCSolverEntry*> &bcMap_;
};

class AbsItotOnBdrCoefficient : public mfem::Coefficient
{
public:
    AbsItotOnBdrCoefficient(const mfem::GridFunction &phil,
                            const mfem::Mesh &mesh,
                            const std::map<int, const BCSolverEntry*> &bcMap)
        : itot_(phil, mesh, bcMap) {}

    double Eval(mfem::ElementTransformation &T,
                const mfem::IntegrationPoint &ip) override
    {
        return std::fabs(itot_.Eval(T, ip));
    }

private:
    ItotOnBdrCoefficient itot_;
};

} // namespace


SolverSettingWidget::SolverSettingWidget(QWidget *parent)
    : BasePage("SolverSettingWidget")
    , ui(new Ui::SolverSettingWidget)
{
    ui->setupUi(this);
}

SolverSettingWidget::~SolverSettingWidget()
{
    delete ui;
}

void SolverSettingWidget::beforeLoad()
{

}

void SolverSettingWidget::beforeUnload()
{

}

static void MoveAndRenameVTU(const QString &folderPath)
{
    // 原始 VTU 文件名（MFEM 自动生成）
    QString oldFile = folderPath + "/tmp/Cycle000000/proc000000.vtu";
    QString newFile = folderPath + "/stress_strain1-1.vtu";

    QFile f(oldFile);
    if (f.exists())
    {
        // 删除目标文件（若存在）
        QFile::remove(newFile);

        if (!f.rename(newFile))
            qWarning() << "[MoveAndRenameVTU] rename failed:" << oldFile << "->" << newFile;
        else
            qDebug() << "[MoveAndRenameVTU] saved:" << newFile;
    }
    else
    {
        qWarning() << "[MoveAndRenameVTU] oldFile not exist:" << oldFile;
    }
}

void SolverSettingWidget::on_secondarySolveButton_clicked()
{
    // 1. 读二次电流分布的求解参数（非线性 + 线性）
    int    order     = ui->orderSpinBox->value();      // 复用一次的阶数
    int    refLevels = ui->refineSpinBox->value();    // 复用一次的细化层数

    double linRelTol = ui->relTolSpinBox->value();    // 线性求解器容差（复用）
    int    linMaxIter = ui->maxIterSpinBox->value();  // 线性求解器最大迭代
    bool   useAMG    = ui->useAmgCheckBox->isChecked();
    bool   printLog  = ui->printLogCheckBox->isChecked();

    double newtonRelTol  = ui->newtonRelTolDoubleSpinBox->value();
    int    newtonMaxIter = ui->newtonMaxIterSpinBox->value();
    double newtonDamping = ui->newtonDampingDoubleSpinBox->value();

    bool   usePrimaryAsInitial = ui->usePrimaryAsInitialCheckBox->isChecked();

    QString prefix2 = ui->secondaryOutputPrefixLineEdit->text().trimmed();
    if (prefix2.isEmpty())
        prefix2 = "secondary_current";
    std::string outputPrefix = prefix2.toStdString();

    auto &bc = BoundaryConditions::instance();

    // 2. 体的电导率 sigma：
    //    优先尝试 SecondaryCurrent,3，如果没有则 fallback 到 PrimaryCurrent,3
    auto volumeBCs_secondary = bc.exportForSolver(SolverType::SecondaryCurrent, 3);
    std::vector<BCSolverEntry> volumeBCs;
    if (!volumeBCs_secondary.empty())
    {
        volumeBCs = volumeBCs_secondary;          // OK: Entry -> Entry
    }
    else
    {
        auto volumeBCs_primary = bc.exportForSolver(SolverType::PrimaryCurrent, 3);
        volumeBCs = volumeBCs_primary;           // OK: Entry -> Entry
    }

    if (volumeBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未找到任意域（体）的电导率设置，请先在电解质设置页面中配置。"));
        return;
    }

    // 3. 电极面：来自 SecondaryCurrent, dim=2（ElectrodeSurfaceSettingWidget）
    auto faceBCs = bc.exportForSolver(SolverType::SecondaryCurrent, 2);
    if (faceBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未找到任何二次电流电极面，请在电极表面设置页面中勾选并配置二次电流分布参数。"));
        return;
    }

    // 4. 构建 attrSigma 映射 & 过滤出勾选了 secondary_enabled 的电极面
    std::map<int,double> attrSigma;
    for (const auto &e : volumeBCs)
    {
        auto it = e.scalars.find("sigma");
        if (it == e.scalars.end()) continue;
        attrSigma[e.physId] = it->second;
    }

    if (attrSigma.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("域电导率 sigma 未正确配置，请检查电解质设置页面。"));
        return;
    }

    std::vector<BCSolverEntry> electrodeBCs; // 参与二次电流分布的电极
    for (const auto &e : faceBCs)
    {
        bool secondary_enabled = false;
        if (auto it = e.scalars.find("secondary_enabled");
            it != e.scalars.end())
        {
            secondary_enabled = (it->second != 0.0);
        }

        if (!secondary_enabled)
            continue; // 没勾选“启用二次电流分布”的忽略

        electrodeBCs.push_back(e);
    }

    if (electrodeBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未勾选任何电极面启用二次电流分布，请在电极表面设置页面中勾选。"));
        return;
    }

    std::cout << "attrSigma (Secondary) mapping:\n";
    for (auto &kv : attrSigma)
    {
        std::cout << "  attr " << kv.first
                  << " -> sigma = " << kv.second << "\n";
    }

    // 5. 读取 Gmsh 网格并构造 MFEM Mesh
    QString exelPath = QCoreApplication::applicationDirPath();
    const QString tempPath = exelPath + "/../tempGrid/";
    QString mshPath = tempPath + "mesh.msh";

    std::string meshFile = mshPath.toStdString();

    mfem::Mesh mesh(meshFile.c_str(),
                    1,   // 生成边/面
                    1,   // 不做额外细化
                    false);
    mesh.PrintInfo(std::cout);
    qDebug() << "[Secondary] NE  =" << mesh.GetNE();
    qDebug() << "[Secondary] NBE =" << mesh.GetNBE();

    // 6. 调用二次电流分布求解器骨架
    try
    {
        solveSecondaryCurrentWithMfem(mesh,
                                      attrSigma,
                                      electrodeBCs,
                                      order,
                                      refLevels,
                                      linRelTol,
                                      linMaxIter,
                                      useAMG,
                                      printLog,
                                      newtonRelTol,
                                      newtonMaxIter,
                                      newtonDamping,
                                      outputPrefix,
                                      usePrimaryAsInitial);

        QMessageBox::information(this, tr("求解完成"),
                                 tr("计算完成。"));
    }
    catch (const std::exception &ex)
    {
        QMessageBox::critical(this, tr("求解错误"),
                              tr("二次电流分布求解过程中发生错误：%1").arg(ex.what()));
    }
}


void SolverSettingWidget::on_caculateButton_clicked()
{
    // 1. 从 UI 读取求解参数（一次）
    int    order     = ui->orderSpinBox->value();
    int    refLevels = ui->refineSpinBox->value();
    double relTol    = ui->relTolSpinBox->value();
    int    maxIter   = ui->maxIterSpinBox->value();
    bool   useAMG    = ui->useAmgCheckBox->isChecked();
    bool   printLog  = ui->printLogCheckBox->isChecked();

    QString prefix = ui->outputPrefixLineEdit->text().trimmed();
    if (prefix.isEmpty())
        prefix = "primary_current";
    std::string outputPrefix = prefix.toStdString();

    // 2. 从 BoundaryConditions 拿一次电流分布的体 / 面参数
    auto &bc = BoundaryConditions::instance();

    // 3D 域 (dim=3)：电导率 sigma（一次）
    auto volumeBCs = bc.exportForSolver(SolverType::PrimaryCurrent, 3);
    // 2D 面 (dim=2)：一次电流分布的 Dirichlet 电位 phi（可以来自另外一个“一次电极设置界面”）
    auto faceBCs   = bc.exportForSolver(SolverType::PrimaryCurrent, 2);

    if (volumeBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未设置任何域（体）的电导率，请在电解质设置页面中配置一次电流分布的参数。"));
        return;
    }
    if (faceBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未设置任何一次电极面电位，请在一次电极表面设置页面中配置。"));
        return;
    }

    // 3. 构建 attribute -> sigma 映射
    std::map<int,double> attrSigma;
    for (const auto &e : volumeBCs)
    {
        auto it = e.scalars.find("sigma");
        if (it == e.scalars.end()) continue;
        attrSigma[e.physId] = it->second;
    }

    if (attrSigma.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("域电导率 sigma 未正确配置，请检查一次电解质设置页面。"));
        return;
    }

    std::cout << "attrSigma (Primary) mapping:\n";
    for (auto &kv : attrSigma)
    {
        std::cout << "  attribute " << kv.first
                  << " -> sigma = " << kv.second << "\n";
    }

    // 4. 边界 attribute -> phi 映射（一次电流分布）
    std::map<int,double> bdrAttrPhi;
    for (const auto &e : faceBCs)
    {
        auto it = e.scalars.find("phi");
        if (it == e.scalars.end()) continue;
        bdrAttrPhi[e.physId] = it->second;
    }

    if (bdrAttrPhi.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("一次电极电位 phi 未正确配置，请检查一次电极表面设置页面。"));
        return;
    }

    // 5. 读取 Gmsh 网格并构造 MFEM Mesh
    QString exelPath = QCoreApplication::applicationDirPath();
    const QString tempPath = exelPath + "/../tempGrid/";
    QString mshPath = tempPath + "mesh.msh";

    std::string meshFile = mshPath.toStdString();

    mfem::Mesh mesh(meshFile.c_str(),
                    1,   // 生成边/面
                    1,   // 不做额外细化
                    false);

    mesh.PrintInfo(std::cout);
    qDebug() << "[Primary] NE  =" << mesh.GetNE();
    qDebug() << "[Primary] NBE =" << mesh.GetNBE();

    // 6. 一次电流分布求解
    try
    {
        solvePrimaryCurrentWithMfem(mesh,
                                    attrSigma,
                                    bdrAttrPhi,
                                    order,
                                    refLevels,
                                    relTol,
                                    maxIter,
                                    useAMG,
                                    printLog,
                                    outputPrefix);

        QMessageBox::information(this, tr("求解完成"),
                                 tr("计算完成。")
                                     .arg(prefix));
    }
    catch (const std::exception &ex)
    {
        QMessageBox::critical(this, tr("求解错误"),
                              tr("一次电流分布求解过程中发生错误：%1").arg(ex.what()));
    }
}


void SolverSettingWidget::solvePrimaryCurrentWithMfem(
    mfem::Mesh& mesh,
    const std::map<int,double>& attrSigma,
    const std::map<int,double>& bdrAttrPhi,
    int order,
    int refLevels,
    double relTol,
    int maxIter,
    bool useAMG,
    bool printLog,
    const std::string& outputPrefix)
{
    using namespace mfem;

    // ---- DEBUG 0: 打印 attrSigma / bdrAttrPhi 映射 ----
    std::cout << "===== DEBUG: attrSigma (volume attribute -> sigma) =====\n";
    for (auto &kv : attrSigma)
    {
        std::cout << "  attr " << kv.first << " -> sigma = " << kv.second << "\n";
    }
    std::cout << "===== DEBUG: bdrAttrPhi (bdr attribute -> phi) =====\n";
    for (auto &kv : bdrAttrPhi)
    {
        std::cout << "  bdr_attr " << kv.first << " -> phi = " << kv.second << "\n";
    }


    const int dim = mesh.Dimension();

    // (1) 网格统一细化
    for (int l = 0; l < refLevels; l++)
    {
        mesh.UniformRefinement();
    }

    // (2) H1 有限元空间
    H1_FECollection fec(order, dim);
    FiniteElementSpace fespace(&mesh, &fec);

    // (3) 构造 σ 系数（按元素 attribute 取）
    Vector sigma_vec(mesh.GetNE());
    sigma_vec = 0.0;
    for (int i = 0; i < mesh.GetNE(); ++i)
    {
        int attr = mesh.GetAttribute(i);
        auto it = attrSigma.find(attr);
        if (it != attrSigma.end())
            sigma_vec(i) = it->second;
        else
            sigma_vec(i) = 1.0; // 没找到就给 1.0，或者直接抛异常
    }
    PWConstCoefficient sigma_coef(sigma_vec);

    // (4) 组装双线性形式 a(u,v) = ∫ σ ∇u·∇v
    BilinearForm a(&fespace);
    a.AddDomainIntegrator(new DiffusionIntegrator(sigma_coef));
    a.Assemble();

    // (5) 右端项 b = 0（一次电流分布是齐次）
    LinearForm b(&fespace);
    b = 0.0;

    // (6) GridFunction x 作为解，也承载 Dirichlet 预设
    GridFunction x(&fespace);
    x = 0.0;

    // (7) 一次性设置所有 Dirichlet 边界
    Array<int> ess_bdr(mesh.bdr_attributes.Max());
    ess_bdr = 0;

    // 标记 essential 边界（和原来一样）
    for (auto &kv : bdrAttrPhi)
    {
        int battr = kv.first;
        if (battr >= 1 && battr <= ess_bdr.Size())
            ess_bdr[battr-1] = 1;
    }

    std::cout << "===== DEBUG: ess_bdr flags (index = attribute-1) =====\n";
    for (int i = 0; i < ess_bdr.Size(); ++i)
    {
        if (ess_bdr[i])
            std::cout << "  attribute " << (i+1)
                      << " is Dirichlet (essential)\n";
    }

    // 用一个 coefficient 来统一决定边界值（按 bdr attribute 查 phi）
    BdrAttrPhiCoefficient phi_coef(mesh, bdrAttrPhi);

    // 一次性 Project 所有 essential 边界
    x.ProjectBdrCoefficient(phi_coef, ess_bdr);

    std::cout << "===== CHECK: phi on boundary DOFs =====\n";
    for (int be = 0; be < mesh.GetNBE(); ++be)
    {
        int battr = mesh.GetBdrAttribute(be);

        auto it = bdrAttrPhi.find(battr);
        if (it == bdrAttrPhi.end()) continue; // 这个边界没有设置 Dirichlet

        double target_phi = it->second;

        ElementTransformation *T = mesh.GetBdrElementTransformation(be);
        Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
        const IntegrationRule &ir = IntRules.Get(geom, 2); // 稍微高一点阶

        double avg_val = 0.0;
        for (int k = 0; k < ir.GetNPoints(); k++)
        {
            const IntegrationPoint &ip = ir.IntPoint(k);
            T->SetIntPoint(&ip);
            double val = x.GetValue(*T, ip);
            avg_val += val;
        }
        avg_val /= ir.GetNPoints();

        std::cout << "bdr " << be
                  << " attr=" << battr
                  << " target=" << target_phi
                  << " avg(phi)=" << avg_val << "\n";
    }

    // (8) 形成线性系统
    SparseMatrix A;
    Vector B, X;
    Array<int> ess_tdof;

    fespace.GetEssentialTrueDofs(ess_bdr, ess_tdof);
    std::cout << "===== DEBUG: number of essential true DOFs = "
              << ess_tdof.Size() << " =====\n";

    a.FormLinearSystem(ess_tdof, x, b, A, X, B);

    // (9) 选择求解器（CG + 可选 AMG 预条件）
    CGSolver solver;
    solver.SetOperator(A);
    solver.SetRelTol(relTol);
    solver.SetMaxIter(maxIter);
    solver.SetPrintLevel(printLog ? 1 : 0);

    std::unique_ptr<Solver> prec;
    if (useAMG)
    {
#ifdef MFEM_USE_HYPRE
        auto *amg = new HypreBoomerAMG(A);
        prec.reset(amg);
        solver.SetPreconditioner(*prec);
#else
        if (printLog)
        {
            std::cerr << "[PrimaryCurrent] MFEM 未启用 HYPRE，无法使用 AMG 预条件。" << std::endl;
        }
#endif
    }

    solver.Mult(B, X);

    // (10) 回到有限元解 x
    a.RecoverFEMSolution(X, b, x);

    std::cout << "===== DEBUG: solution phi range =====\n";
    std::cout << "  phi_min = " << x.Min()
              << ", phi_max = " << x.Max() << "\n";




    // (11) 使用 ParaViewDataCollection 输出 VTU
    {
        ParaViewDataCollection d(outputPrefix.c_str(), &mesh);
        d.SetPrefixPath(".");            // 当前目录，可按需修改
        d.SetDataFormat(VTKFormat::BINARY);
        d.SetHighOrderOutput(true);      // 高阶输出
        d.SetLevelsOfDetail(order);      // LoD 设置成阶数

        d.RegisterField("phi", &x);      // 电位场

        d.SetCycle(0);
        d.SetTime(0.0);
        d.Save();
    }
}

void SolverSettingWidget::solveSecondaryCurrentWithMfem(
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
    bool usePrimaryAsInitial)
{
    using namespace mfem;

    auto NormDof = [](int dof)->int { return (dof < 0) ? (-1 - dof) : dof; };
    auto DofSign = [](int dof)->double { return (dof < 0) ? -1.0 : 1.0; };

    const int dim = mesh.Dimension();

    // 0) refine
    for (int l = 0; l < refLevels; l++) { mesh.UniformRefinement(); }

    // 1) H1 space
    H1_FECollection fec(order, dim);
    FiniteElementSpace fespace(&mesh, &fec);
    const int ndofs = fespace.GetVSize();

    // sigma (piecewise constant on elements)
    Vector sigma_vec(mesh.GetNE());
    sigma_vec = 0.0;

    bool has_missing_sigma = false;
    for (int e = 0; e < mesh.GetNE(); ++e)
    {
        const int attr = mesh.GetAttribute(e);
        auto it = attrSigma.find(attr);
        if (it != attrSigma.end()) sigma_vec(e) = it->second;
        else { sigma_vec(e) = 0.0; has_missing_sigma = true; }
    }
    if (has_missing_sigma)
    {
        std::cerr << "[Secondary][WARN] some volume attrs not in attrSigma => sigma=0.\n";
    }

    PWConstCoefficient sigma_coef(sigma_vec);

    // unknown
    GridFunction phi(&fespace);
    phi = 0.0;
    Vector x(phi.GetData(), ndofs); // alias to phi

    if (usePrimaryAsInitial)
        std::cout << "[Secondary] usePrimaryAsInitial=TRUE, but not loaded here => still 0.\n";

    // 2) linear stiffness K
    BilinearForm a_lin(&fespace);
    a_lin.AddDomainIntegrator(new DiffusionIntegrator(sigma_coef));
    a_lin.Assemble();
    a_lin.Finalize();
    SparseMatrix K = a_lin.SpMat();

    // 3) bcMap: battr -> BC entry
    std::map<int, const BCSolverEntry*> bcMap;
    for (const auto &rec : electrodeBCs) bcMap[rec.physId] = &rec;

    // 3.1) DEBUG coverage (count/area)
    {
        std::map<int,int>  battr_cnt;
        std::map<int,double> battr_area;

        for (int be = 0; be < mesh.GetNBE(); ++be)
        {
            int battr = mesh.GetBdrAttribute(be);
            if (bcMap.find(battr) == bcMap.end()) continue;

            ElementTransformation *Ttr = fespace.GetBdrElementTransformation(be);
            Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
            const IntegrationRule &irA = IntRules.Get(geom, std::max(2, 2*order));

            double area_be = 0.0;
            for (int k = 0; k < irA.GetNPoints(); ++k)
            {
                const IntegrationPoint &ip = irA.IntPoint(k);
                Ttr->SetIntPoint(&ip);
                area_be += ip.weight * Ttr->Weight();
            }

            battr_cnt[battr] += 1;
            battr_area[battr] += area_be;
        }

        std::cout << "===== [Secondary][DEBUG] electrode boundary coverage (count/area) =====\n";
        for (auto &kv : bcMap)
        {
            int battr = kv.first;
            std::cout << "  battr=" << battr
                      << "  count=" << battr_cnt[battr]
                      << "  area="  << battr_area[battr] << "\n";
        }
    }

    // 4) linear solver
    CGSolver lin_solver;
    lin_solver.SetRelTol(linRelTol);
    lin_solver.SetMaxIter(linMaxIter);
    lin_solver.SetPrintLevel(printLog ? 1 : 0);

    std::unique_ptr<Solver> prec;
#ifdef MFEM_USE_HYPRE
    if (useAMG)
    {
        auto *amg = new HypreBoomerAMG();
        prec.reset(amg);
    }
#else
    (void)useAMG;
#endif

    // 5) Newton
    Vector residual(ndofs), delta(ndofs);
    double r0 = -1.0;

    for (int it = 0; it < newtonMaxIter; ++it)
    {
        std::cout << "\n[Secondary] Newton iter " << it << "\n";

        residual = 0.0;
        K.Mult(x, residual);

        std::map<int,double> I_kin_iter;
        double eta_min=1e100, eta_max=-1e100;
        double j_min=1e100, j_max=-1e100;

        for (int be = 0; be < mesh.GetNBE(); ++be)
        {
            const int battr = mesh.GetBdrAttribute(be);
            auto itBC = bcMap.find(battr);
            if (itBC == bcMap.end()) continue;

            const auto &s = itBC->second->scalars;
            const double phi_m = GetScalar(s, "phi", 0.0);
            const double Eeq   = GetScalar(s, "Eeq", 0.0);

            Array<int> vdofs_raw;
            fespace.GetBdrElementDofs(be, vdofs_raw);

            const FiniteElement &fe = *fespace.GetBE(be);
            const int nd = fe.GetDof();

            Array<int> gdofs(nd);
            Vector sgn(nd), el_x(nd);
            for (int i = 0; i < nd; ++i)
            {
                gdofs[i] = NormDof(vdofs_raw[i]);
                sgn[i]   = DofSign(vdofs_raw[i]);
                el_x[i]  = sgn[i] * x[gdofs[i]];
            }

            Vector el_res(nd); el_res = 0.0;

            ElementTransformation *Ttr = fespace.GetBdrElementTransformation(be);
            Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
            const IntegrationRule &ir = IntRules.Get(geom, std::max(2, 2*order));

            Vector shape(nd);

            for (int ip_idx = 0; ip_idx < ir.GetNPoints(); ++ip_idx)
            {
                const IntegrationPoint &ip = ir.IntPoint(ip_idx);
                Ttr->SetIntPoint(&ip);

                const double w = ip.weight * Ttr->Weight();
                fe.CalcShape(ip, shape);

                double phi_e = 0.0;
                for (int j = 0; j < nd; ++j) phi_e += el_x[j] * shape[j];

                const double eta = phi_m - phi_e - Eeq;

                double j_val = 0.0, dj_dphi = 0.0;
                EvalJandDeriv(eta, s, j_val, dj_dphi);

                eta_min = std::min(eta_min, eta);
                eta_max = std::max(eta_max, eta);
                j_min   = std::min(j_min, j_val);
                j_max   = std::max(j_max, j_val);

                for (int i = 0; i < nd; ++i)
                    el_res[i] += (-j_val) * shape[i] * w;

                I_kin_iter[battr] += j_val * w;
            }

            for (int i = 0; i < nd; ++i)
                residual[gdofs[i]] += sgn[i] * el_res[i];
        }

        const double rnorm = residual.Norml2();
        if (it == 0) r0 = std::max(rnorm, 1e-30);

        std::cout << "  ||R||=" << rnorm << "  relR=" << (rnorm / r0) << "\n";
        std::cout << "  eta range=[" << eta_min << "," << eta_max << "]  j range=[" << j_min << "," << j_max << "]\n";

        if (printLog)
        {
            for (auto &kv : I_kin_iter)
                std::cout << "  [iter kin] battr=" << kv.first << "  I_kin=" << kv.second << "\n";
        }

        // Jacobian J = K + boundary term
        SparseMatrix J(K);

        for (int be = 0; be < mesh.GetNBE(); ++be)
        {
            const int battr = mesh.GetBdrAttribute(be);
            auto itBC = bcMap.find(battr);
            if (itBC == bcMap.end()) continue;

            const auto &s = itBC->second->scalars;
            const double phi_m = GetScalar(s, "phi", 0.0);
            const double Eeq   = GetScalar(s, "Eeq", 0.0);

            Array<int> vdofs_raw;
            fespace.GetBdrElementDofs(be, vdofs_raw);

            const FiniteElement &fe = *fespace.GetBE(be);
            const int nd = fe.GetDof();

            Array<int> gdofs(nd);
            Vector sgn(nd), el_x(nd);
            for (int i = 0; i < nd; ++i)
            {
                gdofs[i] = NormDof(vdofs_raw[i]);
                sgn[i]   = DofSign(vdofs_raw[i]);
                el_x[i]  = sgn[i] * x[gdofs[i]];
            }

            DenseMatrix el_J(nd); el_J = 0.0;

            ElementTransformation *Ttr = fespace.GetBdrElementTransformation(be);
            Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
            const IntegrationRule &ir = IntRules.Get(geom, std::max(2, 2*order));

            Vector shape(nd);

            for (int ip_idx = 0; ip_idx < ir.GetNPoints(); ++ip_idx)
            {
                const IntegrationPoint &ip = ir.IntPoint(ip_idx);
                Ttr->SetIntPoint(&ip);

                const double w = ip.weight * Ttr->Weight();
                fe.CalcShape(ip, shape);

                double phi_e = 0.0;
                for (int j = 0; j < nd; ++j) phi_e += el_x[j] * shape[j];

                const double eta = phi_m - phi_e - Eeq;

                double j_val = 0.0, dj_dphi = 0.0;
                EvalJandDeriv(eta, s, j_val, dj_dphi);

                // note: boundary Jacobian contribution is (-dj_dphi) * Ni * Nj
                for (int i = 0; i < nd; ++i)
                    for (int j = 0; j < nd; ++j)
                        el_J(i,j) += (-dj_dphi) * shape[i] * shape[j] * w;
            }

            for (int i = 0; i < nd; ++i)
                for (int j = 0; j < nd; ++j)
                    el_J(i,j) *= (sgn[i] * sgn[j]);

            J.AddSubMatrix(gdofs, gdofs, el_J);
        }

        Vector rhs(ndofs); rhs = residual; rhs *= -1.0;

#ifdef MFEM_USE_HYPRE
        if (useAMG && prec)
        {
            dynamic_cast<HypreBoomerAMG*>(prec.get())->SetOperator(J);
            lin_solver.SetPreconditioner(*prec);
        }
#endif
        lin_solver.SetOperator(J);
        delta = 0.0;
        lin_solver.Mult(rhs, delta);

        if (printLog)
        {
            std::cout << "  [lin] iters=" << lin_solver.GetNumIterations()
            << " final_norm=" << lin_solver.GetFinalNorm()
            << " converged=" << (lin_solver.GetConverged() ? "YES":"NO")
            << "\n";
        }

        x.Add(newtonDamping, delta);

        const double norm_delta = delta.Norml2();
        const double norm_x = x.Norml2();
        const double rel_change = norm_delta / (norm_x + 1e-16);

        std::cout << "  ||delta||=" << norm_delta
                  << "  ||x||=" << norm_x
                  << "  relStep=" << rel_change << "\n";

        if ((rnorm / r0) < newtonRelTol && rel_change < (10.0 * newtonRelTol))
        {
            std::cout << "[Secondary] Newton converged at iter " << it << "\n";
            break;
        }

        if (it == newtonMaxIter - 1)
            std::cerr << "[Secondary][WARN] Newton reached max iterations without convergence.\n";
    }

    // ================== POST ==================
    GridFunction phil(&fespace);
    phil = phi;

    std::cout << "===== [Secondary] phil range =====\n";
    std::cout << "  phil_min=" << phil.Min() << "  phil_max=" << phil.Max() << "\n";

    // ---- (A) IlMag (cell-wise) : |J| where J = -sigma * grad(phil) ----
    L2_FECollection l2c0(0, dim);
    FiniteElementSpace l2fes0(&mesh, &l2c0);
    GridFunction IlMag(&l2fes0);
    IlMag = 0.0;

    for (int e = 0; e < mesh.GetNE(); ++e)
    {
        ElementTransformation *T = fespace.GetElementTransformation(e);
        Geometry::Type geom = mesh.GetElementBaseGeometry(e);
        const IntegrationRule &ir = IntRules.Get(geom, std::max(2, 2*order+2));

        const int attr = mesh.GetAttribute(e);
        auto itS = attrSigma.find(attr);
        const double sigma = (itS != attrSigma.end()) ? itS->second : 0.0;

        double vol = 0.0, mag_int = 0.0;

        for (int k = 0; k < ir.GetNPoints(); ++k)
        {
            const IntegrationPoint &ip = ir.IntPoint(k);
            T->SetIntPoint(&ip);

            const double w = ip.weight * T->Weight();
            vol += w;

            Vector grad(dim);
            phil.GetGradient(*T, grad);

            // J = -sigma * grad(phi)
            grad *= (-sigma);
            mag_int += grad.Norml2() * w;
        }

        IlMag(e) = (vol > 0.0) ? (mag_int / vol) : 0.0;
    }

    // ---- (B) itot / abs_itot on electrode boundaries ----
    Array<int> elec_bdr(mesh.bdr_attributes.Max());
    elec_bdr = 0;
    for (auto &kv : bcMap)
    {
        int battr = kv.first;
        if (battr >= 1 && battr <= elec_bdr.Size()) elec_bdr[battr-1] = 1;
    }

    GridFunction itot(&fespace);     itot = 0.0;
    GridFunction abs_itot(&fespace); abs_itot = 0.0;

    ItotOnBdrCoefficient    itot_coef(phil, mesh, bcMap);
    AbsItotOnBdrCoefficient abs_itot_coef(phil, mesh, bcMap);

    itot.ProjectBdrCoefficient(itot_coef, elec_bdr);
    abs_itot.ProjectBdrCoefficient(abs_itot_coef, elec_bdr);



    // ---- output VTU ----
    {
        QString exePath = QCoreApplication::applicationDirPath();
        QString dataPath = exePath + "/../ProjectFolder/data";
        QDir().mkpath(dataPath);

        std::string prefixPath = dataPath.toStdString();
        std::string collectionName = outputPrefix.empty() ? "tmp" : outputPrefix;

        ParaViewDataCollection d(collectionName, &mesh);
        d.SetPrefixPath(prefixPath);
        d.SetDataFormat(VTKFormat::ASCII);
        d.SetHighOrderOutput(true);
        d.SetLevelsOfDetail(order);

        d.RegisterField("phil", &phil);
        d.RegisterField("IlMag", &IlMag);
        d.RegisterField("itot", &itot);
        d.RegisterField("abs_itot", &abs_itot);

        d.SetCycle(0);
        d.SetTime(0.0);
        d.Save();
    }
}
