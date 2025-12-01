#include "Solver.h"

#include <services/UiFrameService.h>
#include <ServiceRegistry.h>
#include "mfem.hpp"
#include <iostream>
#include <fstream>

using namespace std;


Solver::Solver()
{
    m_curveWidget = new CurveWidget(this);

    UiFrameService * uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    uiFrameService->addWidget(m_curveWidget,"曲线");

    // test();
}

void Solver::test()
{
    // 1. 读 Gmsh 网格
    const char* mesh_file = "C:/tool/grid3.msh";


    mfem::Mesh mesh(mesh_file, 1, 1);
    int dim = mesh.Dimension();

    cout << "Read mesh from: " << mesh_file << endl;
    cout << "Dimension:           " << dim << endl;
    cout << "Elements (cells):    " << mesh.GetNE() << endl;
    cout << "Vertices (nodes):    " << mesh.GetNV() << endl;
    cout << "Boundary elements:   " << mesh.GetNBE() << endl;

    // 打印区域属性（体）和边界属性（面），便于你对照
    cout << "Region attributes (volumes): ";
    for (int i = 0; i < mesh.attributes.Size(); i++)
    {
        cout << mesh.attributes[i] << " ";
    }
    cout << endl;

    cout << "Boundary attributes (faces): ";
    for (int i = 0; i < mesh.bdr_attributes.Size(); i++)
    {
        cout << mesh.bdr_attributes[i] << " ";
    }
    cout << endl;

    // 2. H1 有限元空间：标量场 u（电位）
    int order = 1;
    mfem::H1_FECollection fec(order, dim);
    mfem::FiniteElementSpace fes(&mesh, &fec, 1);  // vdim = 1 → 标量

    cout << "True DOFs:           " << fes.GetTrueVSize() << endl;

    mfem::GridFunction u(&fes);
    u = 0.0;

    // 3. 定义多材料的导电率 σ(x)：两个 Volume 不同
    //    mesh.attributes 里现在有两个值（比如 53 和 54）
    int max_attr = mesh.attributes.Max();
    mfem::Vector sigma_vals(max_attr);
    sigma_vals = 0.0;

    // 这里我们假设 mesh.attributes[0] / [1] 就是你的 volume_1 / volume_2
    int attr1 = mesh.attributes.Size() > 0 ? mesh.attributes[0] : 1;
    int attr2 = mesh.attributes.Size() > 1 ? mesh.attributes[1] : attr1;

    // 给两个体设置不同导电率（你可以根据材料改数值）
    double sigma1 = 1.0;   // volume_1 导电率
    double sigma2 = 5.0;   // volume_2 导电率（更“好导电”一点）

    sigma_vals[attr1 - 1] = sigma1;
    sigma_vals[attr2 - 1] = sigma2;

    mfem::PWConstCoefficient sigma_pc(sigma_vals);  // σ(x) 是分片常数系数

    // 3. 定义 PDE：-div(σ grad u) = 0
    mfem::BilinearForm a(&fes);
    a.AddDomainIntegrator(new mfem::DiffusionIntegrator(sigma_pc));  // 多材料扩散项
    a.Assemble();

    // 右端项 b = 0（无源项）
    mfem::LinearForm b(&fes);
    b = 0.0;
    b.Assemble();

    // 4. 边界条件：指定两块面，一个低电位，一个高电位，其它 Neumann（绝缘）
    mfem::Array<int> ess_bdr;
    ess_bdr.SetSize(mesh.bdr_attributes.Max());
    ess_bdr = 0;

    // ==== 这里根据你的 mesh 选择两个 face 做电极 ====
    // 你贴出来的 PhysicalNames 有 face_1..face_12 （41~52）
    // 这里先假设：
    //   face_1 (41)  → 电位 u = -0.44  （比如铁）
    //   face_7 (47)  → 电位 u =  0.34  （比如铜）
    // 你可以根据几何实际情况把 41 / 47 改成你想要的面 ID
    const int iron_face_attr = 41;  // TODO: 换成你想作为“金属A表面”的 face ID
    const int copper_face_attr = 47; // TODO: 换成你想作为“金属B表面”的 face ID

    cout << "Using bdr attr " << iron_face_attr
         << " as u = -0.44 (metal A) face" << endl;
    cout << "Using bdr attr " << copper_face_attr
         << " as u =  0.34 (metal B) face" << endl;

    if (iron_face_attr >= 1 && iron_face_attr <= ess_bdr.Size())
        ess_bdr[iron_face_attr - 1] = 1;
    if (copper_face_attr >= 1 && copper_face_attr <= ess_bdr.Size())
        ess_bdr[copper_face_attr - 1] = 1;

    // 先给“金属 A 表面”赋 u = -0.44
    mfem::Array<int> bdr_marker(mesh.bdr_attributes.Max());
    bdr_marker = 0;
    if (iron_face_attr >= 1 && iron_face_attr <= bdr_marker.Size())
        bdr_marker[iron_face_attr - 1] = 1;
    mfem::ConstantCoefficient u_Fe(-0.44);
    u.ProjectBdrCoefficient(u_Fe, bdr_marker);

    // 再给“金属 B 表面”赋 u = 0.34
    bdr_marker = 0;
    if (copper_face_attr >= 1 && copper_face_attr <= bdr_marker.Size())
        bdr_marker[copper_face_attr - 1] = 1;
    mfem::ConstantCoefficient u_Cu(0.34);
    u.ProjectBdrCoefficient(u_Cu, bdr_marker);

    // 5. 组装线性系统并求解
    mfem::Array<int> ess_tdof_list;
    fes.GetEssentialTrueDofs(ess_bdr, ess_tdof_list);

    mfem::SparseMatrix A;
    mfem::Vector B, X;
    a.FormLinearSystem(ess_tdof_list, u, b, A, X, B);

    cout << "Linear system size:  " << A.Height() << endl;

    mfem::GSSmoother M(A);
    PCG(A, M, B, X, 1, 500, 1e-12, 0.0);

    // 恢复到 GridFunction u 上
    a.RecoverFEMSolution(X, b, u);

    // 6. 打印 u 的范围，确认不是常数
    double u_min = u.Min();
    double u_max = u.Max();
    cout << "u_min = " << u_min << ", u_max = " << u_max << endl;

    // 7. 计算电流密度 J = σ ∇u （向量场）
    // 7.1 先算 ∇u
    mfem::GradientGridFunctionCoefficient grad_u_coef(&u);  // 向量系数，vdim = dim

    // 为向量场创建一个 H1 向量空间（同阶数，但 vdim = dim）
    mfem::FiniteElementSpace fes_vec(&mesh, &fec, dim);

    // grad_u: 作为一个向量 GridFunction，方便导出到 VTU
    mfem::GridFunction grad_u(&fes_vec);
    grad_u = 0.0;
    grad_u.ProjectCoefficient(grad_u_coef);

    // 7.2 用系数机制构造 J = σ * grad(u)
    //     注意这里是 σ·∇u，方向和 ∇u 一致，如果你想要 J = -σ∇u，
    //     可以在后处理里加一个负号，或者把 sigma1 / sigma2 设成负数。
    mfem::ScalarVectorProductCoefficient J_coef(sigma_pc, grad_u_coef); // σ * ∇u :contentReference[oaicite:0]{index=0}

    mfem::GridFunction J(&fes_vec);
    J = 0.0;
    J.ProjectCoefficient(J_coef);

    // 8. 导出到 ParaView（VTU）
    mfem::ParaViewDataCollection dc("mfem_galvanic_demo", &mesh);
    dc.SetPrefixPath("pv_out_galvanic");
    dc.SetTime(0.0);
    dc.SetCycle(0);

    // 标量场：电位 u
    dc.RegisterField("u", &u);

    // 向量场：grad_u（电位梯度）、J（电流密度）
    dc.RegisterField("grad_u", &grad_u);
    dc.RegisterField("J", &J);

    // 可选：为了调试，改成 ASCII 更好看；实际用二进制即可
    // dc.SetDataFormat(VTKFormat::ASCII);

    dc.Save();

    cout << "ParaView files written to folder: pv_out_galvanic/" << endl;
    cout << "Open pv_out_galvanic/mfem_galvanic_demo.pvd in ParaView." << endl;
    cout << "Field 'u'  = potential, scalar" << endl;
    cout << "Field 'J'  = sigma * grad(u), vector (current density up to sign)" << endl;
    cout << "Field 'grad_u' = grad(u), vector" << endl;
}
