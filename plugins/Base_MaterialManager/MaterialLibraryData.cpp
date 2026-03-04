#include "MaterialLibraryData.h"

#include <QUuid>
#include <utility>

namespace {
static MaterialEntry makeEntry(const QString &name, const QStringList &columns,
                               const QStringList &values) {
  MaterialEntry entry;
  entry.id = QUuid::createUuid().toString();
  entry.name = name;
  const int count = qMin(columns.size(), values.size());
  for (int i = 0; i < count; ++i) {
    entry.properties.insert(columns.at(i), values.at(i));
  }
  return entry;
}
}  // namespace

// ======================================================
// Default material library (HARD-CODED, no parsing)
// ======================================================
QVector<MaterialModel> createDefaultMaterialModels() {
  QVector<MaterialModel> models;

  // =========================
  // 样表1（IB=0）
  // =========================
  MaterialModel isotropicRock;
  isotropicRock.key = QString("huangjinping_isotropic_initial");
  isotropicRock.displayName = QString("各向同性岩体（黄金坪地下厂房）");
  isotropicRock.ib = 0;

  isotropicRock.columns.clear();
  isotropicRock.columns << QString("序号") << QString("母岩编号")
                        << QString("岩类分区")
                        << QString("半开挖区荷载释放系数")
                        << QString("再生开挖级") << QString("变形模量E(GPa)")
                        << QString("泊松比") << QString("容重(t/m3)")
                        << QString("内摩擦系数") << QString("凝聚力C'(MPa)")
                        << QString("洞室边缘松弛系数α0")
                        << QString("松弛距离d0");

  isotropicRock.columnOutputRows =
      QVector<int>(isotropicRock.columns.size(), -1);

  isotropicRock.materials.append(makeEntry(
      QString("原岩区：ITAG_EX=0"), isotropicRock.columns,
      {QString("0"), QString("0"), QString("0"), QString("0"), QString("0"),
       QString("0.0"), QString("0.0"), QString("0.0"), QString("0.0"),
       QString("0.0"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("II"), isotropicRock.columns,
      {QString("3"), QString("3"), QString("0"), QString("0"), QString("0"),
       QString("16.25"), QString("0.23"), QString("2.7"), QString("1.23"),
       QString("1.58"), QString("1.0"), QString("3.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("III"), isotropicRock.columns,
      {QString("4"), QString("4"), QString("0"), QString("0"), QString("0"),
       QString("9.00"), QString("0.25"), QString("2.65"), QString("1.05"),
       QString("1.13"), QString("1.0"), QString("5.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("IV"), isotropicRock.columns,
      {QString("5"), QString("5"), QString("0"), QString("0"), QString("0"),
       QString("5.75"), QString("0.30"), QString("2.65"), QString("0.85"),
       QString("0.78"), QString("1.0"), QString("7.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("V"), isotropicRock.columns,
      {QString("6"), QString("6"), QString("0"), QString("0"), QString("0"),
       QString("2.00"), QString("0.35"), QString("2.60"), QString("0.613"),
       QString("0.35"), QString("1.0"), QString("9.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("f10-14"), isotropicRock.columns,
      {QString("7"), QString("7"), QString("0"), QString("0"), QString("0"),
       QString("0.2"), QString("0.35"), QString("2.23"), QString("0.35"),
       QString("0.05"), QString("1.0"), QString("9.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙"), isotropicRock.columns,
      {QString("8"), QString("8"), QString("0"), QString("0"), QString("0"),
       QString("3.0"), QString("0.38"), QString("1.5"), QString("0.50"),
       QString("0.30"), QString("1.0"), QString("9.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙密集带（IV类范围内）"), isotropicRock.columns,
      {QString("10"), QString("10"), QString("0"), QString("0"), QString("0"),
       QString("2.00"), QString("0.35"), QString("2.60"), QString("0.613"),
       QString("0.35"), QString("1.0"), QString("9.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("开挖区：ITAG_EX=-1"), isotropicRock.columns,
      {QString("0"), QString("0"), QString("-1"), QString("0"), QString("0"),
       QString("0.0"), QString("0.0"), QString("0.0"), QString("0.0"),
       QString("0.0"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("II(开挖区)"), isotropicRock.columns,
      {QString("13"), QString("3"), QString("-1"), QString("0"), QString("0"),
       QString("16.25"), QString("0.23"), QString("2.7"), QString("1.23"),
       QString("1.58"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("III(开挖区)"), isotropicRock.columns,
      {QString("14"), QString("4"), QString("-1"), QString("0"), QString("0"),
       QString("9.00"), QString("0.25"), QString("2.65"), QString("1.05"),
       QString("1.13"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("IV(开挖区)"), isotropicRock.columns,
      {QString("15"), QString("5"), QString("-1"), QString("0"), QString("0"),
       QString("5.75"), QString("0.30"), QString("2.65"), QString("0.85"),
       QString("0.78"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("V(开挖区)"), isotropicRock.columns,
      {QString("16"), QString("6"), QString("-1"), QString("0"), QString("0"),
       QString("2.00"), QString("0.35"), QString("2.60"), QString("0.613"),
       QString("0.35"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("f10-14(开挖区)"), isotropicRock.columns,
      {QString("17"), QString("7"), QString("-1"), QString("0"), QString("0"),
       QString("0.2"), QString("0.35"), QString("2.23"), QString("0.35"),
       QString("0.05"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙(开挖区)"), isotropicRock.columns,
      {QString("18"), QString("8"), QString("-1"), QString("0"), QString("0"),
       QString("3.0"), QString("0.38"), QString("1.5"), QString("0.50"),
       QString("0.30"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙密集带(开挖区)"), isotropicRock.columns,
      {QString("19"), QString("9"), QString("-1"), QString("0"), QString("0"),
       QString("5.75"), QString("0.30"), QString("2.65"), QString("0.85"),
       QString("0.78"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("半开挖区：ITAG_EX=-2"), isotropicRock.columns,
      {QString("0"), QString("0"), QString("-2"), QString("0.3213"),
       QString("0"), QString("0.0"), QString("0.0"), QString("0.0"),
       QString("0.0"), QString("0.0"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("尾水连接管（III）"), isotropicRock.columns,
      {QString("23"), QString("4"), QString("-2"), QString("0.3213"),
       QString("0"), QString("9.00"), QString("0.25"), QString("2.65"),
       QString("1.05"), QString("1.13"), QString("1.0"), QString("5.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("母线洞（III）"), isotropicRock.columns,
      {QString("24"), QString("4"), QString("-2"), QString("0.3213"),
       QString("0"), QString("9.00"), QString("0.25"), QString("2.65"),
       QString("1.05"), QString("1.13"), QString("1.0"), QString("5.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("f10-14 过尾水连接管"), isotropicRock.columns,
      {QString("27"), QString("7"), QString("-2"), QString("0.3213"),
       QString("0"), QString("0.2"), QString("0.35"), QString("2.23"),
       QString("0.35"), QString("0.05"), QString("1.0"), QString("9.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙过母线洞"), isotropicRock.columns,
      {QString("28"), QString("8"), QString("-2"), QString("0.3213"),
       QString("0"), QString("5.75"), QString("0.30"), QString("2.65"),
       QString("0.85"), QString("0.78"), QString("1.0"), QString("7.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙过尾水连接管"), isotropicRock.columns,
      {QString("29"), QString("9"), QString("-2"), QString("0.3213"),
       QString("0"), QString("5.75"), QString("0.30"), QString("2.65"),
       QString("0.85"), QString("0.78"), QString("1.0"), QString("7.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("再生区:ITAG_EX=1"), isotropicRock.columns,
      {QString("0"), QString("0"), QString("1"), QString("0"), QString("0"),
       QString("0.0"), QString("0.0"), QString("0.0"), QString("0.0"),
       QString("0.0"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("吊车梁岩体（母岩为III）"), isotropicRock.columns,
      {QString("43"), QString("4"), QString("1"), QString("0"), QString("-4"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("喷C25砼"), isotropicRock.columns,
      {QString("44"), QString("4"), QString("1"), QString("0"), QString("0"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("母线洞喷C25砼"), isotropicRock.columns,
      {QString("45"), QString("4"), QString("1"), QString("0"), QString("0"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙过吊车梁"), isotropicRock.columns,
      {QString("46"), QString("8"), QString("1"), QString("0"), QString("-4"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("f10-14过喷C25砼"), isotropicRock.columns,
      {QString("47"), QString("7"), QString("1"), QString("0"), QString("0"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));
  isotropicRock.materials.append(makeEntry(
      QString("裂隙过喷C25砼"), isotropicRock.columns,
      {QString("48"), QString("8"), QString("1"), QString("0"), QString("0"),
       QString("22.4"), QString("0.167"), QString("2.4"), QString("1.4"),
       QString("3.9"), QString("0.0"), QString("0.0")}));

  models.append(isotropicRock);

  // =========================
  // 样表2（IB=-1）
  // =========================
  MaterialModel ebModel;
  ebModel.key = QString("soil_eb");
  ebModel.displayName = QString("土体E-B模型");
  ebModel.ib = -1;

  ebModel.columns.clear();
  ebModel.columns << QString("材料编号") << QString("干容重")
                  << QString("天然容重") << QString("饱和容重") << QString("φ0")
                  << QString("Δφ（行：76）") << QString("c(KPa)")
                  << QString("Rf（行：71）") << QString("K（行：69）")
                  << QString("n（行：70）") << QString("Kur（行：72）")
                  << QString("Kb（行：74）") << QString("m（行：75）");

  ebModel.columnOutputRows = QVector<int>(ebModel.columns.size(), -1);
  ebModel.columnOutputRows[8] = 69;   // K
  ebModel.columnOutputRows[9] = 70;   // n
  ebModel.columnOutputRows[7] = 71;   // Rf
  ebModel.columnOutputRows[10] = 72;  // Kur
  ebModel.columnOutputRows[11] = 74;  // Kb
  ebModel.columnOutputRows[12] = 75;  // m
  ebModel.columnOutputRows[5] = 76;   // Δφ

  ebModel.materials.append(makeEntry(
      QString("2A垫层"), ebModel.columns,
      {QString("7"), QString("2.209"), QString("2.29"), QString("2.397"),
       QString("51.6"), QString("5.7"), QString("231.4"), QString("0.69"),
       QString("1082"), QString("0.27"), QString("2164"), QString("545"),
       QString("0.25")}));
  ebModel.materials.append(makeEntry(
      QString("3A过渡料"), ebModel.columns,
      {QString("8"), QString("2.13"), QString("2.22"), QString("2.334"),
       QString("53.8"), QString("8.8"), QString("236.6"), QString("0.75"),
       QString("968"), QString("0.23"), QString("1936"), QString("328"),
       QString("0.17")}));
  ebModel.materials.append(makeEntry(
      QString("3B堆石"), ebModel.columns,
      {QString("9"), QString("2.077"), QString("2.14"), QString("2.284"),
       QString("52.9"), QString("9.3"), QString("196.6"), QString("0.76"),
       QString("985"), QString("0.28"), QString("1722"), QString("238"),
       QString("0.21")}));
  ebModel.materials.append(makeEntry(
      QString("1B弃渣盖重"), ebModel.columns,
      {QString("10"), QString("2.08"), QString("2.19"), QString("2.3"),
       QString("38.3"), QString("3.5"), QString("0.0"), QString("0.75"),
       QString("695"), QString("0.29"), QString("1390"), QString("352"),
       QString("0.29")}));
  ebModel.materials.append(makeEntry(
      QString("1A铺盖"), ebModel.columns,
      {QString("12"), QString("1.42"), QString("1.71"), QString("2.3"),
       QString("31.0"), QString("0.0"), QString("0.0"), QString("0.72"),
       QString("350"), QString("0.43"), QString("700"), QString("350"),
       QString("0.18")}));
  ebModel.materials.append(makeEntry(
      QString("2B特殊垫层"), ebModel.columns,
      {QString("13"), QString("2.218"), QString("2.30"), QString("2.396"),
       QString("42.6"), QString("4.5"), QString("85"), QString("0.71"),
       QString("832"), QString("0.35"), QString("1664"), QString("470"),
       QString("0.39")}));
  ebModel.materials.append(
      makeEntry(QString("3C反滤料"), ebModel.columns,
                {QString("16"), QString("2.12"), QString("2.17"),
                 QString("2.20"), QString("48"), QString("8.0"), QString("0.0"),
                 QString("0.68"), QString("1035"), QString("0.33"),
                 QString("2070"), QString("526"), QString("0.24")}));
  ebModel.materials.append(makeEntry(
      QString("P下游砌石"), ebModel.columns,
      {QString("17"), QString("2.01"), QString("2.07"), QString("2.17"),
       QString("51.8"), QString("9.8"), QString("0.0"), QString("0.78"),
       QString("850"), QString("0.53"), QString("1700"), QString("325"),
       QString("0.33")}));
  ebModel.materials.append(
      makeEntry(QString("①gl+fglQ3"), ebModel.columns,
                {QString("21"), QString("2.09"), QString("2.119"),
                 QString("2.3"), QString("42.6"), QString("4.5"), QString("85"),
                 QString("0.7"), QString("752"), QString("0.29"),
                 QString("1504"), QString("276"), QString("0.29")}));
  ebModel.materials.append(
      makeEntry(QString("④ColQ4"), ebModel.columns,
                {QString("22"), QString("2.1"), QString("2.19"), QString("2.3"),
                 QString("38.3"), QString("3.5"), QString("0.0"),
                 QString("0.75"), QString("695"), QString("0.29"),
                 QString("1390"), QString("352"), QString("0.29")}));
  ebModel.materials.append(
      makeEntry(QString("③Col+dlQ4"), ebModel.columns,
                {QString("23"), QString("1.42"), QString("1.71"),
                 QString("1.9"), QString("31.0"), QString("0"), QString("0.0"),
                 QString("0.72"), QString("350"), QString("0.43"),
                 QString("700"), QString("350"), QString("0.18")}));
  ebModel.materials.append(
      makeEntry(QString("④alQ4"), ebModel.columns,
                {QString("24"), QString("2.09"), QString("2.119"),
                 QString("2.3"), QString("42.6"), QString("4.5"), QString("85"),
                 QString("0.7"), QString("752"), QString("0.29"),
                 QString("1504"), QString("276"), QString("0.29")}));
  ebModel.materials.append(makeEntry(
      QString("泥皮"), ebModel.columns,
      {QString("26"), QString("1.67"), QString("1.67"), QString("1.67"),
       QString("10.0"), QString("0.0"), QString("50"), QString("0.86"),
       QString("1400"), QString("0.66"), QString("2000"), QString("600"),
       QString("0.26")}));
  ebModel.materials.append(makeEntry(
      QString("el+dlQ4"), ebModel.columns,
      {QString("28"), QString("2.12"), QString("2.22"), QString("2.3"),
       QString("40.2"), QString("4.0"), QString("0.0"), QString("0.73"),
       QString("760"), QString("0.27"), QString("1520"), QString("405"),
       QString("0.23")}));

  models.append(ebModel);

  // =========================
  // 样表3（IB=-4）
  // =========================
  MaterialModel dynamicModel;
  dynamicModel.key = QString("dam_dynamic");
  dynamicModel.displayName = QString("坝料动力参数");
  dynamicModel.ib = -4;

  dynamicModel.columns.clear();
  dynamicModel.columns << QString("编号（行：88）")
                       << QString("静力计算编号（行：81）")
                       << QString("是否液化（行：80）")
                       << QString("σd(KPa)（行：83）") << QString("Rf")
                       << QString("φ(°)（行：82）") << QString("c")
                       << QString("K1") << QString("n1")
                       << QString("残余0.1（行：85）")
                       << QString("残余0.3（行：86）")
                       << QString("残余0.5（行：87）")
                       << QString("Kv（行：105）") << QString("Kp（行：106）")
                       << QString("nv（行：107）") << QString("np（行：108）");

  dynamicModel.columnOutputRows = QVector<int>(dynamicModel.columns.size(), -1);
  dynamicModel.columnOutputRows[2] = 80;
  dynamicModel.columnOutputRows[1] = 81;
  dynamicModel.columnOutputRows[5] = 82;
  dynamicModel.columnOutputRows[3] = 83;
  dynamicModel.columnOutputRows[9] = 85;
  dynamicModel.columnOutputRows[10] = 86;
  dynamicModel.columnOutputRows[11] = 87;
  dynamicModel.columnOutputRows[0] = 88;
  dynamicModel.columnOutputRows[12] = 105;
  dynamicModel.columnOutputRows[13] = 106;
  dynamicModel.columnOutputRows[14] = 107;
  dynamicModel.columnOutputRows[15] = 108;

  dynamicModel.materials.append(makeEntry(
      QString("2A垫层"), dynamicModel.columns,
      {QString("81"), QString("7"), QString("0"), QString("970"),
       QString("0.43"), QString("51.5"), QString("100"), QString("0.35"),
       QString("0.25"), QString("150"), QString("150"), QString("150"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("3A过渡料"), dynamicModel.columns,
      {QString("82"), QString("8"), QString("1"), QString("1000"),
       QString("0.45"), QString("52"), QString("200"), QString("0.34"),
       QString("0.22"), QString("180"), QString("180"), QString("180"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("3B堆石"), dynamicModel.columns,
      {QString("83"), QString("9"), QString("0"), QString("830"),
       QString("0.46"), QString("48"), QString("300"), QString("0.32"),
       QString("0.22"), QString("170"), QString("170"), QString("170"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("1B弃渣盖重"), dynamicModel.columns,
      {QString("84"), QString("10"), QString("0"), QString("750"),
       QString("0.40"), QString("38.3"), QString("100"), QString("0.36"),
       QString("0.26"), QString("170"), QString("170"), QString("170"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("1A铺盖"), dynamicModel.columns,
      {QString("85"), QString("12"), QString("0"), QString("750"),
       QString("0.40"), QString("31.0"), QString("200"), QString("0.36"),
       QString("0.26"), QString("170"), QString("170"), QString("170"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("2B特殊垫层"), dynamicModel.columns,
      {QString("86"), QString("13"), QString("0"), QString("970"),
       QString("0.43"), QString("49"), QString("300"), QString("0.35"),
       QString("0.25"), QString("150"), QString("150"), QString("150"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("3C反滤料"), dynamicModel.columns,
      {QString("87"), QString("16"), QString("0"), QString("820"),
       QString("0.45"), QString("48"), QString("100"), QString("0.35"),
       QString("0.24"), QString("160"), QString("160"), QString("160"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("P下游砌石"), dynamicModel.columns,
      {QString("88"), QString("17"), QString("0"), QString("800"),
       QString("0.46"), QString("51.8"), QString("200"), QString("0.32"),
       QString("0.24"), QString("190"), QString("190"), QString("190"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("gl+fglQ3"), dynamicModel.columns,
      {QString("89"), QString("21"), QString("0"), QString("850"),
       QString("0.44"), QString("43.5"), QString("300"), QString("0.33"),
       QString("0.24"), QString("160"), QString("160"), QString("160"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("col+dlQ4(⑤-2)"), dynamicModel.columns,
      {QString("90"), QString("22"), QString("0"), QString("450"),
       QString("0.41"), QString("38.3"), QString("100"), QString("0.42"),
       QString("0.27"), QString("140"), QString("140"), QString("140"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("lQ4"), dynamicModel.columns,
      {QString("91"), QString("23"), QString("1"), QString("740"),
       QString("0.43"), QString("31.0"), QString("200"), QString("0.38"),
       QString("0.25"), QString("150"), QString("150"), QString("150"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("colQ3"), dynamicModel.columns,
      {QString("92"), QString("24"), QString("1"), QString("850"),
       QString("0.44"), QString("43.5"), QString("300"), QString("0.33"),
       QString("0.24"), QString("160"), QString("160"), QString("160"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("泥皮"), dynamicModel.columns,
      {QString("93"), QString("26"), QString("0"), QString("380"),
       QString("0.43"), QString("10.0"), QString("50"), QString("0.44"),
       QString("0.31"), QString("180"), QString("180"), QString("180"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));
  dynamicModel.materials.append(makeEntry(
      QString("el+dlQ4"), dynamicModel.columns,
      {QString("94"), QString("28"), QString("0"), QString("450"),
       QString("0.41"), QString("40.2"), QString("80"), QString("0.42"),
       QString("0.27"), QString("140"), QString("140"), QString("140"),
       QString("1000"), QString("800"), QString("0.5"), QString("0.8")}));

  models.append(dynamicModel);

  // =========================
  // 样表4（IB=0）
  // =========================
  MaterialModel seepageModel;
  seepageModel.key = QString("jianke_seepage");
  seepageModel.displayName = QString("剑科心墙堆石坝稳态渗流参数");
  seepageModel.ib = 0;

  seepageModel.columns.clear();
  seepageModel.columns << QString("材料编号") << QString("渗透系数K(Lu)")
                       << QString("允许比降");
  seepageModel.columnOutputRows = QVector<int>(seepageModel.columns.size(), -1);

  seepageModel.materials.append(
      makeEntry(QString("alQ3"), seepageModel.columns,
                {QString("3"), QString("35000.0"), QString("0.09")}));
  seepageModel.materials.append(
      makeEntry(QString("alQ4"), seepageModel.columns,
                {QString("4"), QString("35000.0"), QString("0.09")}));
  seepageModel.materials.append(
      makeEntry(QString("Col+dlQ4"), seepageModel.columns,
                {QString("5"), QString("4500.0"), QString("0.14")}));
  seepageModel.materials.append(
      makeEntry(QString("垫层料"), seepageModel.columns,
                {QString("6"), QString("720.0"), QString("0.12")}));
  seepageModel.materials.append(
      makeEntry(QString("坝顶填筑"), seepageModel.columns,
                {QString("7"), QString("10.0"), QString("0.13")}));
  seepageModel.materials.append(
      makeEntry(QString("花岗岩堆石料(基准)"), seepageModel.columns,
                {QString("8"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("花岗岩堆石料(基准湿化)"), seepageModel.columns,
                {QString("27"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("防渗土料(基准)"), seepageModel.columns,
                {QString("9"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("防渗土料(基准湿化)"), seepageModel.columns,
                {QString("28"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("防渗土料(比较方案二)"), seepageModel.columns,
                {QString("62"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("反滤层1(基准)"), seepageModel.columns,
                {QString("10"), QString("543.0"), QString("0.15")}));
  seepageModel.materials.append(
      makeEntry(QString("反滤层1(基准湿化)"), seepageModel.columns,
                {QString("29"), QString("543.0"), QString("0.15")}));
  seepageModel.materials.append(
      makeEntry(QString("反滤层2(基准)"), seepageModel.columns,
                {QString("11"), QString("543.0"), QString("0.15")}));
  seepageModel.materials.append(
      makeEntry(QString("反滤层2(基准湿化)"), seepageModel.columns,
                {QString("30"), QString("543.0"), QString("0.15")}));
  seepageModel.materials.append(
      makeEntry(QString("过渡料(基准)"), seepageModel.columns,
                {QString("12"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("过渡料(基准湿化)"), seepageModel.columns,
                {QString("31"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("过渡料(比较方案一)"), seepageModel.columns,
                {QString("60"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("过渡料(比较方案二)"), seepageModel.columns,
                {QString("61"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("大块石护坡"), seepageModel.columns,
                {QString("18"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("大块石填筑"), seepageModel.columns,
                {QString("19"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("弃渣盖重"), seepageModel.columns,
                {QString("22"), QString("400.0"), QString("0.38")}));
  seepageModel.materials.append(
      makeEntry(QString("高塑性粘土"), seepageModel.columns,
                {QString("24"), QString("1.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("粘土/砼接触面泥皮"), seepageModel.columns,
                {QString("25"), QString("1.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("T3x新鲜"), seepageModel.columns,
                {QString("13"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("T3x弱卸荷"), seepageModel.columns,
                {QString("14"), QString("10.0"), QString("0.13")}));
  seepageModel.materials.append(
      makeEntry(QString("T3x强卸荷"), seepageModel.columns,
                {QString("15"), QString("40.0"), QString("0.13")}));
  seepageModel.materials.append(
      makeEntry(QString("γδ53新鲜"), seepageModel.columns,
                {QString("16"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("γδ53弱卸荷"), seepageModel.columns,
                {QString("17"), QString("10.0"), QString("0.13")}));
  seepageModel.materials.append(
      makeEntry(QString("砼"), seepageModel.columns,
                {QString("20"), QString("0.01"), QString("180")}));
  seepageModel.materials.append(
      makeEntry(QString("帷幕/基岩接触面"), seepageModel.columns,
                {QString("21"), QString("3.0"), QString("50")}));
  seepageModel.materials.append(
      makeEntry(QString("挤压带"), seepageModel.columns,
                {QString("23"), QString("50"), QString("40")}));
  seepageModel.materials.append(
      makeEntry(QString("帷幕"), seepageModel.columns,
                {QString("26"), QString("3.00"), QString("18")}));

  models.append(seepageModel);

  // =========================
  // 样表5（IB=-3）
  // =========================
  MaterialModel emuModel;
  emuModel.key = QString("soil_emu");
  emuModel.displayName = QString("土体E-μ模型");
  emuModel.ib = -3;

  emuModel.columns.clear();
  emuModel.columns << QString("材料编号") << QString("干容重")
                   << QString("天然容重（行：90）")
                   << QString("饱和容重（行：89）") << QString("φ0")
                   << QString("Δφ") << QString("c(KPa)") << QString("Rf")
                   << QString("K") << QString("n") << QString("G（行：77）")
                   << QString("D（行：78）") << QString("F（行：79）");

  emuModel.columnOutputRows = QVector<int>(emuModel.columns.size(), -1);
  emuModel.columnOutputRows[10] = 77;  // G
  emuModel.columnOutputRows[11] = 78;  // D
  emuModel.columnOutputRows[12] = 79;  // F
  emuModel.columnOutputRows[3] = 89;   // 饱和容重
  emuModel.columnOutputRows[2] = 90;   // 天然容重

  emuModel.materials.append(
      makeEntry(QString("上游铺盖（回填石渣料）"), emuModel.columns,
                {QString("4"), QString("1.8"), QString("2.0"), QString("2.2"),
                 QString("35"), QString("0"), QString("0.0"), QString("0.75"),
                 QString("500"), QString("0.4"), QString("0.38"),
                 QString("2.7"), QString("0.15")}));
  emuModel.materials.append(
      makeEntry(QString("上游堆石料"), emuModel.columns,
                {QString("5"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("35"), QString("0"), QString("0.0"), QString("0.8"),
                 QString("1000"), QString("0.3"), QString("0.37"),
                 QString("11"), QString("0.1")}));
  emuModel.materials.append(
      makeEntry(QString("上游过渡层"), emuModel.columns,
                {QString("6"), QString("2.10"), QString("2.15"),
                 QString("2.20"), QString("33"), QString("0"), QString("0"),
                 QString("0.78"), QString("900"), QString("0.3"),
                 QString("0.42"), QString("11"), QString("0.08")}));
  emuModel.materials.append(
      makeEntry(QString("沥青砼心墙"), emuModel.columns,
                {QString("7"), QString("2.10"), QString("2.35"),
                 QString("2.40"), QString("24"), QString("0"), QString("120"),
                 QString("0.82"), QString("600"), QString("0.4"),
                 QString("0.42"), QString("7"), QString("0.08")}));
  emuModel.materials.append(
      makeEntry(QString("下游过渡层"), emuModel.columns,
                {QString("9"), QString("2.10"), QString("2.15"),
                 QString("2.20"), QString("33"), QString("0"), QString("0"),
                 QString("0.78"), QString("900"), QString("0.3"),
                 QString("0.42"), QString("11"), QString("0.08")}));
  emuModel.materials.append(
      makeEntry(QString("下游堆石料"), emuModel.columns,
                {QString("10"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("35"), QString("0"), QString("0.0"), QString("0.8"),
                 QString("1000"), QString("0.3"), QString("0.37"),
                 QString("11"), QString("0.1")}));
  emuModel.materials.append(
      makeEntry(QString("上游1级找平堆石"), emuModel.columns,
                {QString("15"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("35"), QString("0"), QString("0.0"), QString("0.8"),
                 QString("1000"), QString("0.3"), QString("0.37"),
                 QString("11"), QString("0.1")}));
  emuModel.materials.append(
      makeEntry(QString("下游1级找平堆石"), emuModel.columns,
                {QString("16"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("35"), QString("0"), QString("0.0"), QString("0.8"),
                 QString("1000"), QString("0.3"), QString("0.37"),
                 QString("11"), QString("0.1")}));
  emuModel.materials.append(makeEntry(
      QString("K5沉积粘土"), emuModel.columns,
      {QString("42"), QString("1.85"), QString("1.90"), QString("2.00"),
       QString("16.4"), QString("0.0"), QString("21.8"), QString("0.87"),
       QString("270"), QString("0.47"), QString("0.46"), QString("0.05"),
       QString("0.58")}));

  models.append(emuModel);

  // =========================
  // 样表6（IB=0）
  // =========================
  MaterialModel barModel;
  barModel.key = QString("huangjinping_bolt");
  barModel.displayName = QString("杆单元参数（黄金坪地下厂房）");
  barModel.ib = 0;

  barModel.columns.clear();
  barModel.columns << QString("序号") << QString("变形模量E1,E2(GPa)")
                   << QString("容重") << QString("杆面积(m2)")
                   << QString("抗拉（压）强度ft(MPa)") << QString("间距(m)")
                   << QString("排距(m)") << QString("预应力T(KN)")
                   << QString("预应系数");
  barModel.columnOutputRows = QVector<int>(barModel.columns.size(), -1);

  barModel.materials.append(
      makeEntry(QString("15m预应力锚索1500KN"), barModel.columns,
                {QString("20"), QString("206"), QString("7.80"),
                 QString("0.002"), QString("1800"), QString("4.5"),
                 QString("4.5"), QString("1500.0"), QString("1.0")}));
  barModel.materials.append(
      makeEntry(QString("8m/9m普通锚杆 φ32"), barModel.columns,
                {QString("21"), QString("206"), QString("7.80"),
                 QString("0.000804"), QString("390"), QString("1.8"),
                 QString("1.8"), QString("0.0"), QString("1.0")}));
  barModel.materials.append(
      makeEntry(QString("6m/7m普通锚杆 φ28"), barModel.columns,
                {QString("22"), QString("206"), QString("7.80"),
                 QString("0.000616"), QString("390"), QString("1.8"),
                 QString("1.8"), QString("0.0"), QString("0.85")}));
  barModel.materials.append(
      makeEntry(QString("8m/9m普通锚杆 φ32（密排）"), barModel.columns,
                {QString("23"), QString("206"), QString("7.80"),
                 QString("0.000804"), QString("390"), QString("1.5"),
                 QString("1.5"), QString("0.0"), QString("0.85")}));
  barModel.materials.append(
      makeEntry(QString("15m预应力锚索1500KN(密排)"), barModel.columns,
                {QString("24"), QString("206"), QString("7.80"),
                 QString("0.002"), QString("1800"), QString("4.0"),
                 QString("4.0"), QString("1500"), QString("0.70")}));
  barModel.materials.append(
      makeEntry(QString("20m预应力锚索1750KN"), barModel.columns,
                {QString("25"), QString("206"), QString("7.80"),
                 QString("0.002"), QString("1800"), QString("4.0"),
                 QString("4.0"), QString("1750"), QString("0.9")}));
  barModel.materials.append(
      makeEntry(QString("45m对穿预应力锚索1750KN"), barModel.columns,
                {QString("26"), QString("206"), QString("7.80"),
                 QString("0.002"), QString("1800"), QString("4.0"),
                 QString("4.0"), QString("1750"), QString("0.9")}));
  barModel.materials.append(
      makeEntry(QString("6m/7m普通锚杆 φ28(1.5m)"), barModel.columns,
                {QString("27"), QString("206"), QString("7.80"),
                 QString("0.000616"), QString("390"), QString("1.5"),
                 QString("1.5"), QString("0.0"), QString("0.9")}));
  barModel.materials.append(
      makeEntry(QString("4.5m/5m普通锚杆 φ25"), barModel.columns,
                {QString("28"), QString("206"), QString("7.80"),
                 QString("0.000491"), QString("390"), QString("1.5"),
                 QString("1.5"), QString("0.0"), QString("0.0")}));
  barModel.materials.append(
      makeEntry(QString("20m预应力锚索1750KN(4.5m间距)"), barModel.columns,
                {QString("29"), QString("206"), QString("7.80"),
                 QString("0.002"), QString("1800"), QString("4.5"),
                 QString("4.5"), QString("1750"), QString("1.0")}));

  models.append(barModel);

  // =========================
  // 样表7（IB=0）
  // =========================
  MaterialModel dagangshanModel;
  dagangshanModel.key = QString("dagangshan_isotropic");
  dagangshanModel.displayName = QString("大岗山高拱坝各向同性岩体参数");
  dagangshanModel.ib = 0;

  dagangshanModel.columns.clear();
  dagangshanModel.columns << QString("序号") << QString("变形模量E(GPa)")
                          << QString("泊松比") << QString("容重(t/m3)")
                          << QString("内摩擦系数") << QString("凝聚力C'(MPa)");
  dagangshanModel.columnOutputRows =
      QVector<int>(dagangshanModel.columns.size(), -1);

  dagangshanModel.materials.append(
      makeEntry(QString("空化"), dagangshanModel.columns,
                {QString("1"), QString("0.001"), QString("0.3"), QString("0.0"),
                 QString("1.7"), QString("200.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("alQ"), dagangshanModel.columns,
                {QString("2"), QString("0.5"), QString("0.4"), QString("1.5"),
                 QString("0.8"), QString("0.05")}));
  dagangshanModel.materials.append(
      makeEntry(QString("坝砼"), dagangshanModel.columns,
                {QString("3"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("置换混凝土"), dagangshanModel.columns,
                {QString("4"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("垫座"), dagangshanModel.columns,
                {QString("5"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β5（F1）"), dagangshanModel.columns,
                {QString("6"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("1.0"), QString("1.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β6"), dagangshanModel.columns,
                {QString("7"), QString("21.5"), QString("0.25"),
                 QString("2.65"), QString("1.3"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β9"), dagangshanModel.columns,
                {QString("8"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("1.0"), QString("1.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β11"), dagangshanModel.columns,
                {QString("9"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("1.0"), QString("1.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("γL5"), dagangshanModel.columns,
                {QString("11"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("1.0"), QString("1.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("左侧坝身孔口与坝体连接单元"), dagangshanModel.columns,
                {QString("17"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("右侧坝身孔口与坝体连接单元"), dagangshanModel.columns,
                {QString("18"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("拱坝/基础退化砼"), dagangshanModel.columns,
                {QString("19"), QString("24.0"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("Ⅱ"), dagangshanModel.columns,
                {QString("20"), QString("21.5"), QString("0.25"),
                 QString("2.65"), QString("1.3"), QString("2.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("Ⅲ1"), dagangshanModel.columns,
                {QString("21"), QString("10"), QString("0.27"), QString("2.62"),
                 QString("1.2"), QString("1.5")}));
  dagangshanModel.materials.append(
      makeEntry(QString("Ⅲ2"), dagangshanModel.columns,
                {QString("22"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("1.0"), QString("1.0")}));
  dagangshanModel.materials.append(
      makeEntry(QString("Ⅳ"), dagangshanModel.columns,
                {QString("23"), QString("3.0"), QString("0.35"),
                 QString("2.58"), QString("0.8"), QString("0.7")}));
  dagangshanModel.materials.append(
      makeEntry(QString("Ⅴ"), dagangshanModel.columns,
                {QString("24"), QString("0.38"), QString("0.35"),
                 QString("2.45"), QString("0.5"), QString("0.2")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β21"), dagangshanModel.columns,
                {QString("25"), QString("3.0"), QString("0.35"),
                 QString("2.58"), QString("0.5"), QString("0.08")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β28"), dagangshanModel.columns,
                {QString("26"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("0.4"), QString("0.03")}));
  dagangshanModel.materials.append(
      makeEntry(QString("β4(f5)"), dagangshanModel.columns,
                {QString("27"), QString("7.5"), QString("0.3"), QString("2.62"),
                 QString("0.35"), QString("0.05")}));

  models.append(dagangshanModel);

  // =========================
  // 样表8（IB=0）
  // =========================
  MaterialModel isotropicFinal;
  isotropicFinal.key = QString("huangjinping_isotropic_final");
  isotropicFinal.displayName = QString("黄金坪地下厂房各向同性岩体（最终）");
  isotropicFinal.ib = 0;

  isotropicFinal.columns.clear();
  isotropicFinal.columns << QString("序号") << QString("母岩编号")
                         << QString("岩类分区")
                         << QString("半开挖区荷载释放系数")
                         << QString("再生开挖级") << QString("变形模量E(GPa)")
                         << QString("泊松比") << QString("容重(t/m3)")
                         << QString("内摩擦系数") << QString("凝聚力C'(MPa)");

  isotropicFinal.columnOutputRows =
      QVector<int>(isotropicFinal.columns.size(), -1);

  isotropicFinal.materials.append(
      makeEntry(QString("原岩区：ITAG_EX=0"), isotropicFinal.columns,
                {QString("0"), QString("0"), QString("0"), QString("0"),
                 QString("0"), QString("0.0"), QString("0.0"), QString("0.0"),
                 QString("0.0"), QString("0.0")}));
  isotropicFinal.materials.append(
      makeEntry(QString("II"), isotropicFinal.columns,
                {QString("3"), QString("3"), QString("0"), QString("0"),
                 QString("0"), QString("16.25"), QString("0.23"),
                 QString("2.7"), QString("1.23"), QString("1.58")}));
  isotropicFinal.materials.append(
      makeEntry(QString("III"), isotropicFinal.columns,
                {QString("4"), QString("4"), QString("0"), QString("0"),
                 QString("0"), QString("9.00"), QString("0.25"),
                 QString("2.65"), QString("1.05"), QString("1.13")}));
  isotropicFinal.materials.append(
      makeEntry(QString("IV"), isotropicFinal.columns,
                {QString("5"), QString("5"), QString("0"), QString("0"),
                 QString("0"), QString("5.75"), QString("0.30"),
                 QString("2.65"), QString("0.85"), QString("0.78")}));
  isotropicFinal.materials.append(
      makeEntry(QString("V"), isotropicFinal.columns,
                {QString("6"), QString("6"), QString("0"), QString("0"),
                 QString("0"), QString("2.00"), QString("0.35"),
                 QString("2.60"), QString("0.613"), QString("0.35")}));
  isotropicFinal.materials.append(
      makeEntry(QString("f10-14"), isotropicFinal.columns,
                {QString("7"), QString("7"), QString("0"), QString("0"),
                 QString("0"), QString("0.2"), QString("0.35"), QString("2.23"),
                 QString("0.35"), QString("0.05")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙"), isotropicFinal.columns,
                {QString("8"), QString("8"), QString("0"), QString("0"),
                 QString("0"), QString("3.0"), QString("0.38"), QString("1.5"),
                 QString("0.50"), QString("0.30")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙密集带（IV类范围内）"), isotropicFinal.columns,
                {QString("10"), QString("10"), QString("0"), QString("0"),
                 QString("0"), QString("2.00"), QString("0.35"),
                 QString("2.60"), QString("0.613"), QString("0.35")}));
  isotropicFinal.materials.append(
      makeEntry(QString("开挖区：ITAG_EX=-1"), isotropicFinal.columns,
                {QString("0"), QString("0"), QString("-1"), QString("1.0"),
                 QString("0"), QString("0.0"), QString("0.0"), QString("0.0"),
                 QString("0.0"), QString("0.0")}));
  isotropicFinal.materials.append(
      makeEntry(QString("II(开挖区)"), isotropicFinal.columns,
                {QString("13"), QString("3"), QString("-1"), QString("1.0"),
                 QString("0"), QString("16.25"), QString("0.23"),
                 QString("2.7"), QString("1.23"), QString("1.58")}));
  isotropicFinal.materials.append(
      makeEntry(QString("III(开挖区)"), isotropicFinal.columns,
                {QString("14"), QString("4"), QString("-1"), QString("1.0"),
                 QString("0"), QString("9.00"), QString("0.25"),
                 QString("2.65"), QString("1.05"), QString("1.13")}));
  isotropicFinal.materials.append(
      makeEntry(QString("IV(开挖区)"), isotropicFinal.columns,
                {QString("15"), QString("5"), QString("-1"), QString("1.0"),
                 QString("0"), QString("5.75"), QString("0.30"),
                 QString("2.65"), QString("0.85"), QString("0.78")}));
  isotropicFinal.materials.append(
      makeEntry(QString("V(开挖区)"), isotropicFinal.columns,
                {QString("16"), QString("6"), QString("-1"), QString("1.0"),
                 QString("0"), QString("2.00"), QString("0.35"),
                 QString("2.60"), QString("0.613"), QString("0.35")}));
  isotropicFinal.materials.append(
      makeEntry(QString("f10-14(开挖区)"), isotropicFinal.columns,
                {QString("17"), QString("7"), QString("-1"), QString("1.0"),
                 QString("0"), QString("0.2"), QString("0.35"), QString("2.23"),
                 QString("0.35"), QString("0.05")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙(开挖区)"), isotropicFinal.columns,
                {QString("18"), QString("8"), QString("-1"), QString("1.0"),
                 QString("0"), QString("3.0"), QString("0.38"), QString("1.5"),
                 QString("0.50"), QString("0.30")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙密集带(开挖区)"), isotropicFinal.columns,
                {QString("19"), QString("9"), QString("-1"), QString("1.0"),
                 QString("0"), QString("5.75"), QString("0.30"),
                 QString("2.65"), QString("0.85"), QString("0.78")}));
  isotropicFinal.materials.append(
      makeEntry(QString("尾水连接管（III）"), isotropicFinal.columns,
                {QString("23"), QString("4"), QString("-1"), QString("0.3213"),
                 QString("0"), QString("9.00"), QString("0.25"),
                 QString("2.65"), QString("1.05"), QString("1.13")}));
  isotropicFinal.materials.append(
      makeEntry(QString("母线洞（III）"), isotropicFinal.columns,
                {QString("24"), QString("4"), QString("-1"), QString("0.3213"),
                 QString("0"), QString("9.00"), QString("0.25"),
                 QString("2.65"), QString("1.05"), QString("1.13")}));
  isotropicFinal.materials.append(
      makeEntry(QString("f10-14 过尾水连接管"), isotropicFinal.columns,
                {QString("27"), QString("7"), QString("-1"), QString("0.3213"),
                 QString("0"), QString("0.2"), QString("0.35"), QString("2.23"),
                 QString("0.35"), QString("0.05")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙过母线洞"), isotropicFinal.columns,
                {QString("28"), QString("8"), QString("-1"), QString("0.3213"),
                 QString("0"), QString("5.75"), QString("0.30"),
                 QString("2.65"), QString("0.85"), QString("0.78")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙过尾水连接管"), isotropicFinal.columns,
                {QString("29"), QString("9"), QString("-1"), QString("0.3213"),
                 QString("0"), QString("5.75"), QString("0.30"),
                 QString("2.65"), QString("0.85"), QString("0.78")}));
  isotropicFinal.materials.append(
      makeEntry(QString("再生区:ITAG_EX=1"), isotropicFinal.columns,
                {QString("0"), QString("0"), QString("1"), QString("0"),
                 QString("0"), QString("0.0"), QString("0.0"), QString("0.0"),
                 QString("0.0"), QString("0.0")}));
  isotropicFinal.materials.append(
      makeEntry(QString("吊车梁岩体（母岩为III）"), isotropicFinal.columns,
                {QString("43"), QString("4"), QString("1"), QString("0"),
                 QString("-4"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));
  isotropicFinal.materials.append(
      makeEntry(QString("喷C25砼"), isotropicFinal.columns,
                {QString("44"), QString("4"), QString("1"), QString("0"),
                 QString("0"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));
  isotropicFinal.materials.append(
      makeEntry(QString("母线洞喷C25砼"), isotropicFinal.columns,
                {QString("45"), QString("4"), QString("1"), QString("0"),
                 QString("0"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙过吊车梁"), isotropicFinal.columns,
                {QString("46"), QString("8"), QString("1"), QString("0"),
                 QString("-4"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));
  isotropicFinal.materials.append(
      makeEntry(QString("f10-14过喷C25砼"), isotropicFinal.columns,
                {QString("47"), QString("7"), QString("1"), QString("0"),
                 QString("0"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));
  isotropicFinal.materials.append(
      makeEntry(QString("裂隙过喷C25砼"), isotropicFinal.columns,
                {QString("48"), QString("8"), QString("1"), QString("0"),
                 QString("0"), QString("22.4"), QString("0.167"),
                 QString("2.4"), QString("1.4"), QString("3.9")}));

  models.append(isotropicFinal);

  // =========================
  // 样表9（IB=-5）
  // =========================
  MaterialModel nanshuiModel;
  nanshuiModel.key = QString("soil_nanshui");
  nanshuiModel.displayName = QString("土体南水模型");
  nanshuiModel.ib = -5;

  nanshuiModel.columns.clear();
  nanshuiModel.columns << QString("材料编号") << QString("干容重")
                       << QString("天然容重") << QString("饱和容重")
                       << QString("φ0") << QString("Δφ") << QString("c(KPa)")
                       << QString("Rf") << QString("K") << QString("Kur")
                       << QString("n") << QString("cd（行：77）")
                       << QString("nd（行：78）") << QString("Rd（行：79）");

  nanshuiModel.columnOutputRows = QVector<int>(nanshuiModel.columns.size(), -1);
  nanshuiModel.columnOutputRows[11] = 77;  // cd
  nanshuiModel.columnOutputRows[12] = 78;  // nd
  nanshuiModel.columnOutputRows[13] = 79;  // Rd

  nanshuiModel.materials.append(makeEntry(
      QString("垫层料"), nanshuiModel.columns,
      {QString("7"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("46.0"), QString("6.0"), QString("0.0"), QString("0.79"),
       QString("867"), QString("1133"), QString("0.39"), QString("0.0043"),
       QString("0.62"), QString("0.72")}));
  nanshuiModel.materials.append(makeEntry(
      QString("过渡料"), nanshuiModel.columns,
      {QString("8"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("50.0"), QString("8.0"), QString("0.0"), QString("0.75"),
       QString("683"), QString("1000"), QString("0.31"), QString("0.0060"),
       QString("0.78"), QString("0.70")}));
  nanshuiModel.materials.append(makeEntry(
      QString("堆石料"), nanshuiModel.columns,
      {QString("9"), QString("2.00"), QString("2.10"), QString("2.20"),
       QString("48.0"), QString("9.0"), QString("0.0"), QString("0.77"),
       QString("500"), QString("750"), QString("0.20"), QString("0.0100"),
       QString("0.75"), QString("0.75")}));
  nanshuiModel.materials.append(makeEntry(
      QString("弱风化料"), nanshuiModel.columns,
      {QString("10"), QString("1.90"), QString("2.05"), QString("2.20"),
       QString("48.0"), QString("12.0"), QString("0.0"), QString("0.80"),
       QString("320"), QString("480"), QString("0.20"), QString("0.0150"),
       QString("0.56"), QString("0.80")}));
  nanshuiModel.materials.append(makeEntry(
      QString("强风化料"), nanshuiModel.columns,
      {QString("11"), QString("1.80"), QString("2.00"), QString("2.20"),
       QString("43.0"), QString("11.0"), QString("0.0"), QString("0.63"),
       QString("120"), QString("180"), QString("0.37"), QString("0.0200"),
       QString("0.25"), QString("0.63")}));
  nanshuiModel.materials.append(makeEntry(
      QString("超径石"), nanshuiModel.columns,
      {QString("12"), QString("2.00"), QString("2.10"), QString("2.20"),
       QString("48.0"), QString("9.0"), QString("0.0"), QString("0.77"),
       QString("500"), QString("750"), QString("0.20"), QString("0.0100"),
       QString("0.75"), QString("0.75")}));

  models.append(nanshuiModel);

  // =========================
  // 样表10（IB=-2）
  // =========================
  MaterialModel kgModel;
  kgModel.key = QString("soil_kg");
  kgModel.displayName = QString("土体科大K-G模型参数");
  kgModel.ib = -2;

  kgModel.columns.clear();
  kgModel.columns << QString("材料编号") << QString("干容重(t/m3)")
                  << QString("天然容重(t/m3)") << QString("饱和容重(t/m3)")
                  << QString("Ki(105Pa)（行：69）")
                  << QString("Kie(105Pa)（行：76）") << QString("αk（行：71）")
                  << QString("αke（行：78）") << QString("Gi(105Pa)（行：74）")
                  << QString("Gie(105Pa)（行：77）") << QString("αG（行：70）")
                  << QString("αGe（行：79）") << QString("βG（行：75）")
                  << QString("a(105Pa)（行：72）") << QString("b（行：73）");

  kgModel.columnOutputRows = QVector<int>(kgModel.columns.size(), -1);
  kgModel.columnOutputRows[4] = 69;   // Ki
  kgModel.columnOutputRows[5] = 76;   // Kie
  kgModel.columnOutputRows[6] = 71;   // αk
  kgModel.columnOutputRows[7] = 78;   // αke
  kgModel.columnOutputRows[8] = 74;   // Gi
  kgModel.columnOutputRows[9] = 77;   // Gie
  kgModel.columnOutputRows[10] = 70;  // αG
  kgModel.columnOutputRows[11] = 79;  // αGe
  kgModel.columnOutputRows[12] = 75;  // βG
  kgModel.columnOutputRows[13] = 72;  // a
  kgModel.columnOutputRows[14] = 73;  // b

  kgModel.materials.append(
      makeEntry(QString("覆盖层3"), kgModel.columns,
                {QString("4"), QString("1.8"), QString("2.0"), QString("2.2"),
                 QString("168"), QString("68"), QString("114"), QString("115"),
                 QString("-60"), QString("-60"), QString("114"), QString("115"),
                 QString("190"), QString("190"), QString("1.92")}));

  kgModel.materials.append(
      makeEntry(QString("覆盖层2"), kgModel.columns,
                {QString("5"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("33"), QString("70"), QString("22"), QString("64"),
                 QString("-35"), QString("-35"), QString("22"), QString("64"),
                 QString("63"), QString("63"), QString("1.83")}));

  kgModel.materials.append(makeEntry(
      QString("心墙掺砾料"), kgModel.columns,
      {QString("6"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("80"), QString("94"), QString("106"), QString("112"),
       QString("-80"), QString("-80"), QString("106"), QString("112"),
       QString("132"), QString("132"), QString("1.40")}));

  kgModel.materials.append(makeEntry(
      QString("反滤层1"), kgModel.columns,
      {QString("7"), QString("2.10"), QString("2.35"), QString("2.40"),
       QString("168"), QString("110"), QString("114"), QString("194"),
       QString("-97"), QString("-97"), QString("114"), QString("194"),
       QString("118"), QString("118"), QString("2.00")}));

  kgModel.materials.append(makeEntry(
      QString("反滤层2"), kgModel.columns,
      {QString("9"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("170"), QString("110"), QString("104"), QString("187"),
       QString("-90"), QString("-90"), QString("104"), QString("187"),
       QString("116"), QString("116"), QString("2.08")}));

  kgModel.materials.append(
      makeEntry(QString("过渡层"), kgModel.columns,
                {QString("10"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("230"), QString("75"), QString("100"), QString("133"),
                 QString("-70"), QString("-70"), QString("100"), QString("133"),
                 QString("140"), QString("140"), QString("1.90")}));

  kgModel.materials.append(
      makeEntry(QString("主堆石"), kgModel.columns,
                {QString("15"), QString("2.0"), QString("2.1"), QString("2.2"),
                 QString("300"), QString("41"), QString("120"), QString("108"),
                 QString("-60"), QString("-60"), QString("120"), QString("108"),
                 QString("200"), QString("200"), QString("1.80")}));

  models.append(kgModel);

  // =========================
  // 样表11（IB=0）三参数流变
  // =========================
  MaterialModel shenPearlModel;
  shenPearlModel.key = QString("shen_zhujiang_rheology");
  shenPearlModel.displayName = QString("沈珠江三参数流变模型");
  shenPearlModel.ib = 0;

  shenPearlModel.columns.clear();
  shenPearlModel.columns << QString("序号")
                         << QString("应变速率系数α（行：14）")
                         << QString("体积流变系数b（行：15）")
                         << QString("剪切流变系数d（行：16）");

  shenPearlModel.columnOutputRows =
      QVector<int>(shenPearlModel.columns.size(), -1);
  shenPearlModel.columnOutputRows[1] = 14;
  shenPearlModel.columnOutputRows[2] = 15;
  shenPearlModel.columnOutputRows[3] = 16;

  shenPearlModel.materials.append(makeEntry(
      QString("垫层料"), shenPearlModel.columns,
      {QString("7"), QString("0.004"), QString("0.0009"), QString("0.007")}));
  shenPearlModel.materials.append(makeEntry(
      QString("过渡料"), shenPearlModel.columns,
      {QString("8"), QString("0.004"), QString("0.0009"), QString("0.007")}));
  shenPearlModel.materials.append(makeEntry(
      QString("堆石料"), shenPearlModel.columns,
      {QString("9"), QString("0.004"), QString("0.0009"), QString("0.007")}));
  shenPearlModel.materials.append(makeEntry(
      QString("弱风化料"), shenPearlModel.columns,
      {QString("10"), QString("0.005"), QString("0.0012"), QString("0.008")}));
  shenPearlModel.materials.append(makeEntry(
      QString("强风化料"), shenPearlModel.columns,
      {QString("11"), QString("0.006"), QString("0.0015"), QString("0.009")}));
  shenPearlModel.materials.append(makeEntry(
      QString("超径石"), shenPearlModel.columns,
      {QString("12"), QString("0.004"), QString("0.0009"), QString("0.007")}));

  models.append(shenPearlModel);

  // =========================
  // 样表12（IB=0）李国英七参数流变
  // =========================
  MaterialModel liguoyingModel;
  liguoyingModel.key = QString("liguoying_rheology");
  liguoyingModel.displayName = QString("李国英七参数流变模型");
  liguoyingModel.ib = 0;

  liguoyingModel.columns.clear();
  liguoyingModel.columns << QString("序号")
                         << QString("应变速率系数α（行：14）")
                         << QString("体积流变系数b（行：15）")
                         << QString("体积流变系数c（行：16）")
                         << QString("剪切流变系数d（行：17）")
                         << QString("体积流变指数m1（行：18）")
                         << QString("体积流变指数m2（行：19）")
                         << QString("剪切流变指数m3（行：20）");

  liguoyingModel.columnOutputRows =
      QVector<int>(liguoyingModel.columns.size(), -1);
  liguoyingModel.columnOutputRows[1] = 14;
  liguoyingModel.columnOutputRows[2] = 15;
  liguoyingModel.columnOutputRows[3] = 16;
  liguoyingModel.columnOutputRows[4] = 17;
  liguoyingModel.columnOutputRows[5] = 18;
  liguoyingModel.columnOutputRows[6] = 19;
  liguoyingModel.columnOutputRows[7] = 20;

  liguoyingModel.materials.append(
      makeEntry(QString("主堆石3B-I"), liguoyingModel.columns,
                {QString("7"), QString("0.006"), QString("0.0006"),
                 QString("0.0002"), QString("0.004"), QString("0.769"),
                 QString("0.635"), QString("0.550")}));
  liguoyingModel.materials.append(
      makeEntry(QString("主堆石3B-II"), liguoyingModel.columns,
                {QString("8"), QString("0.007"), QString("0.003"),
                 QString("0.0001"), QString("0.0025"), QString("0.770"),
                 QString("0.631"), QString("0.530")}));
  liguoyingModel.materials.append(
      makeEntry(QString("次堆石3C"), liguoyingModel.columns,
                {QString("9"), QString("0.005"), QString("0.0007"),
                 QString("0.0004"), QString("0.0043"), QString("0.802"),
                 QString("0.664"), QString("0.717")}));

  models.append(liguoyingModel);

  // =========================
  // 样表13（IB=0）九参数流变
  // =========================
  MaterialModel nineParamModel;
  nineParamModel.key = QString("nine_param_rheology");
  nineParamModel.displayName = QString("九参数流变模型");
  nineParamModel.ib = 0;

  nineParamModel.columns.clear();
  nineParamModel.columns << QString("序号")
                         << QString("应变速率系数λv（行：14）")
                         << QString("体积流变系数Cα（行：15）")
                         << QString("体积流变系数Cβ（行：16）")
                         << QString("体积流变指数dα（行：17）")
                         << QString("体积流变指数dβ（行：18）")
                         << QString("主应变系数c（行：19）")
                         << QString("主应变系数d（行：20）")
                         << QString("主应变流变系数η（行：21）")
                         << QString("主应变流变系数m（行：22）");

  nineParamModel.columnOutputRows =
      QVector<int>(nineParamModel.columns.size(), -1);
  nineParamModel.columnOutputRows[1] = 14;
  nineParamModel.columnOutputRows[2] = 15;
  nineParamModel.columnOutputRows[3] = 16;
  nineParamModel.columnOutputRows[4] = 17;
  nineParamModel.columnOutputRows[5] = 18;
  nineParamModel.columnOutputRows[6] = 19;
  nineParamModel.columnOutputRows[7] = 20;
  nineParamModel.columnOutputRows[8] = 21;
  nineParamModel.columnOutputRows[9] = 22;

  nineParamModel.materials.append(makeEntry(
      QString("主堆石3B-II"), nineParamModel.columns,
      {QString("10"), QString("0.0678"), QString("0.4445"), QString("0.4360"),
       QString("2.0827"), QString("1.6383"), QString("0.2892"),
       QString("0.8465"), QString("0.0831"), QString("0.3899")}));
  nineParamModel.materials.append(makeEntry(
      QString("次堆石3C"), nineParamModel.columns,
      {QString("11"), QString("0.0851"), QString("0.3808"), QString("0.1936"),
       QString("0.6521"), QString("1.0061"), QString("1.4296"),
       QString("0.2852"), QString("0.1116"), QString("1.0384")}));

  models.append(nineParamModel);

  // =========================
  // 样表14（IB=0）西源流变
  // =========================
  MaterialModel xiyuanModel;
  xiyuanModel.key = QString("xiyuan_rheology");
  xiyuanModel.displayName = QString("西源流变模型");
  xiyuanModel.ib = 0;

  xiyuanModel.columns.clear();
  xiyuanModel.columns << QString("序号") << QString("瞬时弹性模量E1(GPa)")
                      << QString("粘塑性模量E2(GPa)") << QString("泊松比μ1")
                      << QString("泊松比μ2")
                      << QString("粘弹性系数η1（行：103）")
                      << QString("粘塑性系数η1（行：104）");

  xiyuanModel.columnOutputRows = QVector<int>(xiyuanModel.columns.size(), -1);
  xiyuanModel.columnOutputRows[5] = 103;
  xiyuanModel.columnOutputRows[6] = 104;

  xiyuanModel.materials.append(
      makeEntry(QString("主堆石3B-II"), xiyuanModel.columns,
                {QString("12"), QString("1.5"), QString("2.0"), QString("0.3"),
                 QString("0.4"), QString("0.5"), QString("0.6")}));
  xiyuanModel.materials.append(
      makeEntry(QString("次堆石3C"), xiyuanModel.columns,
                {QString("13"), QString("1.5"), QString("2.0"), QString("0.3"),
                 QString("0.4"), QString("0.5"), QString("0.6")}));

  models.append(xiyuanModel);

  // =========================
  // 样表15（IB=-6）
  // =========================
  MaterialModel emuBrokenModel;
  emuBrokenModel.key = QString("soil_emu_crush");
  emuBrokenModel.displayName = QString("土体E-μ破碎模型");
  emuBrokenModel.ib = -6;

  emuBrokenModel.columns.clear();
  emuBrokenModel.columns << QString("材料编号") << QString("干容重")
                         << QString("天然容重") << QString("饱和容重")
                         << QString("φ0") << QString("Δφ") << QString("c(KPa)")
                         << QString("Rf") << QString("K") << QString("n")
                         << QString("C1-1") << QString("n1-1")
                         << QString("C1-2") << QString("n1-2")
                         << QString("C1-3") << QString("n1-3");

  emuBrokenModel.columnOutputRows =
      QVector<int>(emuBrokenModel.columns.size(), -1);

  emuBrokenModel.materials.append(makeEntry(
      QString("上游铺盖（回填石渣料）"), emuBrokenModel.columns,
      {QString("4"), QString("1.8"), QString("2.0"), QString("2.2"),
       QString("35"), QString("0"), QString("0.0"), QString("0.75"),
       QString("500"), QString("0.4"), QString("0.38"), QString("2.7"),
       QString("0.38"), QString("2.7"), QString("0.38"), QString("2.7")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("上游堆石料"), emuBrokenModel.columns,
      {QString("5"), QString("2.0"), QString("2.1"), QString("2.2"),
       QString("35"), QString("0"), QString("0.0"), QString("0.8"),
       QString("1000"), QString("0.3"), QString("0.37"), QString("11"),
       QString("0.37"), QString("11"), QString("0.37"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("上游过渡层"), emuBrokenModel.columns,
      {QString("6"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("33"), QString("0"), QString("0"), QString("0.78"),
       QString("900"), QString("0.3"), QString("0.42"), QString("11"),
       QString("0.42"), QString("11"), QString("0.42"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("沥青砼心墙"), emuBrokenModel.columns,
      {QString("7"), QString("2.10"), QString("2.35"), QString("2.40"),
       QString("24"), QString("0"), QString("120"), QString("0.82"),
       QString("600"), QString("0.4"), QString("0.42"), QString("7"),
       QString("0.42"), QString("7"), QString("0.42"), QString("7")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("下游过渡层"), emuBrokenModel.columns,
      {QString("9"), QString("2.10"), QString("2.15"), QString("2.20"),
       QString("33"), QString("0"), QString("0"), QString("0.78"),
       QString("900"), QString("0.3"), QString("0.42"), QString("11"),
       QString("0.42"), QString("11"), QString("0.42"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("下游堆石料"), emuBrokenModel.columns,
      {QString("10"), QString("2.0"), QString("2.1"), QString("2.2"),
       QString("35"), QString("0"), QString("0.0"), QString("0.8"),
       QString("1000"), QString("0.3"), QString("0.37"), QString("11"),
       QString("0.37"), QString("11"), QString("0.37"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("上游1级找平堆石"), emuBrokenModel.columns,
      {QString("15"), QString("2.0"), QString("2.1"), QString("2.2"),
       QString("35"), QString("0"), QString("0.0"), QString("0.8"),
       QString("1000"), QString("0.3"), QString("0.37"), QString("11"),
       QString("0.37"), QString("11"), QString("0.37"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("下游1级找平堆石"), emuBrokenModel.columns,
      {QString("16"), QString("2.0"), QString("2.1"), QString("2.2"),
       QString("35"), QString("0"), QString("0.0"), QString("0.8"),
       QString("1000"), QString("0.3"), QString("0.37"), QString("11"),
       QString("0.37"), QString("11"), QString("0.37"), QString("11")}));
  emuBrokenModel.materials.append(makeEntry(
      QString("K5沉积粘土"), emuBrokenModel.columns,
      {QString("42"), QString("1.85"), QString("1.90"), QString("2.00"),
       QString("16.4"), QString("0.0"), QString("21.8"), QString("0.87"),
       QString("270"), QString("0.47"), QString("0.46"), QString("0.05"),
       QString("0.46"), QString("0.05"), QString("0.46"), QString("0.05")}));

  models.append(emuBrokenModel);

  return models;
}
