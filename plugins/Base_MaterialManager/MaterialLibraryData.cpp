#include "MaterialLibraryData.h"

#include <utility>
#include <QUuid>

namespace
{
static MaterialEntry makeEntry(const QString &name, const QStringList &columns, const QStringList &values)
{
    MaterialEntry entry;
    entry.id = QUuid::createUuid().toString();
    entry.name = name;
    const int count = qMin(columns.size(), values.size());
    for (int i = 0; i < count; ++i) {
        entry.properties.insert(columns.at(i), values.at(i));
    }
    return entry;
}
} // namespace

// ======================================================
// Default material library (HARD-CODED, no parsing)
// ======================================================
QVector<MaterialModel> createDefaultMaterialModels()
{
    QVector<MaterialModel> models;

    // =========================
    // 样表1（IB=0）
    // =========================
    MaterialModel isotropicRock;
    isotropicRock.key = QStringLiteral("huangjinping_isotropic_initial");
    isotropicRock.displayName = QStringLiteral("各向同性岩体（黄金坪地下厂房）");
    isotropicRock.ib = 0;

    isotropicRock.columns.clear();
    isotropicRock.columns
        << QStringLiteral("序号") << QStringLiteral("母岩编号") << QStringLiteral("岩类分区")
        << QStringLiteral("半开挖区荷载释放系数") << QStringLiteral("再生开挖级")
        << QStringLiteral("变形模量E(GPa)") << QStringLiteral("泊松比")
        << QStringLiteral("容重(t/m3)") << QStringLiteral("内摩擦系数")
        << QStringLiteral("凝聚力C'(MPa)") << QStringLiteral("洞室边缘松弛系数α0")
        << QStringLiteral("松弛距离d0");

    isotropicRock.columnOutputRows = QVector<int>(isotropicRock.columns.size(), -1);

    isotropicRock.materials.append(makeEntry(QStringLiteral("原岩区：ITAG_EX=0"), isotropicRock.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("II"), isotropicRock.columns,
        {QStringLiteral("3"), QStringLiteral("3"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("16.25"), QStringLiteral("0.23"), QStringLiteral("2.7"), QStringLiteral("1.23"), QStringLiteral("1.58"), QStringLiteral("1.0"), QStringLiteral("3.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("III"), isotropicRock.columns,
        {QStringLiteral("4"), QStringLiteral("4"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13"), QStringLiteral("1.0"), QStringLiteral("5.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("IV"), isotropicRock.columns,
        {QStringLiteral("5"), QStringLiteral("5"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78"), QStringLiteral("1.0"), QStringLiteral("7.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("V"), isotropicRock.columns,
        {QStringLiteral("6"), QStringLiteral("6"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35"), QStringLiteral("1.0"), QStringLiteral("9.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("f10-14"), isotropicRock.columns,
        {QStringLiteral("7"), QStringLiteral("7"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05"), QStringLiteral("1.0"), QStringLiteral("9.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙"), isotropicRock.columns,
        {QStringLiteral("8"), QStringLiteral("8"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("3.0"), QStringLiteral("0.38"), QStringLiteral("1.5"), QStringLiteral("0.50"), QStringLiteral("0.30"), QStringLiteral("1.0"), QStringLiteral("9.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙密集带（IV类范围内）"), isotropicRock.columns,
        {QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35"), QStringLiteral("1.0"), QStringLiteral("9.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("开挖区：ITAG_EX=-1"), isotropicRock.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("II(开挖区)"), isotropicRock.columns,
        {QStringLiteral("13"), QStringLiteral("3"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("16.25"), QStringLiteral("0.23"), QStringLiteral("2.7"), QStringLiteral("1.23"), QStringLiteral("1.58"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("III(开挖区)"), isotropicRock.columns,
        {QStringLiteral("14"), QStringLiteral("4"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("IV(开挖区)"), isotropicRock.columns,
        {QStringLiteral("15"), QStringLiteral("5"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("V(开挖区)"), isotropicRock.columns,
        {QStringLiteral("16"), QStringLiteral("6"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("f10-14(开挖区)"), isotropicRock.columns,
        {QStringLiteral("17"), QStringLiteral("7"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙(开挖区)"), isotropicRock.columns,
        {QStringLiteral("18"), QStringLiteral("8"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("3.0"), QStringLiteral("0.38"), QStringLiteral("1.5"), QStringLiteral("0.50"), QStringLiteral("0.30"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙密集带(开挖区)"), isotropicRock.columns,
        {QStringLiteral("19"), QStringLiteral("9"), QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("半开挖区：ITAG_EX=-2"), isotropicRock.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("尾水连接管（III）"), isotropicRock.columns,
        {QStringLiteral("23"), QStringLiteral("4"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13"), QStringLiteral("1.0"), QStringLiteral("5.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("母线洞（III）"), isotropicRock.columns,
        {QStringLiteral("24"), QStringLiteral("4"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13"), QStringLiteral("1.0"), QStringLiteral("5.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("f10-14 过尾水连接管"), isotropicRock.columns,
        {QStringLiteral("27"), QStringLiteral("7"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05"), QStringLiteral("1.0"), QStringLiteral("9.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙过母线洞"), isotropicRock.columns,
        {QStringLiteral("28"), QStringLiteral("8"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78"), QStringLiteral("1.0"), QStringLiteral("7.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙过尾水连接管"), isotropicRock.columns,
        {QStringLiteral("29"), QStringLiteral("9"), QStringLiteral("-2"), QStringLiteral("0.3213"), QStringLiteral("0"),
         QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78"), QStringLiteral("1.0"), QStringLiteral("7.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("再生区:ITAG_EX=1"), isotropicRock.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("吊车梁岩体（母岩为III）"), isotropicRock.columns,
        {QStringLiteral("43"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("-4"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("喷C25砼"), isotropicRock.columns,
        {QStringLiteral("44"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("母线洞喷C25砼"), isotropicRock.columns,
        {QStringLiteral("45"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙过吊车梁"), isotropicRock.columns,
        {QStringLiteral("46"), QStringLiteral("8"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("-4"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("f10-14过喷C25砼"), isotropicRock.columns,
        {QStringLiteral("47"), QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicRock.materials.append(makeEntry(QStringLiteral("裂隙过喷C25砼"), isotropicRock.columns,
        {QStringLiteral("48"), QStringLiteral("8"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"),
         QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9"), QStringLiteral("0.0"), QStringLiteral("0.0")}));

    models.append(isotropicRock);

    // =========================
    // 样表2（IB=-1）
    // =========================
    MaterialModel ebModel;
    ebModel.key = QStringLiteral("soil_eb");
    ebModel.displayName = QStringLiteral("土体E-B模型");
    ebModel.ib = -1;

    ebModel.columns.clear();
    ebModel.columns
        << QStringLiteral("材料编号") << QStringLiteral("干容重") << QStringLiteral("天然容重") << QStringLiteral("饱和容重")
        << QStringLiteral("φ0") << QStringLiteral("Δφ（行：76）") << QStringLiteral("c(KPa)") << QStringLiteral("Rf（行：71）") << QStringLiteral("K（行：69）")
        << QStringLiteral("n（行：70）") << QStringLiteral("Kur（行：72）") << QStringLiteral("Kb（行：74）") << QStringLiteral("m（行：75）");

    ebModel.columnOutputRows = QVector<int>(ebModel.columns.size(), -1);
    ebModel.columnOutputRows[8]  = 69; // K
    ebModel.columnOutputRows[9]  = 70; // n
    ebModel.columnOutputRows[7]  = 71; // Rf
    ebModel.columnOutputRows[10] = 72; // Kur
    ebModel.columnOutputRows[11] = 74; // Kb
    ebModel.columnOutputRows[12] = 75; // m
    ebModel.columnOutputRows[5]  = 76; // Δφ

    ebModel.materials.append(makeEntry(QStringLiteral("2A垫层"), ebModel.columns,
        {QStringLiteral("7"), QStringLiteral("2.209"), QStringLiteral("2.29"), QStringLiteral("2.397"), QStringLiteral("51.6"), QStringLiteral("5.7"), QStringLiteral("231.4"), QStringLiteral("0.69"), QStringLiteral("1082"), QStringLiteral("0.27"), QStringLiteral("2164"), QStringLiteral("545"), QStringLiteral("0.25")}));
    ebModel.materials.append(makeEntry(QStringLiteral("3A过渡料"), ebModel.columns,
        {QStringLiteral("8"), QStringLiteral("2.13"), QStringLiteral("2.22"), QStringLiteral("2.334"), QStringLiteral("53.8"), QStringLiteral("8.8"), QStringLiteral("236.6"), QStringLiteral("0.75"), QStringLiteral("968"), QStringLiteral("0.23"), QStringLiteral("1936"), QStringLiteral("328"), QStringLiteral("0.17")}));
    ebModel.materials.append(makeEntry(QStringLiteral("3B堆石"), ebModel.columns,
        {QStringLiteral("9"), QStringLiteral("2.077"), QStringLiteral("2.14"), QStringLiteral("2.284"), QStringLiteral("52.9"), QStringLiteral("9.3"), QStringLiteral("196.6"), QStringLiteral("0.76"), QStringLiteral("985"), QStringLiteral("0.28"), QStringLiteral("1722"), QStringLiteral("238"), QStringLiteral("0.21")}));
    ebModel.materials.append(makeEntry(QStringLiteral("1B弃渣盖重"), ebModel.columns,
        {QStringLiteral("10"), QStringLiteral("2.08"), QStringLiteral("2.19"), QStringLiteral("2.3"), QStringLiteral("38.3"), QStringLiteral("3.5"), QStringLiteral("0.0"), QStringLiteral("0.75"), QStringLiteral("695"), QStringLiteral("0.29"), QStringLiteral("1390"), QStringLiteral("352"), QStringLiteral("0.29")}));
    ebModel.materials.append(makeEntry(QStringLiteral("1A铺盖"), ebModel.columns,
        {QStringLiteral("12"), QStringLiteral("1.42"), QStringLiteral("1.71"), QStringLiteral("2.3"), QStringLiteral("31.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.72"), QStringLiteral("350"), QStringLiteral("0.43"), QStringLiteral("700"), QStringLiteral("350"), QStringLiteral("0.18")}));
    ebModel.materials.append(makeEntry(QStringLiteral("2B特殊垫层"), ebModel.columns,
        {QStringLiteral("13"), QStringLiteral("2.218"), QStringLiteral("2.30"), QStringLiteral("2.396"), QStringLiteral("42.6"), QStringLiteral("4.5"), QStringLiteral("85"), QStringLiteral("0.71"), QStringLiteral("832"), QStringLiteral("0.35"), QStringLiteral("1664"), QStringLiteral("470"), QStringLiteral("0.39")}));
    ebModel.materials.append(makeEntry(QStringLiteral("3C反滤料"), ebModel.columns,
        {QStringLiteral("16"), QStringLiteral("2.12"), QStringLiteral("2.17"), QStringLiteral("2.20"), QStringLiteral("48"), QStringLiteral("8.0"), QStringLiteral("0.0"), QStringLiteral("0.68"), QStringLiteral("1035"), QStringLiteral("0.33"), QStringLiteral("2070"), QStringLiteral("526"), QStringLiteral("0.24")}));
    ebModel.materials.append(makeEntry(QStringLiteral("P下游砌石"), ebModel.columns,
        {QStringLiteral("17"), QStringLiteral("2.01"), QStringLiteral("2.07"), QStringLiteral("2.17"), QStringLiteral("51.8"), QStringLiteral("9.8"), QStringLiteral("0.0"), QStringLiteral("0.78"), QStringLiteral("850"), QStringLiteral("0.53"), QStringLiteral("1700"), QStringLiteral("325"), QStringLiteral("0.33")}));
    ebModel.materials.append(makeEntry(QStringLiteral("①gl+fglQ3"), ebModel.columns,
        {QStringLiteral("21"), QStringLiteral("2.09"), QStringLiteral("2.119"), QStringLiteral("2.3"), QStringLiteral("42.6"), QStringLiteral("4.5"), QStringLiteral("85"), QStringLiteral("0.7"), QStringLiteral("752"), QStringLiteral("0.29"), QStringLiteral("1504"), QStringLiteral("276"), QStringLiteral("0.29")}));
    ebModel.materials.append(makeEntry(QStringLiteral("④ColQ4"), ebModel.columns,
        {QStringLiteral("22"), QStringLiteral("2.1"), QStringLiteral("2.19"), QStringLiteral("2.3"), QStringLiteral("38.3"), QStringLiteral("3.5"), QStringLiteral("0.0"), QStringLiteral("0.75"), QStringLiteral("695"), QStringLiteral("0.29"), QStringLiteral("1390"), QStringLiteral("352"), QStringLiteral("0.29")}));
    ebModel.materials.append(makeEntry(QStringLiteral("③Col+dlQ4"), ebModel.columns,
        {QStringLiteral("23"), QStringLiteral("1.42"), QStringLiteral("1.71"), QStringLiteral("1.9"), QStringLiteral("31.0"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.72"), QStringLiteral("350"), QStringLiteral("0.43"), QStringLiteral("700"), QStringLiteral("350"), QStringLiteral("0.18")}));
    ebModel.materials.append(makeEntry(QStringLiteral("④alQ4"), ebModel.columns,
        {QStringLiteral("24"), QStringLiteral("2.09"), QStringLiteral("2.119"), QStringLiteral("2.3"), QStringLiteral("42.6"), QStringLiteral("4.5"), QStringLiteral("85"), QStringLiteral("0.7"), QStringLiteral("752"), QStringLiteral("0.29"), QStringLiteral("1504"), QStringLiteral("276"), QStringLiteral("0.29")}));
    ebModel.materials.append(makeEntry(QStringLiteral("泥皮"), ebModel.columns,
        {QStringLiteral("26"), QStringLiteral("1.67"), QStringLiteral("1.67"), QStringLiteral("1.67"), QStringLiteral("10.0"), QStringLiteral("0.0"), QStringLiteral("50"), QStringLiteral("0.86"), QStringLiteral("1400"), QStringLiteral("0.66"), QStringLiteral("2000"), QStringLiteral("600"), QStringLiteral("0.26")}));
    ebModel.materials.append(makeEntry(QStringLiteral("el+dlQ4"), ebModel.columns,
        {QStringLiteral("28"), QStringLiteral("2.12"), QStringLiteral("2.22"), QStringLiteral("2.3"), QStringLiteral("40.2"), QStringLiteral("4.0"), QStringLiteral("0.0"), QStringLiteral("0.73"), QStringLiteral("760"), QStringLiteral("0.27"), QStringLiteral("1520"), QStringLiteral("405"), QStringLiteral("0.23")}));

    models.append(ebModel);

    // =========================
    // 样表3（IB=-4）
    // =========================
    MaterialModel dynamicModel;
    dynamicModel.key = QStringLiteral("dam_dynamic");
    dynamicModel.displayName = QStringLiteral("坝料动力参数");
    dynamicModel.ib = -4;

    dynamicModel.columns.clear();
    dynamicModel.columns
        << QStringLiteral("编号（行：88）") << QStringLiteral("静力计算编号（行：81）") << QStringLiteral("是否液化（行：80）") << QStringLiteral("σd(KPa)（行：83）")
        << QStringLiteral("Rf") << QStringLiteral("φ(°)（行：82）") << QStringLiteral("c") << QStringLiteral("K1") << QStringLiteral("n1")
        << QStringLiteral("残余0.1（行：85）") << QStringLiteral("残余0.3（行：86）") << QStringLiteral("残余0.5（行：87）") << QStringLiteral("Kv（行：105）")
        << QStringLiteral("Kp（行：106）") << QStringLiteral("nv（行：107）") << QStringLiteral("np（行：108）");

    dynamicModel.columnOutputRows = QVector<int>(dynamicModel.columns.size(), -1);
    dynamicModel.columnOutputRows[2]  = 80;
    dynamicModel.columnOutputRows[1]  = 81;
    dynamicModel.columnOutputRows[5]  = 82;
    dynamicModel.columnOutputRows[3]  = 83;
    dynamicModel.columnOutputRows[9]  = 85;
    dynamicModel.columnOutputRows[10] = 86;
    dynamicModel.columnOutputRows[11] = 87;
    dynamicModel.columnOutputRows[0]  = 88;
    dynamicModel.columnOutputRows[12] = 105;
    dynamicModel.columnOutputRows[13] = 106;
    dynamicModel.columnOutputRows[14] = 107;
    dynamicModel.columnOutputRows[15] = 108;

    dynamicModel.materials.append(makeEntry(QStringLiteral("2A垫层"), dynamicModel.columns,
        {QStringLiteral("81"), QStringLiteral("7"), QStringLiteral("0"), QStringLiteral("970"), QStringLiteral("0.43"), QStringLiteral("51.5"), QStringLiteral("100"), QStringLiteral("0.35"), QStringLiteral("0.25"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("3A过渡料"), dynamicModel.columns,
        {QStringLiteral("82"), QStringLiteral("8"), QStringLiteral("1"), QStringLiteral("1000"), QStringLiteral("0.45"), QStringLiteral("52"), QStringLiteral("200"), QStringLiteral("0.34"), QStringLiteral("0.22"), QStringLiteral("180"), QStringLiteral("180"), QStringLiteral("180"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("3B堆石"), dynamicModel.columns,
        {QStringLiteral("83"), QStringLiteral("9"), QStringLiteral("0"), QStringLiteral("830"), QStringLiteral("0.46"), QStringLiteral("48"), QStringLiteral("300"), QStringLiteral("0.32"), QStringLiteral("0.22"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("1B弃渣盖重"), dynamicModel.columns,
        {QStringLiteral("84"), QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("750"), QStringLiteral("0.40"), QStringLiteral("38.3"), QStringLiteral("100"), QStringLiteral("0.36"), QStringLiteral("0.26"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("1A铺盖"), dynamicModel.columns,
        {QStringLiteral("85"), QStringLiteral("12"), QStringLiteral("0"), QStringLiteral("750"), QStringLiteral("0.40"), QStringLiteral("31.0"), QStringLiteral("200"), QStringLiteral("0.36"), QStringLiteral("0.26"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("170"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("2B特殊垫层"), dynamicModel.columns,
        {QStringLiteral("86"), QStringLiteral("13"), QStringLiteral("0"), QStringLiteral("970"), QStringLiteral("0.43"), QStringLiteral("49"), QStringLiteral("300"), QStringLiteral("0.35"), QStringLiteral("0.25"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("3C反滤料"), dynamicModel.columns,
        {QStringLiteral("87"), QStringLiteral("16"), QStringLiteral("0"), QStringLiteral("820"), QStringLiteral("0.45"), QStringLiteral("48"), QStringLiteral("100"), QStringLiteral("0.35"), QStringLiteral("0.24"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("P下游砌石"), dynamicModel.columns,
        {QStringLiteral("88"), QStringLiteral("17"), QStringLiteral("0"), QStringLiteral("800"), QStringLiteral("0.46"), QStringLiteral("51.8"), QStringLiteral("200"), QStringLiteral("0.32"), QStringLiteral("0.24"), QStringLiteral("190"), QStringLiteral("190"), QStringLiteral("190"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("gl+fglQ3"), dynamicModel.columns,
        {QStringLiteral("89"), QStringLiteral("21"), QStringLiteral("0"), QStringLiteral("850"), QStringLiteral("0.44"), QStringLiteral("43.5"), QStringLiteral("300"), QStringLiteral("0.33"), QStringLiteral("0.24"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("col+dlQ4(⑤-2)"), dynamicModel.columns,
        {QStringLiteral("90"), QStringLiteral("22"), QStringLiteral("0"), QStringLiteral("450"), QStringLiteral("0.41"), QStringLiteral("38.3"), QStringLiteral("100"), QStringLiteral("0.42"), QStringLiteral("0.27"), QStringLiteral("140"), QStringLiteral("140"), QStringLiteral("140"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("lQ4"), dynamicModel.columns,
        {QStringLiteral("91"), QStringLiteral("23"), QStringLiteral("1"), QStringLiteral("740"), QStringLiteral("0.43"), QStringLiteral("31.0"), QStringLiteral("200"), QStringLiteral("0.38"), QStringLiteral("0.25"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("150"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("colQ3"), dynamicModel.columns,
        {QStringLiteral("92"), QStringLiteral("24"), QStringLiteral("1"), QStringLiteral("850"), QStringLiteral("0.44"), QStringLiteral("43.5"), QStringLiteral("300"), QStringLiteral("0.33"), QStringLiteral("0.24"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("160"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("泥皮"), dynamicModel.columns,
        {QStringLiteral("93"), QStringLiteral("26"), QStringLiteral("0"), QStringLiteral("380"), QStringLiteral("0.43"), QStringLiteral("10.0"), QStringLiteral("50"), QStringLiteral("0.44"), QStringLiteral("0.31"), QStringLiteral("180"), QStringLiteral("180"), QStringLiteral("180"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));
    dynamicModel.materials.append(makeEntry(QStringLiteral("el+dlQ4"), dynamicModel.columns,
        {QStringLiteral("94"), QStringLiteral("28"), QStringLiteral("0"), QStringLiteral("450"), QStringLiteral("0.41"), QStringLiteral("40.2"), QStringLiteral("80"), QStringLiteral("0.42"), QStringLiteral("0.27"), QStringLiteral("140"), QStringLiteral("140"), QStringLiteral("140"), QStringLiteral("1000"), QStringLiteral("800"), QStringLiteral("0.5"), QStringLiteral("0.8")}));

    models.append(dynamicModel);

    // =========================
    // 样表4（IB=0）
    // =========================
    MaterialModel seepageModel;
    seepageModel.key = QStringLiteral("jianke_seepage");
    seepageModel.displayName = QStringLiteral("剑科心墙堆石坝稳态渗流参数");
    seepageModel.ib = 0;

    seepageModel.columns.clear();
    seepageModel.columns << QStringLiteral("材料编号") << QStringLiteral("渗透系数K(Lu)") << QStringLiteral("允许比降");
    seepageModel.columnOutputRows = QVector<int>(seepageModel.columns.size(), -1);

    seepageModel.materials.append(makeEntry(QStringLiteral("alQ3"), seepageModel.columns,
        {QStringLiteral("3"), QStringLiteral("35000.0"), QStringLiteral("0.09")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("alQ4"), seepageModel.columns,
        {QStringLiteral("4"), QStringLiteral("35000.0"), QStringLiteral("0.09")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("Col+dlQ4"), seepageModel.columns,
        {QStringLiteral("5"), QStringLiteral("4500.0"), QStringLiteral("0.14")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("垫层料"), seepageModel.columns,
        {QStringLiteral("6"), QStringLiteral("720.0"), QStringLiteral("0.12")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("坝顶填筑"), seepageModel.columns,
        {QStringLiteral("7"), QStringLiteral("10.0"), QStringLiteral("0.13")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("花岗岩堆石料(基准)"), seepageModel.columns,
        {QStringLiteral("8"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("花岗岩堆石料(基准湿化)"), seepageModel.columns,
        {QStringLiteral("27"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("防渗土料(基准)"), seepageModel.columns,
        {QStringLiteral("9"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("防渗土料(基准湿化)"), seepageModel.columns,
        {QStringLiteral("28"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("防渗土料(比较方案二)"), seepageModel.columns,
        {QStringLiteral("62"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("反滤层1(基准)"), seepageModel.columns,
        {QStringLiteral("10"), QStringLiteral("543.0"), QStringLiteral("0.15")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("反滤层1(基准湿化)"), seepageModel.columns,
        {QStringLiteral("29"), QStringLiteral("543.0"), QStringLiteral("0.15")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("反滤层2(基准)"), seepageModel.columns,
        {QStringLiteral("11"), QStringLiteral("543.0"), QStringLiteral("0.15")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("反滤层2(基准湿化)"), seepageModel.columns,
        {QStringLiteral("30"), QStringLiteral("543.0"), QStringLiteral("0.15")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("过渡料(基准)"), seepageModel.columns,
        {QStringLiteral("12"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("过渡料(基准湿化)"), seepageModel.columns,
        {QStringLiteral("31"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("过渡料(比较方案一)"), seepageModel.columns,
        {QStringLiteral("60"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("过渡料(比较方案二)"), seepageModel.columns,
        {QStringLiteral("61"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("大块石护坡"), seepageModel.columns,
        {QStringLiteral("18"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("大块石填筑"), seepageModel.columns,
        {QStringLiteral("19"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("弃渣盖重"), seepageModel.columns,
        {QStringLiteral("22"), QStringLiteral("400.0"), QStringLiteral("0.38")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("高塑性粘土"), seepageModel.columns,
        {QStringLiteral("24"), QStringLiteral("1.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("粘土/砼接触面泥皮"), seepageModel.columns,
        {QStringLiteral("25"), QStringLiteral("1.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("T3x新鲜"), seepageModel.columns,
        {QStringLiteral("13"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("T3x弱卸荷"), seepageModel.columns,
        {QStringLiteral("14"), QStringLiteral("10.0"), QStringLiteral("0.13")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("T3x强卸荷"), seepageModel.columns,
        {QStringLiteral("15"), QStringLiteral("40.0"), QStringLiteral("0.13")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("γδ53新鲜"), seepageModel.columns,
        {QStringLiteral("16"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("γδ53弱卸荷"), seepageModel.columns,
        {QStringLiteral("17"), QStringLiteral("10.0"), QStringLiteral("0.13")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("砼"), seepageModel.columns,
        {QStringLiteral("20"), QStringLiteral("0.01"), QStringLiteral("180")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("帷幕/基岩接触面"), seepageModel.columns,
        {QStringLiteral("21"), QStringLiteral("3.0"), QStringLiteral("50")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("挤压带"), seepageModel.columns,
        {QStringLiteral("23"), QStringLiteral("50"), QStringLiteral("40")}));
    seepageModel.materials.append(makeEntry(QStringLiteral("帷幕"), seepageModel.columns,
        {QStringLiteral("26"), QStringLiteral("3.00"), QStringLiteral("18")}));

    models.append(seepageModel);

    // =========================
    // 样表5（IB=-3）
    // =========================
    MaterialModel emuModel;
    emuModel.key = QStringLiteral("soil_emu");
    emuModel.displayName = QStringLiteral("土体E-μ模型");
    emuModel.ib = -3;

    emuModel.columns.clear();
    emuModel.columns
        << QStringLiteral("材料编号") << QStringLiteral("干容重") << QStringLiteral("天然容重（行：90）") << QStringLiteral("饱和容重（行：89）")
        << QStringLiteral("φ0") << QStringLiteral("Δφ") << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
        << QStringLiteral("n") << QStringLiteral("G（行：77）") << QStringLiteral("D（行：78）") << QStringLiteral("F（行：79）");

    emuModel.columnOutputRows = QVector<int>(emuModel.columns.size(), -1);
    emuModel.columnOutputRows[10] = 77; // G
    emuModel.columnOutputRows[11] = 78; // D
    emuModel.columnOutputRows[12] = 79; // F
    emuModel.columnOutputRows[3]  = 89; // 饱和容重
    emuModel.columnOutputRows[2]  = 90; // 天然容重

    emuModel.materials.append(makeEntry(QStringLiteral("上游铺盖（回填石渣料）"), emuModel.columns,
        {QStringLiteral("4"), QStringLiteral("1.8"), QStringLiteral("2.0"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.75"), QStringLiteral("500"), QStringLiteral("0.4"), QStringLiteral("0.38"), QStringLiteral("2.7"), QStringLiteral("0.15")}));
    emuModel.materials.append(makeEntry(QStringLiteral("上游堆石料"), emuModel.columns,
        {QStringLiteral("5"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.1")}));
    emuModel.materials.append(makeEntry(QStringLiteral("上游过渡层"), emuModel.columns,
        {QStringLiteral("6"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("33"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.78"), QStringLiteral("900"), QStringLiteral("0.3"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.08")}));
    emuModel.materials.append(makeEntry(QStringLiteral("沥青砼心墙"), emuModel.columns,
        {QStringLiteral("7"), QStringLiteral("2.10"), QStringLiteral("2.35"), QStringLiteral("2.40"), QStringLiteral("24"), QStringLiteral("0"), QStringLiteral("120"), QStringLiteral("0.82"), QStringLiteral("600"), QStringLiteral("0.4"), QStringLiteral("0.42"), QStringLiteral("7"), QStringLiteral("0.08")}));
    emuModel.materials.append(makeEntry(QStringLiteral("下游过渡层"), emuModel.columns,
        {QStringLiteral("9"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("33"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.78"), QStringLiteral("900"), QStringLiteral("0.3"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.08")}));
    emuModel.materials.append(makeEntry(QStringLiteral("下游堆石料"), emuModel.columns,
        {QStringLiteral("10"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.1")}));
    emuModel.materials.append(makeEntry(QStringLiteral("上游1级找平堆石"), emuModel.columns,
        {QStringLiteral("15"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.1")}));
    emuModel.materials.append(makeEntry(QStringLiteral("下游1级找平堆石"), emuModel.columns,
        {QStringLiteral("16"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.1")}));
    emuModel.materials.append(makeEntry(QStringLiteral("K5沉积粘土"), emuModel.columns,
        {QStringLiteral("42"), QStringLiteral("1.85"), QStringLiteral("1.90"), QStringLiteral("2.00"), QStringLiteral("16.4"), QStringLiteral("0.0"), QStringLiteral("21.8"), QStringLiteral("0.87"), QStringLiteral("270"), QStringLiteral("0.47"), QStringLiteral("0.46"), QStringLiteral("0.05"), QStringLiteral("0.58")}));

    models.append(emuModel);

    // =========================
    // 样表6（IB=0）
    // =========================
    MaterialModel barModel;
    barModel.key = QStringLiteral("huangjinping_bolt");
    barModel.displayName = QStringLiteral("杆单元参数（黄金坪地下厂房）");
    barModel.ib = 0;

    barModel.columns.clear();
    barModel.columns
        << QStringLiteral("序号") << QStringLiteral("变形模量E1,E2(GPa)") << QStringLiteral("容重") << QStringLiteral("杆面积(m2)")
        << QStringLiteral("抗拉（压）强度ft(MPa)") << QStringLiteral("间距(m)") << QStringLiteral("排距(m)")
        << QStringLiteral("预应力T(KN)") << QStringLiteral("预应系数");
    barModel.columnOutputRows = QVector<int>(barModel.columns.size(), -1);

    barModel.materials.append(makeEntry(QStringLiteral("15m预应力锚索1500KN"), barModel.columns,
        {QStringLiteral("20"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.002"), QStringLiteral("1800"), QStringLiteral("4.5"), QStringLiteral("4.5"), QStringLiteral("1500.0"), QStringLiteral("1.0")}));
    barModel.materials.append(makeEntry(QStringLiteral("8m/9m普通锚杆 φ32"), barModel.columns,
        {QStringLiteral("21"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.000804"), QStringLiteral("390"), QStringLiteral("1.8"), QStringLiteral("1.8"), QStringLiteral("0.0"), QStringLiteral("1.0")}));
    barModel.materials.append(makeEntry(QStringLiteral("6m/7m普通锚杆 φ28"), barModel.columns,
        {QStringLiteral("22"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.000616"), QStringLiteral("390"), QStringLiteral("1.8"), QStringLiteral("1.8"), QStringLiteral("0.0"), QStringLiteral("0.85")}));
    barModel.materials.append(makeEntry(QStringLiteral("8m/9m普通锚杆 φ32（密排）"), barModel.columns,
        {QStringLiteral("23"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.000804"), QStringLiteral("390"), QStringLiteral("1.5"), QStringLiteral("1.5"), QStringLiteral("0.0"), QStringLiteral("0.85")}));
    barModel.materials.append(makeEntry(QStringLiteral("15m预应力锚索1500KN(密排)"), barModel.columns,
        {QStringLiteral("24"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.002"), QStringLiteral("1800"), QStringLiteral("4.0"), QStringLiteral("4.0"), QStringLiteral("1500"), QStringLiteral("0.70")}));
    barModel.materials.append(makeEntry(QStringLiteral("20m预应力锚索1750KN"), barModel.columns,
        {QStringLiteral("25"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.002"), QStringLiteral("1800"), QStringLiteral("4.0"), QStringLiteral("4.0"), QStringLiteral("1750"), QStringLiteral("0.9")}));
    barModel.materials.append(makeEntry(QStringLiteral("45m对穿预应力锚索1750KN"), barModel.columns,
        {QStringLiteral("26"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.002"), QStringLiteral("1800"), QStringLiteral("4.0"), QStringLiteral("4.0"), QStringLiteral("1750"), QStringLiteral("0.9")}));
    barModel.materials.append(makeEntry(QStringLiteral("6m/7m普通锚杆 φ28(1.5m)"), barModel.columns,
        {QStringLiteral("27"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.000616"), QStringLiteral("390"), QStringLiteral("1.5"), QStringLiteral("1.5"), QStringLiteral("0.0"), QStringLiteral("0.9")}));
    barModel.materials.append(makeEntry(QStringLiteral("4.5m/5m普通锚杆 φ25"), barModel.columns,
        {QStringLiteral("28"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.000491"), QStringLiteral("390"), QStringLiteral("1.5"), QStringLiteral("1.5"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    barModel.materials.append(makeEntry(QStringLiteral("20m预应力锚索1750KN(4.5m间距)"), barModel.columns,
        {QStringLiteral("29"), QStringLiteral("206"), QStringLiteral("7.80"), QStringLiteral("0.002"), QStringLiteral("1800"), QStringLiteral("4.5"), QStringLiteral("4.5"), QStringLiteral("1750"), QStringLiteral("1.0")}));

    models.append(barModel);

    // =========================
    // 样表7（IB=0）
    // =========================
    MaterialModel dagangshanModel;
    dagangshanModel.key = QStringLiteral("dagangshan_isotropic");
    dagangshanModel.displayName = QStringLiteral("大岗山高拱坝各向同性岩体参数");
    dagangshanModel.ib = 0;

    dagangshanModel.columns.clear();
    dagangshanModel.columns
        << QStringLiteral("序号") << QStringLiteral("变形模量E(GPa)") << QStringLiteral("泊松比")
        << QStringLiteral("容重(t/m3)") << QStringLiteral("内摩擦系数") << QStringLiteral("凝聚力C'(MPa)");
    dagangshanModel.columnOutputRows = QVector<int>(dagangshanModel.columns.size(), -1);

    dagangshanModel.materials.append(makeEntry(QStringLiteral("空化"), dagangshanModel.columns,
        {QStringLiteral("1"), QStringLiteral("0.001"), QStringLiteral("0.3"), QStringLiteral("0.0"), QStringLiteral("1.7"), QStringLiteral("200.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("alQ"), dagangshanModel.columns,
        {QStringLiteral("2"), QStringLiteral("0.5"), QStringLiteral("0.4"), QStringLiteral("1.5"), QStringLiteral("0.8"), QStringLiteral("0.05")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("坝砼"), dagangshanModel.columns,
        {QStringLiteral("3"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("置换混凝土"), dagangshanModel.columns,
        {QStringLiteral("4"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("垫座"), dagangshanModel.columns,
        {QStringLiteral("5"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β5（F1）"), dagangshanModel.columns,
        {QStringLiteral("6"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("1.0"), QStringLiteral("1.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β6"), dagangshanModel.columns,
        {QStringLiteral("7"), QStringLiteral("21.5"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.3"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β9"), dagangshanModel.columns,
        {QStringLiteral("8"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("1.0"), QStringLiteral("1.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β11"), dagangshanModel.columns,
        {QStringLiteral("9"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("1.0"), QStringLiteral("1.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("γL5"), dagangshanModel.columns,
        {QStringLiteral("11"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("1.0"), QStringLiteral("1.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("左侧坝身孔口与坝体连接单元"), dagangshanModel.columns,
        {QStringLiteral("17"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("右侧坝身孔口与坝体连接单元"), dagangshanModel.columns,
        {QStringLiteral("18"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("拱坝/基础退化砼"), dagangshanModel.columns,
        {QStringLiteral("19"), QStringLiteral("24.0"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("Ⅱ"), dagangshanModel.columns,
        {QStringLiteral("20"), QStringLiteral("21.5"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.3"), QStringLiteral("2.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("Ⅲ1"), dagangshanModel.columns,
        {QStringLiteral("21"), QStringLiteral("10"), QStringLiteral("0.27"), QStringLiteral("2.62"), QStringLiteral("1.2"), QStringLiteral("1.5")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("Ⅲ2"), dagangshanModel.columns,
        {QStringLiteral("22"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("1.0"), QStringLiteral("1.0")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("Ⅳ"), dagangshanModel.columns,
        {QStringLiteral("23"), QStringLiteral("3.0"), QStringLiteral("0.35"), QStringLiteral("2.58"), QStringLiteral("0.8"), QStringLiteral("0.7")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("Ⅴ"), dagangshanModel.columns,
        {QStringLiteral("24"), QStringLiteral("0.38"), QStringLiteral("0.35"), QStringLiteral("2.45"), QStringLiteral("0.5"), QStringLiteral("0.2")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β21"), dagangshanModel.columns,
        {QStringLiteral("25"), QStringLiteral("3.0"), QStringLiteral("0.35"), QStringLiteral("2.58"), QStringLiteral("0.5"), QStringLiteral("0.08")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β28"), dagangshanModel.columns,
        {QStringLiteral("26"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("0.4"), QStringLiteral("0.03")}));
    dagangshanModel.materials.append(makeEntry(QStringLiteral("β4(f5)"), dagangshanModel.columns,
        {QStringLiteral("27"), QStringLiteral("7.5"), QStringLiteral("0.3"), QStringLiteral("2.62"), QStringLiteral("0.35"), QStringLiteral("0.05")}));

    models.append(dagangshanModel);

    // =========================
    // 样表8（IB=0）
    // =========================
    MaterialModel isotropicFinal;
    isotropicFinal.key = QStringLiteral("huangjinping_isotropic_final");
    isotropicFinal.displayName = QStringLiteral("黄金坪地下厂房各向同性岩体（最终）");
    isotropicFinal.ib = 0;

    isotropicFinal.columns.clear();
    isotropicFinal.columns
        << QStringLiteral("序号") << QStringLiteral("母岩编号") << QStringLiteral("岩类分区") << QStringLiteral("半开挖区荷载释放系数")
        << QStringLiteral("再生开挖级") << QStringLiteral("变形模量E(GPa)") << QStringLiteral("泊松比") << QStringLiteral("容重(t/m3)")
        << QStringLiteral("内摩擦系数") << QStringLiteral("凝聚力C'(MPa)");

    isotropicFinal.columnOutputRows = QVector<int>(isotropicFinal.columns.size(), -1);

    isotropicFinal.materials.append(makeEntry(QStringLiteral("原岩区：ITAG_EX=0"), isotropicFinal.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("II"), isotropicFinal.columns,
        {QStringLiteral("3"), QStringLiteral("3"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("16.25"), QStringLiteral("0.23"), QStringLiteral("2.7"), QStringLiteral("1.23"), QStringLiteral("1.58")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("III"), isotropicFinal.columns,
        {QStringLiteral("4"), QStringLiteral("4"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("IV"), isotropicFinal.columns,
        {QStringLiteral("5"), QStringLiteral("5"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("V"), isotropicFinal.columns,
        {QStringLiteral("6"), QStringLiteral("6"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("f10-14"), isotropicFinal.columns,
        {QStringLiteral("7"), QStringLiteral("7"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙"), isotropicFinal.columns,
        {QStringLiteral("8"), QStringLiteral("8"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("3.0"), QStringLiteral("0.38"), QStringLiteral("1.5"), QStringLiteral("0.50"), QStringLiteral("0.30")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙密集带（IV类范围内）"), isotropicFinal.columns,
        {QStringLiteral("10"), QStringLiteral("10"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("开挖区：ITAG_EX=-1"), isotropicFinal.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("II(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("13"), QStringLiteral("3"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("16.25"), QStringLiteral("0.23"), QStringLiteral("2.7"), QStringLiteral("1.23"), QStringLiteral("1.58")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("III(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("14"), QStringLiteral("4"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("IV(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("15"), QStringLiteral("5"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("V(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("16"), QStringLiteral("6"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("2.00"), QStringLiteral("0.35"), QStringLiteral("2.60"), QStringLiteral("0.613"), QStringLiteral("0.35")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("f10-14(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("17"), QStringLiteral("7"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("18"), QStringLiteral("8"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("3.0"), QStringLiteral("0.38"), QStringLiteral("1.5"), QStringLiteral("0.50"), QStringLiteral("0.30")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙密集带(开挖区)"), isotropicFinal.columns,
        {QStringLiteral("19"), QStringLiteral("9"), QStringLiteral("-1"), QStringLiteral("1.0"), QStringLiteral("0"), QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("尾水连接管（III）"), isotropicFinal.columns,
        {QStringLiteral("23"), QStringLiteral("4"), QStringLiteral("-1"), QStringLiteral("0.3213"), QStringLiteral("0"), QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("母线洞（III）"), isotropicFinal.columns,
        {QStringLiteral("24"), QStringLiteral("4"), QStringLiteral("-1"), QStringLiteral("0.3213"), QStringLiteral("0"), QStringLiteral("9.00"), QStringLiteral("0.25"), QStringLiteral("2.65"), QStringLiteral("1.05"), QStringLiteral("1.13")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("f10-14 过尾水连接管"), isotropicFinal.columns,
        {QStringLiteral("27"), QStringLiteral("7"), QStringLiteral("-1"), QStringLiteral("0.3213"), QStringLiteral("0"), QStringLiteral("0.2"), QStringLiteral("0.35"), QStringLiteral("2.23"), QStringLiteral("0.35"), QStringLiteral("0.05")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙过母线洞"), isotropicFinal.columns,
        {QStringLiteral("28"), QStringLiteral("8"), QStringLiteral("-1"), QStringLiteral("0.3213"), QStringLiteral("0"), QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙过尾水连接管"), isotropicFinal.columns,
        {QStringLiteral("29"), QStringLiteral("9"), QStringLiteral("-1"), QStringLiteral("0.3213"), QStringLiteral("0"), QStringLiteral("5.75"), QStringLiteral("0.30"), QStringLiteral("2.65"), QStringLiteral("0.85"), QStringLiteral("0.78")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("再生区:ITAG_EX=1"), isotropicFinal.columns,
        {QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0"), QStringLiteral("0.0")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("吊车梁岩体（母岩为III）"), isotropicFinal.columns,
        {QStringLiteral("43"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("-4"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("喷C25砼"), isotropicFinal.columns,
        {QStringLiteral("44"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("母线洞喷C25砼"), isotropicFinal.columns,
        {QStringLiteral("45"), QStringLiteral("4"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙过吊车梁"), isotropicFinal.columns,
        {QStringLiteral("46"), QStringLiteral("8"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("-4"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("f10-14过喷C25砼"), isotropicFinal.columns,
        {QStringLiteral("47"), QStringLiteral("7"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));
    isotropicFinal.materials.append(makeEntry(QStringLiteral("裂隙过喷C25砼"), isotropicFinal.columns,
        {QStringLiteral("48"), QStringLiteral("8"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("22.4"), QStringLiteral("0.167"), QStringLiteral("2.4"), QStringLiteral("1.4"), QStringLiteral("3.9")}));

    models.append(isotropicFinal);

    // =========================
    // 样表9（IB=-5）
    // =========================
    MaterialModel nanshuiModel;
    nanshuiModel.key = QStringLiteral("soil_nanshui");
    nanshuiModel.displayName = QStringLiteral("土体南水模型");
    nanshuiModel.ib = -5;

    nanshuiModel.columns.clear();
    nanshuiModel.columns
        << QStringLiteral("材料编号") << QStringLiteral("干容重") << QStringLiteral("天然容重") << QStringLiteral("饱和容重")
        << QStringLiteral("φ0") << QStringLiteral("Δφ") << QStringLiteral("c(KPa)") << QStringLiteral("Rf") << QStringLiteral("K")
        << QStringLiteral("Kur") << QStringLiteral("n") << QStringLiteral("cd（行：77）") << QStringLiteral("nd（行：78）") << QStringLiteral("Rd（行：79）");

    nanshuiModel.columnOutputRows = QVector<int>(nanshuiModel.columns.size(), -1);
    nanshuiModel.columnOutputRows[11] = 77; // cd
    nanshuiModel.columnOutputRows[12] = 78; // nd
    nanshuiModel.columnOutputRows[13] = 79; // Rd

    nanshuiModel.materials.append(makeEntry(QStringLiteral("垫层料"), nanshuiModel.columns,
        {QStringLiteral("7"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("46.0"), QStringLiteral("6.0"), QStringLiteral("0.0"), QStringLiteral("0.79"), QStringLiteral("867"), QStringLiteral("1133"), QStringLiteral("0.39"), QStringLiteral("0.0043"), QStringLiteral("0.62"), QStringLiteral("0.72")}));
    nanshuiModel.materials.append(makeEntry(QStringLiteral("过渡料"), nanshuiModel.columns,
        {QStringLiteral("8"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("50.0"), QStringLiteral("8.0"), QStringLiteral("0.0"), QStringLiteral("0.75"), QStringLiteral("683"), QStringLiteral("1000"), QStringLiteral("0.31"), QStringLiteral("0.0060"), QStringLiteral("0.78"), QStringLiteral("0.70")}));
    nanshuiModel.materials.append(makeEntry(QStringLiteral("堆石料"), nanshuiModel.columns,
        {QStringLiteral("9"), QStringLiteral("2.00"), QStringLiteral("2.10"), QStringLiteral("2.20"), QStringLiteral("48.0"), QStringLiteral("9.0"), QStringLiteral("0.0"), QStringLiteral("0.77"), QStringLiteral("500"), QStringLiteral("750"), QStringLiteral("0.20"), QStringLiteral("0.0100"), QStringLiteral("0.75"), QStringLiteral("0.75")}));
    nanshuiModel.materials.append(makeEntry(QStringLiteral("弱风化料"), nanshuiModel.columns,
        {QStringLiteral("10"), QStringLiteral("1.90"), QStringLiteral("2.05"), QStringLiteral("2.20"), QStringLiteral("48.0"), QStringLiteral("12.0"), QStringLiteral("0.0"), QStringLiteral("0.80"), QStringLiteral("320"), QStringLiteral("480"), QStringLiteral("0.20"), QStringLiteral("0.0150"), QStringLiteral("0.56"), QStringLiteral("0.80")}));
    nanshuiModel.materials.append(makeEntry(QStringLiteral("强风化料"), nanshuiModel.columns,
        {QStringLiteral("11"), QStringLiteral("1.80"), QStringLiteral("2.00"), QStringLiteral("2.20"), QStringLiteral("43.0"), QStringLiteral("11.0"), QStringLiteral("0.0"), QStringLiteral("0.63"), QStringLiteral("120"), QStringLiteral("180"), QStringLiteral("0.37"), QStringLiteral("0.0200"), QStringLiteral("0.25"), QStringLiteral("0.63")}));
    nanshuiModel.materials.append(makeEntry(QStringLiteral("超径石"), nanshuiModel.columns,
        {QStringLiteral("12"), QStringLiteral("2.00"), QStringLiteral("2.10"), QStringLiteral("2.20"), QStringLiteral("48.0"), QStringLiteral("9.0"), QStringLiteral("0.0"), QStringLiteral("0.77"), QStringLiteral("500"), QStringLiteral("750"), QStringLiteral("0.20"), QStringLiteral("0.0100"), QStringLiteral("0.75"), QStringLiteral("0.75")}));

    models.append(nanshuiModel);

    // =========================
    // 样表10（IB=-2）
    // =========================
    MaterialModel kgModel;
    kgModel.key = QStringLiteral("soil_kg");
    kgModel.displayName = QStringLiteral("土体科大K-G模型参数");
    kgModel.ib = -2;

    kgModel.columns.clear();
    kgModel.columns
        << QStringLiteral("材料编号")
        << QStringLiteral("干容重(t/m3)")
        << QStringLiteral("天然容重(t/m3)")
        << QStringLiteral("饱和容重(t/m3)")
        << QStringLiteral("Ki(105Pa)（行：69）")
        << QStringLiteral("Kie(105Pa)（行：76）")
        << QStringLiteral("αk（行：71）")
        << QStringLiteral("αke（行：78）")
        << QStringLiteral("Gi(105Pa)（行：74）")
        << QStringLiteral("Gie(105Pa)（行：77）")
        << QStringLiteral("αG（行：70）")
        << QStringLiteral("αGe（行：79）")
        << QStringLiteral("βG（行：75）")
        << QStringLiteral("a(105Pa)（行：72）")
        << QStringLiteral("b（行：73）");

    kgModel.columnOutputRows = QVector<int>(kgModel.columns.size(), -1);
    kgModel.columnOutputRows[4]  = 69; // Ki
    kgModel.columnOutputRows[5]  = 76; // Kie
    kgModel.columnOutputRows[6]  = 71; // αk
    kgModel.columnOutputRows[7]  = 78; // αke
    kgModel.columnOutputRows[8]  = 74; // Gi
    kgModel.columnOutputRows[9]  = 77; // Gie
    kgModel.columnOutputRows[10] = 70; // αG
    kgModel.columnOutputRows[11] = 79; // αGe
    kgModel.columnOutputRows[12] = 75; // βG
    kgModel.columnOutputRows[13] = 72; // a
    kgModel.columnOutputRows[14] = 73; // b

    kgModel.materials.append(makeEntry(QStringLiteral("覆盖层3"), kgModel.columns,
        {QStringLiteral("4"),  QStringLiteral("1.8"),  QStringLiteral("2.0"),  QStringLiteral("2.2"),
         QStringLiteral("168"), QStringLiteral("68"),
         QStringLiteral("114"), QStringLiteral("115"),
         QStringLiteral("-60"), QStringLiteral("-60"),
         QStringLiteral("114"), QStringLiteral("115"),
         QStringLiteral("190"), QStringLiteral("190"),  QStringLiteral("1.92")}));

    kgModel.materials.append(makeEntry(QStringLiteral("覆盖层2"), kgModel.columns,
        {QStringLiteral("5"),  QStringLiteral("2.0"),  QStringLiteral("2.1"),  QStringLiteral("2.2"),
         QStringLiteral("33"),  QStringLiteral("70"),
         QStringLiteral("22"),  QStringLiteral("64"),
         QStringLiteral("-35"), QStringLiteral("-35"),
         QStringLiteral("22"),  QStringLiteral("64"),
         QStringLiteral("63"),  QStringLiteral("63"),   QStringLiteral("1.83")}));

    kgModel.materials.append(makeEntry(QStringLiteral("心墙掺砾料"), kgModel.columns,
        {QStringLiteral("6"),  QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"),
         QStringLiteral("80"),  QStringLiteral("94"),
         QStringLiteral("106"), QStringLiteral("112"),
         QStringLiteral("-80"), QStringLiteral("-80"),
         QStringLiteral("106"), QStringLiteral("112"),
         QStringLiteral("132"), QStringLiteral("132"),  QStringLiteral("1.40")}));

    kgModel.materials.append(makeEntry(QStringLiteral("反滤层1"), kgModel.columns,
        {QStringLiteral("7"),  QStringLiteral("2.10"), QStringLiteral("2.35"), QStringLiteral("2.40"),
         QStringLiteral("168"), QStringLiteral("110"),
         QStringLiteral("114"), QStringLiteral("194"),
         QStringLiteral("-97"), QStringLiteral("-97"),
         QStringLiteral("114"), QStringLiteral("194"),
         QStringLiteral("118"), QStringLiteral("118"),  QStringLiteral("2.00")}));

    kgModel.materials.append(makeEntry(QStringLiteral("反滤层2"), kgModel.columns,
        {QStringLiteral("9"),  QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"),
         QStringLiteral("170"), QStringLiteral("110"),
         QStringLiteral("104"), QStringLiteral("187"),
         QStringLiteral("-90"), QStringLiteral("-90"),
         QStringLiteral("104"), QStringLiteral("187"),
         QStringLiteral("116"), QStringLiteral("116"),  QStringLiteral("2.08")}));

    kgModel.materials.append(makeEntry(QStringLiteral("过渡层"), kgModel.columns,
        {QStringLiteral("10"), QStringLiteral("2.0"),  QStringLiteral("2.1"),  QStringLiteral("2.2"),
         QStringLiteral("230"), QStringLiteral("75"),
         QStringLiteral("100"), QStringLiteral("133"),
         QStringLiteral("-70"), QStringLiteral("-70"),
         QStringLiteral("100"), QStringLiteral("133"),
         QStringLiteral("140"), QStringLiteral("140"),  QStringLiteral("1.90")}));

    kgModel.materials.append(makeEntry(QStringLiteral("主堆石"), kgModel.columns,
        {QStringLiteral("15"), QStringLiteral("2.0"),  QStringLiteral("2.1"),  QStringLiteral("2.2"),
         QStringLiteral("300"), QStringLiteral("41"),
         QStringLiteral("120"), QStringLiteral("108"),
         QStringLiteral("-60"), QStringLiteral("-60"),
         QStringLiteral("120"), QStringLiteral("108"),
         QStringLiteral("200"), QStringLiteral("200"),  QStringLiteral("1.80")}));

    models.append(kgModel);

    // =========================
    // 样表11（IB=0）三参数流变
    // =========================
    MaterialModel shenPearlModel;
    shenPearlModel.key = QStringLiteral("shen_zhujiang_rheology");
    shenPearlModel.displayName = QStringLiteral("沈珠江三参数流变模型");
    shenPearlModel.ib = 0;

    shenPearlModel.columns.clear();
    shenPearlModel.columns
        << QStringLiteral("序号") << QStringLiteral("应变速率系数α（行：14）") << QStringLiteral("体积流变系数b（行：15）") << QStringLiteral("剪切流变系数d（行：16）");

    shenPearlModel.columnOutputRows = QVector<int>(shenPearlModel.columns.size(), -1);
    shenPearlModel.columnOutputRows[1] = 14;
    shenPearlModel.columnOutputRows[2] = 15;
    shenPearlModel.columnOutputRows[3] = 16;

    shenPearlModel.materials.append(makeEntry(QStringLiteral("垫层料"), shenPearlModel.columns,
        {QStringLiteral("7"), QStringLiteral("0.004"), QStringLiteral("0.0009"), QStringLiteral("0.007")}));
    shenPearlModel.materials.append(makeEntry(QStringLiteral("过渡料"), shenPearlModel.columns,
        {QStringLiteral("8"), QStringLiteral("0.004"), QStringLiteral("0.0009"), QStringLiteral("0.007")}));
    shenPearlModel.materials.append(makeEntry(QStringLiteral("堆石料"), shenPearlModel.columns,
        {QStringLiteral("9"), QStringLiteral("0.004"), QStringLiteral("0.0009"), QStringLiteral("0.007")}));
    shenPearlModel.materials.append(makeEntry(QStringLiteral("弱风化料"), shenPearlModel.columns,
        {QStringLiteral("10"), QStringLiteral("0.005"), QStringLiteral("0.0012"), QStringLiteral("0.008")}));
    shenPearlModel.materials.append(makeEntry(QStringLiteral("强风化料"), shenPearlModel.columns,
        {QStringLiteral("11"), QStringLiteral("0.006"), QStringLiteral("0.0015"), QStringLiteral("0.009")}));
    shenPearlModel.materials.append(makeEntry(QStringLiteral("超径石"), shenPearlModel.columns,
        {QStringLiteral("12"), QStringLiteral("0.004"), QStringLiteral("0.0009"), QStringLiteral("0.007")}));

    models.append(shenPearlModel);

    // =========================
    // 样表12（IB=0）李国英七参数流变
    // =========================
    MaterialModel liguoyingModel;
    liguoyingModel.key = QStringLiteral("liguoying_rheology");
    liguoyingModel.displayName = QStringLiteral("李国英七参数流变模型");
    liguoyingModel.ib = 0;

    liguoyingModel.columns.clear();
    liguoyingModel.columns
        << QStringLiteral("序号") << QStringLiteral("应变速率系数α（行：14）") << QStringLiteral("体积流变系数b（行：15）") << QStringLiteral("体积流变系数c（行：16）")
        << QStringLiteral("剪切流变系数d（行：17）") << QStringLiteral("体积流变指数m1（行：18）") << QStringLiteral("体积流变指数m2（行：19）") << QStringLiteral("剪切流变指数m3（行：20）");

    liguoyingModel.columnOutputRows = QVector<int>(liguoyingModel.columns.size(), -1);
    liguoyingModel.columnOutputRows[1] = 14;
    liguoyingModel.columnOutputRows[2] = 15;
    liguoyingModel.columnOutputRows[3] = 16;
    liguoyingModel.columnOutputRows[4] = 17;
    liguoyingModel.columnOutputRows[5] = 18;
    liguoyingModel.columnOutputRows[6] = 19;
    liguoyingModel.columnOutputRows[7] = 20;

    liguoyingModel.materials.append(makeEntry(QStringLiteral("主堆石3B-I"), liguoyingModel.columns,
        {QStringLiteral("7"), QStringLiteral("0.006"), QStringLiteral("0.0006"), QStringLiteral("0.0002"), QStringLiteral("0.004"), QStringLiteral("0.769"), QStringLiteral("0.635"), QStringLiteral("0.550")}));
    liguoyingModel.materials.append(makeEntry(QStringLiteral("主堆石3B-II"), liguoyingModel.columns,
        {QStringLiteral("8"), QStringLiteral("0.007"), QStringLiteral("0.003"), QStringLiteral("0.0001"), QStringLiteral("0.0025"), QStringLiteral("0.770"), QStringLiteral("0.631"), QStringLiteral("0.530")}));
    liguoyingModel.materials.append(makeEntry(QStringLiteral("次堆石3C"), liguoyingModel.columns,
        {QStringLiteral("9"), QStringLiteral("0.005"), QStringLiteral("0.0007"), QStringLiteral("0.0004"), QStringLiteral("0.0043"), QStringLiteral("0.802"), QStringLiteral("0.664"), QStringLiteral("0.717")}));

    models.append(liguoyingModel);

    // =========================
    // 样表13（IB=0）九参数流变
    // =========================
    MaterialModel nineParamModel;
    nineParamModel.key = QStringLiteral("nine_param_rheology");
    nineParamModel.displayName = QStringLiteral("九参数流变模型");
    nineParamModel.ib = 0;

    nineParamModel.columns.clear();
    nineParamModel.columns
        << QStringLiteral("序号") << QStringLiteral("应变速率系数λv（行：14）") << QStringLiteral("体积流变系数Cα（行：15）") << QStringLiteral("体积流变系数Cβ（行：16）")
        << QStringLiteral("体积流变指数dα（行：17）") << QStringLiteral("体积流变指数dβ（行：18）") << QStringLiteral("主应变系数c（行：19）") << QStringLiteral("主应变系数d（行：20）")
        << QStringLiteral("主应变流变系数η（行：21）") << QStringLiteral("主应变流变系数m（行：22）");

    nineParamModel.columnOutputRows = QVector<int>(nineParamModel.columns.size(), -1);
    nineParamModel.columnOutputRows[1] = 14;
    nineParamModel.columnOutputRows[2] = 15;
    nineParamModel.columnOutputRows[3] = 16;
    nineParamModel.columnOutputRows[4] = 17;
    nineParamModel.columnOutputRows[5] = 18;
    nineParamModel.columnOutputRows[6] = 19;
    nineParamModel.columnOutputRows[7] = 20;
    nineParamModel.columnOutputRows[8] = 21;
    nineParamModel.columnOutputRows[9] = 22;

    nineParamModel.materials.append(makeEntry(QStringLiteral("主堆石3B-II"), nineParamModel.columns,
        {QStringLiteral("10"), QStringLiteral("0.0678"), QStringLiteral("0.4445"), QStringLiteral("0.4360"), QStringLiteral("2.0827"), QStringLiteral("1.6383"), QStringLiteral("0.2892"), QStringLiteral("0.8465"), QStringLiteral("0.0831"), QStringLiteral("0.3899")}));
    nineParamModel.materials.append(makeEntry(QStringLiteral("次堆石3C"), nineParamModel.columns,
        {QStringLiteral("11"), QStringLiteral("0.0851"), QStringLiteral("0.3808"), QStringLiteral("0.1936"), QStringLiteral("0.6521"), QStringLiteral("1.0061"), QStringLiteral("1.4296"), QStringLiteral("0.2852"), QStringLiteral("0.1116"), QStringLiteral("1.0384")}));

    models.append(nineParamModel);

    // =========================
    // 样表14（IB=0）西源流变
    // =========================
    MaterialModel xiyuanModel;
    xiyuanModel.key = QStringLiteral("xiyuan_rheology");
    xiyuanModel.displayName = QStringLiteral("西源流变模型");
    xiyuanModel.ib = 0;

    xiyuanModel.columns.clear();
    xiyuanModel.columns
        << QStringLiteral("序号") << QStringLiteral("瞬时弹性模量E1(GPa)") << QStringLiteral("粘塑性模量E2(GPa)")
        << QStringLiteral("泊松比μ1") << QStringLiteral("泊松比μ2") << QStringLiteral("粘弹性系数η1（行：103）") << QStringLiteral("粘塑性系数η1（行：104）");

    xiyuanModel.columnOutputRows = QVector<int>(xiyuanModel.columns.size(), -1);
    xiyuanModel.columnOutputRows[5] = 103;
    xiyuanModel.columnOutputRows[6] = 104;

    xiyuanModel.materials.append(makeEntry(QStringLiteral("主堆石3B-II"), xiyuanModel.columns,
        {QStringLiteral("12"), QStringLiteral("1.5"), QStringLiteral("2.0"), QStringLiteral("0.3"), QStringLiteral("0.4"), QStringLiteral("0.5"), QStringLiteral("0.6")}));
    xiyuanModel.materials.append(makeEntry(QStringLiteral("次堆石3C"), xiyuanModel.columns,
        {QStringLiteral("13"), QStringLiteral("1.5"), QStringLiteral("2.0"), QStringLiteral("0.3"), QStringLiteral("0.4"), QStringLiteral("0.5"), QStringLiteral("0.6")}));

    models.append(xiyuanModel);

    // =========================
    // 样表15（IB=-6）
    // =========================
    MaterialModel emuBrokenModel;
    emuBrokenModel.key = QStringLiteral("soil_emu_crush");
    emuBrokenModel.displayName = QStringLiteral("土体E-μ破碎模型");
    emuBrokenModel.ib = -6;

    emuBrokenModel.columns.clear();
    emuBrokenModel.columns
        << QStringLiteral("材料编号") << QStringLiteral("干容重") << QStringLiteral("天然容重") << QStringLiteral("饱和容重") << QStringLiteral("φ0") << QStringLiteral("Δφ") << QStringLiteral("c(KPa)")
        << QStringLiteral("Rf") << QStringLiteral("K") << QStringLiteral("n") << QStringLiteral("C1-1") << QStringLiteral("n1-1") << QStringLiteral("C1-2") << QStringLiteral("n1-2")
        << QStringLiteral("C1-3") << QStringLiteral("n1-3");

    emuBrokenModel.columnOutputRows = QVector<int>(emuBrokenModel.columns.size(), -1);

    emuBrokenModel.materials.append(makeEntry(QStringLiteral("上游铺盖（回填石渣料）"), emuBrokenModel.columns,
        {QStringLiteral("4"), QStringLiteral("1.8"), QStringLiteral("2.0"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.75"), QStringLiteral("500"), QStringLiteral("0.4"), QStringLiteral("0.38"), QStringLiteral("2.7"), QStringLiteral("0.38"), QStringLiteral("2.7"), QStringLiteral("0.38"), QStringLiteral("2.7")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("上游堆石料"), emuBrokenModel.columns,
        {QStringLiteral("5"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("上游过渡层"), emuBrokenModel.columns,
        {QStringLiteral("6"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("33"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.78"), QStringLiteral("900"), QStringLiteral("0.3"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.42"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("沥青砼心墙"), emuBrokenModel.columns,
        {QStringLiteral("7"), QStringLiteral("2.10"), QStringLiteral("2.35"), QStringLiteral("2.40"), QStringLiteral("24"), QStringLiteral("0"), QStringLiteral("120"), QStringLiteral("0.82"), QStringLiteral("600"), QStringLiteral("0.4"), QStringLiteral("0.42"), QStringLiteral("7"), QStringLiteral("0.42"), QStringLiteral("7"), QStringLiteral("0.42"), QStringLiteral("7")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("下游过渡层"), emuBrokenModel.columns,
        {QStringLiteral("9"), QStringLiteral("2.10"), QStringLiteral("2.15"), QStringLiteral("2.20"), QStringLiteral("33"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0.78"), QStringLiteral("900"), QStringLiteral("0.3"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.42"), QStringLiteral("11"), QStringLiteral("0.42"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("下游堆石料"), emuBrokenModel.columns,
        {QStringLiteral("10"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("上游1级找平堆石"), emuBrokenModel.columns,
        {QStringLiteral("15"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("下游1级找平堆石"), emuBrokenModel.columns,
        {QStringLiteral("16"), QStringLiteral("2.0"), QStringLiteral("2.1"), QStringLiteral("2.2"), QStringLiteral("35"), QStringLiteral("0"), QStringLiteral("0.0"), QStringLiteral("0.8"), QStringLiteral("1000"), QStringLiteral("0.3"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11"), QStringLiteral("0.37"), QStringLiteral("11")}));
    emuBrokenModel.materials.append(makeEntry(QStringLiteral("K5沉积粘土"), emuBrokenModel.columns,
        {QStringLiteral("42"), QStringLiteral("1.85"), QStringLiteral("1.90"), QStringLiteral("2.00"), QStringLiteral("16.4"), QStringLiteral("0.0"), QStringLiteral("21.8"), QStringLiteral("0.87"), QStringLiteral("270"), QStringLiteral("0.47"), QStringLiteral("0.46"), QStringLiteral("0.05"), QStringLiteral("0.46"), QStringLiteral("0.05"), QStringLiteral("0.46"), QStringLiteral("0.05")}));

    models.append(emuBrokenModel);

    return models;
}
