#include "MaterialLibraryData.h"

namespace
{
MaterialModel createHuangjinpingModel()
{
    MaterialModel model;
    model.key = QStringLiteral("huangjinping_underground_plant");
    model.displayName = QStringLiteral("黄金坪地下厂房模型");
    model.ib = 0;

    model.columns << QStringLiteral("母岩编号") << QStringLiteral("岩类分区")
                  << QStringLiteral("半开挖区\n荷载释放系数")
                  << QStringLiteral("再生时对应\n开挖级\n\n0：随开挖级再生\n-i：在-i级再生")
                  << QStringLiteral("变形\n模量\nE（GPa）") << QStringLiteral("泊松\n比\nμ")
                  << QStringLiteral("容重\n\nt/m3")
                  << QStringLiteral("抗剪断强度\n内摩擦系数\n ")
                  << QStringLiteral("凝聚力C‘\n（MPa）")
                  << QStringLiteral("洞室边缘\n松弛系数\nα0")
                  << QStringLiteral("松弛距离d0");

    model.columnOutputRows = QVector<int>(model.columns.size(), -1);
    return model;
}
} // namespace

QVector<MaterialModel> createDefaultMaterialModels()
{
    return {createHuangjinpingModel()};
}
