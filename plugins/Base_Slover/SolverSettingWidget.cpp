#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

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

void SolverSettingWidget::on_caculateButton_clicked()
{
    // 1. 从 UI 读取求解参数
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
    auto& bc = BoundaryConditions::instance();

    // 3D 域 (dim=3)：电导率 sigma
    auto volumeBCs = bc.exportForSolver(SolverType::PrimaryCurrent, 3);
    // 2D 面 (dim=2)：Dirichlet 电位 phi
    auto faceBCs   = bc.exportForSolver(SolverType::PrimaryCurrent, 2);

    if (volumeBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未设置任何域（体）的电导率，请先在电解质设置页面中配置。"));
        return;
    }
    if (faceBCs.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("未设置任何电极面电位，请先在电极表面设置页面中配置。"));
        return;
    }

    // 3. 构建 attribute -> sigma 映射 & bdr_attribute -> phi 映射
    std::map<int,double> attrSigma;   // volume attribute -> sigma
    for (const auto &e : volumeBCs)
    {
        auto it = e.scalars.find("sigma");
        if (it == e.scalars.end()) continue;
        // 假设：mfem::Mesh 中元素 attribute == gmsh physId
        attrSigma[e.physId] = it->second;
    }

    std::cout << "attrSigma mapping:\n";
    for (auto &kv : attrSigma)
    {
        std::cout << "  attribute " << kv.first
                  << " -> sigma = " << kv.second << std::endl;
    }

    std::map<int,double> bdrAttrPhi;  // boundary attribute -> phi
    for (const auto &e : faceBCs)
    {
        auto it = e.scalars.find("phi");
        if (it == e.scalars.end()) continue;
        // 假设：mfem::Mesh 中边界 attribute == gmsh face physId
        bdrAttrPhi[e.physId] = it->second;
    }

    if (attrSigma.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("域电导率 sigma 未正确配置，请检查设置。"));
        return;
    }
    if (bdrAttrPhi.empty())
    {
        QMessageBox::warning(this, tr("求解失败"),
                             tr("电极电位 phi 未正确配置，请检查设置。"));
        return;
    }

    // 4. 获取 MFEM Mesh（你自己的 Mesh 管理器）
    QString	exelPath = QCoreApplication::applicationDirPath();
    const QString tempPath = exelPath + "/../tempGrid/";
    QString mshPath = tempPath + "mesh.msh";

    std::string meshFile = mshPath.toStdString();

    // 对 Gmsh v2 ASCII 网格，MFEM 常用这个构造函数：
    // Mesh(const char *filename, int generate_edges = 1,
    //      int refine = 1, bool fix_orientation = false);
    // 对 3D 网格想要自动生成边界，可以用：
    mfem::Mesh mesh(meshFile.c_str(),
                    1,   // 生成边/面
                    1,   // refine=1 表示不做额外细化
                    false // 修正单元方向
                    );
    mesh.PrintInfo(std::cout);
    qDebug()<<"123 "<< mesh.GetNE();
    qDebug()<<"123 " <<mesh.GetNBE();
    // 5. 调用一次电流分布求解
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
                                 tr("一次电流分布计算完成，结果已输出为 VTU（ParaView 格式）。"));
    }
    catch (const std::exception& ex)
    {
        QMessageBox::critical(this, tr("求解错误"),
                              tr("求解过程中发生错误：%1").arg(ex.what()));
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
