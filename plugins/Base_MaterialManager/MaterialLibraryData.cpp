#include "MaterialLibraryData.h"

namespace
{
MaterialModel createHuangjinpingModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_underground_plant");
    model.displayName = QStringLiteral("黄金坪厂房");
    model.ib = 0;

    model.columns << QStringLiteral("编号") << QStringLiteral("岩类分区")
                  << QStringLiteral("半开挖区荷载释放系数")
                  << QStringLiteral("再生时对应开挖级")
                  << QStringLiteral("变形模量E（GPa）") << QStringLiteral("泊松比μ")
                  << QStringLiteral("容重t/m3")
                  << QStringLiteral("抗剪断强度内摩擦系数")
                  << QStringLiteral("凝聚力C‘（MPa）")
                  << QStringLiteral("洞室边缘松弛系数α0")
                  << QStringLiteral("松弛距离d0");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createDamDynamicModel()
{
    MaterialModel model;
    model.key = QStringLiteral("dam_dynamic");
    model.displayName = QStringLiteral("坝料动力");
    model.ib = -4;

    model.columns << QStringLiteral("编号") << QStringLiteral("静力计算编号")
                  << QStringLiteral("是否液化") << QStringLiteral("DRYK1(KPa)")
                  << QStringLiteral("DRYN1") << QStringLiteral("FAI(°)")
                  << QStringLiteral("CO(KPa)") << QStringLiteral("PU")
                  << QStringLiteral("DRYDAM") << QStringLiteral("RATIAO1(0.1)")
                  << QStringLiteral("RATIAO2(0.3)") << QStringLiteral("RATIAO3(0.5)")
                  << QStringLiteral("Kv")
                  << QStringLiteral("Kp") << QStringLiteral("nv") << QStringLiteral("np");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSeepageModel()
{
    MaterialModel model;
    model.key = QStringLiteral("jk_corewall_steady_seepage");
    model.displayName = QStringLiteral("剑科稳态渗流");
    model.ib = -2;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("渗透系数K(Lu)")
                  << QStringLiteral("允许比降");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSoilEmuModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_emu");
    model.displayName = QStringLiteral("土体E-μ模型");
    model.ib = -3;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("G") << QStringLiteral("D")
                  << QStringLiteral("F");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createSoilEmuCrushModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_emu_crush");
    model.displayName = QStringLiteral("土体E-μ破碎模型");
    model.ib = -6;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("C1") << QStringLiteral("n1")
                  << QStringLiteral("C2") << QStringLiteral("n2") << QStringLiteral("C3")
                  << QStringLiteral("n3");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createHuangjinpingBarModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_bar_element");
    model.displayName = QStringLiteral("黄金坪杆单元");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("变模E1,E2（GPa）")
                  << QStringLiteral("容重t/m3") << QStringLiteral("杆面积（m2）")
                  << QStringLiteral("抗拉（压）强度ft（MPa）") << QStringLiteral("间距(m)")
                  << QStringLiteral("排距(m)") << QStringLiteral("预应力T（KN）")
                  << QStringLiteral("预应系数");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createDagangshanIsotropicModel()
{
    MaterialModel model;
    model.key = QStringLiteral("dagangshan_isotropic");
    model.displayName = QStringLiteral("大岗山各向同性");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("变形模量E（GPa）")
                  << QStringLiteral("泊松比μ") << QStringLiteral("容重t/m3")
                  << QStringLiteral("抗剪断强度内摩擦系数")
                  << QStringLiteral("凝聚力C‘（MPa）");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createNanshuiModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_nanshui");
    model.displayName = QStringLiteral("南水模型");
    model.ib = -5;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0") << QStringLiteral("Δφ") << QStringLiteral("c(KPa)")
                  << QStringLiteral("Rf") << QStringLiteral("K") << QStringLiteral("Kur")
                  << QStringLiteral("n") << QStringLiteral("cd") << QStringLiteral("nd")
                  << QStringLiteral("Rd");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createSoilKgModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_kg");
    model.displayName = QStringLiteral("土体K-G模型");
    model.ib = -2;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("Ki,Kie(10^5Pa)") << QStringLiteral("αk,αke")
                  << QStringLiteral("Gi,Gie(10^5Pa)") << QStringLiteral("αG,αGe")
                  << QStringLiteral("βG") << QStringLiteral("a(10^5Pa)")
                  << QStringLiteral("b");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createShenzhujiangCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("shenzhujiang_creep");
    model.displayName = QStringLiteral("沈珠江三参数");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("应变速率系数α")
                  << QStringLiteral("体积流变系数b") << QStringLiteral("剪切流变系数d");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createLiguoyingCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("liguoying_creep");
    model.displayName = QStringLiteral("李国英七参数");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("应变速率系数α")
                  << QStringLiteral("体积流变系数b") << QStringLiteral("体积流变系数c")
                  << QStringLiteral("剪切流变系数d") << QStringLiteral("体积流变指数m1")
                  << QStringLiteral("体积流变指数m2") << QStringLiteral("剪切流变指数m3");

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

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}


MaterialModel createXiyuanCreepModel()
{
    MaterialModel model;
    model.key = QStringLiteral("xiyuan_creep");
    model.displayName = QStringLiteral("西源流变");
    model.ib = 0;

    model.columns << QStringLiteral("序号") << QStringLiteral("瞬时弹性模量E1（GPa）")
                  << QStringLiteral("粘塑性模量E2（GPa）") << QStringLiteral("泊松比μ1")
                  << QStringLiteral("泊松比μ2") << QStringLiteral("粘弹性系数η1")
                  << QStringLiteral("粘塑性系数η2");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}

MaterialModel createSoilEbModel()
{
    MaterialModel model;
    model.key = QStringLiteral("soil_eb");
    model.displayName = QStringLiteral("土体E-B模型");
    model.ib = -1;

    model.columns << QStringLiteral("材料编号") << QStringLiteral("干容重(t/m3)")
                  << QStringLiteral("天然容重(t/m3)") << QStringLiteral("饱和容重(t/m3)")
                  << QStringLiteral("φ0(°)") << QStringLiteral("Δφ(°)")
                  << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
                  << QStringLiteral("n") << QStringLiteral("Kur") << QStringLiteral("Kb")
                  << QStringLiteral("m");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}
} // namespace

QVector<MaterialModel> createDefaultMaterialModels()
{
    return {createHuangjinpingModel(), createSoilEbModel(), createSoilEmuModel(),
            createSoilEmuCrushModel(),
            createDamDynamicModel(), createHuangjinpingBarModel(), createDagangshanIsotropicModel(),
            createNanshuiModel(), createSoilKgModel()};
}

QVector<MaterialModel> createSeepageSubModels()
{
    return {createSeepageModel(), createShenzhujiangCreepModel(), createNineParamCreepModel(),
            createLiguoyingCreepModel(), createXiyuanCreepModel()};
}
