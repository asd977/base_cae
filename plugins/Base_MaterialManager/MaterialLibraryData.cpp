#include "MaterialLibraryData.h"

namespace
{
MaterialModel createHuangjinpingModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_underground_plant");
    model.displayName = QStringLiteral("黄金坪地下厂房模型");
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
    model.displayName = QStringLiteral("坝料动力参数");
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
    return {createHuangjinpingModel(), createSoilEbModel(), createDamDynamicModel()};
}
