#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QDir>
#include <QMessageBox>

#include <mesh/BoundaryConditions.h>

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


    // ---- DEBUG 3: 检查 ProjectBdrCoefficient 之后，实际边界上的 x 值 ----
    std::cout << "===== DEBUG: value of x on each boundary element center (AFTER ProjectBdrCoefficient) =====\n";
    for (int i = 0; i < mesh.GetNBE(); i++)
    {
        int battr = mesh.GetBdrAttribute(i);

        // 边界单元的几何中心
        Array<int> v;
        mesh.GetBdrElement(i)->GetVertices(v);
        double cx=0, cy=0, cz=0;
        for (int k = 0; k < v.Size(); k++)
        {
            const double *p = mesh.GetVertex(v[k]);
            cx += p[0]; cy += p[1]; cz += p[2];
        }
        cx /= v.Size(); cy /= v.Size(); cz /= v.Size();

        // 在该边界单元中心点 eval 一下 x
        ElementTransformation *T = mesh.GetBdrElementTransformation(i);
        Geometry::Type geom = mesh.GetBdrElementBaseGeometry(i);
        const IntegrationRule &ir = IntRules.Get(geom, 1);

        const IntegrationPoint &ip = ir.IntPoint(0);
        T->SetIntPoint(&ip);

        double val = x.GetValue(*T, ip);

        std::cout << "  bdr " << i
                  << " attr=" << battr
                  << " center=(" << cx << "," << cy << "," << cz << ")"
                  << "  x(center)=" << val;

        auto it = bdrAttrPhi.find(battr);
        if (it != bdrAttrPhi.end())
            std::cout << "  (target phi=" << it->second << ")";
        std::cout << "\n";
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

    const int dim = mesh.Dimension();

    // --------- 0. 网格统一细化 ---------
    for (int l = 0; l < refLevels; l++)
    {
        mesh.UniformRefinement();
    }

    // --------- 1. H1 空间 & sigma 系数 ---------
    H1_FECollection fec(order, dim);
    FiniteElementSpace fespace(&mesh, &fec);

    const int ndofs = fespace.GetVSize();

    // *** 1.1: 构造 sigma，只允许在 attrSigma 里配置的体作为电解质 ***
    Vector sigma_vec(mesh.GetNE());
    sigma_vec = 0.0;

    std::cout << "[Secondary] attrSigma (volume attribute -> sigma):\n";
    for (auto &kv : attrSigma)
    {
        std::cout << "  attr " << kv.first << " -> sigma=" << kv.second << "\n";
    }

    bool has_missing_sigma = false;
    for (int i = 0; i < mesh.GetNE(); ++i)
    {
        int attr = mesh.GetAttribute(i);
        auto it = attrSigma.find(attr);
        if (it != attrSigma.end())
        {
            sigma_vec(i) = it->second; // 电解质导电率
        }
        else
        {
            // 没配置 sigma 的体：当成绝缘体（不导电），sigma = 0
            sigma_vec(i) = 0.0;
            has_missing_sigma = true;
            std::cerr << "[Secondary] WARNING: element " << i
                      << " attribute=" << attr
                      << " 未在 attrSigma 中配置 sigma，按绝缘体处理 (sigma=0)。\n";
        }
    }
    if (has_missing_sigma)
    {
        std::cerr << "[Secondary] 提示：建议在 ElectrolyteSettingWidget 中只选择真正的电解质体，"
                  << "其它体可以不参与本方程，或者在网格里根本不要导入。\n";
    }

    PWConstCoefficient sigma_coef(sigma_vec);

    // 电位场 phi，x 作为它的别名向量（共用数据指针）
    GridFunction phi(&fespace);
    phi = 0.0;
    Vector x(phi.GetData(), ndofs);  // 注意：不分配内存，只是别名

    if (usePrimaryAsInitial)
    {
        std::cout << "[Secondary] usePrimaryAsInitial=TRUE，但暂未从文件加载一次结果，初值仍为 0。\n";
        // 将来你可以在这里从一次求解结果中读回 phi。
    }

    // --------- 2. 线性部分刚度矩阵 K = ∫ σ∇u·∇v ---------
    BilinearForm a_lin(&fespace);
    a_lin.AddDomainIntegrator(new DiffusionIntegrator(sigma_coef));
    a_lin.Assemble();
    a_lin.Finalize();
    SparseMatrix K = a_lin.SpMat();   // 拷贝一份基准矩阵

    // --------- 3. electrode physId -> BCEntry 映射 ---------
    std::map<int, const BCSolverEntry*> bcMap;
    for (const auto &rec : electrodeBCs)
    {
        bcMap[rec.physId] = &rec;
    }

    std::cout << "===== [Secondary] Electrode BCs from UI =====\n";
    for (const auto &kv : bcMap)
    {
        int phys = kv.first;
        const BCSolverEntry &rec = *(kv.second);
        std::cout << "  face physId = " << phys << "\n";
        for (auto &s : rec.scalars)
        {
            std::cout << "    " << s.first << " = " << s.second << "\n";
        }
    }

    // --------- 4. Newton 迭代准备 ---------
    Vector x_old(ndofs);
    Vector delta(ndofs);
    Vector residual(ndofs);

    CGSolver lin_solver;
    lin_solver.SetRelTol(linRelTol);
    lin_solver.SetMaxIter(linMaxIter);
    lin_solver.SetPrintLevel(printLog ? 1 : 0);

    std::unique_ptr<Solver> prec;

#ifdef MFEM_USE_HYPRE
    HypreBoomerAMG *amg = nullptr;
    if (useAMG)
    {
        amg = new HypreBoomerAMG();
        prec.reset(amg);
    }
#else
    (void)useAMG;
#endif

    // ---- j(eta) 和 j'(eta) 的计算器 ----
    auto assemble_j_and_jprime = [](
                                     double eta,
                                     const std::map<std::string,double> &s,
                                     double &j, double &dj_dphi)
    {
        const double ln10 = 2.302585092994046; // log(10)
        const double F    = 96485.0;           // C/mol
        const double Rg   = 8.314;             // J/(mol*K)

        int model_type = 1;
        if (auto it = s.find("model_type"); it != s.end())
            model_type = static_cast<int>(it->second);

        double T = 298.15;
        if (auto it = s.find("temp"); it != s.end())
            T = it->second;

        j = 0.0;
        dj_dphi = 0.0;

        if (model_type == 1)
        {
            // ---------- Tafel ----------
            bool useA = false;
            if (auto it = s.find("use_anodic"); it != s.end())
                useA = (it->second != 0.0);

            bool useC = false;
            if (auto it = s.find("use_cathodic"); it != s.end())
                useC = (it->second != 0.0);

            double djd_eta = 0.0;

            if (useA)
            {
                double beta_a = 0.1;
                double i0_a   = 0.0;
                if (auto it = s.find("beta_a"); it != s.end())
                    beta_a = it->second;
                if (auto it = s.find("i0_a"); it != s.end())
                    i0_a = it->second;

                double j_a = i0_a * std::pow(10.0, eta / beta_a);
                j += j_a;
                djd_eta += (ln10 / beta_a) * j_a;
            }

            if (useC)
            {
                double beta_c = 0.1;
                double i0_c   = 0.0;
                if (auto it = s.find("beta_c"); it != s.end())
                    beta_c = it->second;
                if (auto it = s.find("i0_c"); it != s.end())
                    i0_c = it->second;

                double t    = std::pow(10.0, -eta / beta_c);
                double j_c  = -i0_c * t;
                j += j_c;

                djd_eta += (ln10 / beta_c) * i0_c * t;
            }

            // eta = phi_m - phi_e - Eeq => d eta / d phi_e = -1
            dj_dphi = -djd_eta;
        }
        else
        {
            // ---------- Butler-Volmer ----------
            double i0      = 0.0;
            double alpha_a = 0.5;
            double alpha_c = 0.5;
            if (auto it = s.find("i0"); it != s.end())       i0 = it->second;
            if (auto it = s.find("alpha_a"); it != s.end())  alpha_a = it->second;
            if (auto it = s.find("alpha_c"); it != s.end())  alpha_c = it->second;

            double k_a = alpha_a * F / (Rg * T);
            double k_c = alpha_c * F / (Rg * T);

            double exp_a = std::exp(k_a * eta);
            double exp_c = std::exp(-k_c * eta);

            j = i0 * (exp_a - exp_c);

            double djd_eta = i0 * (k_a * exp_a + k_c * exp_c);
            dj_dphi = -djd_eta; // d eta / d phi_e = -1
        }
    };

    // --------- 5. Newton 迭代 ---------
    for (int it = 0; it < newtonMaxIter; ++it)
    {
        std::cout << "[Secondary] Newton iter " << it << std::endl;

        x_old = x;

        // 残量 R = K * x - ∑_bdr ∫ j(η(φ)) N_i dΓ
        residual.SetSize(ndofs);
        residual = 0.0;

        // 5.1 域内部分：R_dom = K * x
        K.Mult(x, residual);

        // 5.2 边界非线性部分
        for (int be = 0; be < mesh.GetNBE(); ++be)
        {
            int battr = mesh.GetBdrAttribute(be);
            auto itBC = bcMap.find(battr);
            if (itBC == bcMap.end())
                continue; // 不是二次电流电极

            const BCSolverEntry &rec = *(itBC->second);
            const auto &s = rec.scalars;

            double phi_m = 0.0;
            if (auto itp = s.find("phi"); itp != s.end())
                phi_m = itp->second;

            double Eeq = 0.0;
            if (auto ite = s.find("Eeq"); ite != s.end())
                Eeq = ite->second;

            Array<int> vdofs;
            fespace.GetBdrElementDofs(be, vdofs);

            const FiniteElement &fe = *fespace.GetBE(be);
            const int nd = fe.GetDof();

            Vector el_x(nd);
            for (int i = 0; i < nd; ++i)
                el_x[i] = x[vdofs[i]];

            Vector el_res(nd);
            el_res = 0.0;

            ElementTransformation *Ttr = mesh.GetBdrElementTransformation(be);
            Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
            const IntegrationRule &ir = IntRules.Get(geom, 2 * order);

            Vector shape(nd);

            for (int ip_idx = 0; ip_idx < ir.GetNPoints(); ++ip_idx)
            {
                const IntegrationPoint &ip = ir.IntPoint(ip_idx);
                Ttr->SetIntPoint(&ip);

                double w = ip.weight * Ttr->Weight();

                fe.CalcShape(ip, shape);

                double phi_e = 0.0;
                for (int j = 0; j < nd; ++j)
                    phi_e += el_x[j] * shape[j];

                double eta = phi_m - phi_e - Eeq;
                double j_val = 0.0, dj_dphi = 0.0;
                assemble_j_and_jprime(eta, s, j_val, dj_dphi);

                // 残量边界项：R_i += - ∫ j(φ) N_i dΓ
                for (int i = 0; i < nd; ++i)
                    el_res[i] += (-j_val) * shape[i] * w;
            }

            // 累加到全局残量
            for (int i = 0; i < nd; ++i)
                residual[vdofs[i]] += el_res[i];
        }

        // --------- 5.3 构造雅可比矩阵 J = K + 边界项 ---------
        SparseMatrix J(K); // 基于线性部分复制一份

        for (int be = 0; be < mesh.GetNBE(); ++be)
        {
            int battr = mesh.GetBdrAttribute(be);
            auto itBC = bcMap.find(battr);
            if (itBC == bcMap.end())
                continue;

            const BCSolverEntry &rec = *(itBC->second);
            const auto &s = rec.scalars;

            double phi_m = 0.0;
            if (auto itp = s.find("phi"); itp != s.end())
                phi_m = itp->second;

            double Eeq = 0.0;
            if (auto ite = s.find("Eeq"); ite != s.end())
                Eeq = ite->second;

            Array<int> vdofs;
            fespace.GetBdrElementDofs(be, vdofs);

            const FiniteElement &fe = *fespace.GetBE(be);
            const int nd = fe.GetDof();

            Vector el_x(nd);
            for (int i = 0; i < nd; ++i)
                el_x[i] = x[vdofs[i]];

            DenseMatrix el_J(nd);
            el_J = 0.0;

            ElementTransformation *Ttr = mesh.GetBdrElementTransformation(be);
            Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
            const IntegrationRule &ir = IntRules.Get(geom, 2 * order);

            Vector shape(nd);

            for (int ip_idx = 0; ip_idx < ir.GetNPoints(); ++ip_idx)
            {
                const IntegrationPoint &ip = ir.IntPoint(ip_idx);
                Ttr->SetIntPoint(&ip);

                double w = ip.weight * Ttr->Weight();

                fe.CalcShape(ip, shape);

                double phi_e = 0.0;
                for (int j = 0; j < nd; ++j)
                    phi_e += el_x[j] * shape[j];

                double eta = phi_m - phi_e - Eeq;
                double j_val = 0.0, dj_dphi = 0.0;
                assemble_j_and_jprime(eta, s, j_val, dj_dphi);

                // 雅可比边界项：J_ij += - ∫ j'(φ) N_j N_i dΓ
                for (int i = 0; i < nd; ++i)
                    for (int j = 0; j < nd; ++j)
                        el_J(i,j) += (-dj_dphi) * shape[i] * shape[j] * w;
            }

            // 加到全局 J
            J.AddSubMatrix(vdofs, vdofs, el_J);
        }

        // --------- 5.5 解 J * delta = - residual ---------
        Vector rhs(ndofs);
        rhs = residual;
        rhs *= -1.0;

#ifdef MFEM_USE_HYPRE
        if (useAMG && prec)
        {
            dynamic_cast<HypreBoomerAMG*>(prec.get())->SetOperator(J);
            lin_solver.SetPreconditioner(*prec);
        }
#endif
        lin_solver.SetOperator(J);

        delta.SetSize(ndofs);
        delta = 0.0;
        lin_solver.Mult(rhs, delta);

        // --------- 5.6 更新解 x_new = x + damping * delta ---------
        x.Add(newtonDamping, delta);

        // --------- 5.7 收敛检查 ---------
        double norm_delta = delta.Norml2();
        double norm_x     = x.Norml2();
        double rel_change = norm_delta / (norm_x + 1e-16);

        std::cout << "    ||delta|| = " << norm_delta
                  << ", ||x|| = " << norm_x
                  << ", rel_change = " << rel_change << std::endl;

        if (rel_change < newtonRelTol)
        {
            std::cout << "[Secondary] Newton converged at iter " << it << std::endl;
            break;
        }

        if (it == newtonMaxIter - 1)
        {
            std::cout << "[Secondary] Newton reached max iterations without full convergence.\n";
        }
    }

    // ================== 6. 生成各种场 ==================

    // 6.1 电解质电位 phil（直接就是求解出来的 phi，和 COMSOL 的 phil 对应）
    GridFunction phil(&fespace);
    phil = phi;

    // 6.2 对地电极电位 cd.phisext   （这里只在电极 DOF 上有意义）
    GridFunction phisext(&fespace);
    phisext = 0.0;

    Vector sum_phi_m(ndofs);
    Vector cnt_phi_m(ndofs);
    sum_phi_m = 0.0;
    cnt_phi_m = 0.0;

    for (int be = 0; be < mesh.GetNBE(); ++be)
    {
        int battr = mesh.GetBdrAttribute(be);
        auto itBC = bcMap.find(battr);
        if (itBC == bcMap.end())
            continue;   // 不是电极边界，跳过

        const BCSolverEntry &rec = *(itBC->second);
        const auto &s = rec.scalars;

        double phi_m = 0.0;
        if (auto itp = s.find("phi"); itp != s.end())
            phi_m = itp->second;   // 金属电极电位（对地）

        Array<int> vdofs;
        fespace.GetBdrElementDofs(be, vdofs);

        for (int k = 0; k < vdofs.Size(); ++k)
        {
            int dof = vdofs[k];
            sum_phi_m[dof] += phi_m;
            cnt_phi_m[dof] += 1.0;
        }
    }

    for (int i = 0; i < ndofs; ++i)
    {
        if (cnt_phi_m[i] > 0.0)
            phisext[i] = sum_phi_m[i] / cnt_phi_m[i];
        else
            phisext[i] = 0.0;      // 非电极区域：这里先设 0
    }

    // 6.3 界面腐蚀电流密度 cd.jcorr（A/m^2，带符号，定义在电极边界上）
    GridFunction jcorr(&fespace);
    jcorr = 0.0;

    Vector sum_j(ndofs);
    Vector cnt_j(ndofs);
    sum_j = 0.0;
    cnt_j = 0.0;

    // 额外保存每个边界单元的平均 j，用于将来单独导出“纯表面 vtu”
    std::vector<double> jcorr_bdr_avg(mesh.GetNBE(), 0.0);
    std::vector<double> abs_jcorr_bdr_avg(mesh.GetNBE(), 0.0);

    // 用最终的 phi 再在电极边界上算一遍 j(eta)，做一个平均场
    for (int be = 0; be < mesh.GetNBE(); ++be)
    {
        int battr = mesh.GetBdrAttribute(be);
        auto itBC = bcMap.find(battr);
        if (itBC == bcMap.end())
            continue;   // 非电极边界

        const BCSolverEntry &rec = *(itBC->second);
        const auto &s = rec.scalars;

        double phi_m = 0.0;
        if (auto itp = s.find("phi"); itp != s.end())
            phi_m = itp->second;

        double Eeq = 0.0;
        if (auto ite = s.find("Eeq"); ite != s.end())
            Eeq = ite->second;

        Array<int> vdofs;
        fespace.GetBdrElementDofs(be, vdofs);

        const FiniteElement &fe = *fespace.GetBE(be);
        const int nd = fe.GetDof();

        Vector el_x(nd);
        for (int i = 0; i < nd; ++i)
            el_x[i] = phi[vdofs[i]];   // 用最终解 phi

        ElementTransformation *Ttr = mesh.GetBdrElementTransformation(be);
        Geometry::Type geom = mesh.GetBdrElementBaseGeometry(be);
        const IntegrationRule &ir = IntRules.Get(geom, 2 * order);

        Vector shape(nd);

        double num = 0.0;
        double den = 0.0;

        for (int ip_idx = 0; ip_idx < ir.GetNPoints(); ++ip_idx)
        {
            const IntegrationPoint &ip = ir.IntPoint(ip_idx);
            Ttr->SetIntPoint(&ip);

            double w = ip.weight * Ttr->Weight();

            fe.CalcShape(ip, shape);

            double phi_e = 0.0;
            for (int j = 0; j < nd; ++j)
                phi_e += el_x[j] * shape[j];

            double eta = phi_m - phi_e - Eeq;
            double j_val = 0.0, dj_dphi = 0.0;
            assemble_j_and_jprime(eta, s, j_val, dj_dphi);

            num += j_val * w;
            den += w;
        }

        double j_avg = 0.0;
        if (den > 0.0)
            j_avg = num / den;

        jcorr_bdr_avg[be]      = j_avg;
        abs_jcorr_bdr_avg[be]  = std::fabs(j_avg);

        // 把这个边界单元上的平均腐蚀电流 j_avg 分配给该边界单元关联的 dof
        for (int k = 0; k < vdofs.Size(); ++k)
        {
            int dof = vdofs[k];
            sum_j[dof] += j_avg;
            cnt_j[dof] += 1.0;
        }
    }

    for (int i = 0; i < ndofs; ++i)
    {
        if (cnt_j[i] > 0.0)
            jcorr[i] = sum_j[i] / cnt_j[i];   // 电极边界上的腐蚀强度（A/m^2）
        else
            jcorr[i] = 0.0;                   // 非电极区域设为 0
    }

    // 为了对齐 COMSOL 的 abs(cd.itot)，这里再做一个绝对值版本
    GridFunction abs_cd_itot(&fespace);
    abs_cd_itot = 0.0;
    for (int i = 0; i < ndofs; ++i)
    {
        abs_cd_itot[i] = std::fabs(jcorr[i]); // |界面电流密度|
    }

    // 6.4 电解质电流密度向量 Je = -sigma * grad(phil)  （体内 L2 向量场）
    const int vdim = dim;
    L2_FECollection l2_fec(order, dim);
    FiniteElementSpace l2_vfes(&mesh, &l2_fec, vdim);

    GridFunction Je(&l2_vfes);
    Je = 0.0;

    ElectrolyteCurrentCoefficient Je_coef(mesh, phil, attrSigma);
    Je.ProjectCoefficient(Je_coef);

    // 6.5 拆分 Je 为三个分量：cd.Ilx, cd.Ily, cd.Ilz
    FiniteElementSpace l2_sfes(&mesh, &l2_fec); // 标量 L2 空间

    GridFunction cd_Ilx(&l2_sfes);
    GridFunction cd_Ily(&l2_sfes);
    GridFunction cd_Ilz(&l2_sfes);

    cd_Ilx = 0.0;
    cd_Ily = 0.0;
    cd_Ilz = 0.0;

    FiniteElementSpace *vfes = Je.FESpace();
    int nd_l2   = vfes->GetNDofs();
    int vdim_l2 = vfes->GetVDim();
    const Vector &Je_data = Je;  // Je 作为 Vector 看

    MFEM_VERIFY(vdim_l2 >= dim, "Je vdim < mesh dim ?");

    Ordering::Type ord = vfes->GetOrdering();

    if (ord == Ordering::byNODES)
    {
        // data(i + j*nd) : i=dof, j=分量
        for (int i = 0; i < nd_l2; ++i)
        {
            cd_Ilx(i) = Je_data(i + 0*nd_l2);
            if (dim > 1) cd_Ily(i) = Je_data(i + 1*nd_l2);
            if (dim > 2) cd_Ilz(i) = Je_data(i + 2*nd_l2);
        }
    }
    else // Ordering::byVDIM
    {
        // data(j + vdim*i) : i=dof, j=分量
        for (int i = 0; i < nd_l2; ++i)
        {
            cd_Ilx(i) = Je_data(0 + vdim_l2*i);
            if (dim > 1) cd_Ily(i) = Je_data(1 + vdim_l2*i);
            if (dim > 2) cd_Ilz(i) = Je_data(2 + vdim_l2*i);
        }
    }

    // 6.6 体内电流密度模长：cd.IlMag = |Je|，对应 COMSOL 的 cd.IlMag （L2 体场）
    GridFunction cd_IlMag(&l2_sfes);
    cd_IlMag = 0.0;

    int nd_l2_scalar = l2_sfes.GetNDofs();
    for (int i = 0; i < nd_l2_scalar; ++i)
    {
        double Jx = cd_Ilx(i);
        double Jy = (dim > 1) ? cd_Ily(i) : 0.0;
        double Jz = (dim > 2) ? cd_Ilz(i) : 0.0;

        cd_IlMag(i) = std::sqrt(Jx*Jx + Jy*Jy + Jz*Jz);
    }

    // --------- 7. 打印各字段范围，方便和 COMSOL 对比 ---------
    auto print_min_max = [](const char* name, const GridFunction &gf)
    {
        double minv = gf.Min();
        double maxv = gf.Max();
        std::cout << "[Secondary] " << name
                  << " : min = " << minv
                  << ", max = " << maxv << std::endl;
    };

    std::cout << "[Secondary] ===== Field ranges (for debug) =====\n";
    print_min_max("phil (V)",          phil);
    print_min_max("cd.IlMag (A/m^2)",  cd_IlMag);
    print_min_max("jcorr / cd.itot (A/m^2)", jcorr);
    print_min_max("abs(cd.itot) (A/m^2)",    abs_cd_itot);

    // ================== 8. 输出 VTU ==================
    {
        QString exePath  = QCoreApplication::applicationDirPath();
        QString dataPath = exePath + "/../ProjectFolder/data";
        QDir().mkpath(dataPath);  // 确保目录存在

        std::string prefixPath     = dataPath.toStdString();
        std::string collectionName = outputPrefix.empty() ? "tmp" : outputPrefix;

        mfem::ParaViewDataCollection d(collectionName, &mesh);
        d.SetPrefixPath(prefixPath);
        d.SetDataFormat(VTKFormat::ASCII);

        d.SetHighOrderOutput(true);
        d.SetLevelsOfDetail(order);

        // ---------- 电位 ----------
        d.RegisterField("phi2",  &phisext); // 对地电极电位（COMSOL: phi2）
        d.RegisterField("phil",  &phil);    // 电解质电位（COMSOL: phil）

        // ---------- 体内电流（向量 + 组件 + 模长） ----------
        d.RegisterField("Je",      &Je);       // 电流密度向量
        d.RegisterField("cd.Ilx",  &cd_Ilx);   // x 分量
        d.RegisterField("cd.Ily",  &cd_Ily);   // y 分量
        d.RegisterField("cd.Ilz",  &cd_Ilz);   // z 分量
        d.RegisterField("cd.IlMag",&cd_IlMag); // 体内电流模长，对齐 COMSOL cd.IlMag

        // ---------- 界面电流（腐蚀强度） ----------
        d.RegisterField("cd.jcorr",     &jcorr);       // 带符号界面腐蚀电流
        d.RegisterField("cd.itot",      &jcorr);       // 同一内容，用 COMSOL 的名字
        d.RegisterField("abs(cd.itot)", &abs_cd_itot); // 绝对值，用来对齐 COMSOL 的 abs(cd.itot)

        d.SetCycle(0);
        d.SetTime(0.0);
        d.Save();
    }

    // 你之前的搬运 / 改名逻辑
    QString exePath  = QCoreApplication::applicationDirPath();
    QString dataPath = exePath + "/../ProjectFolder/data";

    MoveAndRenameVTU(dataPath);

    std::cout << "[Secondary] 输出 VTU 完成："
              << "phi2（对地电极电位）、"
              << "phil（电解质电位）、"
              << "Je / cd.Ilx/cd.Ily/cd.Ilz（体内电流密度及分量）、"
              << "cd.IlMag（体内电流模长）、"
              << "cd.jcorr / cd.itot（界面电流）、"
              << "abs(cd.itot)（界面电流绝对值）\n";
}
