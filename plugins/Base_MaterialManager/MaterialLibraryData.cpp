#include "MaterialLibraryData.h"

namespace
{
MaterialModel createLinearElasticModel()
{
    MaterialModel model;
    model.key = QStringLiteral("linear_elastic");
    model.displayName = QStringLiteral("线弹性实体材料");
    model.ib = 0;
    model.columns << QStringLiteral("序号") << QStringLiteral("变形模量E（GPa）")
                  << QStringLiteral("泊松比μ") << QStringLiteral("容重t/m3");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("弹性模量（GPa）")
                         << QStringLiteral("泊松比") << QStringLiteral("容重（t/m³）");
    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createHoekBrownModel()
{
    MaterialModel model;
    model.key = QStringLiteral("hoek_brown_rock");
    model.displayName = QStringLiteral("霍克-布朗本构");
    model.ib = 11;

    model.columns << QStringLiteral("序号") << QStringLiteral("变形模量E（GPa）")
                  << QStringLiteral("泊松比μ") << QStringLiteral("容重t/m3")
                  << QStringLiteral("岩体常数mb") << QStringLiteral("岩体常数a")
                  << QStringLiteral("岩体常数s")
                  << QStringLiteral("完整岩石单轴抗压强度σci（MPa）");
    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createDruckerPragerModel()
{
    MaterialModel model;
    model.key = QStringLiteral("drucker_prager_rock");
    model.displayName = QStringLiteral("Drucker–Prager本构");
    model.ib = 3;
    model.columns << QStringLiteral("序号") << QStringLiteral("变形模量E（GPa）")
                  << QStringLiteral("泊松比μ") << QStringLiteral("容重t/m3")
                  << QStringLiteral("内摩擦角φ（°）") << QStringLiteral("凝聚力C‘（MPa）")
                  << QStringLiteral("剪胀角(°)") << QStringLiteral("DP子类型")
                  << QStringLiteral("抗压强度Rc（MPa）") << QStringLiteral("抗拉强度Rt（MPa）");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("变形模量（GPa）")
                         << QStringLiteral("泊松比") << QStringLiteral("容重（t/m³）")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("凝聚力（MPa）")
                         << QStringLiteral("剪胀角（°）") << QStringLiteral("D-P 屈服面类型")
                         << QStringLiteral("抗压强度Rc（MPa，0自动）")
                         << QStringLiteral("抗拉强度Rt（MPa，0自动）");
    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createHuangjinpingModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_underground_plant");
    model.displayName = QStringLiteral("围岩开挖模型");
    model.ib = 2;

    model.columns << QStringLiteral("编号") << QStringLiteral("母岩材料编号")
                  << QStringLiteral("岩类分区")
                  << QStringLiteral("半开挖区荷载释放系数")
                  << QStringLiteral("再生时对应开挖级")
                  << QStringLiteral("变形模量E（GPa）") << QStringLiteral("泊松比μ")
                  << QStringLiteral("容重t/m3")
                  << QStringLiteral("抗剪断强度内摩擦系数")
                  << QStringLiteral("凝聚力C‘（MPa）")
                  << QStringLiteral("剪胀角(°)")
                  << QStringLiteral("洞室边缘松弛系数α0")
                  << QStringLiteral("松弛距离d0");
    model.displayColumns << QStringLiteral("编号") << QStringLiteral("母岩材料")
                         << QStringLiteral("开挖状态") << QStringLiteral("荷载释放系数")
                         << QStringLiteral("再生开挖级") << QStringLiteral("变形模量（GPa）")
                         << QStringLiteral("泊松比") << QStringLiteral("容重（t/m³）")
                         << QStringLiteral("内摩擦系数 f") << QStringLiteral("凝聚力（MPa）")
                         << QStringLiteral("剪胀角（°）") << QStringLiteral("松弛系数")
                         << QStringLiteral("松弛距离");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createDamDynamicModel()
{
    MaterialModel model;
    model.key = QStringLiteral("dam_dynamic");
    model.displayName = QStringLiteral("坝料动力本构");
    model.ib = -4;

    model.columns << QStringLiteral("编号") << QStringLiteral("是否液化")
                  << QStringLiteral("DRYK1(KPa)")
                  << QStringLiteral("DRYN1") << QStringLiteral("FAI(°)")
                  << QStringLiteral("CO(KPa)") << QStringLiteral("PU")
                  << QStringLiteral("DRYDAM") << QStringLiteral("RATIAO1(0.1)")
                  << QStringLiteral("RATIAO2(0.3)") << QStringLiteral("RATIAO3(0.5)")
                  << QStringLiteral("Kv")
                  << QStringLiteral("Kp") << QStringLiteral("nv") << QStringLiteral("np");
    model.displayColumns << QStringLiteral("编号") << QStringLiteral("是否液化")
                         << QStringLiteral("动力刚度系数（kPa）") << QStringLiteral("动力刚度指数")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("凝聚力（kPa）")
                         << QStringLiteral("泊松比") << QStringLiteral("最大阻尼比")
                         << QStringLiteral("液化循环次数（0.1）") << QStringLiteral("液化循环次数（0.3）")
                         << QStringLiteral("液化循环次数（0.5）") << QStringLiteral("回归系数 1")
                         << QStringLiteral("回归系数 2") << QStringLiteral("回归指数 1")
                         << QStringLiteral("回归指数 2");
    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSeepageModel()
{
    MaterialModel model;
    model.key = QStringLiteral("jk_corewall_steady_seepage");
    model.displayName = QStringLiteral("各向同性渗流");
    model.ib = -2;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("渗透系数K(Lu)")
                  << QStringLiteral("比储量S") << QStringLiteral("允许比降");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("渗透系数（Lu）")
                         << QStringLiteral("比储量 Ss（1/m）") << QStringLiteral("允许水力比降");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createAnisotropicSeepageModel()
{
    MaterialModel model;
    model.key = QStringLiteral("jk_corewall_anisotropic_seepage");
    model.displayName = QStringLiteral("各向异性渗流");
    model.ib = -2;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("渗透系数Kx(Lu)")
                  << QStringLiteral("渗透系数Ky(Lu)") << QStringLiteral("渗透系数Kz(Lu)")
                  << QStringLiteral("比储量S") << QStringLiteral("允许比降");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("X向渗透系数（Lu）")
                         << QStringLiteral("Y向渗透系数（Lu）") << QStringLiteral("Z向渗透系数（Lu）")
                         << QStringLiteral("比储量 Ss（1/m）") << QStringLiteral("允许水力比降");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSoilEmuModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_emu");
    model.displayName = QStringLiteral("E-μ本构");
    model.ib = -3;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("Kur") << QStringLiteral("Nur")
                  << QStringLiteral("G") << QStringLiteral("F") << QStringLiteral("D");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("干容重（t/m³）")
                         << QStringLiteral("天然容重（t/m³）") << QStringLiteral("饱和容重（t/m³）")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("摩擦角增量（°）")
                         << QStringLiteral("凝聚力（kPa）") << QStringLiteral("破坏比")
                         << QStringLiteral("加载刚度系数 K") << QStringLiteral("加载刚度指数 n")
                         << QStringLiteral("卸载刚度系数 Kur") << QStringLiteral("卸载刚度指数 Nur")
                         << QStringLiteral("初始泊松比基数 G")
                         << QStringLiteral("围压修正系数 F")
                         << QStringLiteral("应力水平修正系数 D");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createSoilEmuCrushModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_emu_crush");
    model.displayName = QStringLiteral("E-μ破碎本构");
    model.ib = -6;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("Kur") << QStringLiteral("Nur")
                  << QStringLiteral("C1") << QStringLiteral("n1")
                  << QStringLiteral("C2") << QStringLiteral("n2") << QStringLiteral("C3")
                  << QStringLiteral("n3");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("干容重（t/m³）")
                         << QStringLiteral("天然容重（t/m³）") << QStringLiteral("饱和容重（t/m³）")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("摩擦角增量（°）")
                         << QStringLiteral("凝聚力（kPa）") << QStringLiteral("破坏比")
                         << QStringLiteral("加载刚度系数") << QStringLiteral("加载刚度指数")
                         << QStringLiteral("卸载刚度系数") << QStringLiteral("卸载刚度指数")
                         << QStringLiteral("破碎系数 1") << QStringLiteral("破碎指数 1")
                         << QStringLiteral("破碎系数 2") << QStringLiteral("破碎指数 2")
                         << QStringLiteral("破碎系数 3") << QStringLiteral("破碎指数 3");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createPlanarElasticBeamModel()
{
    MaterialModel model;model.key=QStringLiteral("planar_elastic_beam");model.displayName=QStringLiteral("平面弹性梁");model.ib=1;
    model.columns << QStringLiteral("序号") << QStringLiteral("弹性模量E（GPa）") << QStringLiteral("截面积A（m2）")
        << QStringLiteral("截面惯性矩Iz（m4）") << QStringLiteral("容重t/m3");
    model.displayColumns=model.columns;model.columnOutputRows=QVector<int>(model.columns.size(),-1);
    return model;
}

MaterialModel createHuangjinpingBarModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_bar_element");
    model.displayName = QStringLiteral("锚杆单元模型");
    model.ib = 1;

    // 保留旧字段名“变模E1,E2”“抗拉（压）强度ft”作为内部 key，
    // 这样旧工程无需重命名字段即可兼容；界面显示名称按求解器真实语义拆开。
    model.columns << QStringLiteral("序号") << QStringLiteral("变模E1,E2（GPa）")
                  << QStringLiteral("屈服后变模E2（GPa）")
                  << QStringLiteral("容重t/m3") << QStringLiteral("杆面积（m2）")
                  << QStringLiteral("抗拉（压）强度ft（MPa）")
                  << QStringLiteral("极限抗压强度Rc（MPa）")
                  << QStringLiteral("屈服抗拉强度Ryt（MPa）")
                  << QStringLiteral("屈服抗压强度Ryc（MPa）")
                  << QStringLiteral("间距(m)") << QStringLiteral("排距(m)")
                  << QStringLiteral("预应力T（KN）") << QStringLiteral("预应系数");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("初始弹性模量 E1（GPa）")
                         << QStringLiteral("屈服后弹性模量 E2（GPa）")
                         << QStringLiteral("容重（t/m³）") << QStringLiteral("截面积（m²）")
                         << QStringLiteral("极限抗拉强度 Rt（MPa）")
                         << QStringLiteral("极限抗压强度 Rc（MPa）")
                         << QStringLiteral("屈服抗拉强度 Ryt（MPa）")
                         << QStringLiteral("屈服抗压强度 Ryc（MPa）")
                         << QStringLiteral("锚杆间距（m）") << QStringLiteral("锚杆排距（m）")
                         << QStringLiteral("预应力（kN）") << QStringLiteral("预应力系数");
    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createDagangshanIsotropicModel()
{
    MaterialModel model;
    model.key = QStringLiteral("dagangshan_isotropic");
    model.displayName = QStringLiteral("摩尔-库仑本构");
    model.ib = 2;

    model.columns << QStringLiteral("序号") << QStringLiteral("变形模量E（GPa）")
                  << QStringLiteral("泊松比μ") << QStringLiteral("容重t/m3")
                  << QStringLiteral("抗剪断强度内摩擦系数")
                  << QStringLiteral("凝聚力C‘（MPa）")
                  << QStringLiteral("抗压强度Rc（MPa）")
                  << QStringLiteral("抗拉强度Rt（MPa）")
                  << QStringLiteral("残余抗剪断强度内摩擦系数")
                  << QStringLiteral("残余凝聚力C‘（MPa）")
                  << QStringLiteral("残余摩擦系数沿用峰值")
                  << QStringLiteral("残余凝聚力沿用峰值")
                  << QStringLiteral("剪胀角(°)");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("变形模量（GPa）")
                         << QStringLiteral("泊松比") << QStringLiteral("容重（t/m³）")
                         << QStringLiteral("内摩擦系数 f") << QStringLiteral("凝聚力（MPa）")
                         << QStringLiteral("抗压强度 Rc（0=自动）")
                         << QStringLiteral("抗拉强度 Rt（0=自动）")
                         << QStringLiteral("残余内摩擦系数 fr")
                         << QStringLiteral("残余凝聚力（MPa）")
                         << QStringLiteral("残余摩擦系数沿用峰值")
                         << QStringLiteral("残余凝聚力沿用峰值")
                         << QStringLiteral("剪胀角（°）");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createNanshuiModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_nanshui");
    model.displayName = QStringLiteral("南水非线性本构");
    model.ib = -5;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0") << QStringLiteral("Δφ") << QStringLiteral("c(KPa)")
                  << QStringLiteral("Rf") << QStringLiteral("K") << QStringLiteral("n")
                  << QStringLiteral("Kur") << QStringLiteral("Nur")
                  << QStringLiteral("cd") << QStringLiteral("nd") << QStringLiteral("Rd");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("干容重（t/m³）")
                         << QStringLiteral("天然容重（t/m³）") << QStringLiteral("饱和容重（t/m³）")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("摩擦角增量（°）")
                         << QStringLiteral("凝聚力（kPa）") << QStringLiteral("破坏比")
                         << QStringLiteral("加载刚度系数") << QStringLiteral("加载刚度指数")
                         << QStringLiteral("卸载刚度系数") << QStringLiteral("卸载刚度指数")
                         << QStringLiteral("模型系数 1") << QStringLiteral("模型指数")
                         << QStringLiteral("模型系数 2");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSoilKgModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_kg");
    model.displayName = QStringLiteral("K-G本构");
    model.ib = -2;

    // 前四个 K-G 字段保留旧内部 key，显示语义改为“加载”；新增四个独立卸载参数。
    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("Ki,Kie(10^5Pa)") << QStringLiteral("αk,αke")
                  << QStringLiteral("Gi,Gie(10^5Pa)") << QStringLiteral("αG,αGe")
                  << QStringLiteral("βG") << QStringLiteral("a(10^5Pa)")
                  << QStringLiteral("b")
                  << QStringLiteral("Kie(10^5Pa)") << QStringLiteral("αke")
                  << QStringLiteral("Gie(10^5Pa)") << QStringLiteral("αGe");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("干容重（t/m³）")
                         << QStringLiteral("天然容重（t/m³）") << QStringLiteral("饱和容重（t/m³）")
                         << QStringLiteral("加载体积刚度参数 Ki（10⁵ Pa）")
                         << QStringLiteral("加载体积刚度系数 αk")
                         << QStringLiteral("加载剪切刚度参数 Gi（10⁵ Pa）")
                         << QStringLiteral("加载剪切刚度系数 αG")
                         << QStringLiteral("加载剪切修正系数 βG")
                         << QStringLiteral("强度参数 a（kPa）")
                         << QStringLiteral("强度参数 b")
                         << QStringLiteral("卸载体积刚度参数 Kie（10⁵ Pa）")
                         << QStringLiteral("卸载体积刚度系数 αke")
                         << QStringLiteral("卸载剪切刚度参数 Gie（10⁵ Pa）")
                         << QStringLiteral("卸载剪切刚度系数 αGe");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createShenzhujiangCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("shenzhujiang_creep");
    model.displayName = QStringLiteral("三参数流变");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("应变速率系数α")
                  << QStringLiteral("体积流变系数b") << QStringLiteral("剪切流变系数d");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("应变速率系数")
                         << QStringLiteral("体积流变系数") << QStringLiteral("剪切流变系数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createLiguoyingCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("liguoying_creep");
    model.displayName = QStringLiteral("七参数流变");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("应变速率系数α")
                  << QStringLiteral("体积流变系数b") << QStringLiteral("体积流变系数c")
                  << QStringLiteral("剪切流变系数d") << QStringLiteral("体积流变指数m1")
                  << QStringLiteral("体积流变指数m2") << QStringLiteral("剪切流变指数m3");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("应变速率系数")
                         << QStringLiteral("体积流变系数 1") << QStringLiteral("体积流变系数 2")
                         << QStringLiteral("剪切流变系数") << QStringLiteral("体积流变指数 1")
                         << QStringLiteral("体积流变指数 2") << QStringLiteral("剪切流变指数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createNineParamCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("nine_param_creep");
    model.displayName = QStringLiteral("九参数流变");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("应变速率系数λv")
                  << QStringLiteral("体积流变系数Cα") << QStringLiteral("体积流变系数Cβ")
                  << QStringLiteral("体积流变指数dα") << QStringLiteral("体积流变指数dβ")
                  << QStringLiteral("主应变系数c") << QStringLiteral("主应变系数d")
                  << QStringLiteral("主应变流变系数η") << QStringLiteral("主应变流变系数m");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("应变速率系数")
                         << QStringLiteral("体积流变系数 1") << QStringLiteral("体积流变系数 2")
                         << QStringLiteral("体积流变指数 1") << QStringLiteral("体积流变指数 2")
                         << QStringLiteral("主应变系数 1") << QStringLiteral("主应变系数 2")
                         << QStringLiteral("主应变流变系数") << QStringLiteral("主应变流变指数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createXiyuanCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("xiyuan_creep");
    model.displayName = QStringLiteral("六参数流变");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("瞬时弹性模量E1（GPa）")
                  << QStringLiteral("粘塑性模量E2（GPa）") << QStringLiteral("泊松比μ1")
                  << QStringLiteral("泊松比μ2") << QStringLiteral("粘弹性系数η1")
                  << QStringLiteral("粘塑性系数η2");
    model.displayColumns << QStringLiteral("序号") << QStringLiteral("瞬时弹性模量（GPa）")
                         << QStringLiteral("黏塑性模量（GPa）") << QStringLiteral("泊松比 1")
                         << QStringLiteral("泊松比 2") << QStringLiteral("黏弹性系数")
                         << QStringLiteral("黏塑性系数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createSoilEbModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_eb");
    model.displayName = QStringLiteral("E-B本构");
    model.ib = -1;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("Kur") << QStringLiteral("Nur")
                  << QStringLiteral("Kb") << QStringLiteral("m");
    model.displayColumns << QStringLiteral("材料编号") << QStringLiteral("干容重（t/m³）")
                         << QStringLiteral("天然容重（t/m³）") << QStringLiteral("饱和容重（t/m³）")
                         << QStringLiteral("内摩擦角（°）") << QStringLiteral("摩擦角增量（°）")
                         << QStringLiteral("凝聚力（kPa）") << QStringLiteral("破坏比")
                         << QStringLiteral("加载刚度系数") << QStringLiteral("加载刚度指数")
                         << QStringLiteral("卸载刚度系数") << QStringLiteral("卸载刚度指数")
                         << QStringLiteral("体积刚度系数") << QStringLiteral("体积刚度指数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}
} // namespace

QString defaultMaterialPropertyValue(const QString& modelKey, const QString& column)
{
    if (modelKey == QStringLiteral("linear_elastic"))
    {
        if (column == QStringLiteral("泊松比μ")) return QStringLiteral("0.25");
        return QString();
    }
    if(modelKey==QStringLiteral("planar_elastic_beam"))
    {
        if(column==QStringLiteral("弹性模量E（GPa）"))return QStringLiteral("200");
        if(column==QStringLiteral("截面积A（m2）"))return QStringLiteral("0.01");
        if(column==QStringLiteral("截面惯性矩Iz（m4）"))return QStringLiteral("0.0001");
        if(column==QStringLiteral("容重t/m3"))return QStringLiteral("7.85");
    }
    const auto valueFrom = [&column](const QMap<QString, QString>& values) -> QString {
        const auto it = values.constFind(column);
        return it == values.constEnd() ? QStringLiteral("0") : it.value();
    };

    // 初值优先取自本模块已有材料/渗流/流变样表中的代表行。
    // 关联编号以及样表未单列、但程序已有明确中性语义的可选字段使用 0。
    // 只用于新建/缺失字段初始化，不覆盖工程中已有值。
    if (modelKey == QStringLiteral("hoek_brown_rock"))
    {
        // Sample-only starting values; the user must calibrate actual material data.
        static const QMap<QString, QString> values = {
            {QStringLiteral("变形模量E（GPa）"), QStringLiteral("10")},
            {QStringLiteral("泊松比μ"), QStringLiteral("0.25")},
            {QStringLiteral("容重t/m3"), QStringLiteral("2.6")},
            {QStringLiteral("岩体常数mb"), QStringLiteral("1.5")},
            {QStringLiteral("岩体常数a"), QStringLiteral("0.5")},
            {QStringLiteral("岩体常数s"), QStringLiteral("0.01")},
            {QStringLiteral("完整岩石单轴抗压强度σci（MPa）"), QStringLiteral("100")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("drucker_prager_rock"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("变形模量E（GPa）"), QStringLiteral("10")},
            {QStringLiteral("泊松比μ"), QStringLiteral("0.27")},
            {QStringLiteral("容重t/m3"), QStringLiteral("2.62")},
            {QStringLiteral("内摩擦角φ（°）"), QStringLiteral("35")},
            {QStringLiteral("凝聚力C‘（MPa）"), QStringLiteral("1.5")},
            {QStringLiteral("剪胀角(°)"), QStringLiteral("0")},
            {QStringLiteral("DP子类型"), QStringLiteral("1")},
            {QStringLiteral("抗压强度Rc（MPa）"), QStringLiteral("0")},
            {QStringLiteral("抗拉强度Rt（MPa）"), QStringLiteral("0")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("huangjinping_underground_plant"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("母岩材料编号"), QStringLiteral("0")},
            {QStringLiteral("岩类分区"), QStringLiteral("0")},
            {QStringLiteral("半开挖区荷载释放系数"), QStringLiteral("1")},
            {QStringLiteral("再生时对应开挖级"), QStringLiteral("0")},
            {QStringLiteral("变形模量E（GPa）"), QStringLiteral("9.0")},
            {QStringLiteral("泊松比μ"), QStringLiteral("0.25")},
            {QStringLiteral("容重t/m3"), QStringLiteral("2.65")},
            {QStringLiteral("抗剪断强度内摩擦系数"), QStringLiteral("1.05")},
            {QStringLiteral("凝聚力C‘（MPa）"), QStringLiteral("1.13")},
            {QStringLiteral("剪胀角(°)"), QStringLiteral("0")},
            {QStringLiteral("洞室边缘松弛系数α0"), QStringLiteral("1.0")},
            {QStringLiteral("松弛距离d0"), QStringLiteral("5.0")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("soil_eb"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("干容重(t/m3)"), QStringLiteral("2.209")},
            {QStringLiteral("天然容重(t/m3)"), QStringLiteral("2.29")},
            {QStringLiteral("饱和容重(t/m3)"), QStringLiteral("2.397")},
            {QStringLiteral("φ0(°)"), QStringLiteral("51.6")},
            {QStringLiteral("Δφ(°)"), QStringLiteral("5.7")},
            {QStringLiteral("c(KPa)"), QStringLiteral("231.4")},
            {QStringLiteral("Rf"), QStringLiteral("0.69")},
            {QStringLiteral("K"), QStringLiteral("1082")},
            {QStringLiteral("n"), QStringLiteral("0.27")},
            {QStringLiteral("Kur"), QStringLiteral("2164")},
            {QStringLiteral("Nur"), QStringLiteral("0.27")},
            {QStringLiteral("Kb"), QStringLiteral("545")},
            {QStringLiteral("m"), QStringLiteral("0.25")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("soil_emu"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("干容重(t/m3)"), QStringLiteral("2.0")},
            {QStringLiteral("天然容重(t/m3)"), QStringLiteral("2.1")},
            {QStringLiteral("饱和容重(t/m3)"), QStringLiteral("2.2")},
            {QStringLiteral("φ0(°)"), QStringLiteral("35")},
            {QStringLiteral("Δφ(°)"), QStringLiteral("0")},
            {QStringLiteral("c(KPa)"), QStringLiteral("0")},
            {QStringLiteral("Rf"), QStringLiteral("0.8")},
            {QStringLiteral("K"), QStringLiteral("1000")},
            {QStringLiteral("n"), QStringLiteral("0.3")},
            {QStringLiteral("Kur"), QStringLiteral("1500")},
            {QStringLiteral("Nur"), QStringLiteral("0.3")},
            {QStringLiteral("G"), QStringLiteral("0.37")},
            {QStringLiteral("D"), QStringLiteral("11")},
            {QStringLiteral("F"), QStringLiteral("0.1")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("soil_emu_crush"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("干容重(t/m3)"), QStringLiteral("2.0")},
            {QStringLiteral("天然容重(t/m3)"), QStringLiteral("2.1")},
            {QStringLiteral("饱和容重(t/m3)"), QStringLiteral("2.2")},
            {QStringLiteral("φ0(°)"), QStringLiteral("35")},
            {QStringLiteral("Δφ(°)"), QStringLiteral("0")},
            {QStringLiteral("c(KPa)"), QStringLiteral("0")},
            {QStringLiteral("Rf"), QStringLiteral("0.8")},
            {QStringLiteral("K"), QStringLiteral("1000")},
            {QStringLiteral("n"), QStringLiteral("0.3")},
            {QStringLiteral("Kur"), QStringLiteral("1500")},
            {QStringLiteral("Nur"), QStringLiteral("0.3")},
            {QStringLiteral("C1"), QStringLiteral("0.37")},
            {QStringLiteral("n1"), QStringLiteral("11")},
            {QStringLiteral("C2"), QStringLiteral("0.37")},
            {QStringLiteral("n2"), QStringLiteral("11")},
            {QStringLiteral("C3"), QStringLiteral("0.37")},
            {QStringLiteral("n3"), QStringLiteral("11")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("dam_dynamic"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("是否液化"), QStringLiteral("0")},
            {QStringLiteral("DRYK1(KPa)"), QStringLiteral("970")},
            {QStringLiteral("DRYN1"), QStringLiteral("0.43")},
            {QStringLiteral("FAI(°)"), QStringLiteral("51.5")},
            {QStringLiteral("CO(KPa)"), QStringLiteral("100")},
            {QStringLiteral("PU"), QStringLiteral("0.35")},
            {QStringLiteral("DRYDAM"), QStringLiteral("0.25")},
            {QStringLiteral("RATIAO1(0.1)"), QStringLiteral("150")},
            {QStringLiteral("RATIAO2(0.3)"), QStringLiteral("150")},
            {QStringLiteral("RATIAO3(0.5)"), QStringLiteral("150")},
            {QStringLiteral("Kv"), QStringLiteral("1000")},
            {QStringLiteral("Kp"), QStringLiteral("800")},
            {QStringLiteral("nv"), QStringLiteral("0.5")},
            {QStringLiteral("np"), QStringLiteral("0.8")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("huangjinping_bar_element"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("变模E1,E2（GPa）"), QStringLiteral("206")},
            {QStringLiteral("屈服后变模E2（GPa）"), QStringLiteral("206")},
            {QStringLiteral("容重t/m3"), QStringLiteral("7.80")},
            {QStringLiteral("杆面积（m2）"), QStringLiteral("0.000804")},
            {QStringLiteral("抗拉（压）强度ft（MPa）"), QStringLiteral("390")},
            {QStringLiteral("极限抗压强度Rc（MPa）"), QStringLiteral("390")},
            {QStringLiteral("屈服抗拉强度Ryt（MPa）"), QStringLiteral("390")},
            {QStringLiteral("屈服抗压强度Ryc（MPa）"), QStringLiteral("390")},
            {QStringLiteral("间距(m)"), QStringLiteral("1.8")},
            {QStringLiteral("排距(m)"), QStringLiteral("1.8")},
            {QStringLiteral("预应力T（KN）"), QStringLiteral("0")},
            {QStringLiteral("预应系数"), QStringLiteral("1.0")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("dagangshan_isotropic"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("变形模量E（GPa）"), QStringLiteral("10")},
            {QStringLiteral("泊松比μ"), QStringLiteral("0.27")},
            {QStringLiteral("容重t/m3"), QStringLiteral("2.62")},
            {QStringLiteral("抗剪断强度内摩擦系数"), QStringLiteral("1.2")},
            {QStringLiteral("凝聚力C‘（MPa）"), QStringLiteral("1.5")},
            // Rc/Rt 为0表示自动推导；两个独立标志控制残余强度是否沿用峰值。
            {QStringLiteral("抗压强度Rc（MPa）"), QStringLiteral("0")},
            {QStringLiteral("抗拉强度Rt（MPa）"), QStringLiteral("0")},
            {QStringLiteral("残余抗剪断强度内摩擦系数"), QStringLiteral("0")},
            {QStringLiteral("残余凝聚力C‘（MPa）"), QStringLiteral("0")},
            {QStringLiteral("残余摩擦系数沿用峰值"), QStringLiteral("1")},
            {QStringLiteral("残余凝聚力沿用峰值"), QStringLiteral("1")},
            {QStringLiteral("剪胀角(°)"), QStringLiteral("0")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("soil_nanshui"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("干容重(t/m3)"), QStringLiteral("2.10")},
            {QStringLiteral("天然容重(t/m3)"), QStringLiteral("2.15")},
            {QStringLiteral("饱和容重(t/m3)"), QStringLiteral("2.20")},
            {QStringLiteral("φ0"), QStringLiteral("46.0")},
            {QStringLiteral("Δφ"), QStringLiteral("6.0")},
            {QStringLiteral("c(KPa)"), QStringLiteral("0")},
            {QStringLiteral("Rf"), QStringLiteral("0.79")},
            {QStringLiteral("K"), QStringLiteral("867")},
            {QStringLiteral("n"), QStringLiteral("0.39")},
            {QStringLiteral("Kur"), QStringLiteral("1133")},
            {QStringLiteral("Nur"), QStringLiteral("0.39")},
            {QStringLiteral("cd"), QStringLiteral("0.0043")},
            {QStringLiteral("nd"), QStringLiteral("0.62")},
            {QStringLiteral("Rd"), QStringLiteral("0.72")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("soil_kg"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("干容重(t/m3)"), QStringLiteral("2.10")},
            {QStringLiteral("天然容重(t/m3)"), QStringLiteral("2.15")},
            {QStringLiteral("饱和容重(t/m3)"), QStringLiteral("2.20")},
            {QStringLiteral("Ki,Kie(10^5Pa)"), QStringLiteral("80")},
            {QStringLiteral("αk,αke"), QStringLiteral("94")},
            {QStringLiteral("Gi,Gie(10^5Pa)"), QStringLiteral("106")},
            {QStringLiteral("αG,αGe"), QStringLiteral("112")},
            {QStringLiteral("βG"), QStringLiteral("-80")},
            {QStringLiteral("a(10^5Pa)"), QStringLiteral("132")},
            {QStringLiteral("b"), QStringLiteral("1.40")},
            {QStringLiteral("Kie(10^5Pa)"), QStringLiteral("80")},
            {QStringLiteral("αke"), QStringLiteral("94")},
            {QStringLiteral("Gie(10^5Pa)"), QStringLiteral("106")},
            {QStringLiteral("αGe"), QStringLiteral("112")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("jk_corewall_steady_seepage"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("渗透系数K(Lu)"), QStringLiteral("400")},
            // 与旧版求解输入保持兼容：原来 CONMAT(:,101) 未配置时固定写 1.0。
            {QStringLiteral("比储量S"), QStringLiteral("1.0")},
            {QStringLiteral("允许比降"), QStringLiteral("0.38")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("jk_corewall_anisotropic_seepage"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("渗透系数Kx(Lu)"), QStringLiteral("400")},
            {QStringLiteral("渗透系数Ky(Lu)"), QStringLiteral("400")},
            {QStringLiteral("渗透系数Kz(Lu)"), QStringLiteral("400")},
            {QStringLiteral("比储量S"), QStringLiteral("1.0")},
            {QStringLiteral("允许比降"), QStringLiteral("0.38")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("shenzhujiang_creep"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("应变速率系数α"), QStringLiteral("0.004")},
            {QStringLiteral("体积流变系数b"), QStringLiteral("0.0009")},
            {QStringLiteral("剪切流变系数d"), QStringLiteral("0.007")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("liguoying_creep"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("应变速率系数α"), QStringLiteral("0.006")},
            {QStringLiteral("体积流变系数b"), QStringLiteral("0.0006")},
            {QStringLiteral("体积流变系数c"), QStringLiteral("0.0002")},
            {QStringLiteral("剪切流变系数d"), QStringLiteral("0.004")},
            {QStringLiteral("体积流变指数m1"), QStringLiteral("0.769")},
            {QStringLiteral("体积流变指数m2"), QStringLiteral("0.635")},
            {QStringLiteral("剪切流变指数m3"), QStringLiteral("0.550")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("nine_param_creep"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("应变速率系数λv"), QStringLiteral("0.0678")},
            {QStringLiteral("体积流变系数Cα"), QStringLiteral("0.4445")},
            {QStringLiteral("体积流变系数Cβ"), QStringLiteral("0.4360")},
            {QStringLiteral("体积流变指数dα"), QStringLiteral("2.0827")},
            {QStringLiteral("体积流变指数dβ"), QStringLiteral("1.6383")},
            {QStringLiteral("主应变系数c"), QStringLiteral("0.2892")},
            {QStringLiteral("主应变系数d"), QStringLiteral("0.8465")},
            {QStringLiteral("主应变流变系数η"), QStringLiteral("0.0831")},
            {QStringLiteral("主应变流变系数m"), QStringLiteral("0.3899")},
        };
        return valueFrom(values);
    }

    if (modelKey == QStringLiteral("xiyuan_creep"))
    {
        static const QMap<QString, QString> values = {
            {QStringLiteral("瞬时弹性模量E1（GPa）"), QStringLiteral("1.5")},
            {QStringLiteral("粘塑性模量E2（GPa）"), QStringLiteral("2.0")},
            {QStringLiteral("泊松比μ1"), QStringLiteral("0.3")},
            {QStringLiteral("泊松比μ2"), QStringLiteral("0.4")},
            {QStringLiteral("粘弹性系数η1"), QStringLiteral("0.5")},
            {QStringLiteral("粘塑性系数η2"), QStringLiteral("0.6")},
        };
        return valueFrom(values);
    }

    return QStringLiteral("0");
}

QVector<MaterialModel> createDefaultMaterialModels()
{
    return {createLinearElasticModel(), createHoekBrownModel(), createDruckerPragerModel(), createHuangjinpingModel(), createSoilEbModel(), createSoilEmuModel(),
            createSoilEmuCrushModel(),
            createDamDynamicModel(), createHuangjinpingBarModel(), createDagangshanIsotropicModel(),
            createNanshuiModel(), createSoilKgModel(),createPlanarElasticBeamModel()};
}

QVector<MaterialModel> createSeepageSubModels()
{
    return {createSeepageModel(), createAnisotropicSeepageModel(),
            createShenzhujiangCreepModel(), createNineParamCreepModel(),
            createLiguoyingCreepModel(), createXiyuanCreepModel()};
}
