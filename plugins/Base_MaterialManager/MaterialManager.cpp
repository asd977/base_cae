#include "MaterialManager.h"

#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <PickManager.h>
#include <ContextMenuHub.h>
#include <EventManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>
#include <vtkCellData.h>
#include <vtkIntArray.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QGuiApplication>
#include <algorithm>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>
#include <cmath>
#include <QDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QDialogButtonBox>
#include <QShortcut>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "MaterialEntryWidget.h"
#include "MaterialModelWidget.h"
#include "MaterialTransitionPage.h"
#include "MaterialStrengthConversion.h"

namespace
{

constexpr int REFERENCE_DIALOG_WIDTH  = 1100;
constexpr int REFERENCE_DIALOG_HEIGHT = 680;
constexpr int DEFAULT_NCON            = 200;
constexpr int MIN_NMAT                = 10;
constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int MAX_SOLVER_MATYPE       = 300;
constexpr double CO_THRESHOLD         = 1.0e-1;

bool isComputableMatype(int matype)
{
    return matype >= FIRST_COMPUTABLE_MATYPE;
}

bool isDamStaticModel(const QString& modelKey)
{
    static const QSet<QString> keys = {
        QStringLiteral("soil_eb"), QStringLiteral("soil_kg"),
        QStringLiteral("soil_emu"), QStringLiteral("soil_nanshui"),
        QStringLiteral("soil_emu_crush"), QStringLiteral("linear_elastic")};
    return keys.contains(modelKey);
}

void migrateLegacyMaterialProperties(const QString& modelKey,
                                     QMap<QString, QString>* properties)
{
    if (!properties)
        return;

    auto copyIfMissing = [properties](const QString& target, const QString& source)
    {
        if (!properties->contains(target) && properties->contains(source))
            properties->insert(target, properties->value(source));
    };

    if (modelKey == QStringLiteral("soil_eb"))
    {
        // 旧版导出固定 Nur=n。
        copyIfMissing(QStringLiteral("Nur"), QStringLiteral("n"));
    }
    else if (modelKey == QStringLiteral("soil_emu") ||
             modelKey == QStringLiteral("soil_emu_crush"))
    {
        // 旧版导出固定 Kur=1.5*K、Nur=n。
        if (!properties->contains(QStringLiteral("Kur")) &&
            properties->contains(QStringLiteral("K")))
        {
            bool ok = false;
            const double k = properties->value(QStringLiteral("K")).toDouble(&ok);
            if (ok && std::isfinite(k))
                properties->insert(QStringLiteral("Kur"), QString::number(1.5 * k, 'g', 15));
        }
        copyIfMissing(QStringLiteral("Nur"), QStringLiteral("n"));
    }
    else if (modelKey == QStringLiteral("soil_nanshui"))
    {
        // 旧版导出固定 Nur=n。
        copyIfMissing(QStringLiteral("Nur"), QStringLiteral("n"));
    }
    else if (modelKey == QStringLiteral("soil_kg"))
    {
        // 旧版 UI 把加载/卸载参数合并为一组；升级时先复制，保持原计算结果。
        copyIfMissing(QStringLiteral("Kie(10^5Pa)"), QStringLiteral("Ki,Kie(10^5Pa)"));
        copyIfMissing(QStringLiteral("αke"), QStringLiteral("αk,αke"));
        copyIfMissing(QStringLiteral("Gie(10^5Pa)"), QStringLiteral("Gi,Gie(10^5Pa)"));
        copyIfMissing(QStringLiteral("αGe"), QStringLiteral("αG,αGe"));
    }
    else if (modelKey == QStringLiteral("huangjinping_bar_element"))
    {
        // 旧版锚杆把 E1/E2、Rc/Rt/Ryc/Ryt 分别绑成同一个输入值。
        copyIfMissing(QStringLiteral("屈服后变模E2（GPa）"),
                      QStringLiteral("变模E1,E2（GPa）"));
        copyIfMissing(QStringLiteral("极限抗压强度Rc（MPa）"),
                      QStringLiteral("抗拉（压）强度ft（MPa）"));
        copyIfMissing(QStringLiteral("屈服抗拉强度Ryt（MPa）"),
                      QStringLiteral("抗拉（压）强度ft（MPa）"));
        copyIfMissing(QStringLiteral("屈服抗压强度Ryc（MPa）"),
                      QStringLiteral("抗拉（压）强度ft（MPa）"));
    }
}

bool defaultMaterialNameIndex(const QString& name, int* index)
{
    const QString trimmed = name.trimmed();
    const QString prefix = QStringLiteral("材料");
    if (!trimmed.startsWith(prefix))
        return false;

    bool ok = false;
    const int value = trimmed.mid(prefix.size()).toInt(&ok);
    if (!ok || value <= 0)
        return false;

    if (index)
        *index = value;
    return true;
}

QString compactMaterialLabel(int matype, const MaterialEntry& entry)
{
    const QString name = entry.name.trimmed().isEmpty() ? QStringLiteral("未命名材料")
                                                        : entry.name.trimmed();
    return QStringLiteral("编号 %1  %2").arg(matype).arg(name);
}

struct ReferenceTableData
{
    QString modelName;
    QString tableName;
    QStringList columns;
    QVector<QStringList> rows;
};

QVector<ReferenceTableData> buildMaterialReferenceTables()
{
    QVector<ReferenceTableData> tables;
    ReferenceTableData t;

    t = {};
    t.modelName = QStringLiteral("围岩开挖模型");
    t.tableName = QStringLiteral("样表1 黄金坪地下厂房各向同性岩体物理力学参数表");
    t.columns << "岩体代号"
              << "序号"
              << "母岩编号"
              << "ITAG_EX"
              << "半开挖区荷载释放系数"
              << "再生时对应开挖级"
              << "E(GPa)"
              << "μ"
              << "容重"
              << "内摩擦系数"
              << "凝聚力C'"
              << "α0"
              << "d0";
    t.rows.append(QStringList() << "II" << "3" << "3" << "0" << "0" << "0" << "16.25" << "0.23" << "2.7" << "1.23" << "1.58" << "1.0" << "3.0");
    t.rows.append(QStringList() << "III" << "4" << "4" << "0" << "0" << "0" << "9.00" << "0.25" << "2.65" << "1.05" << "1.13" << "1.0" << "5.0");
    t.rows.append(QStringList() << "IV" << "5" << "5" << "0" << "0" << "0" << "5.75" << "0.30" << "2.65" << "0.85" << "0.78" << "1.0" << "7.0");
    t.rows.append(QStringList() << "V" << "6" << "6" << "0" << "0" << "0" << "2.00" << "0.35" << "2.60" << "0.613" << "0.35" << "1.0" << "9.0");
    t.rows.append(QStringList() << "f10-14" << "7" << "7" << "0" << "0" << "0" << "0.2" << "0.35" << "2.23" << "0.35" << "0.05" << "1.0" << "9.0");
    t.rows.append(QStringList() << "裂隙" << "8" << "8" << "0" << "0" << "0" << "3.0" << "0.38" << "1.5" << "0.50" << "0.30" << "1.0" << "9.0");
    t.rows.append(QStringList() << "裂隙密集带" << "10" << "10" << "0" << "0" << "0" << "2.00" << "0.35" << "2.60" << "0.613" << "0.35" << "1.0" << "9.0");
    t.rows.append(QStringList() << "II" << "13" << "3" << "-1" << "0" << "0" << "16.25" << "0.23" << "2.7" << "1.23" << "1.58" << "0.0" << "0.0");
    t.rows.append(QStringList() << "III" << "14" << "4" << "-1" << "0" << "0" << "9.00" << "0.25" << "2.65" << "1.05" << "1.13" << "0.0" << "0.0");
    t.rows.append(QStringList() << "IV" << "15" << "5" << "-1" << "0" << "0" << "5.75" << "0.30" << "2.65" << "0.85" << "0.78" << "0.0" << "0.0");
    t.rows.append(QStringList() << "V" << "16" << "6" << "-1" << "0" << "0" << "2.00" << "0.35" << "2.60" << "0.613" << "0.35" << "0.0" << "0.0");
    t.rows.append(QStringList() << "f10-14" << "17" << "7" << "-1" << "0" << "0" << "0.2" << "0.35" << "2.23" << "0.35" << "0.05" << "0.0" << "0.0");
    t.rows.append(QStringList() << "裂隙" << "18" << "8" << "-1" << "0" << "0" << "3.0" << "0.38" << "1.5" << "0.50" << "0.30" << "0.0" << "0.0");
    t.rows.append(QStringList() << "裂隙密集带" << "19" << "9" << "-1" << "0" << "0" << "5.75" << "0.30" << "2.65" << "0.85" << "0.78" << "0.0" << "0.0");
    t.rows.append(QStringList() << "尾水连接管（III）" << "23" << "4" << "-2" << "0.3213" << "0" << "9.00" << "0.25" << "2.65" << "1.05" << "1.13" << "1.0" << "5.0");
    t.rows.append(QStringList() << "母线洞（III）" << "24" << "4" << "-2" << "0.3213" << "0" << "9.00" << "0.25" << "2.65" << "1.05" << "1.13" << "1.0" << "5.0");
    t.rows.append(QStringList() << "f10-14过尾水连接管" << "27" << "7" << "-2" << "0.3213" << "0" << "0.2" << "0.35" << "2.23" << "0.35" << "0.05" << "1.0" << "9.0");
    t.rows.append(QStringList() << "裂隙过母线洞" << "28" << "8" << "-2" << "0.3213" << "0" << "5.75" << "0.30" << "2.65" << "0.85" << "0.78" << "1.0" << "7.0");
    t.rows.append(QStringList() << "裂隙过尾水连接管" << "29" << "9" << "-2" << "0.3213" << "0" << "5.75" << "0.30" << "2.65" << "0.85" << "0.78" << "1.0" << "7.0");
    t.rows.append(QStringList() << "吊车梁岩体（母岩为III）" << "43" << "4" << "2" << "0" << "-4" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    t.rows.append(QStringList() << "喷C25砼" << "44" << "4" << "2" << "0" << "0" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    t.rows.append(QStringList() << "母线洞喷C25砼" << "45" << "4" << "2" << "0" << "0" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    t.rows.append(QStringList() << "裂隙过吊车梁" << "46" << "8" << "2" << "0" << "-4" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    t.rows.append(QStringList() << "f10-14过喷C25砼" << "47" << "7" << "2" << "0" << "0" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    t.rows.append(QStringList() << "裂隙过喷C25砼" << "48" << "8" << "2" << "0" << "0" << "22.4" << "0.167" << "2.4" << "1.4" << "3.9" << "0.0" << "0.0");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("E-B本构");
    t.tableName = QStringLiteral("样表2 土体E-B模型参数(IB=-1)");
    t.columns << "材料" << "编号" << "干容重" << "天然容重" << "饱和容重" << "φ0" << "Δφ" << "c(KPa)"
              << "Rf"
              << "K"
              << "N"
              << "Kur"
              << "Nur"
              << "Kb"
              << "m";
    t.rows.append(QStringList() << "2A垫层" << "7" << "2.209" << "2.29" << "2.397" << "51.6" << "5.7" << "231.4" << "0.69" << "1082" << "0.27" << "2164" << "0.27" << "545" << "0.25");
    t.rows.append(QStringList() << "3A过渡料" << "8" << "2.13" << "2.22" << "2.334" << "53.8" << "8.8" << "236.6" << "0.75" << "968" << "0.23" << "1936" << "0.23" << "328" << "0.17");
    t.rows.append(QStringList() << "3B堆石" << "9" << "2.077" << "2.14" << "2.284" << "52.9" << "9.3" << "196.6" << "0.76" << "985" << "0.28" << "1722" << "0.28" << "238" << "0.21");
    t.rows.append(QStringList() << "1B弃渣盖重" << "10" << "2.08" << "2.19" << "2.3" << "38.3" << "3.5" << "0.0" << "0.75" << "695" << "0.29" << "1390" << "0.29" << "352" << "0.29");
    t.rows.append(QStringList() << "1A铺盖" << "12" << "1.42" << "1.71" << "2.3" << "31.0" << "0.0" << "0.0" << "0.72" << "350" << "0.43" << "700" << "0.43" << "350" << "0.18");
    t.rows.append(QStringList() << "2B特殊垫层" << "13" << "2.218" << "2.30" << "2.396" << "42.6" << "4.5" << "85" << "0.71" << "832" << "0.35" << "1664" << "0.35" << "470" << "0.39");
    t.rows.append(QStringList() << "3C反滤料" << "16" << "2.12" << "2.17" << "2.20" << "48" << "8.0" << "0.0" << "0.68" << "1035" << "0.33" << "2070" << "0.33" << "526" << "0.24");
    t.rows.append(QStringList() << "P下游砌石" << "17" << "2.01" << "2.07" << "2.17" << "51.8" << "9.8" << "0.0" << "0.78" << "850" << "0.53" << "1700" << "0.53" << "325" << "0.33");
    t.rows.append(QStringList() << "①gl+fglQ3" << "21" << "2.09" << "2.119" << "2.3" << "42.6" << "4.5" << "85" << "0.7" << "752" << "0.29" << "1504" << "0.29" << "276" << "0.29");
    t.rows.append(QStringList() << "④ColQ4" << "22" << "2.1" << "2.19" << "2.3" << "38.3" << "3.5" << "0.0" << "0.75" << "695" << "0.29" << "1390" << "0.29" << "352" << "0.29");
    t.rows.append(QStringList() << "③Col+dlQ4" << "23" << "1.42" << "1.71" << "1.9" << "31.0" << "0" << "0.0" << "0.72" << "350" << "0.43" << "700" << "0.43" << "350" << "0.18");
    t.rows.append(QStringList() << "④alQ4" << "24" << "2.09" << "2.119" << "2.3" << "42.6" << "4.5" << "85" << "0.7" << "752" << "0.29" << "1504" << "0.29" << "276" << "0.29");
    t.rows.append(QStringList() << "泥皮" << "26" << "1.67" << "1.67" << "1.67" << "10.0" << "0.0" << "50" << "0.86" << "1400" << "0.66" << "2000" << "0.66" << "600" << "0.26");
    t.rows.append(QStringList() << "el+dlQ4" << "28" << "2.12" << "2.22" << "2.3" << "40.2" << "4.0" << "0.0" << "0.73" << "760" << "0.27" << "1520" << "0.27" << "405" << "0.23");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("坝料动力本构");
    t.tableName = QStringLiteral("样表3 坝料动力参数(IB=-4)");
    t.columns << "材料"
              << "编号"
              << "静力编号"
              << "液化"
              << "DRYK1"
              << "DRYN1"
              << "FAI"
              << "CO"
              << "PU"
              << "DRYDAM"
              << "R1(0.1)"
              << "R2(0.3)"
              << "R3(0.5)"
              << "Kv"
              << "Kp"
              << "nv"
              << "np";
    t.rows.append(QStringList() << "2A垫层" << "81" << "7" << "0" << "970" << "0.43" << "51.5" << "100" << "0.35" << "0.25" << "150" << "150" << "150" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "3A过渡料" << "82" << "8" << "1" << "1000" << "0.45" << "52" << "200" << "0.34" << "0.22" << "180" << "180" << "180" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "3B堆石" << "83" << "9" << "0" << "830" << "0.46" << "48" << "300" << "0.32" << "0.22" << "170" << "170" << "170" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "1B弃渣盖重" << "84" << "10" << "0" << "750" << "0.40" << "38.3" << "100" << "0.36" << "0.26" << "170" << "170" << "170" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "1A铺盖" << "85" << "12" << "0" << "750" << "0.40" << "31.0" << "200" << "0.36" << "0.26" << "170" << "170" << "170" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "2B特殊垫层" << "86" << "13" << "0" << "970" << "0.43" << "49" << "300" << "0.35" << "0.25" << "150" << "150" << "150" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "3C反滤料" << "87" << "16" << "0" << "820" << "0.45" << "48" << "100" << "0.35" << "0.24" << "160" << "160" << "160" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "P下游砌石" << "88" << "17" << "0" << "800" << "0.46" << "51.8" << "200" << "0.32" << "0.24" << "190" << "190" << "190" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "gl+fglQ3" << "89" << "21" << "0" << "850" << "0.44" << "43.5" << "300" << "0.33" << "0.24" << "160" << "160" << "160" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "col+dlQ4(⑤-2)" << "90" << "22" << "0" << "450" << "0.41" << "38.3" << "100" << "0.42" << "0.27" << "140" << "140" << "140" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "lQ4" << "91" << "23" << "1" << "740" << "0.43" << "31.0" << "200" << "0.38" << "0.25" << "150" << "150" << "150" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "colQ3" << "92" << "24" << "1" << "850" << "0.44" << "43.5" << "300" << "0.33" << "0.24" << "160" << "160" << "160" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "泥皮" << "93" << "26" << "0" << "380" << "0.43" << "10.0" << "50" << "0.44" << "0.31" << "180" << "180" << "180" << "1000" << "800" << "0.5" << "0.8");
    t.rows.append(QStringList() << "el+dlQ4" << "94" << "28" << "0" << "450" << "0.41" << "40.2" << "80" << "0.42" << "0.27" << "140" << "140" << "140" << "1000" << "800" << "0.5" << "0.8");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("E-μ本构");
    t.tableName = QStringLiteral("样表5 土体E-μ模型参数(IB=-3)");
    t.columns << "材料" << "编号" << "干容重" << "天然容重" << "饱和容重" << "φ0" << "Δφ" << "c" << "Rf" << "K" << "n" << "Kur" << "Nur" << "G" << "D" << "F";
    t.rows.append(QStringList() << "上游铺盖（回填石渣料）" << "4" << "1.8" << "2.0" << "2.2" << "35" << "0" << "0.0" << "0.75" << "500" << "0.4" << "750" << "0.4" << "0.38" << "2.7" << "0.15");
    t.rows.append(QStringList() << "上游堆石料" << "5" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.1");
    t.rows.append(QStringList() << "上游过渡层" << "6" << "2.10" << "2.15" << "2.20" << "33" << "0" << "0" << "0.78" << "900" << "0.3" << "1350" << "0.3" << "0.42" << "11" << "0.08");
    t.rows.append(QStringList() << "沥青砼心墙" << "7" << "2.10" << "2.35" << "2.40" << "24" << "0" << "120" << "0.82" << "600" << "0.4" << "900" << "0.4" << "0.42" << "7" << "0.08");
    t.rows.append(QStringList() << "下游过渡层" << "9" << "2.10" << "2.15" << "2.20" << "33" << "0" << "0" << "0.78" << "900" << "0.3" << "1350" << "0.3" << "0.42" << "11" << "0.08");
    t.rows.append(QStringList() << "下游堆石料" << "10" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.1");
    t.rows.append(QStringList() << "上游1级找平堆石" << "15" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.1");
    t.rows.append(QStringList() << "下游1级找平堆石" << "16" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.1");
    t.rows.append(QStringList() << "K5沉积粘土" << "42" << "1.85" << "1.90" << "2.00" << "16.4" << "0.0" << "21.8" << "0.87" << "270" << "0.47" << "405" << "0.47" << "0.46" << "0.05" << "0.58");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("E-μ破碎本构");
    t.tableName = QStringLiteral("样表15 土体E-μ破碎模型参数(IB=-6)");
    t.columns << "材料" << "编号" << "干容重" << "天然容重" << "饱和容重" << "φ0" << "Δφ" << "c" << "Rf" << "K" << "n" << "Kur" << "Nur" << "C1" << "n1" << "C2" << "n2" << "C3" << "n3";
    t.rows.append(QStringList() << "上游铺盖（回填石渣料）" << "4" << "1.8" << "2.0" << "2.2" << "35" << "0" << "0.0" << "0.75" << "500" << "0.4" << "750" << "0.4" << "0.38" << "2.7" << "0.38" << "2.7" << "0.38" << "2.7");
    t.rows.append(QStringList() << "上游堆石料" << "5" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.37" << "11" << "0.37" << "11");
    t.rows.append(QStringList() << "上游过渡层" << "6" << "2.10" << "2.15" << "2.20" << "33" << "0" << "0" << "0.78" << "900" << "0.3" << "1350" << "0.3" << "0.42" << "11" << "0.42" << "11" << "0.42" << "11");
    t.rows.append(QStringList() << "沥青砼心墙" << "7" << "2.10" << "2.35" << "2.40" << "24" << "0" << "120" << "0.82" << "600" << "0.4" << "900" << "0.4" << "0.42" << "7" << "0.42" << "7" << "0.42" << "7");
    t.rows.append(QStringList() << "下游过渡层" << "9" << "2.10" << "2.15" << "2.20" << "33" << "0" << "0" << "0.78" << "900" << "0.3" << "1350" << "0.3" << "0.42" << "11" << "0.42" << "11" << "0.42" << "11");
    t.rows.append(QStringList() << "下游堆石料" << "10" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.37" << "11" << "0.37" << "11");
    t.rows.append(QStringList() << "上游1级找平堆石" << "15" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.37" << "11" << "0.37" << "11");
    t.rows.append(QStringList() << "下游1级找平堆石" << "16" << "2.0" << "2.1" << "2.2" << "35" << "0" << "0.0" << "0.8" << "1000" << "0.3" << "1500" << "0.3" << "0.37" << "11" << "0.37" << "11" << "0.37" << "11");
    t.rows.append(QStringList() << "K5沉积粘土" << "42" << "1.85" << "1.90" << "2.00" << "16.4" << "0.0" << "21.8" << "0.87" << "270" << "0.47" << "405" << "0.47" << "0.46" << "0.05" << "0.46" << "0.05" << "0.46" << "0.05");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("锚杆单元模型");
    t.tableName = QStringLiteral("样表6 杆单元参数（黄金坪地下厂房）");
    t.columns << "杆单元类型" << "序号" << "E1" << "E2" << "容重" << "面积" << "Rt" << "Rc" << "Ryt" << "Ryc" << "间距" << "排距" << "预应力T" << "预应系数";
    t.rows.append(QStringList() << "15m预应力锚索1500KN虚拟杆面积" << "20" << "206" << "206" << "7.80" << "0.002" << "1800" << "1800" << "1800" << "1800" << "4.5" << "4.5" << "1500.0" << "1.0");
    t.rows.append(QStringList() << "8m/9m普通锚杆φ32" << "21" << "206" << "206" << "7.80" << "0.000804" << "390" << "390" << "390" << "390" << "1.8" << "1.8" << "0.0" << "1.0");
    t.rows.append(QStringList() << "6m/7m普通锚杆φ28" << "22" << "206" << "206" << "7.80" << "0.000616" << "390" << "390" << "390" << "390" << "1.8" << "1.8" << "0.0" << "0.85");
    t.rows.append(QStringList() << "8m/9m普通锚杆φ32" << "23" << "206" << "206" << "7.80" << "0.000804" << "390" << "390" << "390" << "390" << "1.5" << "1.5" << "0.0" << "0.85");
    t.rows.append(QStringList() << "15m预应力锚索1500KN虚拟杆面积" << "24" << "206" << "206" << "7.80" << "0.002" << "1800" << "1800" << "1800" << "1800" << "4.0" << "4.0" << "1500" << "0.70");
    t.rows.append(QStringList() << "20m预应力锚索1750KN虚拟杆面积" << "25" << "206" << "206" << "7.80" << "0.002" << "1800" << "1800" << "1800" << "1800" << "4.0" << "4.0" << "1750" << "0.9");
    t.rows.append(QStringList() << "45m对穿预应力锚索1750KN虚拟杆面积" << "26" << "206" << "206" << "7.80" << "0.002" << "1800" << "1800" << "1800" << "1800" << "4.0" << "4.0" << "1750" << "0.9");
    t.rows.append(QStringList() << "6m/7m普通锚杆φ28间排距1.5m" << "27" << "206" << "206" << "7.80" << "0.000616" << "390" << "390" << "390" << "390" << "1.5" << "1.5" << "0.0" << "0.9");
    t.rows.append(QStringList() << "4.5m/5m普通锚杆φ25" << "28" << "206" << "206" << "7.80" << "0.000491" << "390" << "390" << "390" << "390" << "1.5" << "1.5" << "0.0" << "0.0");
    t.rows.append(QStringList() << "20m预应力锚索1750KN虚拟杆面积" << "29" << "206" << "206" << "7.80" << "0.002" << "1800" << "1800" << "1800" << "1800" << "4.5" << "4.5" << "1750" << "1.0");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("摩尔-库仑本构");
    t.tableName = QStringLiteral("样表7 大岗山高拱坝各向同性岩体物理力学参数表");
    t.columns << "岩体代号" << "序号" << "E" << "μ" << "容重" << "内摩擦系数" << "凝聚力C'";
    t.rows.append(QStringList() << "空化" << "1" << "0.001" << "0.3" << "0.0" << "1.7" << "200.0");
    t.rows.append(QStringList() << "alQ" << "2" << "0.5" << "0.4" << "1.5" << "0.8" << "0.05");
    t.rows.append(QStringList() << "坝砼" << "3" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "置换混凝土" << "4" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "垫座" << "5" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "β5（F1）" << "6" << "7.5" << "0.3" << "2.62" << "1.0" << "1.0");
    t.rows.append(QStringList() << "β6" << "7" << "21.5" << "0.25" << "2.65" << "1.3" << "2.0");
    t.rows.append(QStringList() << "β9" << "8" << "7.5" << "0.3" << "2.62" << "1.0" << "1.0");
    t.rows.append(QStringList() << "β11" << "9" << "7.5" << "0.3" << "2.62" << "1.0" << "1.0");
    t.rows.append(QStringList() << "γL5" << "11" << "7.5" << "0.3" << "2.62" << "1.0" << "1.0");
    t.rows.append(QStringList() << "左侧坝身孔口与坝体连接单元" << "17" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "右侧坝身孔口与坝体连接单元" << "18" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "拱坝/基础退化砼" << "19" << "24.0" << "0.167" << "2.4" << "1.4" << "2.0");
    t.rows.append(QStringList() << "Ⅱ" << "20" << "21.5" << "0.25" << "2.65" << "1.3" << "2.0");
    t.rows.append(QStringList() << "Ⅲ1" << "21" << "10" << "0.27" << "2.62" << "1.2" << "1.5");
    t.rows.append(QStringList() << "Ⅲ2" << "22" << "7.5" << "0.3" << "2.62" << "1.0" << "1.0");
    t.rows.append(QStringList() << "Ⅳ" << "23" << "3.0" << "0.35" << "2.58" << "0.8" << "0.7");
    t.rows.append(QStringList() << "Ⅴ" << "24" << "0.38" << "0.35" << "2.45" << "0.5" << "0.2");
    t.rows.append(QStringList() << "β21" << "25" << "3.0" << "0.35" << "2.58" << "0.5" << "0.08");
    t.rows.append(QStringList() << "β28" << "26" << "7.5" << "0.3" << "2.62" << "0.4" << "0.03");
    t.rows.append(QStringList() << "β4(f5)" << "27" << "7.5" << "0.3" << "2.62" << "0.35" << "0.05");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("南水非线性本构");
    t.tableName = QStringLiteral("样表9 土体南水模型参数（IB=-5）");
    t.columns << "材料" << "编号" << "干容重" << "天然容重" << "饱和容重" << "φ0" << "Δφ" << "c" << "Rf" << "K" << "n" << "Kur" << "Nur" << "cd" << "nd" << "Rd";
    t.rows.append(QStringList() << "垫层料" << "7" << "2.10" << "2.15" << "2.20" << "46.0" << "6.0" << "0.0" << "0.79" << "867" << "0.39" << "1133" << "0.39" << "0.0043" << "0.62" << "0.72");
    t.rows.append(QStringList() << "过渡料" << "8" << "2.10" << "2.15" << "2.20" << "50.0" << "8.0" << "0.0" << "0.75" << "683" << "0.31" << "1000" << "0.31" << "0.0060" << "0.78" << "0.70");
    t.rows.append(QStringList() << "堆石料" << "9" << "2.00" << "2.10" << "2.20" << "48.0" << "9.0" << "0.0" << "0.77" << "500" << "0.20" << "750" << "0.20" << "0.0100" << "0.75" << "0.75");
    t.rows.append(QStringList() << "弱风化料" << "10" << "1.90" << "2.05" << "2.20" << "48.0" << "12.0" << "0.0" << "0.80" << "320" << "0.20" << "480" << "0.20" << "0.0150" << "0.56" << "0.80");
    t.rows.append(QStringList() << "强风化料" << "11" << "1.80" << "2.00" << "2.20" << "43.0" << "11.0" << "0.0" << "0.63" << "120" << "0.37" << "180" << "0.37" << "0.0200" << "0.25" << "0.63");
    t.rows.append(QStringList() << "超径石" << "12" << "2.00" << "2.10" << "2.20" << "48.0" << "9.0" << "0.0" << "0.77" << "500" << "0.20" << "750" << "0.20" << "0.0100" << "0.75" << "0.75");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("K-G本构");
    t.tableName = QStringLiteral("样表10 土体科大K-G模型参数(IB=-2)");
    t.columns << "材料" << "编号" << "干容重" << "天然容重" << "饱和容重" << "Ki" << "αk" << "Gi" << "αG" << "βG" << "a" << "b" << "Kie" << "αke" << "Gie" << "αGe";
    t.rows.append(QStringList() << "覆盖层3" << "4" << "1.8" << "2.0" << "2.2" << "168" << "68" << "114" << "115" << "-60" << "190" << "1.92" << "168" << "68" << "114" << "115");
    t.rows.append(QStringList() << "覆盖层2" << "5" << "2.0" << "2.1" << "2.2" << "33" << "70" << "22" << "64" << "-35" << "63" << "1.83" << "33" << "70" << "22" << "64");
    t.rows.append(QStringList() << "心墙掺砾料" << "6" << "2.10" << "2.15" << "2.20" << "80" << "94" << "106" << "112" << "-80" << "132" << "1.40" << "80" << "94" << "106" << "112");
    t.rows.append(QStringList() << "反滤层1" << "7" << "2.10" << "2.35" << "2.40" << "168" << "110" << "114" << "194" << "-97" << "118" << "2.00" << "168" << "110" << "114" << "194");
    t.rows.append(QStringList() << "反滤层2" << "9" << "2.10" << "2.15" << "2.20" << "170" << "110" << "104" << "187" << "-90" << "116" << "2.08" << "170" << "110" << "104" << "187");
    t.rows.append(QStringList() << "过渡层" << "10" << "2.0" << "2.1" << "2.2" << "230" << "75" << "100" << "133" << "-70" << "140" << "1.90" << "230" << "75" << "100" << "133");
    t.rows.append(QStringList() << "主堆石" << "15" << "2.0" << "2.1" << "2.2" << "300" << "41" << "120" << "108" << "-60" << "200" << "1.80" << "300" << "41" << "120" << "108");
    tables.append(t);

    return tables;
}

QVector<ReferenceTableData> buildSeepageReferenceTables()
{
    QVector<ReferenceTableData> tables;
    ReferenceTableData t;
    t.modelName = QStringLiteral("稳态渗流");
    t.tableName = QStringLiteral("样表4 剑科心墙堆石坝各向同性稳态渗流参数");
    t.columns << "材料" << "编号" << "K(Lu)" << "允许比降";
    t.rows.append(QStringList() << "alQ3" << "3" << "35000.0" << "0.09");
    t.rows.append(QStringList() << "alQ4" << "4" << "35000.0" << "0.09");
    t.rows.append(QStringList() << "Col+dlQ4" << "5" << "4500.0" << "0.14");
    t.rows.append(QStringList() << "垫层料" << "6" << "720.0" << "0.12");
    t.rows.append(QStringList() << "坝顶填筑" << "7" << "10.0" << "0.13");
    t.rows.append(QStringList() << "花岗岩堆石料(基准)" << "8" << "400.0" << "0.38");
    t.rows.append(QStringList() << "花岗岩堆石料(基准湿化)" << "27" << "400.0" << "0.38");
    t.rows.append(QStringList() << "防渗土料(基准)" << "9" << "3.0" << "50");
    t.rows.append(QStringList() << "防渗土料(基准湿化)" << "28" << "3.0" << "50");
    t.rows.append(QStringList() << "防渗土料(比较方案二)" << "62" << "3.0" << "50");
    t.rows.append(QStringList() << "反滤层1(基准)" << "10" << "543.0" << "0.15");
    t.rows.append(QStringList() << "反滤层1(基准湿化)" << "29" << "543.0" << "0.15");
    t.rows.append(QStringList() << "反滤层2(基准)" << "11" << "543.0" << "0.15");
    t.rows.append(QStringList() << "反滤层2(基准湿化)" << "30" << "543.0" << "0.15");
    t.rows.append(QStringList() << "过渡料(基准)" << "12" << "400.0" << "0.38");
    t.rows.append(QStringList() << "过渡料(基准湿化)" << "31" << "400.0" << "0.38");
    t.rows.append(QStringList() << "过渡料(比较方案一)" << "60" << "400.0" << "0.38");
    t.rows.append(QStringList() << "过渡料(比较方案二)" << "61" << "400.0" << "0.38");
    t.rows.append(QStringList() << "大块石护坡" << "18" << "400.0" << "0.38");
    t.rows.append(QStringList() << "大块石填筑" << "19" << "400.0" << "0.38");
    t.rows.append(QStringList() << "弃渣盖重" << "22" << "400.0" << "0.38");
    t.rows.append(QStringList() << "高塑性粘土" << "24" << "1.0" << "50");
    t.rows.append(QStringList() << "粘土/砼接触面泥皮" << "25" << "1.0" << "50");
    t.rows.append(QStringList() << "T3x新鲜" << "13" << "3.0" << "50");
    t.rows.append(QStringList() << "T3x弱卸荷" << "14" << "10.0" << "0.13");
    t.rows.append(QStringList() << "T3x强卸荷" << "15" << "40.0" << "0.13");
    t.rows.append(QStringList() << "γδ53新鲜" << "16" << "3.0" << "50");
    t.rows.append(QStringList() << "γδ53弱卸荷" << "17" << "10.0" << "0.13");
    t.rows.append(QStringList() << "砼" << "20" << "0.01" << "180");
    t.rows.append(QStringList() << "帷幕/基岩接触面" << "21" << "3.0" << "50");
    t.rows.append(QStringList() << "挤压带" << "23" << "50" << "40");
    t.rows.append(QStringList() << "帷幕" << "26" << "3.00" << "18");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("三参数流变");
    t.tableName = QStringLiteral("样表11 沈珠江三参数流变模型参数表");
    t.columns << "岩体代号" << "序号" << "α" << "b" << "d";
    t.rows.append(QStringList() << "垫层料" << "7" << "0.004" << "0.0009" << "0.007");
    t.rows.append(QStringList() << "过渡料" << "8" << "0.004" << "0.0009" << "0.007");
    t.rows.append(QStringList() << "堆石料" << "9" << "0.004" << "0.0009" << "0.007");
    t.rows.append(QStringList() << "弱风化料" << "10" << "0.005" << "0.0012" << "0.008");
    t.rows.append(QStringList() << "强风化料" << "11" << "0.006" << "0.0015" << "0.009");
    t.rows.append(QStringList() << "超径石" << "12" << "0.004" << "0.0009" << "0.007");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("七参数流变");
    t.tableName = QStringLiteral("样表12 李国英七参数流变模型参数表");
    t.columns << "岩体代号" << "序号" << "α" << "b" << "c" << "d" << "m1" << "m2" << "m3";
    t.rows.append(QStringList() << "主堆石3B-I" << "7" << "0.006" << "0.0006" << "0.0002" << "0.004" << "0.769" << "0.635" << "0.550");
    t.rows.append(QStringList() << "主堆石3B-II" << "8" << "0.007" << "0.003" << "0.0001" << "0.0025" << "0.770" << "0.631" << "0.530");
    t.rows.append(QStringList() << "次堆石3C" << "9" << "0.005" << "0.0007" << "0.0004" << "0.0043" << "0.802" << "0.664" << "0.717");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("九参数流变");
    t.tableName = QStringLiteral("样表13 九参数流变模型参数表");
    t.columns << "岩体代号"
              << "序号"
              << "λv"
              << "Cα"
              << "Cβ"
              << "dα"
              << "dβ"
              << "c"
              << "d"
              << "η"
              << "m";
    t.rows.append(QStringList() << "主堆石3B-II" << "10" << "0.0678" << "0.4445" << "0.4360" << "2.0827" << "1.6383" << "0.2892" << "0.8465" << "0.0831" << "0.3899");
    t.rows.append(QStringList() << "次堆石3C" << "11" << "0.0851" << "0.3808" << "0.1936" << "0.6521" << "1.0061" << "1.4296" << "0.2852" << "0.1116" << "1.0384");
    tables.append(t);

    t = {};
    t.modelName = QStringLiteral("六参数流变");
    t.tableName = QStringLiteral("样表14 西源流变模型参数表");
    t.columns << "岩体代号" << "序号" << "E1" << "E2" << "μ1" << "μ2" << "η1" << "η2";
    t.rows.append(QStringList() << "主堆石3B-II" << "12" << "1.5" << "2.0" << "0.3" << "0.4" << "0.5" << "0.6");
    t.rows.append(QStringList() << "次堆石3C" << "13" << "1.5" << "2.0" << "0.3" << "0.4" << "0.5" << "0.6");
    tables.append(t);

    return tables;
}

void showReferenceListDialog(const QString& title, const QVector<ReferenceTableData>& items,
                             QWidget* parent)
{
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(REFERENCE_DIALOG_WIDTH, REFERENCE_DIALOG_HEIGHT);

    auto* layout = new QVBoxLayout(dialog);
    auto* splitter = new QSplitter(Qt::Horizontal, dialog);
    auto* list = new QListWidget(splitter);
    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    auto* tableName = new QLabel(right);
    auto* viewer = new QTableWidget(right);
    viewer->setEditTriggers(QAbstractItemView::AllEditTriggers);
    viewer->setSelectionBehavior(QAbstractItemView::SelectRows);
    viewer->setSelectionMode(QAbstractItemView::ExtendedSelection);
    viewer->verticalHeader()->setVisible(false);
    viewer->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rightLayout->addWidget(tableName);
    rightLayout->addWidget(viewer);

    auto* copyShortcut = new QShortcut(QKeySequence::Copy, viewer);
    QObject::connect(copyShortcut, &QShortcut::activated, viewer,
                     [viewer]()
                     {
                         const QModelIndexList selected = viewer->selectionModel()->selectedIndexes();
                         if (selected.isEmpty())
                         {
                             return;
                         }

                         int minRow = selected.first().row();
                         int maxRow = selected.first().row();
                         int minCol = selected.first().column();
                         int maxCol = selected.first().column();
                         for (const QModelIndex& index : selected)
                         {
                             minRow = qMin(minRow, index.row());
                             maxRow = qMax(maxRow, index.row());
                             minCol = qMin(minCol, index.column());
                             maxCol = qMax(maxCol, index.column());
                         }

                         QString text;
                         for (int r = minRow; r <= maxRow; ++r)
                         {
                             QStringList line;
                             for (int c = minCol; c <= maxCol; ++c)
                             {
                                 QTableWidgetItem* item = viewer->item(r, c);
                                 line << (item ? item->text() : QString());
                             }
                             text += line.join('\t');
                             if (r < maxRow)
                             {
                                 text += '\n';
                             }
                         }
                         QGuiApplication::clipboard()->setText(text);
                     });

    for (const auto& item : items)
    {
        list->addItem(item.modelName);
    }

    QObject::connect(list, &QListWidget::currentRowChanged, dialog,
                     [viewer, tableName, items](int row)
                     {
                         if (row >= 0 && row < items.size())
                         {
                             const auto& data = items[row];
                             tableName->setText(data.tableName);
                             viewer->setColumnCount(data.columns.size());
                             viewer->setHorizontalHeaderLabels(data.columns);
                             viewer->setRowCount(data.rows.size());
                             for (int r = 0; r < data.rows.size(); ++r)
                             {
                                 const QStringList& vals = data.rows[r];
                                 for (int c = 0; c < data.columns.size(); ++c)
                                 {
                                     viewer->setItem(
                                         r, c,
                                         new QTableWidgetItem(c < vals.size() ? vals[c] : QString()));
                                 }
                             }
                         }
                     });

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 9);
    layout->addWidget(splitter);

    if (!items.isEmpty())
    {
        list->setCurrentRow(0);
    }

    dialog->show();
}

void syncAutoNumberProperties(MaterialModel* model, MaterialEntry* entry)
{
    if (!model || !entry)
    {
        return;
    }

    QList<int> matypes = entry->appliedMatypes.values();
    std::sort(matypes.begin(), matypes.end());

    QStringList tokens;
    for (int matype : matypes)
    {
        if (isComputableMatype(matype))
        {
            tokens.append(QString::number(matype));
        }
    }
    const QString value = tokens.join(QLatin1Char(','));

    for (const QString& column : model->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
        {
            entry->properties.insert(column, value);
        }
    }
}
} // namespace

MaterialManager::MaterialManager()
    : m_materialManagerWidget(nullptr), m_rootItem(nullptr)
{
    m_projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
    ServiceRegistry::instance().registerService<MaterialManagerService>(this);
    setupTreeWidget();
    m_seepageModels = createSeepageSubModels();
    loadDefaultLibrary();
    ensureEntryIds();

    EventManager::instance().registerHandler(
        QStringLiteral("mesh.groups.changed"), this,
        [this](const QVariantMap&)
        {
            onMeshGroupsChanged();
        });
}

BasePage* MaterialManager::getMaterialManagerWidget()
{
    if (!m_materialManagerWidget)
    {
        m_materialManagerWidget = new MaterialManagerWidget(this);
    }
    return m_materialManagerWidget;
}

void MaterialManager::newProject()
{
    const QString workPath =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    newProjectForPath(workPath);
}

bool MaterialManager::saveProject()
{
    if (m_workPath.isEmpty())
    {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("module"), QStringLiteral("Material"));

    QJsonArray modelArray;
    for (const auto& model : m_models)
    {
        QJsonObject modelObj;
        modelObj.insert(QStringLiteral("key"), model.key);
        modelObj.insert(QStringLiteral("display_name"), model.displayName);

        QJsonArray entryArray;
        for (const auto& entry : model.materials)
        {
            QJsonObject entryObj;
            entryObj.insert(QStringLiteral("id"), entry.id);
            entryObj.insert(QStringLiteral("name"), entry.name);

            QJsonObject propertiesObj;
            for (auto it = entry.properties.constBegin(); it != entry.properties.constEnd(); ++it)
            {
                propertiesObj.insert(it.key(), it.value());
            }
            entryObj.insert(QStringLiteral("properties"), propertiesObj);

            QList<int> matypes = entry.appliedMatypes.values();
            std::sort(matypes.begin(), matypes.end());
            QJsonArray appliedArray;
            for (int matype : matypes)
            {
                if (isComputableMatype(matype))
                    appliedArray.append(matype);
            }
            entryObj.insert(QStringLiteral("applied_matypes"), appliedArray);
            const bool hasValidTransition = isComputableMatype(entry.transitionMatype);
            entryObj.insert(QStringLiteral("transition_only"),
                            entry.transitionOnly && hasValidTransition);
            entryObj.insert(QStringLiteral("transition_matype"),
                            hasValidTransition ? entry.transitionMatype : -1);

            entryObj.insert(QStringLiteral("seepage_model_key"), entry.seepageModelKey);
            entryObj.insert(QStringLiteral("creep_model_key"), entry.creepModelKey);
            QJsonObject seepagePropertiesObj;
            for (auto it = entry.seepageProperties.constBegin();
                 it != entry.seepageProperties.constEnd(); ++it)
            {
                seepagePropertiesObj.insert(it.key(), it.value());
            }
            entryObj.insert(QStringLiteral("seepage_properties"), seepagePropertiesObj);
            entryArray.append(entryObj);
        }

        modelObj.insert(QStringLiteral("entries"), entryArray);
        modelArray.append(modelObj);
    }
    root.insert(QStringLiteral("models"), modelArray);

    QJsonArray addedEntryArray;
    const QList<QString> addedIds = m_addedEntryIds.values();
    for (const QString& id : addedIds)
    {
        addedEntryArray.append(id);
    }
    root.insert(QStringLiteral("added_entry_ids"), addedEntryArray);

    QJsonArray transitionArray;
    for (const auto& pair : std::as_const(m_materialTransitions))
    {
        QJsonObject row;
        row.insert(QStringLiteral("old"), pair.first);
        row.insert(QStringLiteral("new"), pair.second);
        transitionArray.append(row);
    }
    root.insert(QStringLiteral("material_transitions"), transitionArray);
    // 旧版全局材料变换数据继续保存用于工程迁移，但不再创建材料树节点。
    root.insert(QStringLiteral("show_transition_node"), false);

    QFile file(QDir(m_workPath).filePath(PublicConstants::kMaterialProjectFileName));
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool MaterialManager::loadProject()
{
    const QString workPath =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    newProjectForPath(workPath);

    if (m_workPath.isEmpty())
    {
        return false;
    }

    QFile file(QDir(m_workPath).filePath(PublicConstants::kMaterialProjectFileName));
    if (!file.open(QIODevice::ReadOnly))
    {
        return true;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.contains(QStringLiteral("models")))
    {
        const QVector<MaterialModel> defaults = createDefaultMaterialModels();
        m_models.clear();
        m_models.reserve(defaults.size());
        for (const auto& model : defaults)
        {
            MaterialModel copy = model;
            copy.materials.clear();
            m_models.append(copy);
        }

        auto findModelIndex = [this](const QString& key) -> int
        {
            for (int i = 0; i < m_models.size(); ++i)
            {
                if (m_models[i].key == key)
                {
                    return i;
                }
            }
            return -1;
        };

        const QJsonArray modelArray = root.value(QStringLiteral("models")).toArray();
        for (const auto& modelValue : modelArray)
        {
            const QJsonObject modelObj = modelValue.toObject();
            const QString key = modelObj.value(QStringLiteral("key")).toString();
            const QString displayName = modelObj.value(QStringLiteral("display_name")).toString();

            int modelIndex = findModelIndex(key);
            if (modelIndex < 0)
            {
                MaterialModel extra;
                extra.key = key;
                extra.displayName = displayName;
                modelIndex = m_models.size();
                m_models.append(extra);
            }

            const QJsonArray entries = modelObj.value(QStringLiteral("entries")).toArray();
            for (const auto& entryValue : entries)
            {
                const QJsonObject entryObj = entryValue.toObject();
                MaterialEntry entry;
                entry.id = entryObj.value(QStringLiteral("id")).toString();
                entry.name = entryObj.value(QStringLiteral("name")).toString();

                const QJsonObject properties = entryObj.value(QStringLiteral("properties")).toObject();
                for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
                {
                    // 旧工程曾把静力->动力关系误存为动力材料属性。新版本按工况保存，
                    // 加载时直接丢弃旧字段，避免它被当成“自定义列”重新加入材料模型。
                    if (key == QStringLiteral("dam_dynamic") &&
                        (it.key() == QStringLiteral("静力计算编号") ||
                         it.key() == QStringLiteral("静力母材料编号")))
                    {
                        continue;
                    }
                    entry.properties.insert(it.key(), it.value().toString());
                    if (!m_models[modelIndex].columns.contains(it.key()))
                    {
                        m_models[modelIndex].columns.append(it.key());
                    }
                }
                migrateLegacyMaterialProperties(key, &entry.properties);
                // 旧工程没有残余强度继承标志：旧格式残余值>0为独立设置，
                // 0代表沿用峰值。只对缺失标志迁移，不改变新版工程的真实零值。
                if (key == QStringLiteral("dagangshan_isotropic"))
                {
                    const auto migrateResidualFlag = [&entry](const QString& flag,
                                                               const QString& valueColumn)
                    {
                        if (entry.properties.contains(flag))
                            return;
                        bool ok = false;
                        const double value = entry.properties.value(valueColumn).toDouble(&ok);
                        entry.properties.insert(flag, (ok && value > 0.0)
                                                           ? QStringLiteral("0")
                                                           : QStringLiteral("1"));
                    };
                    migrateResidualFlag(QStringLiteral("残余摩擦系数沿用峰值"),
                                        QStringLiteral("残余抗剪断强度内摩擦系数"));
                    migrateResidualFlag(QStringLiteral("残余凝聚力沿用峰值"),
                                        QStringLiteral("残余凝聚力C‘（MPa）"));
                }
                for (const QString& column : m_models[modelIndex].columns)
                {
                    if (!entry.properties.contains(column))
                    {
                        entry.properties.insert(
                            column, MaterialManager::isAutoNumberColumn(column)
                                        ? QString()
                                        : defaultMaterialPropertyValue(key, column));
                    }
                }

                const QJsonArray applied = entryObj.value(QStringLiteral("applied_matypes")).toArray();
                for (const auto& matypeValue : applied)
                {
                    const int matype = matypeValue.toInt(-1);
                    if (isComputableMatype(matype))
                        entry.appliedMatypes.insert(matype);
                }
                const int transitionMatype =
                    entryObj.value(QStringLiteral("transition_matype")).toInt(-1);
                entry.transitionMatype =
                    isComputableMatype(transitionMatype) ? transitionMatype : -1;
                const bool supportsCalculationStage =
                    isDamStaticModel(key) || key == QStringLiteral("dam_dynamic");
                entry.transitionOnly = supportsCalculationStage &&
                                       entryObj.value(QStringLiteral("transition_only")).toBool(false) &&
                                       isComputableMatype(entry.transitionMatype);
                if (!entry.transitionOnly)
                    entry.transitionMatype = -1;

                const QString legacySupplementalKey =
                    entryObj.value(QStringLiteral("seepage_model_key")).toString();
                if (legacySupplementalKey == QStringLiteral("jk_corewall_steady_seepage") ||
                    legacySupplementalKey == QStringLiteral("jk_corewall_anisotropic_seepage"))
                    entry.seepageModelKey = legacySupplementalKey;
                else if (!legacySupplementalKey.isEmpty())
                    entry.creepModelKey = legacySupplementalKey;
                const QString storedCreepKey =
                    entryObj.value(QStringLiteral("creep_model_key")).toString();
                if (!storedCreepKey.isEmpty())
                    entry.creepModelKey = storedCreepKey;
                const QJsonObject seepageProperties =
                    entryObj.value(QStringLiteral("seepage_properties")).toObject();
                for (auto it = seepageProperties.constBegin(); it != seepageProperties.constEnd(); ++it)
                {
                    entry.seepageProperties.insert(it.key(), it.value().toString());
                }

                // 旧工程可能没有保存后来新增的高级参数。只补缺失项，不覆盖已有值。
                const auto ensureSupplementalDefaults =
                    [this, &entry](const QString& supplementalKey)
                    {
                        if (supplementalKey.isEmpty())
                            return;
                        for (const MaterialModel& supplemental : m_seepageModels)
                        {
                            if (supplemental.key != supplementalKey)
                                continue;
                            for (const QString& column : supplemental.columns)
                            {
                                if (isAutoNumberColumn(column) ||
                                    entry.seepageProperties.contains(column))
                                    continue;
                                entry.seepageProperties.insert(
                                    column, defaultMaterialPropertyValue(supplemental.key, column));
                            }
                            break;
                        }
                    };
                ensureSupplementalDefaults(entry.seepageModelKey);
                ensureSupplementalDefaults(entry.creepModelKey);

                syncAutoNumberProperties(&m_models[modelIndex], &entry);

                m_models[modelIndex].materials.append(entry);
            }
        }
    }

    m_addedEntryIds.clear();
    const QJsonArray addedArray = root.value(QStringLiteral("added_entry_ids")).toArray();
    for (const auto& idValue : addedArray)
    {
        const QString id = idValue.toString();
        if (!id.isEmpty())
        {
            m_addedEntryIds.insert(id);
        }
    }

    m_materialTransitions.clear();
    const QJsonArray transitionArray = root.value(QStringLiteral("material_transitions")).toArray();
    for (const auto& val : transitionArray)
    {
        const QJsonObject row = val.toObject();
        const int oldMatype = row.value(QStringLiteral("old")).toInt();
        const int newMatype = row.value(QStringLiteral("new")).toInt();
        if (isComputableMatype(oldMatype) && isComputableMatype(newMatype))
            m_materialTransitions.push_back({oldMatype, newMatype});
    }
    bool recoveredFromCalculationData = false;
    if (m_addedEntryIds.isEmpty())
    {
        const QString legacyImat = QDir(SolverFilePaths::projectInputDirectory(m_workPath))
                                       .filePath(QStringLiteral("IMAT.DAT"));
        int importedCount = 0;
        QString importError;
        if (QFileInfo::exists(legacyImat) &&
            importIMATFile(legacyImat, &importedCount, &importError) && importedCount > 0)
        {
            recoveredFromCalculationData = true;
            qInfo().noquote() << QStringLiteral("已从工程计算数据恢复 %1 个材料设置。")
                                     .arg(importedCount);
        }
        else if (!importError.isEmpty())
        {
            qWarning().noquote() << QStringLiteral("材料设置恢复失败：%1").arg(importError);
        }
    }

    ensureEntryIds();
    notifyLibraryChanged();
    // 工程打开时会在后台重建条目页面，避免把条目高亮残留到初始状态。
    clearAllMatypeHighlight();
    if (recoveredFromCalculationData && !saveProject())
        qWarning() << "failed to persist recovered material settings";
    return true;
}

void MaterialManager::addMaterialFromMenu(const QString& modelKey)
{
    const QString entryId = addMaterialToModel(modelKey, true);
    if (!entryId.isEmpty())
    {
        focusOnMaterial(entryId);
    }
}

void MaterialManager::showBuiltinMaterialListDialog()
{
    showReferenceListDialog(QStringLiteral("内置材料参数参考"),
                            buildMaterialReferenceTables(), m_materialManagerWidget);
}

void MaterialManager::showBuiltinSeepageListDialog()
{
    showReferenceListDialog(QStringLiteral("内置渗流参数参考"),
                            buildSeepageReferenceTables(), m_materialManagerWidget);
}

void MaterialManager::showMaterialTransitionDialog()
{
    if (!m_rootItem)
    {
        return;
    }

    ensureTransitionNode();

    m_transitionPage->reloadFromManager();
    if (m_projectManagerService)
        m_projectManagerService->setCurrentItem(m_transitionNode);
}

void MaterialManager::ensureTransitionNode()
{
    if (!m_rootItem)
    {
        return;
    }
    if (!m_transitionPage)
    {
        m_transitionPage = new MaterialTransitionPage(this);
    }
    if (!m_transitionNode)
    {
        m_transitionNode = new ProjectNode(QStringLiteral("材料变换"), QUuid::createUuid().toString(),
                                           m_transitionPage);
        m_transitionNode->setData(0, ProjectNode::RoleUuid, m_transitionNode->getUuid());
        m_transitionNode->setData(0, ProjectNode::RoleModule, QString("Material"));
        m_transitionNode->setData(0, ProjectNode::RoleKind, QString("MaterialTransition"));
        m_rootItem->addChild(m_transitionNode);
    }
    else
    {
        m_transitionNode->setText(0, QStringLiteral("材料变换"));
    }
}

QVector<QPair<int, int>> MaterialManager::materialTransitions() const
{
    return m_materialTransitions;
}

void MaterialManager::setMaterialTransitions(const QVector<QPair<int, int>>& transitions)
{
    m_materialTransitions.clear();
    for (const auto& pair : transitions)
    {
        if (isComputableMatype(pair.first) && isComputableMatype(pair.second))
            m_materialTransitions.push_back(pair);
    }
}

bool MaterialManager::hasConfiguredMaterials() const
{
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            if (!entry.appliedMatypes.isEmpty() ||
                (entry.transitionOnly && isComputableMatype(entry.transitionMatype)))
            {
                return true;
            }
        }
    }
    return false;
}

QString MaterialManager::addMaterialToModel(const QString& modelKey, bool addToProjectTree)
{
    MaterialModel* model = findModel(modelKey);
    if (!model)
    {
        return QString();
    }

    // 材料名称在整个工程内保持唯一，避免切换模型后出现多个“材料1”。
    int maxIndex = 0;
    for (const auto& existingModel : m_models)
    {
        for (const auto& existingEntry : existingModel.materials)
        {
            if (!existingEntry.name.startsWith(QStringLiteral("材料")))
                continue;

            bool ok = false;
            const int index =
                existingEntry.name.mid(QStringLiteral("材料").size()).toInt(&ok);
            if (ok)
                maxIndex = qMax(maxIndex, index);
        }
    }

    MaterialEntry entry;
    entry.id = QUuid::createUuid().toString();
    entry.name = makeUniqueMaterialName(QString("材料%1").arg(maxIndex + 1));
    for (const QString& column : model->columns)
    {
        entry.properties.insert(
            column, isAutoNumberColumn(column) ? QString()
                                               : defaultMaterialPropertyValue(model->key, column));
    }

    model->materials.append(entry);

    if (addToProjectTree)
    {
        m_addedEntryIds.insert(entry.id);
        rebuildProjectTree();
    }
    if (m_materialManagerWidget)
    {
        m_materialManagerWidget->refreshSummary(m_models);
    }

    return entry.id;
}

void MaterialManager::setupTreeWidget()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();
            if (uuid.isEmpty())
                return;

            if (kind == "MaterialEntry")
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "重命名",
                                          [this, uuid]
                                          {
                                              renameEntryFromProjectTree(uuid);
                                          });
                const auto targets = assignableMaterialEntries();
                if (targets.size() > 1)
                {
                    QMenu* mergeMenu = menu->addMenu(QStringLiteral("合并到其他材料"));
                    for (const auto& pair : targets)
                    {
                        if (pair.first == uuid)
                            continue;
                        mergeMenu->addAction(pair.second, this,
                                             [this, uuid, targetId = pair.first]
                                             {
                                                 mergeEntryIntoEntry(uuid, targetId);
                                             });
                    }
                }
                ContextMenuHub::addAction(menu, this, "删除",
                                          [this, uuid]
                                          {
                                              removeEntryFromProjectTree(uuid);
                                          });
                return;
            }

            if (kind == "MaterialModel")
            {
                QString modelKey;
                for (auto it = m_modelItems.constBegin(); it != m_modelItems.constEnd(); ++it)
                {
                    if (it.value() && it.value()->data(0, ProjectNode::RoleUuid).toString() == uuid)
                    {
                        modelKey = it.key();
                        break;
                    }
                }
                if (modelKey.isEmpty())
                    return;

                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "删除",
                                          [this, modelKey]
                                          {
                                              const auto model = findModel(modelKey);
                                              if (!model)
                                                  return;

                                              QStringList entryIds;
                                              entryIds.reserve(model->materials.size());
                                              for (const auto& entry : model->materials)
                                                  entryIds.append(entry.id);
                                              for (const QString& entryId : entryIds)
                                                  clearEntryProjectState(entryId);

                                              rebuildProjectTree();
                                          });
            }
        },
        100,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "Material";
        });
}

void MaterialManager::renameEntryFromProjectTree(const QString& entryId)
{
    MaterialEntry* entry = findEntry(entryId);
    if (!entry)
        return;

    const QString currentName = entry->name.trimmed();
    bool ok = false;
    QString requestedName = QInputDialog::getText(
        nullptr, QStringLiteral("重命名材料"), QStringLiteral("请输入新名称："),
        QLineEdit::Normal, currentName, &ok);
    if (!ok)
        return;

    requestedName = requestedName.trimmed();
    if (requestedName.isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("名称无效"),
                             QStringLiteral("材料名称不能为空。"));
        return;
    }

    const QString uniqueName = makeUniqueMaterialName(requestedName, entryId);
    if (uniqueName == currentName)
        return;

    entry->name = uniqueName;
    notifyEntryUpdated(entryId, true);
    if (auto* widget = m_entryWidgets.value(entryId, nullptr))
        widget->refreshEntryName();
    if (m_materialManagerWidget)
        m_materialManagerWidget->refreshSummary(m_models);
}

void MaterialManager::loadDefaultLibrary()
{
    m_selectedMatypes.clear();
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
    m_matypeOwnerById.clear();
    m_models = createDefaultMaterialModels();
    notifyLibraryChanged();
}

void MaterialManager::newProjectForPath(const QString& workPath)
{
    m_workPath = workPath;

    qDeleteAll(m_modelWidgets);
    m_modelWidgets.clear();
    qDeleteAll(m_entryWidgets);
    m_entryWidgets.clear();

    delete m_materialManagerWidget;
    m_materialManagerWidget = nullptr;
    m_transitionPage = nullptr;
    m_transitionNode = nullptr;

    m_rootItem = nullptr;
    m_modelItems.clear();
    m_entryItems.clear();
    m_addedEntryIds.clear();
    m_materialTransitions.clear();
    m_selectedMatypes.clear();
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
    m_matypeOwnerById.clear();

    m_materialManagerWidget = new MaterialManagerWidget(this);
    loadDefaultLibrary();
    ensureEntryIds();

    if (!m_projectManagerService)
    {
        return;
    }

    m_rootItem = new ProjectNode(QString("材料"));
    m_rootItem->setData(0, ProjectNode::RoleUuid, m_rootItem->getUuid());
    m_rootItem->setData(0, ProjectNode::RoleModule, QString("Material"));
    m_rootItem->setPage(m_materialManagerWidget);
    m_projectManagerService->addTreeItem(m_rootItem);

}

const QVector<MaterialModel>& MaterialManager::models() const
{
    return m_models;
}

QVector<MaterialModel>& MaterialManager::models()
{
    return m_models;
}

MaterialModel* MaterialManager::findModel(const QString& key)
{
    for (auto& model : m_models)
    {
        if (model.key == key)
        {
            return &model;
        }
    }
    return nullptr;
}

MaterialEntry* MaterialManager::findEntry(const QString& id, MaterialModel** outModel)
{
    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (entry.id == id)
            {
                if (outModel)
                {
                    *outModel = &model;
                }
                return &entry;
            }
        }
    }
    if (outModel)
    {
        *outModel = nullptr;
    }
    return nullptr;
}

QString MaterialManager::makeUniqueMaterialName(const QString& requestedName,
                                                const QString& excludeEntryId) const
{
    QString baseName = requestedName.trimmed();
    if (baseName.isEmpty())
        baseName = QStringLiteral("未命名材料");

    QSet<QString> usedNames;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!excludeEntryId.isEmpty() && entry.id == excludeEntryId)
                continue;
            usedNames.insert(entry.name.trimmed().toCaseFolded());
        }
    }

    if (!usedNames.contains(baseName.toCaseFolded()))
        return baseName;

    int defaultIndex = 0;
    if (defaultMaterialNameIndex(baseName, &defaultIndex))
    {
        for (int candidateIndex = defaultIndex + 1;; ++candidateIndex)
        {
            const QString candidate = QStringLiteral("材料%1").arg(candidateIndex);
            if (!usedNames.contains(candidate.toCaseFolded()))
                return candidate;
        }
    }

    for (int suffix = 2;; ++suffix)
    {
        const QString candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(suffix);
        if (!usedNames.contains(candidate.toCaseFolded()))
            return candidate;
    }
}

bool MaterialManager::changeEntryModel(const QString& entryId,
                                       const QString& targetModelKey,
                                       QString* error)
{
    if (error)
        error->clear();

    if (entryId.isEmpty() || targetModelKey.isEmpty())
    {
        if (error)
            *error = QStringLiteral("材料或目标材料模型无效。");
        return false;
    }

    int sourceModelIndex = -1;
    int sourceEntryIndex = -1;
    int targetModelIndex = -1;

    for (int modelIndex = 0; modelIndex < m_models.size(); ++modelIndex)
    {
        if (m_models.at(modelIndex).key == targetModelKey)
            targetModelIndex = modelIndex;

        const auto& materials = m_models.at(modelIndex).materials;
        for (int entryIndex = 0; entryIndex < materials.size(); ++entryIndex)
        {
            if (materials.at(entryIndex).id == entryId)
            {
                sourceModelIndex = modelIndex;
                sourceEntryIndex = entryIndex;
                break;
            }
        }
    }

    if (sourceModelIndex < 0 || sourceEntryIndex < 0)
    {
        if (error)
            *error = QStringLiteral("没有找到当前材料。");
        return false;
    }
    if (targetModelIndex < 0)
    {
        if (error)
            *error = QStringLiteral("没有找到目标材料模型。");
        return false;
    }
    if (sourceModelIndex == targetModelIndex)
        return true;

    MaterialEntry movedEntry = m_models[sourceModelIndex].materials.at(sourceEntryIndex);
    MaterialModel& targetModel = m_models[targetModelIndex];
    if (movedEntry.appliedMatypes.contains(2) &&
        targetModel.key != QStringLiteral("linear_elastic") &&
        targetModel.key != QStringLiteral("planar_elastic_beam"))
    {
        if (error)
            *error = QStringLiteral(
                "材料编号 2 在求解器中固定为线弹性本构（CONMAT(2,1)=0），"
                "已施加编号 2 的材料不能切换为非线性或土体本构。");
        return false;
    }

    // 不删除旧模型参数：当前模型只读取自身 columns，保留旧值可避免用户切回时丢失数据。
    // 目标模型缺少的字段再补默认值。若目标模型是旧版已存在的模型，先按旧规则补出
    // 新拆分字段，避免切换后计算结果突然变化。
    migrateLegacyMaterialProperties(targetModel.key, &movedEntry.properties);
    for (const QString& column : targetModel.columns)
    {
        if (!movedEntry.properties.contains(column))
        {
            movedEntry.properties.insert(
                column, isAutoNumberColumn(column)
                            ? QString()
                            : defaultMaterialPropertyValue(targetModel.key, column));
        }
    }

    const bool targetSupportsCalculationStage =
        isDamStaticModel(targetModel.key) || targetModel.key == QStringLiteral("dam_dynamic");
    if (!targetSupportsCalculationStage || !isComputableMatype(movedEntry.transitionMatype))
    {
        movedEntry.transitionOnly = false;
        movedEntry.transitionMatype = -1;
    }

    m_models[sourceModelIndex].materials.removeAt(sourceEntryIndex);
    targetModel.materials.append(movedEntry);
    syncAutoNumberProperties(&targetModel, &targetModel.materials.last());

    // entryId 和网格关联保持不变，因此树节点和所有高亮状态都可以继续复用。
    notifyLibraryChanged(true);
    focusOnMaterial(entryId);
    return true;
}

void MaterialManager::notifyLibraryChanged(bool refreshProjectTree)
{
    if (refreshProjectTree)
    {
        rebuildProjectTree();
    }

    if (m_materialManagerWidget)
    {
        m_materialManagerWidget->refreshSummary(m_models);
    }
}

void MaterialManager::notifyEntryUpdated(const QString& entryId, bool nameChanged)
{
    if (nameChanged)
    {
        ProjectNode* item = m_entryItems.value(entryId, nullptr);
        MaterialEntry* entry = findEntry(entryId);
        if (item && entry)
        {
            item->setText(0, entry->name);
            item->setName(entry->name);
            if (BasePage* page = item->getPage())
                page->setDisplayName(entry->name);
        }
    }
}

QVector<QStringList> MaterialManager::getValueStrings()
{
    QVector<QStringList> results;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            QStringList row;
            row << model.key << model.displayName << entry.name;
            QStringList encoded;
            for (const QString& column : model.columns)
            {
                encoded << (column + QLatin1Char(':') + entry.properties.value(column));
            }
            row << encoded.join(QString("|;|"));
            QList<int> appliedList = entry.appliedMatypes.values();
            std::sort(appliedList.begin(), appliedList.end());
            QStringList appliedStrings;
            for (int value : appliedList)
            {
                if (isComputableMatype(value))
                    appliedStrings << QString::number(value);
            }
            row << appliedStrings.join(QLatin1Char(','));
            const bool hasValidTransition = isComputableMatype(entry.transitionMatype);
            row << (entry.transitionOnly && hasValidTransition ? QStringLiteral("1")
                                                               : QStringLiteral("0"));
            row << QString::number(hasValidTransition ? entry.transitionMatype : -1);
            results.append(row);
        }
    }
    return results;
}

void MaterialManager::setValueStrings(QVector<QStringList> values)
{
    m_selectedMatypes.clear();
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
    m_matypeOwnerById.clear();
    if (values.isEmpty())
    {
        loadDefaultLibrary();
        return;
    }

    QVector<MaterialModel> defaults = createDefaultMaterialModels();
    m_models.clear();
    m_models.reserve(defaults.size());
    for (const auto& model : defaults)
    {
        MaterialModel cleared = model;
        cleared.materials.clear();
        m_models.append(cleared);
    }

    auto findIndex = [this](const QString& key) -> int
    {
        for (int i = 0; i < m_models.size(); ++i)
        {
            if (m_models.at(i).key == key)
            {
                return i;
            }
        }
        return -1;
    };

    for (const auto& list : values)
    {
        if (list.size() < 4)
        {
            continue;
        }
        const QString key = list.at(0);
        const QString displayName = list.at(1);
        const QString name = list.at(2);
        const QString encoded = list.at(3);
        const QString appliedMatypes = (list.size() > 4) ? list.at(4) : QString();
        const int transitionMatypeRaw = (list.size() > 6) ? list.at(6).toInt() : -1;
        const int transitionMatype =
            isComputableMatype(transitionMatypeRaw) ? transitionMatypeRaw : -1;
        const bool transitionOnly =
            (list.size() > 5) ? (list.at(5) == QStringLiteral("1") && isComputableMatype(transitionMatype))
                              : false;

        int index = findIndex(key);
        if (index < 0)
        {
            MaterialModel extra;
            extra.key = key;
            extra.displayName = displayName;
            index = m_models.size();
            m_models.append(extra);
        }

        MaterialEntry entry;
        entry.name = name;
        const QStringList pairs = encoded.split(QString("|;|"), Qt::SkipEmptyParts);
        for (const QString& pair : pairs)
        {
            int sep = pair.indexOf(QLatin1Char(':'));
            if (sep <= 0)
            {
                continue;
            }
            QString columnName = pair.left(sep);
            QString value = pair.mid(sep + 1);
            entry.properties.insert(columnName, value);
            if (!m_models[index].columns.contains(columnName))
            {
                m_models[index].columns.append(columnName);
            }
        }
        migrateLegacyMaterialProperties(key, &entry.properties);
        for (const QString& column : m_models[index].columns)
        {
            if (!entry.properties.contains(column))
            {
                entry.properties.insert(
                    column, isAutoNumberColumn(column)
                                ? QString()
                                : defaultMaterialPropertyValue(m_models[index].key, column));
            }
        }

        m_models[index].materials.append(entry);
        const QStringList matypeList =
            appliedMatypes.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& matypeStr : matypeList)
        {
            bool ok = false;
            int value = matypeStr.toInt(&ok);
            if (ok && isComputableMatype(value))
            {
                m_models[index].materials.last().appliedMatypes.insert(value);
            }
        }
        const bool supportsCalculationStage =
            isDamStaticModel(key) || key == QStringLiteral("dam_dynamic");
        m_models[index].materials.last().transitionOnly =
            supportsCalculationStage && transitionOnly;
        m_models[index].materials.last().transitionMatype =
            supportsCalculationStage && transitionOnly ? transitionMatype : -1;
        syncAutoNumberProperties(&m_models[index], &m_models[index].materials.last());
    }

    ensureEntryIds();
    notifyLibraryChanged();
}

bool MaterialManager::isAutoNumberColumn(const QString& columnName)
{
    return columnName == QStringLiteral("序号") || columnName == QStringLiteral("编号") ||
           columnName == QStringLiteral("材料编号");
}

bool MaterialManager::isSeepageModelKey(const QString& modelKey) const
{
    for (const auto& model : m_seepageModels)
    {
        if (model.key == modelKey)
        {
            return true;
        }
    }
    return false;
}

const QVector<MaterialModel>& MaterialManager::seepageModels() const
{
    return m_seepageModels;
}

bool MaterialManager::validateTransitionMatype(const QString& entryId, int matype, QString* error) const
{
    if (!isComputableMatype(matype))
    {
        if (error)
            *error = QStringLiteral("编号必须大于等于 2；1 表示不进入计算。");
        return false;
    }
    if (matype > MAX_SOLVER_MATYPE)
    {
        if (error)
            *error = QStringLiteral("材料编号不能超过 300。");
        return false;
    }
    if (matype == 2)
    {
        if (error)
        {
            *error = QStringLiteral(
                "材料编号 2 是求解器保留的线弹性编号，仅用于兼容旧数据。新建计算阶段材料请使用 3 以上的空闲编号。");
        }
        return false;
    }

    const MaterialModel* currentModel = nullptr;
    const MaterialEntry* currentEntry = nullptr;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (entry.id == entryId)
            {
                currentModel = &model;
                currentEntry = &entry;
                break;
            }
        }
        if (currentEntry)
            break;
    }
    if (!currentEntry || !currentModel || !m_addedEntryIds.contains(entryId))
    {
        if (error)
            *error = QStringLiteral("当前材料已不在工程中，请重新添加后再设置。");
        return false;
    }
    if (!isDamStaticModel(currentModel->key) &&
        currentModel->key != QStringLiteral("dam_dynamic"))
    {
        if (error)
            *error = QStringLiteral("该材料模型不支持作为浸润、动力或地震波动计算阶段材料。");
        return false;
    }
    if (!currentEntry->appliedMatypes.isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral(
                "该材料已经施加到网格，不能同时作为计算阶段材料。请复制或新建一份材料参数后再设置。");
        }
        return false;
    }

    const QSet<int> meshMatypes = availableMatypes();
    if (meshMatypes.contains(matype))
    {
        if (error)
            *error = QStringLiteral("该编号已存在于网格 MATYPE 中。");
        return false;
    }

    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            if (entry.id == entryId)
                continue;
            if (entry.appliedMatypes.contains(matype))
            {
                if (error)
                    *error = QStringLiteral("该编号已被其他材料施加到网格。");
                return false;
            }
            if (entry.transitionOnly && entry.transitionMatype == matype)
            {
                if (error)
                    *error = QStringLiteral("该编号已被其他计算阶段材料占用。");
                return false;
            }
        }
    }
    return true;
}

QMap<int, QString> MaterialManager::transitionSourceLabels() const
{
    return assignedMaterialLabels();
}

QMap<int, QString> MaterialManager::assignedMaterialLabels() const
{
    QMap<int, QString> labels;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            for (int matype : entry.appliedMatypes)
            {
                if (!isComputableMatype(matype) || labels.contains(matype))
                    continue;
                labels.insert(matype, QStringLiteral("%1/%2（编号 %3）")
                                          .arg(model.displayName, entry.name,
                                               QString::number(matype)));
            }
        }
    }
    return labels;
}

QMap<int, QString> MaterialManager::transitionTargetLabels() const
{
    return calculationMaterialLabels();
}

QMap<int, QString> MaterialManager::calculationMaterialLabels() const
{
    QMap<int, QString> labels;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            for (int matype : entry.appliedMatypes)
            {
                if (!isComputableMatype(matype) || labels.contains(matype))
                    continue;
                labels.insert(matype, QStringLiteral("%1/%2（编号 %3）")
                                          .arg(model.displayName, entry.name,
                                               QString::number(matype)));
            }
            if (entry.transitionOnly && isComputableMatype(entry.transitionMatype) &&
                !labels.contains(entry.transitionMatype))
            {
                labels.insert(entry.transitionMatype,
                              QStringLiteral("%1/%2（阶段材料，编号 %3）")
                                  .arg(model.displayName, entry.name,
                                       QString::number(entry.transitionMatype)));
            }
        }
    }
    return labels;
}

QMap<int, QString> MaterialManager::damStaticMaterialLabels() const
{
    QMap<int, QString> labels;
    for (const auto& model : m_models)
    {
        if (!isDamStaticModel(model.key))
            continue;
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            for (int matype : entry.appliedMatypes)
            {
                if (isComputableMatype(matype) && !labels.contains(matype))
                    labels.insert(matype, compactMaterialLabel(matype, entry));
            }
        }
    }
    return labels;
}

QMap<int, QString> MaterialManager::damWetMaterialLabels(int sourceMatype) const
{
    QMap<int, QString> labels;
    for (const auto& model : m_models)
    {
        if (!isDamStaticModel(model.key))
            continue;
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            if (entry.transitionOnly && isComputableMatype(entry.transitionMatype) &&
                entry.transitionMatype != sourceMatype &&
                !labels.contains(entry.transitionMatype))
            {
                labels.insert(entry.transitionMatype,
                              compactMaterialLabel(entry.transitionMatype, entry));
            }
        }
    }
    return labels;
}

QMap<int, QString> MaterialManager::damDynamicMaterialLabels() const
{
    QMap<int, QString> labels;
    for (const auto& model : m_models)
    {
        if (model.key != QStringLiteral("dam_dynamic"))
            continue;
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;

            if (entry.transitionOnly && isComputableMatype(entry.transitionMatype) &&
                !labels.contains(entry.transitionMatype))
            {
                labels.insert(entry.transitionMatype,
                              compactMaterialLabel(entry.transitionMatype, entry));
            }
        }
    }
    return labels;
}

void MaterialManager::focusOnMaterial(const QString& entryId)
{
    if (!m_projectManagerService)
    {
        return;
    }
    ProjectNode* item = m_entryItems.value(entryId, nullptr);
    if (item)
    {
        if (m_rootItem)
            m_rootItem->setExpanded(true);
        if (auto* parent = dynamic_cast<ProjectNode*>(item->parent()))
            parent->setExpanded(true);
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::focusOnModel(const QString& modelKey)
{
    if (!m_projectManagerService)
    {
        return;
    }
    ProjectNode* item = m_modelItems.value(modelKey, nullptr);
    if (item)
    {
        if (m_rootItem)
            m_rootItem->setExpanded(true);
        m_projectManagerService->setCurrentItem(item);
    }
}

void MaterialManager::ensureEntryIds()
{
    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (entry.id.isEmpty())
                entry.id = QUuid::createUuid().toString();
        }
    }
    normalizeMaterialNames();
}

void MaterialManager::normalizeMaterialNames()
{
    QSet<QString> usedNames;
    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            QString baseName = entry.name.trimmed();
            if (baseName.isEmpty())
                baseName = QStringLiteral("未命名材料");

            QString candidate = baseName;
            if (usedNames.contains(candidate.toCaseFolded()))
            {
                int defaultIndex = 0;
                if (defaultMaterialNameIndex(baseName, &defaultIndex))
                {
                    for (int candidateIndex = defaultIndex + 1;; ++candidateIndex)
                    {
                        candidate = QStringLiteral("材料%1").arg(candidateIndex);
                        if (!usedNames.contains(candidate.toCaseFolded()))
                            break;
                    }
                }
                else
                {
                    for (int suffix = 2;; ++suffix)
                    {
                        candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(suffix);
                        if (!usedNames.contains(candidate.toCaseFolded()))
                            break;
                    }
                }
            }

            entry.name = candidate;
            usedNames.insert(candidate.toCaseFolded());
        }
    }
}

void MaterialManager::rebuildProjectTree()
{
    if (!m_rootItem)
    {
        return;
    }

    QSet<QString> activeModelKeys;
    QSet<QString> validEntries;

    for (const QString& entryId : std::as_const(m_addedEntryIds))
    {
        MaterialModel* model = nullptr;
        MaterialEntry* entry = findEntry(entryId, &model);
        if (!entry || !model)
        {
            continue;
        }
        validEntries.insert(entryId);
        activeModelKeys.insert(model->key);

        ProjectNode* modelItem = m_modelItems.value(model->key, nullptr);
        if (!modelItem)
        {
            auto* modelWidget = new MaterialModelWidget(this);
            modelItem =
                new ProjectNode(model->displayName, QUuid::createUuid().toString(), modelWidget);
            modelItem->setData(0, ProjectNode::RoleUuid, modelItem->getUuid());
            modelItem->setData(0, ProjectNode::RoleModule, QString("Material"));
            modelItem->setData(0, ProjectNode::RoleKind, QString("MaterialModel"));
            m_modelWidgets.insert(model->key, modelWidget);
            m_modelItems.insert(model->key, modelItem);
            m_rootItem->addChild(modelItem);
        }
        modelItem->setText(0, model->displayName);
        if (m_modelWidgets.contains(model->key))
        {
            m_modelWidgets[model->key]->setModel(*model);
        }

        ProjectNode* entryItem = m_entryItems.value(entryId, nullptr);
        bool entryCreated = false;
        if (!entryItem)
        {
            auto* entryWidget = new MaterialEntryWidget(this);
            entryWidget->setEntry(model->key, entryId);
            entryItem = new ProjectNode(entry->name, entryId, entryWidget);
            entryItem->setData(0, ProjectNode::RoleUuid, entryId);
            entryItem->setData(0, ProjectNode::RoleModule, QString("Material"));
            entryItem->setData(0, ProjectNode::RoleKind, QString("MaterialEntry"));
            m_entryWidgets.insert(entryId, entryWidget);
            m_entryItems.insert(entryId, entryItem);
            modelItem->addChild(entryItem);
            entryCreated = true;
        }
        else if (entryItem->parent() != modelItem)
        {
            auto* oldParent = dynamic_cast<ProjectNode*>(entryItem->parent());
            if (oldParent)
            {
                oldParent->removeChild(entryItem);
            }
            modelItem->addChild(entryItem);
        }
        entryItem->setText(0, entry->name);
        if (!entryCreated && m_entryWidgets.contains(entryId))
        {
            m_entryWidgets[entryId]->setEntry(model->key, entryId);
        }
    }

    // remove stale entry items
    QList<QString> staleEntries;
    for (auto it = m_entryItems.cbegin(); it != m_entryItems.cend(); ++it)
    {
        if (!validEntries.contains(it.key()))
        {
            staleEntries.append(it.key());
        }
    }
    for (const QString& id : staleEntries)
    {
        ProjectNode* item = m_entryItems.take(id);
        if (item)
        {
            if (auto* parent = dynamic_cast<ProjectNode*>(item->parent()))
            {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_entryWidgets.contains(id))
        {
            delete m_entryWidgets.take(id);
        }
    }
    m_addedEntryIds = validEntries;

    // remove stale models
    QList<QString> staleModels;
    for (auto it = m_modelItems.cbegin(); it != m_modelItems.cend(); ++it)
    {
        if (!activeModelKeys.contains(it.key()))
        {
            staleModels.append(it.key());
        }
    }
    for (const QString& modelKey : staleModels)
    {
        ProjectNode* item = m_modelItems.take(modelKey);
        if (item)
        {
            QList<QString> orphanEntries;
            for (auto it = m_entryItems.begin(); it != m_entryItems.end();)
            {
                if (it.value()->parent() == item)
                {
                    orphanEntries.append(it.key());
                    ProjectNode* entryItem = it.value();
                    item->removeChild(entryItem);
                    delete entryItem;
                    it = m_entryItems.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (const QString& id : orphanEntries)
            {
                if (m_entryWidgets.contains(id))
                {
                    delete m_entryWidgets.take(id);
                }
            }
            if (auto* parent = dynamic_cast<ProjectNode*>(item->parent()))
            {
                parent->removeChild(item);
            }
            delete item;
        }
        if (m_modelWidgets.contains(modelKey))
        {
            delete m_modelWidgets.take(modelKey);
        }
    }

}

void MaterialManager::clearProjectTree()
{
    if (!m_rootItem)
    {
        return;
    }
    const auto children = m_rootItem->getChildren();
    for (ProjectNode* child : children)
    {
        m_rootItem->removeChild(child);
        delete child;
    }
}

void MaterialManager::addEntryToProjectTree(const QString& entryId)
{
    ensureEntryIds();
    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (!entry || !model)
    {
        return;
    }

    m_addedEntryIds.insert(entryId);
    rebuildProjectTree();
}

void MaterialManager::removeEntryFromProjectTree(const QString& entryId)
{
    if (!m_addedEntryIds.contains(entryId))
    {
        return;
    }
    clearEntryProjectState(entryId);
    rebuildProjectTree();
}

void MaterialManager::clearEntryProjectState(const QString& entryId)
{
    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (entry)
    {
        QSet<int> removedMatypes(entry->appliedMatypes.cbegin(), entry->appliedMatypes.cend());
        if (entry->transitionMatype > 0)
            removedMatypes.insert(entry->transitionMatype);
        if (!removedMatypes.isEmpty())
        {
            m_materialTransitions.erase(
                std::remove_if(m_materialTransitions.begin(), m_materialTransitions.end(),
                               [&](const QPair<int, int>& pair) {
                                   return removedMatypes.contains(pair.first) ||
                                          removedMatypes.contains(pair.second);
                               }),
                m_materialTransitions.end());
        }
        entry->appliedMatypes.clear();
        entry->transitionOnly = false;
        entry->transitionMatype = -1;
        syncAutoNumberProperties(model, entry);
    }
    for (auto it = m_matypeOwnerById.begin(); it != m_matypeOwnerById.end();)
    {
        if (it.value() == entryId)
            it = m_matypeOwnerById.erase(it);
        else
            ++it;
    }
    m_addedEntryIds.remove(entryId);
    m_selectedMatypes.remove(entryId);
    m_previewMatypes.remove(entryId);
    m_preDeleteMatypes.remove(entryId);
    clearMatypeHighlight(entryId);
}

QSet<int> MaterialManager::availableMatypes() const
{
    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service)
    {
        return {};
    }
    QSet<int> filtered;
    for (int matype : service->getAvailableMatypes())
    {
        if (isComputableMatype(matype))
            filtered.insert(matype);
    }
    return filtered;
}

void MaterialManager::highlightMatypes(const QString& entryId, const QSet<int>& matypes,
                                       int selectedMatype,
                                       int previewMatype, int preDeleteMatype)
{

    if (entryId.isEmpty())
        return;

    const QString domain = m_pickDomain.isEmpty() ? QStringLiteral("MaterialManager") : m_pickDomain;
    const QString uuid = m_pickUuid.isEmpty() ? entryId : m_pickUuid;

    auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();

    auto keysForMatype = [service](int matype) -> QSet<DomainKey>
    {
        QSet<DomainKey> keys;

        if (!isComputableMatype(matype) || !service)
            return keys;

        // 优先从真实 MeshTagSet 获取。这样同一个 MATYPE 同时存在实体单元(dim=3)
        // 和杆单元(dim=1)时，两类 DomainKey 都会被加入。
        const QVector<MeshTagSet> sets = service->domainTagSets();
        for (const MeshTagSet& set : sets)
        {
            if (set.key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) != 0)
                continue;
            if (set.key.id != matype)
                continue;
            if (set.key.dim <= 0)
                continue;
            if (service->cellIdsOfTag(set.key).isEmpty())
                continue;

            keys.insert(DomainKey{set.key.category, set.key.dim, set.key.id});
        }

        auto gridHasMatype = [](vtkUnstructuredGrid* grid, int id) -> bool
        {
            auto* arr = grid && grid->GetCellData()
                            ? vtkIntArray::SafeDownCast(grid->GetCellData()->GetArray("MATYPE"))
                            : nullptr;
            if (!grid || !arr)
                return false;

            const vtkIdType n = std::min(grid->GetNumberOfCells(), arr->GetNumberOfTuples());
            for (vtkIdType cid = 0; cid < n; ++cid)
            {
                if (arr->GetValue(cid) == id)
                    return true;
            }
            return false;
        };

        // 拆分后的 lineGrid 可能没有通过 domainTagSets 暴露出来，但 CellData/MATYPE 仍然存在。
        // 这里显式补充 dim=1，保证材料规则可以驱动杆单元高亮。
        if (gridHasMatype(service->getVtkUnstructuredGridLine(), matype))
            keys.insert(DomainKey{QStringLiteral("MATYPE"), 1, matype});

        if (gridHasMatype(service->getVtkUnstructuredGridSolid(), matype))
        {
            const int solidDim = PublicVariables::instance()
                                     .get(PublicConstants::kMeshDimension)
                                     .toInt();
            keys.insert(DomainKey{QStringLiteral("MATYPE"), solidDim == 2 ? 2 : 3, matype});
        }

        // 兼容旧接口：如果 domainTagSets/CellData 都没返回，再看 availableDomains。
        if (keys.isEmpty())
        {
            const QVector<DomainKey> domains = service->availableDomains(QStringLiteral("MATYPE"));
            for (const DomainKey& key : domains)
            {
                if (!key.isValid())
                    continue;
                if (key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) != 0)
                    continue;
                if (key.id != matype)
                    continue;

                keys.insert(key);
            }
        }

        return keys;
    };

    auto preferredKeyForMatype = [&keysForMatype](int matype) -> DomainKey
    {
        const QSet<DomainKey> keys = keysForMatype(matype);
        if (keys.isEmpty())
            return DomainKey{};

        QList<DomainKey> list = keys.values();
        std::sort(list.begin(), list.end(),
                  [](const DomainKey& a, const DomainKey& b)
                  {
                      // preview / preDelete 只能给一个 DomainKey。
                      // 优先实体域，其次面域，最后杆单元。
                      if (a.dim != b.dim)
                          return a.dim > b.dim;
                      return a.id < b.id;
                  });
        return list.first();
    };

    QSet<DomainKey> applied;
    for (int matype : matypes)
        applied.unite(keysForMatype(matype));

    PickManager::instance().setAppliedDomains(domain, uuid, applied);

    const QSet<int> multiSelected = m_selectedMatypeSets.value(entryId);
    if (multiSelected.size() > 1)
    {
        QSet<DomainKey> selKeys;
        for (int m : multiSelected)
        {
            const DomainKey k = preferredKeyForMatype(m);
            if (k.isValid())
                selKeys.insert(k);
        }
        PickManager::instance().setSelectedDomains(domain, uuid, selKeys);
    }
    else
    {
        PickManager::instance().setSelectedDomain(
            domain,
            uuid,
            selectedMatype > 0 ? preferredKeyForMatype(selectedMatype) : DomainKey{});
    }

    PickManager::instance().setPreviewDomain(
        domain,
        uuid,
        previewMatype > 0 ? preferredKeyForMatype(previewMatype) : DomainKey{});

    PickManager::instance().setPreDeleteDomain(
        domain,
        uuid,
        preDeleteMatype > 0 ? preferredKeyForMatype(preDeleteMatype) : DomainKey{});
}

void MaterialManager::refreshMatypeHighlight(const QString& entryId)
{
    MaterialEntry* entry = findEntry(entryId);
    if (!entry)
        return;

    const int preview = m_previewMatypes.value(entryId, -1);
    const int preDelete = m_preDeleteMatypes.value(entryId, -1);
    const int selected = m_selectedMatypes.value(entryId, -1);
    highlightMatypes(entryId, entry->appliedMatypes, selected, preview, preDelete);
}

int MaterialManager::selectedMatypeForEntry(const QString& entryId) const
{
    return m_selectedMatypes.value(entryId, -1);
}

void MaterialManager::setSelectedMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
    {
        return;
    }
    if (!isComputableMatype(matype))
    {
        m_selectedMatypes.remove(entryId);
        return;
    }
    m_selectedMatypes.insert(entryId, matype);
}

QSet<int> MaterialManager::selectedMatypesForEntry(const QString& entryId) const
{
    return m_selectedMatypeSets.value(entryId);
}

void MaterialManager::setSelectedMatypesForEntry(const QString& entryId, const QSet<int>& matypes)
{
    if (entryId.isEmpty())
        return;
    QSet<int> clean;
    for (int m : matypes)
    {
        if (isComputableMatype(m))
            clean.insert(m);
    }
    if (clean.isEmpty())
    {
        m_selectedMatypeSets.remove(entryId);
        return;
    }
    m_selectedMatypeSets.insert(entryId, clean);
    m_selectedMatypes.insert(entryId, *clean.cbegin());
}

QVector<QPair<QString, QString>> MaterialManager::assignableMaterialEntries() const
{
    QVector<QPair<QString, QString>> out;
    for (const auto& model : m_models)
    {
        for (const auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id) || entry.transitionOnly)
                continue;
            const QString label = entry.name.trimmed().isEmpty()
                                      ? QStringLiteral("未命名材料")
                                      : entry.name.trimmed();
            out.push_back({entry.id, label});
        }
    }
    return out;
}

bool MaterialManager::assignMatypesToMaterial(const QString& entryId, const QSet<int>& matypes,
                                              QString* message)
{
    return applyMatypesToEntry(entryId, matypes, message);
}

bool MaterialManager::applyMatypesToEntry(const QString& entryId, const QSet<int>& matypes,
                                          QString* message)
{
    auto fail = [message](const QString& text)
    {
        if (message)
            *message = text;
        return false;
    };

    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (!entry || !model)
        return fail(QStringLiteral("未找到目标材料。"));
    if (entry->transitionOnly)
        return fail(QStringLiteral("该材料已作为计算阶段材料使用，不能直接施加到网格。"));

    QList<int> validList;
    for (int m : matypes)
    {
        if (m >= 3 && m <= MAX_SOLVER_MATYPE)
            validList.push_back(m);
    }
    std::sort(validList.begin(), validList.end());
    if (validList.isEmpty())
        return fail(QStringLiteral("所选网格组不包含可施加的材料编号（3～300）。"));

    // Filter out transition-occupied IDs
    QSet<int> transitionLocked;
    for (const auto& candidateModel : m_models)
    {
        for (const auto& candidateEntry : candidateModel.materials)
        {
            if (m_addedEntryIds.contains(candidateEntry.id) &&
                candidateEntry.id != entryId && candidateEntry.transitionOnly)
            {
                transitionLocked.insert(candidateEntry.transitionMatype);
            }
        }
    }

    QList<int> actionable;
    for (int m : validList)
    {
        if (!transitionLocked.contains(m))
            actionable.push_back(m);
    }
    if (actionable.isEmpty())
        return fail(QStringLiteral("所选编号已被计算阶段材料占用，无法直接施加。"));

    auto findOwnerEntryId = [&](int targetMatype) -> QString
    {
        for (const auto& mdl : m_models)
            for (const auto& e : mdl.materials)
                if (m_addedEntryIds.contains(e.id) && e.appliedMatypes.contains(targetMatype))
                    return e.id;
        return {};
    };

    QMap<int, QString> conflictOwners;
    for (int m : actionable)
    {
        QString ownerId = m_matypeOwnerById.value(m);
        if (!ownerId.isEmpty() && !m_addedEntryIds.contains(ownerId))
        {
            m_matypeOwnerById.remove(m);
            ownerId.clear();
        }
        if (ownerId.isEmpty())
            ownerId = findOwnerEntryId(m);
        if (!ownerId.isEmpty() && ownerId != entryId)
            conflictOwners.insert(m, ownerId);
    }

    if (!conflictOwners.isEmpty())
    {
        QStringList conflictIds;
        for (auto it = conflictOwners.cbegin(); it != conflictOwners.cend(); ++it)
            conflictIds << QString::number(it.key());
        const QString targetName = entry->name.trimmed().isEmpty()
                                       ? QStringLiteral("未命名材料")
                                       : entry->name.trimmed();
        const auto answer = QMessageBox::question(
            nullptr, QStringLiteral("批量转移材料分组"),
            QStringLiteral("材料分组 [%1] 当前已关联其他材料。继续施加会将它们统一转移到“%2”，是否继续？")
                .arg(conflictIds.join(QStringLiteral(", ")), targetName),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
    }

    QSet<QString> touchedOwners;
    for (auto it = conflictOwners.cbegin(); it != conflictOwners.cend(); ++it)
    {
        const int m = it.key();
        const QString& ownerId = it.value();
        MaterialModel* ownerModel = nullptr;
        if (MaterialEntry* ownerEntry = findEntry(ownerId, &ownerModel))
        {
            ownerEntry->appliedMatypes.remove(m);
            if (m_selectedMatypes.value(ownerId, -1) == m)
                m_selectedMatypes.remove(ownerId);
            m_selectedMatypeSets[ownerId].remove(m);
            if (m_previewMatypes.value(ownerId, -1) == m)
                m_previewMatypes.remove(ownerId);
            if (m_preDeleteMatypes.value(ownerId, -1) == m)
                m_preDeleteMatypes.remove(ownerId);
            syncAutoNumberProperties(ownerModel, ownerEntry);
            touchedOwners.insert(ownerId);
        }
    }
    for (const QString& ownerId : touchedOwners)
    {
        if (MaterialEntry* ownerEntry = findEntry(ownerId))
        {
            refreshEntryWidgetMatypes(ownerId, ownerEntry->appliedMatypes,
                                      m_selectedMatypes.value(ownerId, -1));
            refreshMatypeHighlight(ownerId);
        }
    }

    int lastMatype = actionable.last();
    for (int m : actionable)
    {
        entry->appliedMatypes.insert(m);
        m_matypeOwnerById.insert(m, entryId);
    }
    setSelectedMatypesForEntry(entryId, {});
    setSelectedMatypeForEntry(entryId, lastMatype);
    syncAutoNumberProperties(model, entry);
    setPreviewMatypeForEntry(entryId, -1);
    setPreDeleteMatypeForEntry(entryId, -1);

    refreshEntryWidgetMatypes(entryId, entry->appliedMatypes, lastMatype);
    notifyEntryUpdated(entryId, false);
    refreshMatypeHighlight(entryId);
    EventManager::instance().sendEvent(QStringLiteral("material.assignment.changed"),
                                       {{QStringLiteral("entryId"), entryId}});
    if (message)
        *message = QStringLiteral("已将 %1 个网格组赋予“%2”。")
                       .arg(actionable.size())
                       .arg(entry->name.trimmed().isEmpty() ? QStringLiteral("当前材料")
                                                            : entry->name.trimmed());
    return true;
}

bool MaterialManager::mergeEntryIntoEntry(const QString& sourceEntryId, const QString& targetEntryId)
{
    if (sourceEntryId.isEmpty() || targetEntryId.isEmpty() || sourceEntryId == targetEntryId)
        return false;
    MaterialModel* srcModel = nullptr;
    MaterialEntry* srcEntry = findEntry(sourceEntryId, &srcModel);
    MaterialModel* dstModel = nullptr;
    MaterialEntry* dstEntry = findEntry(targetEntryId, &dstModel);
    if (!srcEntry || !dstEntry || dstEntry->transitionOnly)
        return false;

    const QString srcName = srcEntry->name.trimmed().isEmpty()
                                ? QStringLiteral("未命名材料")
                                : srcEntry->name.trimmed();
    const QString dstName = dstEntry->name.trimmed().isEmpty()
                                ? QStringLiteral("未命名材料")
                                : dstEntry->name.trimmed();
    if (QMessageBox::question(
            nullptr, QStringLiteral("合并材料"),
            QStringLiteral("确定将“%1”合并到“%2”吗？\n\n“%1”已关联的 %3 个网格区域将全部转归“%2”，并移除“%1”。")
                .arg(srcName, dstName)
                .arg(srcEntry->appliedMatypes.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return false;
    }

    const QSet<int> transferred = srcEntry->appliedMatypes;
    srcEntry->appliedMatypes.clear();
    for (int m : transferred)
    {
        dstEntry->appliedMatypes.insert(m);
        m_matypeOwnerById.insert(m, targetEntryId);
    }
    syncAutoNumberProperties(dstModel, dstEntry);
    removeEntryFromProjectTree(sourceEntryId);
    refreshEntryWidgetMatypes(targetEntryId, dstEntry->appliedMatypes,
                              m_selectedMatypes.value(targetEntryId, -1));
    notifyEntryUpdated(targetEntryId, false);
    focusOnMaterial(targetEntryId);
    refreshMatypeHighlight(targetEntryId);
    EventManager::instance().sendEvent(QStringLiteral("material.assignment.changed"),
                                       {{QStringLiteral("entryId"), targetEntryId}});
    return true;
}

void MaterialManager::onMeshGroupsChanged()
{
    const QSet<int> currentMatypes = availableMatypes();
    for (auto& mdl : m_models)
    {
        for (auto& entry : mdl.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;
            QSet<int> validApplied;
            for (int m : entry.appliedMatypes)
            {
                if (currentMatypes.contains(m))
                    validApplied.insert(m);
                else if (m_matypeOwnerById.value(m) == entry.id)
                    m_matypeOwnerById.remove(m);
            }
            if (validApplied != entry.appliedMatypes)
            {
                entry.appliedMatypes = validApplied;
                syncAutoNumberProperties(&mdl, &entry);
            }
            if (MaterialEntryWidget* w = m_entryWidgets.value(entry.id, nullptr))
            {
                w->invalidateMatypeOptions();
                w->refreshAppliedMatypes(entry.appliedMatypes, -1);
            }
        }
    }
    if (!m_pickUuid.isEmpty())
        refreshMatypeHighlight(m_pickUuid);
}

void MaterialManager::applyMatypeToEntry(const QString& entryId, int matype)
{
    if (!isComputableMatype(matype))
        return;
    if (matype > MAX_SOLVER_MATYPE)
    {
        QMessageBox::warning(nullptr, QStringLiteral("材料编号超出范围"),
                             QStringLiteral("求解器只接受 1 至 300 的材料编号，请先修改网格材料分组。"));
        return;
    }

    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (!entry || !model)
        return;

    if (entry->transitionOnly)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("材料用途冲突"),
            QStringLiteral("该材料已作为计算阶段材料使用，不能再直接施加到网格。"));
        return;
    }

    if (matype == 2)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("材料编号受限"),
            QStringLiteral(
                "材料编号 2 是求解器保留的线弹性编号，仅用于兼容旧数据，不能新施加材料。\n\n请先把网格材料分组改为 3 以上的空闲编号。"));
        return;
    }

    for (const auto& candidateModel : m_models)
    {
        for (const auto& candidateEntry : candidateModel.materials)
        {
            if (!m_addedEntryIds.contains(candidateEntry.id) ||
                candidateEntry.id == entryId || !candidateEntry.transitionOnly ||
                candidateEntry.transitionMatype != matype)
            {
                continue;
            }
            QMessageBox::warning(
                nullptr, QStringLiteral("材料编号已被使用"),
                QStringLiteral("材料编号 %1 已作为“%2”的计算阶段编号，不能再施加到网格。")
                    .arg(matype)
                    .arg(candidateEntry.name.trimmed().isEmpty()
                             ? QStringLiteral("未命名材料")
                             : candidateEntry.name.trimmed()));
            return;
        }
    }

    auto findOwnerEntryId = [&](int targetMatype) -> QString
    {
        for (const auto& model : m_models)
        {
            for (const auto& e : model.materials)
            {
                if (m_addedEntryIds.contains(e.id) &&
                    e.appliedMatypes.contains(targetMatype))
                    return e.id;
            }
        }
        return {};
    };

    QString ownerId = m_matypeOwnerById.value(matype);
    if (!ownerId.isEmpty() && !m_addedEntryIds.contains(ownerId))
    {
        m_matypeOwnerById.remove(matype);
        ownerId.clear();
    }
    if (ownerId.isEmpty())
        ownerId = findOwnerEntryId(matype);
    if (!ownerId.isEmpty() && ownerId != entryId)
    {
        MaterialModel* ownerModel = nullptr;
        MaterialEntry* ownerEntry = findEntry(ownerId, &ownerModel);
        if (ownerEntry)
        {
            const QString ownerName = ownerEntry->name.trimmed().isEmpty()
                                          ? QStringLiteral("未命名材料")
                                          : ownerEntry->name.trimmed();
            const auto answer = QMessageBox::question(
                nullptr, QStringLiteral("材料分组已被使用"),
                QStringLiteral(
                    "材料分组 %1 当前属于“%2”。继续施加会将该分组转移到“%3”。是否继续？")
                    .arg(matype)
                    .arg(ownerName, entry->name.trimmed().isEmpty()
                                        ? QStringLiteral("未命名材料")
                                        : entry->name.trimmed()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;

            ownerEntry->appliedMatypes.remove(matype);
            if (m_selectedMatypes.value(ownerId, -1) == matype)
                m_selectedMatypes.remove(ownerId);
            if (m_previewMatypes.value(ownerId, -1) == matype)
                m_previewMatypes.remove(ownerId);
            if (m_preDeleteMatypes.value(ownerId, -1) == matype)
                m_preDeleteMatypes.remove(ownerId);
            syncAutoNumberProperties(ownerModel, ownerEntry);
            refreshEntryWidgetMatypes(ownerId, ownerEntry->appliedMatypes,
                                      m_selectedMatypes.value(ownerId, -1));
            refreshMatypeHighlight(ownerId);
        }
    }

    setSelectedMatypeForEntry(entryId, matype);
    entry->appliedMatypes.insert(matype);
    m_matypeOwnerById.insert(matype, entryId);
    syncAutoNumberProperties(model, entry);

    // 施加后通常清掉 predelete/preview
    setPreviewMatypeForEntry(entryId, -1);
    setPreDeleteMatypeForEntry(entryId, -1);

    refreshEntryWidgetMatypes(entryId, entry->appliedMatypes, matype);
    notifyEntryUpdated(entryId, false);
    refreshMatypeHighlight(entryId);
}

void MaterialManager::removeMatypeFromEntry(const QString& entryId, int matype)
{
    if (matype < 0)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = findEntry(entryId, &model);
    if (!entry)
        return;

    if (entry->appliedMatypes.remove(matype))
    {
        if (m_matypeOwnerById.value(matype) == entryId)
            m_matypeOwnerById.remove(matype);
        if (m_selectedMatypes.value(entryId, -1) == matype)
        {
            m_selectedMatypes.remove(entryId);
        }
        if (m_previewMatypes.value(entryId, -1) == matype)
        {
            m_previewMatypes.remove(entryId);
        }
        if (m_preDeleteMatypes.value(entryId, -1) == matype)
        {
            m_preDeleteMatypes.remove(entryId);
        }

        syncAutoNumberProperties(model, entry);
        refreshEntryWidgetMatypes(entryId, entry->appliedMatypes,
                                  m_selectedMatypes.value(entryId, -1));
        notifyEntryUpdated(entryId, false);
        refreshMatypeHighlight(entryId);
    }
}

void MaterialManager::refreshEntryWidgetMatypes(const QString& entryId, const QSet<int>& applied,
                                                int selectedMatype)
{
    auto* widget = m_entryWidgets.value(entryId, nullptr);
    if (!widget)
    {
        return;
    }
    widget->refreshAppliedMatypes(applied, selectedMatype);
}

bool MaterialManager::generateCaculateFile(
    const QMap<int, int>& damDynamicSourceMappings)
{
    // ===== 1) 收集：只要“已添加到工程 + 关联了 MATYPE”的材料，并建立 matype ->
    // (model, entry) 映射 =====
    struct MatInfo
    {
        MaterialModel* model = nullptr;
        MaterialEntry* entry = nullptr;
    };

    QMap<int, MatInfo> matypeMap; // matype -> info
    int maxMatype = -1;

    for (auto& model : m_models)
    {
        for (auto& entry : model.materials)
        {
            if (!m_addedEntryIds.contains(entry.id))
                continue;

            QSet<int> exportMatypes = entry.appliedMatypes;
            if (entry.transitionOnly && isComputableMatype(entry.transitionMatype))
                exportMatypes.insert(entry.transitionMatype);
            if (exportMatypes.isEmpty())
                continue;

            for (int matype : exportMatypes)
            {
                if (!isComputableMatype(matype))
                    continue;
                maxMatype = qMax(maxMatype, matype);

                const auto existing = matypeMap.constFind(matype);
                if (existing != matypeMap.constEnd() && existing->entry != &entry)
                {
                    QMessageBox::warning(
                        nullptr, QStringLiteral("材料分组重复"),
                        QStringLiteral(
                            "材料分组 %1 同时关联了“%2”和“%3”。请在材料管理中保留一个关联后再计算，系统不会自动选择或覆盖。")
                            .arg(matype)
                            .arg(existing->entry ? existing->entry->name
                                                 : QStringLiteral("未命名材料"),
                                 entry.name));
                    return false;
                }
                matypeMap[matype] = MatInfo{&model, &entry};
            }
        }
    }

    if (maxMatype < 0 || m_workPath.isEmpty())
        return false;
    if (maxMatype > MAX_SOLVER_MATYPE)
    {
        QMessageBox::warning(nullptr, QStringLiteral("材料编号超出范围"),
                             QStringLiteral("材料编号不能超过 300，请先修改网格分组和材料关联。"));
        return false;
    }

    // 本次求解传入的静动关系必须同时指向已配置的坝体静力材料和动力材料。
    // 这里校验的是“工况输入”，不会修改任何 MaterialEntry。
    for (auto mappingIt = damDynamicSourceMappings.constBegin();
         mappingIt != damDynamicSourceMappings.constEnd(); ++mappingIt)
    {
        const int dynamicMatype = mappingIt.key();
        const int sourceMatype = mappingIt.value();
        const auto dynamicIt = matypeMap.constFind(dynamicMatype);
        const auto sourceIt = matypeMap.constFind(sourceMatype);
        const bool dynamicOk =
            isComputableMatype(dynamicMatype) && dynamicIt != matypeMap.constEnd() &&
            dynamicIt->model && dynamicIt->model->key == QStringLiteral("dam_dynamic");
        const bool sourceOk =
            isComputableMatype(sourceMatype) && sourceMatype != dynamicMatype &&
            sourceIt != matypeMap.constEnd() && sourceIt->model &&
            isDamStaticModel(sourceIt->model->key);
        if (!dynamicOk || !sourceOk)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("动力材料关系无效"),
                QStringLiteral("当前工况中的静动材料关系 %1 -> %2 无效。"
                               "请检查静力材料是否已施加到坝体，以及动力材料是否已配置。")
                    .arg(sourceMatype)
                    .arg(dynamicMatype));
            return false;
        }
    }

    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        const MatInfo& info = it.value();
        if (!info.model || !info.entry)
            continue;

        for (int columnIndex = 0; columnIndex < info.model->columns.size(); ++columnIndex)
        {
            const QString& column = info.model->columns.at(columnIndex);
            if (isAutoNumberColumn(column))
                continue;

            // 围岩开挖模型中这两个参数是条件写入 CONMAT 的；未启用时不应因为
            // 历史工程中的空值阻止计算。
            if (info.model->key == QStringLiteral("huangjinping_underground_plant"))
            {
                const QString excavationState =
                    info.entry->properties.value(QStringLiteral("岩类分区"));
                if (column == QStringLiteral("半开挖区荷载释放系数") &&
                    excavationState != QStringLiteral("-1"))
                {
                    continue;
                }
                if (column == QStringLiteral("再生时对应开挖级") &&
                    excavationState != QStringLiteral("1"))
                {
                    continue;
                }
            }

            bool ok = false;
            const double numericValue =
                info.entry->properties.value(column).trimmed().toDouble(&ok);
            if (!ok || !std::isfinite(numericValue))
            {
                const QString displayColumn =
                    columnIndex < info.model->displayColumns.size() &&
                            !info.model->displayColumns.at(columnIndex).trimmed().isEmpty()
                        ? info.model->displayColumns.at(columnIndex)
                        : column;
                QMessageBox::warning(
                    nullptr, QStringLiteral("材料参数不完整"),
                    QStringLiteral("材料“%1”的“%2”必须填写为数值。")
                        .arg(info.entry->name.trimmed().isEmpty()
                                 ? QStringLiteral("未命名材料")
                                 : info.entry->name.trimmed(),
                             displayColumn));
                return false;
            }
        }

        // Semantic validation at export time, including values imported from older projects.
        // Do not silently repair physically invalid model parameters.
        const QString modelKey = info.model->key;
        auto number = [&](const QString& name) {
            return info.entry->properties.value(name).toDouble();
        };
        auto invalid = [&](const QString& message) {
            QMessageBox::warning(nullptr, QStringLiteral("材料参数无效"),
                                 QStringLiteral("材料“%1”（编号%2）：%3")
                                     .arg(info.entry->name).arg(it.key()).arg(message));
            return false;
        };
        const bool elasticSolid = modelKey == QStringLiteral("linear_elastic") ||
                                  modelKey == QStringLiteral("hoek_brown_rock") ||
                                  modelKey == QStringLiteral("drucker_prager_rock") ||
                                  modelKey == QStringLiteral("dagangshan_isotropic") ||
                                  modelKey == QStringLiteral("huangjinping_underground_plant");
        if (modelKey == QStringLiteral("huangjinping_bar_element") &&
            (number(QStringLiteral("变模E1,E2（GPa）")) <= 0.0 ||
             number(QStringLiteral("杆面积（m2）")) <= 0.0 ||
             number(QStringLiteral("容重t/m3")) < 0.0))
            return invalid(QStringLiteral("锚杆的初始弹性模量E1和截面积必须大于0，容重不能为负。"));
        if(modelKey==QStringLiteral("planar_elastic_beam") &&
            (number(QStringLiteral("弹性模量E（GPa）"))<=0 || number(QStringLiteral("截面积A（m2）"))<=0 ||
             number(QStringLiteral("截面惯性矩Iz（m4）"))<=0 || number(QStringLiteral("容重t/m3"))<0))
            return invalid(QStringLiteral("梁的弹性模量、截面积和惯性矩必须大于0，密度不能为负。"));
        if (elasticSolid)
        {
            const double young = number(QStringLiteral("变形模量E（GPa）"));
            const double nu = number(QStringLiteral("泊松比μ"));
            const double density = number(QStringLiteral("容重t/m3"));
            if (young <= 0.0 || density <= 0.0 || nu <= -1.0 || nu >= 0.5)
                return invalid(QStringLiteral("E、容重必须大于0，泊松比必须在(-1, 0.5)内。"));
        }
        if (modelKey == QStringLiteral("huangjinping_underground_plant") &&
            number(QStringLiteral("岩类分区")) == -1.0)
        {
            const double release = number(QStringLiteral("半开挖区荷载释放系数"));
            if (release < 0.0 || release > 1.0)
                return invalid(QStringLiteral("荷载释放系数必须在0～1之间，全释放取1。"));
        }
        if (modelKey == QStringLiteral("hoek_brown_rock"))
        {
            if (number(QStringLiteral("岩体常数mb")) <= 0.0 ||
                number(QStringLiteral("岩体常数s")) <= 0.0 ||
                number(QStringLiteral("岩体常数a")) <= 0.0 ||
                number(QStringLiteral("岩体常数a")) > 1.0 ||
                number(QStringLiteral("完整岩石单轴抗压强度σci（MPa）")) <= 0.0)
                return invalid(QStringLiteral("Hoek–Brown要求mb、s、σci大于0，且0<a<=1。"));
        }
        // SOLVER READ_IMAT unconditionally forces CONMAT(2,1)=0 (linear elastic).
        if (it.key() == 2 &&
            modelKey != QStringLiteral("linear_elastic") &&
            modelKey != QStringLiteral("planar_elastic_beam"))
            return invalid(QStringLiteral(
                "材料编号 2 在求解器中固定为线弹性本构（CONMAT(2,1)=0），"
                "请勿将非线性或土体本构分配为编号 2，请改用编号 3 及以上。"));

        if (modelKey == QStringLiteral("drucker_prager_rock"))
        {
            const double dpType = number(QStringLiteral("DP子类型"));
            const double friction = number(QStringLiteral("内摩擦角φ（°）"));
            const double cohesion = number(QStringLiteral("凝聚力C‘（MPa）"));
            if (dpType < 1 || dpType > 6 || std::floor(dpType) != dpType ||
                friction < 0 || friction >= 90 ||
                cohesion < 0 ||
                number(QStringLiteral("剪胀角(°)")) < 0 ||
                number(QStringLiteral("剪胀角(°)")) >= 90 ||
                number(QStringLiteral("抗压强度Rc（MPa）")) < 0 ||
                number(QStringLiteral("抗拉强度Rt（MPa）")) < 0)
                return invalid(QStringLiteral("D-P子类型须为1～6，角度须在[0,90)内，强度不可为负。"));
            if (friction <= 1.0e-5 && cohesion <= 1.0e-9)
                return invalid(QStringLiteral("内摩擦角和凝聚力不能同时为0。"));
        }
        if (modelKey == QStringLiteral("dagangshan_isotropic") ||
            modelKey == QStringLiteral("huangjinping_underground_plant"))
        {
            const double friction = number(QStringLiteral("抗剪断强度内摩擦系数"));
            const double cohesion = number(QStringLiteral("凝聚力C‘（MPa）"));
            if (friction < 0 || cohesion < 0 ||
                number(QStringLiteral("剪胀角(°)")) < 0 ||
                number(QStringLiteral("剪胀角(°)")) >= 90)
                return invalid(QStringLiteral("摩擦系数、凝聚力和剪胀角不能为负，剪胀角须小于90°。"));
            if (modelKey == QStringLiteral("huangjinping_underground_plant") &&
                friction <= 1.0e-5 && cohesion <= 1.0e-9)
                return invalid(QStringLiteral("摩擦系数和凝聚力不能同时为0。"));
        }
        if (modelKey == QStringLiteral("dagangshan_isotropic"))
        {
            if (number(QStringLiteral("残余抗剪断强度内摩擦系数")) < 0 ||
                number(QStringLiteral("残余凝聚力C‘（MPa）")) < 0 ||
                number(QStringLiteral("抗压强度Rc（MPa）")) < 0 ||
                number(QStringLiteral("抗拉强度Rt（MPa）")) < 0)
                return invalid(QStringLiteral("残余强度及抗拉、抗压强度不能为负。"));
            const QString frictionFlag = info.entry->properties.value(
                QStringLiteral("残余摩擦系数沿用峰值"), QStringLiteral("1"));
            const QString cohesionFlag = info.entry->properties.value(
                QStringLiteral("残余凝聚力沿用峰值"), QStringLiteral("1"));
            if ((frictionFlag != QStringLiteral("0") && frictionFlag != QStringLiteral("1")) ||
                (cohesionFlag != QStringLiteral("0") && cohesionFlag != QStringLiteral("1")))
                return invalid(QStringLiteral("残余强度继承选项必须为0或1。"));
            const double residualFriction = frictionFlag == QStringLiteral("0")
                                                ? number(QStringLiteral("残余抗剪断强度内摩擦系数"))
                                                : number(QStringLiteral("抗剪断强度内摩擦系数"));
            const double residualCohesion = cohesionFlag == QStringLiteral("0")
                                                ? number(QStringLiteral("残余凝聚力C‘（MPa）"))
                                                : number(QStringLiteral("凝聚力C‘（MPa）"));
            // The solver rejects both residual friction and cohesion approaching zero.
            if (residualFriction <= 1.0e-5 && residualCohesion <= 1.0e-9)
                return invalid(QStringLiteral("残余摩擦系数和残余凝聚力不能同时为0。"));
        }
        if (isDamStaticModel(modelKey) && modelKey != QStringLiteral("linear_elastic"))
        {
            if (number(QStringLiteral("干容重(t/m3)")) <= 0.0 ||
                number(QStringLiteral("天然容重(t/m3)")) <= 0.0 ||
                number(QStringLiteral("饱和容重(t/m3)")) <= 0.0)
                return invalid(QStringLiteral("干、天然、饱和容重均须大于0。"));
            if (modelKey == QStringLiteral("soil_eb") ||
                modelKey == QStringLiteral("soil_emu") ||
                modelKey == QStringLiteral("soil_emu_crush") ||
                modelKey == QStringLiteral("soil_nanshui"))
            {
                if (number(QStringLiteral("K")) <= 0.0001)
                    return invalid(QStringLiteral("土体模量系数 K 必须大于 0.0001。"));
            }
            else if (modelKey == QStringLiteral("soil_kg"))
            {
                if (number(QStringLiteral("Ki,Kie(10^5Pa)")) <= 0.0001)
                    return invalid(QStringLiteral("K-G 本构体积刚度参数 Ki 必须大于 0.0001。"));
            }
        }
        if (modelKey == QStringLiteral("dam_dynamic"))
        {
            if (number(QStringLiteral("DRYK1(KPa)")) <= 0.0001)
                return invalid(QStringLiteral("坝料动力刚度系数 DRYK1 必须大于 0.0001（无量纲）。"));
            if (std::lround(number(QStringLiteral("是否液化"))) == 1 &&
                (number(QStringLiteral("RATIAO1(0.1)")) <= 1.0e-5 ||
                 number(QStringLiteral("RATIAO2(0.3)")) <= 1.0e-5 ||
                 number(QStringLiteral("RATIAO3(0.5)")) <= 1.0e-5))
                return invalid(QStringLiteral("启用液化时，三个液化循环次数参数均须大于 0。"));
        }

        // SOLVER 的 CONMAT(14..22) 被渗流方向矩阵与前三类流变参数复用。
        // 同时导出会覆盖参数，禁止这种无法无损表达的组合。
        const bool hasSeepage = !info.entry->seepageModelKey.isEmpty();
        const QString creepKey = info.entry->creepModelKey;
        if (creepKey == QStringLiteral("xiyuan_creep"))
            return invalid(QStringLiteral(
                "西源流变模型（ICREEP=4）会覆盖基体弹性参数且当前求解器分支恒输出零蠕变应变，"
                "请改用其他流变模型或取消附加西源流变。"));
        if (hasSeepage && !creepKey.isEmpty())
            return invalid(QStringLiteral(
                "渗流与当前流变模型共用求解器参数位置，不能同时设置在同一种材料上。"
                "请拆分计算工况或取消其中一个附加模型。"));

        const QStringList supplementalKeys = {info.entry->seepageModelKey,
                                              info.entry->creepModelKey};
        for (const QString& supplementalKey : supplementalKeys)
        {
            if (supplementalKey.isEmpty())
                continue;
            const MaterialModel* supplementalModel = nullptr;
            for (const auto& candidate : m_seepageModels)
            {
                if (candidate.key == supplementalKey)
                {
                    supplementalModel = &candidate;
                    break;
                }
            }
            if (!supplementalModel)
                continue;
            for (int columnIndex = 0; columnIndex < supplementalModel->columns.size();
                 ++columnIndex)
            {
                const QString& column = supplementalModel->columns.at(columnIndex);
                if (isAutoNumberColumn(column))
                    continue;
                bool ok = false;
                const double numericValue =
                    info.entry->seepageProperties.value(column).trimmed().toDouble(&ok);
                const QString displayColumn =
                    columnIndex < supplementalModel->displayColumns.size() &&
                            !supplementalModel->displayColumns.at(columnIndex).trimmed().isEmpty()
                        ? supplementalModel->displayColumns.at(columnIndex)
                        : column;
                if (!ok || !std::isfinite(numericValue))
                {
                    QMessageBox::warning(
                        nullptr, QStringLiteral("材料参数不完整"),
                        QStringLiteral("材料“%1”的“%2”必须填写为数值。")
                            .arg(info.entry->name.trimmed().isEmpty()
                                     ? QStringLiteral("未命名材料")
                                     : info.entry->name.trimmed(),
                                 displayColumn));
                    return false;
                }

                const bool isPermeability =
                    column == QStringLiteral("渗透系数K(Lu)") ||
                    column == QStringLiteral("渗透系数Kx(Lu)") ||
                    column == QStringLiteral("渗透系数Ky(Lu)") ||
                    column == QStringLiteral("渗透系数Kz(Lu)");
                const bool mustBePositive = isPermeability ||
                                            column == QStringLiteral("比储量S") ||
                                            column == QStringLiteral("允许比降");
                if (mustBePositive && numericValue <= 0.0)
                {
                    QMessageBox::warning(
                        nullptr, QStringLiteral("渗流参数无效"),
                        QStringLiteral("材料“%1”的“%2”必须大于 0。")
                            .arg(info.entry->name.trimmed().isEmpty()
                                     ? QStringLiteral("未命名材料")
                                     : info.entry->name.trimmed(),
                                 displayColumn));
                    return false;
                }
            }
        }

        if (info.model->key == QStringLiteral("huangjinping_underground_plant"))
        {
            bool sourceOk = false;
            const double sourceValue =
                info.entry->properties.value(QStringLiteral("母岩材料编号")).toDouble(&sourceOk);
            const int sourceMatype =
                sourceOk ? static_cast<int>(std::llround(sourceValue)) : it.key();
            if (sourceOk && sourceValue >= FIRST_COMPUTABLE_MATYPE &&
                (std::fabs(sourceValue - sourceMatype) > 1.0e-8 ||
                 !matypeMap.contains(sourceMatype)))
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("围岩材料设置不完整"),
                    QStringLiteral("材料“%1”的母岩材料编号必须对应已配置的材料；留空或填0时默认为当前材料。")
                        .arg(info.entry->name.trimmed().isEmpty()
                                 ? QStringLiteral("未命名材料")
                                 : info.entry->name.trimmed()));
                return false;
            }
        }
    }

    // ===== 2) NMAT / NCON =====
    const int ncon = DEFAULT_NCON;
    const int nmatReal = maxMatype;                        // 1..maxMatype
    const int nmat = qMax(MIN_NMAT, ((nmatReal + 9) / 10) * 10); // 补齐到 10 的倍数（每组 10 列）

    // ===== 3) 输出文件 =====
    QDir workDir(SolverFilePaths::outputInputDirectory(m_workPath));
    if (!QDir().mkpath(workDir.absolutePath()))
        return false;
    const QString filePath = workDir.filePath(QStringLiteral("IMAT.DAT"));

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);

    // ===== 4) 预先构造数值表：values[row(0..199)][matIndex(0..nmat-1)] =====
    QVector<QVector<double>> values(ncon, QVector<double>(nmat, 0.0));

    // Fortran: CONMAT=0.0 后，再补全局默认值 + 材料1默认模板
    auto setValue = [&](int row, int matIndex, double value)
    {
        if (row >= 1 && row <= ncon && matIndex >= 0 && matIndex < nmat)
        {
            values[row - 1][matIndex] = value;
        }
    };

    auto setAllMatRow = [&](int row, double value)
    {
        if (row < 1 || row > ncon)
            return;
        for (int matIndex = 0; matIndex < nmat; ++matIndex)
        {
            values[row - 1][matIndex] = value;
        }
    };

    // ===== 4.1) 对应 Fortran 里的全体材料默认值 =====
    // CONMAT(:,65)=1.3   !! RATIO_E
    // CONMAT(:,66)=1.0   !! RATIO_C
    // CONMAT(:,75)=1.0   !! DRYM
    // CONMAT(:,14)=1.0   !! Q(1,1)
    // CONMAT(:,18)=1.0   !! Q(2,2)
    // CONMAT(:,22)=1.0   !! Q(3,3)
    setAllMatRow(65, 1.3);
    setAllMatRow(66, 1.0);
    setAllMatRow(75, 1.0);
    setAllMatRow(14, 1.0);
    setAllMatRow(18, 1.0);
    setAllMatRow(22, 1.0);

    // ===== 4.2) 对应 Fortran 里的材料1默认值（VOID M1）=====
    // 注意：这里是 IMAT=1，对应 matIndex=0
    if (nmat >= 1)
    {
        const int m1 = 0;

        setValue(2,   m1, 100.0);       // E//
        setValue(3,   m1, 100.0);       // E-|
        setValue(4,   m1, 0.30);        // POU
        setValue(5,   m1, 100.0);       // jcr
        setValue(7,   m1, 0.0);         // ROU
        setValue(8,   m1, 1.0);         // TE

        setValue(9,   m1, 2000.0e6);    // RC
        setValue(10,  m1, 200.0e6);     // RT
        setValue(11,  m1, 78.0);        // FAI
        setValue(12,  m1, 200.0e6);     // CO

        setValue(60,  m1, 100.0);       // Rip

        setValue(91,  m1, 2000.0e6);    // RC
        setValue(92,  m1, 200.0e6);     // RT
        setValue(94,  m1, 78.0);        // FAIP
        setValue(95,  m1, 200.0e6);     // COP

        setValue(96,  m1, 0.0001e-7);   // K1
        setValue(97,  m1, 0.0001e-7);   // K2
        setValue(98,  m1, 0.0001e-7);   // K3
        setValue(101, m1, 1.0);         // SPECIFIC STORAGE S
    }

    auto valueOf = [](const MaterialEntry* entry, const QString& key) -> double
    {
        if (!entry)
        {
            return 0.0;
        }
        bool ok = false;
        const double value = entry->properties.value(key, QStringLiteral("0")).toDouble(&ok);
        return ok ? value : 0.0;
    };

    auto seepageValueOf = [](const MaterialEntry* entry, const QString& key) -> double
    {
        if (!entry)
        {
            return 0.0;
        }
        bool ok = false;
        const double value = entry->seepageProperties.value(key, QStringLiteral("0")).toDouble(&ok);
        return ok ? value : 0.0;
    };

    auto applySeepageModel = [&](const QString& seepageModelKey, const MaterialEntry* entry,
                                  int matIndex)
    {
        auto setRow = [&](int row, double value)
        {
            if (row >= 1 && row <= ncon)
            {
                values[row - 1][matIndex] = value;
            }
        };

        if (seepageModelKey == QStringLiteral("jk_corewall_steady_seepage"))
        {
            const double tol = 1.0e-3;
            const double akLu = seepageValueOf(entry, QStringLiteral("渗透系数K(Lu)"));
            const double specificStorage = seepageValueOf(entry, QStringLiteral("比储量S"));
            const double rip = seepageValueOf(entry, QStringLiteral("允许比降"));
            const double ak = akLu * 1.0e-7;
            setRow(96, ak);
            setRow(97, ak);
            setRow(98, ak);
            setRow(101, specificStorage);
            setRow(60, rip);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            if (values[7 - 1][matIndex] < tol)
                setRow(7, 2.0e3);
            if (values[89 - 1][matIndex] < 100.0)
                setRow(89, qMax(1.0e3, values[7 - 1][matIndex]));
            if (values[90 - 1][matIndex] < tol)
                setRow(90, 2.0e3);
            return;
        }
        if (seepageModelKey == QStringLiteral("jk_corewall_anisotropic_seepage"))
        {
            const double tol = 1.0e-3;
            const double kx = seepageValueOf(entry, QStringLiteral("渗透系数Kx(Lu)")) * 1.0e-7;
            const double ky = seepageValueOf(entry, QStringLiteral("渗透系数Ky(Lu)")) * 1.0e-7;
            const double kz = seepageValueOf(entry, QStringLiteral("渗透系数Kz(Lu)")) * 1.0e-7;
            const double specificStorage = seepageValueOf(entry, QStringLiteral("比储量S"));
            const double rip = seepageValueOf(entry, QStringLiteral("允许比降"));
            setRow(96, kx);
            setRow(97, ky);
            setRow(98, kz);
            setRow(101, specificStorage);
            setRow(60, rip);
            // 求解器当前直接按全局 X/Y/Z 使用 Kx/Ky/Kz，方向矩阵保持单位阵。
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            if (values[7 - 1][matIndex] < tol)
                setRow(7, 2.0e3);
            if (values[89 - 1][matIndex] < 100.0)
                setRow(89, qMax(1.0e3, values[7 - 1][matIndex]));
            if (values[90 - 1][matIndex] < tol)
                setRow(90, 2.0e3);
            return;
        }
        if (seepageModelKey == QStringLiteral("shenzhujiang_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数α")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数b")));
            setRow(16, seepageValueOf(entry, QStringLiteral("剪切流变系数d")));
            setRow(102, 1.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("liguoying_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数α")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数b")));
            setRow(16, seepageValueOf(entry, QStringLiteral("体积流变系数c")));
            setRow(17, seepageValueOf(entry, QStringLiteral("剪切流变系数d")));
            setRow(18, seepageValueOf(entry, QStringLiteral("体积流变指数m1")));
            setRow(19, seepageValueOf(entry, QStringLiteral("体积流变指数m2")));
            setRow(20, seepageValueOf(entry, QStringLiteral("剪切流变指数m3")));
            setRow(102, 2.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("nine_param_creep"))
        {
            setRow(14, seepageValueOf(entry, QStringLiteral("应变速率系数λv")));
            setRow(15, seepageValueOf(entry, QStringLiteral("体积流变系数Cα")));
            setRow(16, seepageValueOf(entry, QStringLiteral("体积流变系数Cβ")));
            setRow(17, seepageValueOf(entry, QStringLiteral("体积流变指数dα")));
            setRow(18, seepageValueOf(entry, QStringLiteral("体积流变指数dβ")));
            setRow(19, seepageValueOf(entry, QStringLiteral("主应变系数c")));
            setRow(20, seepageValueOf(entry, QStringLiteral("主应变系数d")));
            setRow(21, seepageValueOf(entry, QStringLiteral("主应变流变系数η")));
            setRow(22, seepageValueOf(entry, QStringLiteral("主应变流变系数m")));
            setRow(102, 3.0);
            return;
        }
        if (seepageModelKey == QStringLiteral("xiyuan_creep"))
        {
            setRow(2, seepageValueOf(entry, QStringLiteral("瞬时弹性模量E1（GPa）")) * 1.0e9);
            setRow(3, seepageValueOf(entry, QStringLiteral("粘塑性模量E2（GPa）")) * 1.0e9);
            setRow(4, seepageValueOf(entry, QStringLiteral("泊松比μ1")));
            setRow(5, seepageValueOf(entry, QStringLiteral("泊松比μ2")));
            setRow(103, seepageValueOf(entry, QStringLiteral("粘弹性系数η1")));
            setRow(104, seepageValueOf(entry, QStringLiteral("粘塑性系数η2")));
            setRow(102, 4.0);
        }
    };

    auto applySupplementalModels = [&](const MaterialEntry* entry, int matIndex)
    {
        if (!entry)
            return;
        applySeepageModel(entry->seepageModelKey, entry, matIndex);
        applySeepageModel(entry->creepModelKey, entry, matIndex);
    };

    // 填值：黄金坪地下厂房模型按 CONMAT 行号写入，其他模型仍走 columnOutputRows
    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        const int matype = it.key();
        const MatInfo& info = it.value();
        if (!info.model || !info.entry)
            continue;
        // IMAT=1 is reserved as VOID/no-calculation material.
        if (!isComputableMatype(matype) || matype > nmat)
            continue;

        const int matIndex = matype - 1;

        if (info.model->key == QStringLiteral("linear_elastic"))
        {
            bool youngOk = false, poissonOk = false, densityOk = false;
            const double youngGpa = info.entry->properties.value(QStringLiteral("变形模量E（GPa）")).toDouble(&youngOk);
            const double poisson = info.entry->properties.value(QStringLiteral("泊松比μ")).toDouble(&poissonOk);
            const double density = info.entry->properties.value(QStringLiteral("容重t/m3")).toDouble(&densityOk);
            if (!youngOk || !poissonOk || !densityOk || !std::isfinite(youngGpa) ||
                !std::isfinite(poisson) || !std::isfinite(density) || youngGpa <= 0 ||
                poisson <= -1 || poisson >= 0.5 || density < 0)
            {
                QMessageBox::warning(nullptr, QStringLiteral("请完善线弹性材料"),
                    QStringLiteral("材料“%1”需要正的弹性模量、-1至0.5之间的泊松比和非负容重。")
                        .arg(info.entry->name));
                return false;
            }
            const double young = youngGpa * 1.0e9;
            values[0][matIndex] = 0.0; // Native IBUCK=0: pure elastic continuum.
            values[1][matIndex] = young;
            values[2][matIndex] = young;
            values[3][matIndex] = poisson;
            values[4][matIndex] = poisson;
            values[5][matIndex] = young / (2.0 * (1.0 + poisson));
            values[6][matIndex] = density * 1.0e3;
            values[88][matIndex] = density * 1.0e3; // WETROU
            values[89][matIndex] = density * 1.0e3; // PACROU
            values[7][matIndex] = 1.0;
            // Solver's CANYU_CF rejects materials whose DRYFAI_P (row 94) and DRYCO_P (row 95)
            // are both < 0.001 even for pure elastic IBUCK=0 materials; provide large
            // non-yielding nominal strengths matching solver default IMAT=1.
            values[8][matIndex] = 2.0e9;
            values[9][matIndex] = 2.0e8;
            values[10][matIndex] = 78.0;
            values[11][matIndex] = 2.0e8;
            values[93][matIndex] = 78.0;
            values[94][matIndex] = 2.0e8;
            values[83][matIndex] = matype;
            applySupplementalModels(info.entry, matIndex);
            continue;
        }
        if (info.model->key == QStringLiteral("hoek_brown_rock"))
        {
            const double ee = valueOf(info.entry, QStringLiteral("变形模量E（GPa）")) * 1.0e9;
            const double pu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rou = valueOf(info.entry, QStringLiteral("容重t/m3")) * 1.0e3;
            const double fmb = valueOf(info.entry, QStringLiteral("岩体常数mb"));
            const double fa = valueOf(info.entry, QStringLiteral("岩体常数a"));
            const double fs = valueOf(info.entry, QStringLiteral("岩体常数s"));
            const double sgci =
                valueOf(info.entry, QStringLiteral("完整岩石单轴抗压强度σci（MPa）")) *
                1.0e6;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                    values[row - 1][matIndex] = value;
            };
            setRow(1, 11.0);
            setRow(2, ee);
            setRow(3, ee);
            setRow(4, pu);
            setRow(5, pu);
            setRow(6, std::fabs(1.0 + pu) > 1.0e-12 ? ee / 2.0 / (1.0 + pu) : 0.0);
            setRow(7, rou);
            setRow(8, 1.0);
            setRow(13, 0.0);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            setRow(23, 0.0);
            setRow(35, 0.0);
            setRow(64, 0.0);
            setRow(65, 1.0);
            setRow(66, 1.0);
            setRow(70, fa);
            setRow(75, fmb);
            setRow(76, fs);
            setRow(84, static_cast<double>(matype));
            setRow(115, sgci);
            MaterialStrengthConversion::HoekBrownEquivalent equivalent;
            if (!MaterialStrengthConversion::hoekBrown(fmb, fs, fa, sgci / 1.0e6,
                                                        &equivalent))
            {
                QMessageBox::warning(nullptr, QStringLiteral("材料参数无效"),
                                     QStringLiteral("霍克-布朗材料“%1”无法生成强度参数。")
                                         .arg(info.entry->name));
                return false;
            }
            setRow(9, equivalent.uniaxialCompressivePa);
            setRow(10, equivalent.tensilePa);
            setRow(11, equivalent.frictionAnglesDeg[0]);
            setRow(12, equivalent.cohesionPa);
            setRow(91, equivalent.uniaxialCompressivePa);
            setRow(92, equivalent.tensilePa);
            setRow(94, equivalent.frictionAnglesDeg[0]);
            setRow(95, equivalent.cohesionPa);
            setRow(99, equivalent.frictionAnglesDeg[0]);
            for (int i = 0; i < 10; ++i)
                setRow(116 + i, equivalent.frictionAnglesDeg[i]);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("drucker_prager_rock"))
        {
            const double ee = valueOf(info.entry, QStringLiteral("变形模量E（GPa）")) * 1.0e9;
            const double nu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rho = valueOf(info.entry, QStringLiteral("容重t/m3")) * 1.0e3;
            const double phi = valueOf(info.entry, QStringLiteral("内摩擦角φ（°）"));
            const double cohesion = valueOf(info.entry, QStringLiteral("凝聚力C‘（MPa）")) * 1.0e6;
            const double psi = valueOf(info.entry, QStringLiteral("剪胀角(°)"));
            const double dpType = valueOf(info.entry, QStringLiteral("DP子类型"));
            double rc = valueOf(info.entry, QStringLiteral("抗压强度Rc（MPa）")) * 1.0e6;
            double rt = valueOf(info.entry, QStringLiteral("抗拉强度Rt（MPa）")) * 1.0e6;
            // The same legacy Rc/Rt auto approximation used by Mohr-Coulomb.
            const double angle = phi * 3.14159265358979323846 / 180.0;
            const double cosPhi = std::cos(angle), sinPhi = std::sin(angle);
            if (rc <= 0.0)
                rc = cohesion > CO_THRESHOLD ? 2.0 * cohesion * cosPhi / (1.0 - sinPhi) : 2.0;
            if (rt <= 0.0)
                rt = cohesion > CO_THRESHOLD ?
                    qMax(cohesion, 2.0 * cohesion * cosPhi / (1.0 + sinPhi)) : 1.0;
            auto setRow = [&](int row, double value) { values[row - 1][matIndex] = value; };
            setRow(1, 3.0);
            setRow(2, ee); setRow(3, ee);
            setRow(4, nu); setRow(5, nu);
            setRow(6, ee / (2.0 * (1.0 + nu)));
            setRow(7, rho); setRow(8, 1.0);
            setRow(9, rc); setRow(10, rt);
            setRow(11, phi); setRow(12, cohesion);
            setRow(13, 0.0); setRow(14, 1.0); setRow(18, 1.0); setRow(22, 1.0);
            setRow(65, 1.0); setRow(66, 1.0);
            setRow(84, static_cast<double>(matype));
            setRow(91, rc); setRow(92, rt);
            setRow(94, phi); setRow(95, cohesion);
            setRow(99, psi);
            setRow(126, dpType);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("huangjinping_underground_plant")) // 黄金坪厂房(样表1 2)
        {
            double matypeOri = valueOf(info.entry, QStringLiteral("母岩材料编号"));
            if (qRound64(matypeOri) < FIRST_COMPUTABLE_MATYPE)
                matypeOri = static_cast<double>(matype);
            const double itagEx = valueOf(info.entry, QStringLiteral("岩类分区"));
            const double alfaBa = valueOf(info.entry, QStringLiteral("半开挖区荷载释放系数"));
            const double laReborn = valueOf(info.entry, QStringLiteral("再生时对应开挖级"));
            const double eeGpa = valueOf(info.entry, QStringLiteral("变形模量E（GPa）"));
            const double pu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rouTm3 = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double ff = valueOf(info.entry, QStringLiteral("抗剪断强度内摩擦系数"));
            const double coMpa = valueOf(info.entry, QStringLiteral("凝聚力C‘（MPa）"));
            const double dilationAngleInput = valueOf(info.entry, QStringLiteral("剪胀角(°)"));
            const double alfa0 = valueOf(info.entry, QStringLiteral("洞室边缘松弛系数α0"));
            const double d0 = valueOf(info.entry, QStringLiteral("松弛距离d0"));

            const double ibuck = 2.0;
            const double alfa1 = 1.0;

            const double ee = eeGpa * 1.0e9;
            const double rou = rouTm3 * 1.0e3;
            const double fai = std::atan(ff) * 180.0 / 3.141592653589793;
            const double dilationAngle = dilationAngleInput;
            const double co = coMpa * 1.0e6;

            // 默认替代：用 Mohr-Coulomb 近似计算 RC/RT（原 DEFAULT_RTRC 未给出）
            double rt = 0.0;
            double rc = 0.0;

            if (co > CO_THRESHOLD)
            {
                const double faiRad = fai * 3.141592653589793 / 180.0;
                const double sinF = std::sin(faiRad);
                const double cosF = std::cos(faiRad);

                rt = 2.0 * co * cosF / (1.0 + sinF);
                rc = 2.0 * co * cosF / (1.0 - sinF);

                if (rt < co)
                    rt = co;
            }
            else
            {
                rt = 1.0;
                rc = 2.0;
            }

            const double gg = (std::fabs(1.0 + pu) > 1.0e-12) ? (ee / 2.0 / (1.0 + pu)) : 0.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, ibuck);
            setRow(2, ee);
            setRow(3, ee);
            setRow(4, pu);
            setRow(5, pu);
            setRow(6, gg);
            setRow(7, rou);
            setRow(8, 1.0);
            setRow(9, rc);
            setRow(10, rt);
            setRow(11, fai);
            setRow(12, co);
            setRow(13, 0.0);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            setRow(23, 0.0);
            setRow(35, 0.0);
            setRow(64, 0.0);
            setRow(65, 1.3);
            setRow(66, 1.0);
            setRow(67, 0.05);
            setRow(77, alfa0);
            setRow(78, d0);
            setRow(79, alfa1);
            setRow(84, matypeOri);
            setRow(91, rc);
            setRow(92, rt);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, dilationAngle);
            setRow(100, itagEx);

            if (qRound64(itagEx) == -1)
            {
                setRow(58, alfaBa);
            }
            if (qRound64(itagEx) == 1)
            {
                setRow(59, laReborn);
            }
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("dagangshan_isotropic")) // 大岗山各向同性
        {
            const double eeGpa = valueOf(info.entry, QStringLiteral("变形模量E（GPa）"));
            const double pu = valueOf(info.entry, QStringLiteral("泊松比μ"));
            const double rouTm3 = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double ff = valueOf(info.entry, QStringLiteral("抗剪断强度内摩擦系数"));
            const double coMpa = valueOf(info.entry, QStringLiteral("凝聚力C‘（MPa）"));
            const double rcInputMpa = valueOf(info.entry, QStringLiteral("抗压强度Rc（MPa）"));
            const double rtInputMpa = valueOf(info.entry, QStringLiteral("抗拉强度Rt（MPa）"));
            const double residualFf =
                valueOf(info.entry, QStringLiteral("残余抗剪断强度内摩擦系数"));
            const double residualCoMpa =
                valueOf(info.entry, QStringLiteral("残余凝聚力C‘（MPa）"));
            const double dilationAngle = valueOf(info.entry, QStringLiteral("剪胀角(°)"));

            const double ibuck = 2.0;
            const double matypeOri = static_cast<double>(matype);
            const double itagEx = 0.0;
            const double alfaBa = 0.0;
            const double laReborn = 0.0;

            const double ee = eeGpa * 1.0e9;
            const double rou = rouTm3 * 1.0e3;
            const double co = coMpa * 1.0e6;
            const double fai = std::atan(ff) * 180.0 / 3.141592653589793;
            const double gg = (std::fabs(1.0 + pu) > 1.0e-12) ? (ee / 2.0 / (1.0 + pu)) : 0.0;

            // Rc/Rt 填 0 时保持旧版自动推导；大于 0 时使用用户独立输入。
            double rt = rtInputMpa > 0.0 ? rtInputMpa * 1.0e6 : 0.0;
            double rc = rcInputMpa > 0.0 ? rcInputMpa * 1.0e6 : 0.0;

            if (rt <= 0.0 || rc <= 0.0)
            {
                double autoRt = 0.0;
                double autoRc = 0.0;
                if (co > CO_THRESHOLD)
                {
                    const double faiRad = fai * 3.141592653589793 / 180.0;
                    const double sinF = std::sin(faiRad);
                    const double cosF = std::cos(faiRad);

                    autoRt = 2.0 * co * cosF / (1.0 + sinF);
                    autoRc = 2.0 * co * cosF / (1.0 - sinF);
                    if (autoRt < co)
                        autoRt = co;
                }
                else
                {
                    autoRt = 1.0;
                    autoRc = 2.0;
                }

                if (rt <= 0.0)
                    rt = autoRt;
                if (rc <= 0.0)
                    rc = autoRc;
            }

            const bool inheritResidualFriction =
                info.entry->properties.value(QStringLiteral("残余摩擦系数沿用峰值"),
                                              QStringLiteral("1")) != QStringLiteral("0");
            const bool inheritResidualCohesion =
                info.entry->properties.value(QStringLiteral("残余凝聚力沿用峰值"),
                                              QStringLiteral("1")) != QStringLiteral("0");
            const double faiResidual = inheritResidualFriction
                ? fai : std::atan(residualFf) * 180.0 / 3.141592653589793;
            const double coResidual = inheritResidualCohesion ? co : residualCoMpa * 1.0e6;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, ibuck);
            setRow(2, ee);
            setRow(3, ee);
            setRow(4, pu);
            setRow(5, pu);
            setRow(6, gg);
            setRow(7, rou);
            setRow(8, 1.0);
            setRow(9, rc);
            setRow(10, rt);
            setRow(11, fai);
            setRow(12, co);
            setRow(13, 0.0);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);
            setRow(23, 0.0);
            setRow(35, 0.0);
            setRow(64, 0.0);
            setRow(65, 1.3);
            setRow(66, 1.0);
            setRow(67, 0.05);
            setRow(84, matypeOri);
            setRow(91, rc);
            setRow(92, rt);
            setRow(94, faiResidual);
            setRow(95, coResidual);
            setRow(99, dilationAngle);
            setRow(100, itagEx);

            if (qRound64(itagEx) == -1)
            {
                setRow(58, alfaBa);
            }
            if (qRound64(itagEx) == 1)
            {
                setRow(59, laReborn);
            }
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_eb")) // 土体E-B模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0(°)"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryNur = valueOf(info.entry, QStringLiteral("Nur"));
            const double dryKb = valueOf(info.entry, QStringLiteral("Kb"));
            const double dryM = valueOf(info.entry, QStringLiteral("m"));


            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -1.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryNur);
            setRow(74, dryKb);
            setRow(75, dryM);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(99, fai);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_emu")) // 土体E-μ模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0(°)"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryNur = valueOf(info.entry, QStringLiteral("Nur"));
            const double g = valueOf(info.entry, QStringLiteral("G"));
            const double d = valueOf(info.entry, QStringLiteral("D"));
            const double f = valueOf(info.entry, QStringLiteral("F"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -3.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryNur);
            setRow(77, g);             // 对应 DRYG
            setRow(78, d);             // 对应 DRYD
            setRow(79, f);             // 对应 DRYF
            setRow(75, dryN);
            setRow(76, dFai);
            setRow(78, d);
            setRow(79, f);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_emu_crush")) // 土体E-μ破碎模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0(°)"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryNur = valueOf(info.entry, QStringLiteral("Nur"));
            const double cbk1 = valueOf(info.entry, QStringLiteral("C1"));
            const double cn1 = valueOf(info.entry, QStringLiteral("n1"));
            const double cbk2 = valueOf(info.entry, QStringLiteral("C2"));
            const double cn2 = valueOf(info.entry, QStringLiteral("n2"));
            const double cbk3 = valueOf(info.entry, QStringLiteral("C3"));
            const double cn3 = valueOf(info.entry, QStringLiteral("n3"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -6.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryNur);
            setRow(75, dryN);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(109, cbk1);
            setRow(110, cn1);
            setRow(111, cbk2);
            setRow(112, cn2);
            setRow(113, cbk3);
            setRow(114, cn3);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_nanshui")) // 南水模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double fai = valueOf(info.entry, QStringLiteral("φ0"));
            const double dFai = valueOf(info.entry, QStringLiteral("Δφ"));
            const double coKpa = valueOf(info.entry, QStringLiteral("c(KPa)"));
            const double rf = valueOf(info.entry, QStringLiteral("Rf"));
            const double dryK = valueOf(info.entry, QStringLiteral("K"));
            const double dryN = valueOf(info.entry, QStringLiteral("n"));
            const double dryKur = valueOf(info.entry, QStringLiteral("Kur"));
            const double dryNur = valueOf(info.entry, QStringLiteral("Nur"));
            const double dryCd = valueOf(info.entry, QStringLiteral("cd"));
            const double dryNd = valueOf(info.entry, QStringLiteral("nd"));
            const double dryRd = valueOf(info.entry, QStringLiteral("Rd"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;
            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -5.0);
            setRow(7, dryRou);
            setRow(8, 1.0);
            setRow(89, wetRou);
            setRow(90, pacRou);
            setRow(69, dryK);
            setRow(70, dryN);
            setRow(71, rf);
            setRow(72, dryKur);
            setRow(73, dryNur);
            setRow(77, dryCd);
            setRow(78, dryNd);
            setRow(79, dryRd);
            setRow(76, dFai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("soil_kg")) // 土体K-G模型
        {
            const double dryRouTm3 = valueOf(info.entry, QStringLiteral("干容重(t/m3)"));
            const double pacRouTm3 = valueOf(info.entry, QStringLiteral("天然容重(t/m3)"));
            const double wetRouTm3 = valueOf(info.entry, QStringLiteral("饱和容重(t/m3)"));
            const double aki = valueOf(info.entry, QStringLiteral("Ki,Kie(10^5Pa)"));
            const double alfak = valueOf(info.entry, QStringLiteral("αk,αke"));
            const double agi = valueOf(info.entry, QStringLiteral("Gi,Gie(10^5Pa)"));
            const double alfag = valueOf(info.entry, QStringLiteral("αG,αGe"));
            const double betag = valueOf(info.entry, QStringLiteral("βG"));
            const double akg = valueOf(info.entry, QStringLiteral("a(10^5Pa)"));
            const double bkg = valueOf(info.entry, QStringLiteral("b"));
            const double akie = valueOf(info.entry, QStringLiteral("Kie(10^5Pa)"));
            const double alfake = valueOf(info.entry, QStringLiteral("αke"));
            const double agie = valueOf(info.entry, QStringLiteral("Gie(10^5Pa)"));
            const double alfage = valueOf(info.entry, QStringLiteral("αGe"));

            const double dryRou = dryRouTm3 * 1.0e3;
            const double pacRou = pacRouTm3 * 1.0e3;
            const double wetRou = wetRouTm3 * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -2.0);
            setRow(8, 1.0);
            setRow(7, dryRou);
            setRow(90, pacRou);
            setRow(89, wetRou);
            setRow(69, aki);
            setRow(70, alfak);
            setRow(74, agi);
            setRow(71, alfag);
            setRow(75, betag);
            setRow(72, akg);
            setRow(73, bkg);
            setRow(76, akie);
            setRow(78, alfake);
            setRow(77, agie);
            setRow(79, alfage);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("shenzhujiang_creep")) // 沈珠江三参数流变
        {
            const double a1 = valueOf(info.entry, QStringLiteral("应变速率系数α"));
            const double b1 = valueOf(info.entry, QStringLiteral("体积流变系数b"));
            const double d1 = valueOf(info.entry, QStringLiteral("剪切流变系数d"));
            const double icreep = 1.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, a1);
            setRow(15, b1);
            setRow(16, d1);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("liguoying_creep")) // 李国英七参数流变
        {
            const double a1 = valueOf(info.entry, QStringLiteral("应变速率系数α"));
            const double b1 = valueOf(info.entry, QStringLiteral("体积流变系数b"));
            const double c1 = valueOf(info.entry, QStringLiteral("体积流变系数c"));
            const double d1 = valueOf(info.entry, QStringLiteral("剪切流变系数d"));
            const double fm1 = valueOf(info.entry, QStringLiteral("体积流变指数m1"));
            const double fm2 = valueOf(info.entry, QStringLiteral("体积流变指数m2"));
            const double fm3 = valueOf(info.entry, QStringLiteral("剪切流变指数m3"));
            const double icreep = 2.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, a1);
            setRow(15, b1);
            setRow(16, c1);
            setRow(17, d1);
            setRow(18, fm1);
            setRow(19, fm2);
            setRow(20, fm3);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("nine_param_creep")) // 九参数流变
        {
            const double almdV = valueOf(info.entry, QStringLiteral("应变速率系数λv"));
            const double cAlfa = valueOf(info.entry, QStringLiteral("体积流变系数Cα"));
            const double cBeta = valueOf(info.entry, QStringLiteral("体积流变系数Cβ"));
            const double dAlfa = valueOf(info.entry, QStringLiteral("体积流变指数dα"));
            const double dBeta = valueOf(info.entry, QStringLiteral("体积流变指数dβ"));
            const double c1 = valueOf(info.entry, QStringLiteral("主应变系数c"));
            const double d1 = valueOf(info.entry, QStringLiteral("主应变系数d"));
            const double ata1 = valueOf(info.entry, QStringLiteral("主应变流变系数η"));
            const double fm1 = valueOf(info.entry, QStringLiteral("主应变流变系数m"));
            const double icreep = 3.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(14, almdV);
            setRow(15, cAlfa);
            setRow(16, cBeta);
            setRow(17, dAlfa);
            setRow(18, dBeta);
            setRow(19, c1);
            setRow(20, d1);
            setRow(21, ata1);
            setRow(22, fm1);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("xiyuan_creep")) // 西源流变
        {
            const double ee1 = valueOf(info.entry, QStringLiteral("瞬时弹性模量E1（GPa）"));
            const double ee2 = valueOf(info.entry, QStringLiteral("粘塑性模量E2（GPa）"));
            const double pu1 = valueOf(info.entry, QStringLiteral("泊松比μ1"));
            const double pu2 = valueOf(info.entry, QStringLiteral("泊松比μ2"));
            const double vc1 = valueOf(info.entry, QStringLiteral("粘弹性系数η1"));
            const double vc2 = valueOf(info.entry, QStringLiteral("粘塑性系数η2"));
            const double icreep = 4.0;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(2, ee1 * 1.0e9);
            setRow(3, ee2 * 1.0e9);
            setRow(4, pu1);
            setRow(5, pu2);
            setRow(103, vc1);
            setRow(104, vc2);
            setRow(102, icreep);
            continue;
        }

        if (info.model->key == QStringLiteral("dam_dynamic")) // 坝料动力
        {
            // MAORI 是本次计算中的工况关系，不是动力材料的固有属性。
            const int sourceMatype = damDynamicSourceMappings.value(matype, 0);
            const double maori = static_cast<double>(sourceMatype);
            const double ifliqd = valueOf(info.entry, QStringLiteral("是否液化"));
            const double dryk1 = valueOf(info.entry, QStringLiteral("DRYK1(KPa)"));
            const double dryn1 = valueOf(info.entry, QStringLiteral("DRYN1"));
            const double fai = valueOf(info.entry, QStringLiteral("FAI(°)"));
            const double coKpa = valueOf(info.entry, QStringLiteral("CO(KPa)"));
            const double pu = valueOf(info.entry, QStringLiteral("PU"));
            const double drydam = valueOf(info.entry, QStringLiteral("DRYDAM"));
            const double ratiao1 = valueOf(info.entry, QStringLiteral("RATIAO1(0.1)"));
            const double ratiao2 = valueOf(info.entry, QStringLiteral("RATIAO2(0.3)"));
            const double ratiao3 = valueOf(info.entry, QStringLiteral("RATIAO3(0.5)"));

            double ckv = valueOf(info.entry, QStringLiteral("Kv"));
            double ckp = valueOf(info.entry, QStringLiteral("Kp"));
            double cnv = valueOf(info.entry, QStringLiteral("nv"));
            double cnp = valueOf(info.entry, QStringLiteral("np"));

            if (ckv < 0.0001)
                ckv = dryk1 * 1.1;
            if (ckp < 0.0001)
                ckp = ckv * 0.8;
            if (cnv < 0.0001)
                cnv = dryn1;
            if (cnp < 0.0001)
                cnp = dryn1 * 1.5;
            if (cnp > 0.9)
                cnp = 0.9;

            const double co = coKpa * 1.0e3;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, -4.0);
            setRow(13, ifliqd);
            setRow(35, 1.0);
            setRow(4, pu);
            setRow(5, pu);
            setRow(58, 0.1);
            setRow(59, 0.3);
            setRow(60, 0.5);
            setRow(61, 0.1);
            setRow(62, 0.3);
            setRow(63, 0.5);
            setRow(80, dryk1);
            setRow(81, dryn1);
            setRow(82, drydam);
            setRow(83, co);
            setRow(84, maori);
            setRow(85, ratiao1);
            setRow(86, ratiao2);
            setRow(87, ratiao3);
            setRow(88, fai);
            setRow(11, fai);
            setRow(12, co);
            setRow(94, fai);
            setRow(95, co);
            setRow(105, ckv);
            setRow(106, ckp);
            setRow(107, cnv);
            setRow(108, cnp);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        if (info.model->key == QStringLiteral("jk_corewall_steady_seepage")) // 各向同性稳定渗流场参数
        {
            const double tol = 1.0e-3;
            const double akLu = valueOf(info.entry, QStringLiteral("渗透系数K(Lu)"));
            const double specificStorage = valueOf(info.entry, QStringLiteral("比储量S"));
            const double rip = valueOf(info.entry, QStringLiteral("允许比降"));
            const double ak = akLu * 1.0e-7;

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(96, ak);
            setRow(97, ak);
            setRow(98, ak);
            setRow(101, specificStorage);
            setRow(60, rip);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);

            if (values[7 - 1][matIndex] < tol)
                setRow(7, 2.0e3);
            if (values[89 - 1][matIndex] < 100.0)
                setRow(89, qMax(1.0e3, values[7 - 1][matIndex]));
            if (values[90 - 1][matIndex] < tol)
                setRow(90, 2.0e3);
            continue;
        }

        if (info.model->key == QStringLiteral("jk_corewall_anisotropic_seepage"))
        {
            const double tol = 1.0e-3;
            const double kx = valueOf(info.entry, QStringLiteral("渗透系数Kx(Lu)")) * 1.0e-7;
            const double ky = valueOf(info.entry, QStringLiteral("渗透系数Ky(Lu)")) * 1.0e-7;
            const double kz = valueOf(info.entry, QStringLiteral("渗透系数Kz(Lu)")) * 1.0e-7;
            const double specificStorage = valueOf(info.entry, QStringLiteral("比储量S"));
            const double rip = valueOf(info.entry, QStringLiteral("允许比降"));

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                    values[row - 1][matIndex] = value;
            };

            setRow(96, kx);
            setRow(97, ky);
            setRow(98, kz);
            setRow(101, specificStorage);
            setRow(60, rip);
            setRow(14, 1.0);
            setRow(15, 0.0);
            setRow(16, 0.0);
            setRow(17, 0.0);
            setRow(18, 1.0);
            setRow(19, 0.0);
            setRow(20, 0.0);
            setRow(21, 0.0);
            setRow(22, 1.0);

            if (values[7 - 1][matIndex] < tol)
                setRow(7, 2.0e3);
            if (values[89 - 1][matIndex] < 100.0)
                setRow(89, qMax(1.0e3, values[7 - 1][matIndex]));
            if (values[90 - 1][matIndex] < tol)
                setRow(90, 2.0e3);
            continue;
        }

        if(info.model->key==QStringLiteral("planar_elastic_beam"))
        {
            const double young=valueOf(info.entry,QStringLiteral("弹性模量E（GPa）"))*1.e9;
            values[0][matIndex]=1.;values[1][matIndex]=young;values[2][matIndex]=young;
            values[5][matIndex]=young/2;values[6][matIndex]=valueOf(info.entry,QStringLiteral("容重t/m3"))*1.e3;
            values[7][matIndex]=valueOf(info.entry,QStringLiteral("截面积A（m2）"));
            values[45][matIndex]=valueOf(info.entry,QStringLiteral("截面惯性矩Iz（m4）"));
            continue;
        }
        if (info.model->key == QStringLiteral("huangjinping_bar_element")) // 黄金坪杆单元
        {
            const double ee1 = valueOf(info.entry, QStringLiteral("变模E1,E2（GPa）"));
            const double ee2 = valueOf(info.entry, QStringLiteral("屈服后变模E2（GPa）"));
            const double dryRou = valueOf(info.entry, QStringLiteral("容重t/m3"));
            const double area = valueOf(info.entry, QStringLiteral("杆面积（m2）"));
            const double rtMpa = valueOf(info.entry, QStringLiteral("抗拉（压）强度ft（MPa）"));
            const double rcMpa = valueOf(info.entry, QStringLiteral("极限抗压强度Rc（MPa）"));
            const double rytMpa = valueOf(info.entry, QStringLiteral("屈服抗拉强度Ryt（MPa）"));
            const double rycMpa = valueOf(info.entry, QStringLiteral("屈服抗压强度Ryc（MPa）"));
            const double ba = valueOf(info.entry, QStringLiteral("间距(m)"));
            const double bb = valueOf(info.entry, QStringLiteral("排距(m)"));
            const double fbar = valueOf(info.entry, QStringLiteral("预应力T（KN）"));
            const double alfaFbar = valueOf(info.entry, QStringLiteral("预应系数"));

            auto setRow = [&](int row, double value)
            {
                if (row >= 1 && row <= ncon)
                {
                    values[row - 1][matIndex] = value;
                }
            };

            setRow(1, 1.0);
            setRow(13, 2.0);
            // 杆单元的 c 项沿用旧版由抗拉强度构造的兼容值。
            setRow(12, rtMpa * 1.0e6 * 0.512);
            setRow(2, ee1 * 1.0e9);
            setRow(3, ee2 * 1.0e9);
            setRow(7, dryRou * 1.0e3);
            setRow(8, area);
            setRow(10, rtMpa * 1.0e6);
            setRow(9, rcMpa * 1.0e6);
            setRow(92, rytMpa * 1.0e6);
            setRow(91, rycMpa * 1.0e6);
            setRow(93, fbar * 1.0e3);
            setRow(38, ba);
            setRow(37, bb);
            setRow(39, 1.0);
            setRow(89, alfaFbar);
            applySupplementalModels(info.entry, matIndex);
            continue;
        }

        const auto& cols = info.model->columns;
        const auto& lineMap = info.model->columnOutputRows;
        const int count = qMin(cols.size(), lineMap.size());

        for (int c = 0; c < count; ++c)
        {
            const int targetRow = lineMap.at(c); // 1-based
            if (targetRow <= 0 || targetRow > ncon)
                continue;

            const QString key = cols.at(c);
            const QString strVal = info.entry->properties.value(key, QString("0"));

            bool ok = false;
            const double v = strVal.toDouble(&ok);
            values[targetRow - 1][matIndex] = ok ? v : 0.0;
        }

        applySupplementalModels(info.entry, matIndex);
    }

    // 动力材料必须在全部静力材料写完后再继承母材料。材料编号不应影响继承结果。
    const QList<int> dynamicOverrideRows = {
        1, 4, 5, 11, 12, 13, 35, 58, 59, 60, 61, 62, 63, 80, 81, 82,
        83, 84, 85, 86, 87, 88, 94, 95, 105, 106, 107, 108};
    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        const MatInfo& info = it.value();
        if (!info.model || !info.entry ||
            info.model->key != QStringLiteral("dam_dynamic"))
        {
            continue;
        }

        const int matIndex = it.key() - 1;
        const auto mappingIt = damDynamicSourceMappings.constFind(it.key());
        if (mappingIt == damDynamicSourceMappings.constEnd())
        {
            // If this is a dam dynamic calculation, a stage material without a
            // static source must not silently inherit zero-valued elastic data.
            if (!damDynamicSourceMappings.isEmpty() && info.entry->transitionOnly)
            {
                QMessageBox::warning(nullptr, QStringLiteral("缺少动力材料映射"),
                                     QStringLiteral("动力材料“%1”未指定对应静力材料。")
                                         .arg(info.entry->name));
                return false;
            }
            continue;
        }
        const int sourceMatype = mappingIt.value();
        const int sourceIndex = sourceMatype - 1;
        if (sourceIndex < 0 || sourceIndex >= nmat)
            continue; // 前置校验已处理正常错误，这里仅做越界保护。
        QMap<int, double> overrides;
        for (int row : dynamicOverrideRows)
            overrides.insert(row, values[row - 1][matIndex]);

        for (int row = 0; row < ncon; ++row)
            values[row][matIndex] = values[row][sourceIndex];
        for (auto overrideIt = overrides.constBegin(); overrideIt != overrides.constEnd();
             ++overrideIt)
        {
            values[overrideIt.key() - 1][matIndex] = overrideIt.value();
        }
        applySupplementalModels(info.entry, matIndex);
    }

    // ===== 5) 按你示例的 1P 科学计数格式输出：0.2000E+01 这种 =====
    auto fmt1P = [](double v) -> QString
    {
        double mant = 0.0;
        int exp = 0;

        const double av = std::fabs(v);
        if (av > 0.0)
        {
            exp = static_cast<int>(std::floor(std::log10(av))) + 1; // 1P: exp 比常规多 1
            mant = v / std::pow(10.0, exp);

            // 处理极端情况：由于浮点舍入导致 mant 变成 1.0000
            if (std::fabs(mant) >= 1.0)
            {
                mant /= 10.0;
                exp += 1;
            }
        }

        const QString mantStr = QString::asprintf("% .4f", mant); // 正数前带空格，负数带 -
        const QString expStr = QString::asprintf("E%+03d", exp);  // E+00 / E+01 / E-01

        // 每格固定宽度，方便列对齐
        return QString("%1").arg(mantStr + expStr, 13);
    };

    auto imatLabel = [](int matype) -> QString
    {
        // 尽量贴近你示例：IMAT= 10 这种（两位数前空一格）
        if (matype < 10)
            return QString("IMAT=%1").arg(matype);
        if (matype < 100)
            return QString("IMAT= %1").arg(matype);
        return QString("IMAT=%1").arg(matype);
    };

    // ===== 6) 文件头 =====
    // 示例：        300         200    NMAT  NCON
    out << QString("%1%2    NMAT  NCON\n").arg(nmat, 12).arg(ncon, 12);

    // ===== 7) 分组输出：每组 10 列 =====
    const int groupCount = nmat / 10;

    for (int g = 0; g < groupCount; ++g)
    {
        const int startMat = g * 10 + 1;

        out << QString(" CONMAT(NMAT,NCON)   GROUP NUM=%1\n").arg(g + 1, 4);

        // 组头 IMAT=...
        QStringList head;
        head.reserve(10);
        for (int k = 0; k < 10; ++k)
        {
            head << imatLabel(startMat + k);
        }
        out << head.join(QString("       ")) << "\n";

        // 200 行
        for (int r = 0; r < ncon; ++r)
        {
            QString line;
            line.reserve(10 * 13 + 8);

            for (int k = 0; k < 10; ++k)
            {
                const int matype = startMat + k;
                const int matIndex = matype - 1;
                const double v = (matIndex >= 0 && matIndex < nmat) ? values[r][matIndex] : 0.0;
                line += fmt1P(v);
            }

            // 行号（1..200）
            line += QString("%1").arg(r + 1, 5);
            out << line << "\n";
        }
    }

    out.flush();
    if (!file.commit())
        return false;

    // ===== 8) 同步生成 MACB.DAT / MACHAN.DAT =====
    QList<int> macbMatypes;
    for (auto it = matypeMap.constBegin(); it != matypeMap.constEnd(); ++it)
    {
        if (it->entry && it->entry->appliedMatypes.contains(it.key()))
            macbMatypes.append(it.key());
    }

    auto writeNumDat = [&](const QString& fileName, const QList<int>& ids) -> bool
    {
        QSaveFile outFile(workDir.filePath(fileName));
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return false;
        }

        QTextStream ts(&outFile);
        ts << QString("%1   NUM\n").arg(ids.size(), 12);
        for (int id : ids)
        {
            ts << QString("%1\n").arg(id, 8);
        }
        ts.flush();
        return outFile.commit();
    };

    if (!writeNumDat(QStringLiteral("MACB.DAT"), macbMatypes))
    {
        return false;
    }
    QSaveFile transitionFile(workDir.filePath(QStringLiteral("MACHAN.DAT")));
    if (!transitionFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream transitionStream(&transitionFile);
    // 材料替换现由各分析独立写入。保留空文件只为兼容仍要求该文件存在的旧流程。
    transitionStream << QString("%1   NUM\n").arg(0, 12);
    transitionStream.flush();
    if (!transitionFile.commit())
    {
        return false;
    }

    return true;
}


bool MaterialManager::importIMATFile(const QString& filePath, int* importedCount, QString* error)
{
    if (importedCount)
    {
        *importedCount = 0;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
        {
            *error = QStringLiteral("无法打开 IMAT.DAT 文件。");
        }
        return false;
    }

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd())
    {
        lines << in.readLine();
    }
    file.close();

    if (lines.isEmpty())
    {
        if (error)
        {
            *error = QStringLiteral("IMAT.DAT 文件为空。");
        }
        return false;
    }

    auto numericTokens = [](const QString& line) -> QStringList
    {
        QStringList result;
        static const QRegularExpression numberRe(
            QStringLiteral("[-+]?\\d*\\.?\\d+(?:[EeDd][-+]?\\d+)?"));
        QRegularExpressionMatchIterator it = numberRe.globalMatch(line);
        while (it.hasNext())
        {
            QString token = it.next().captured(0);
            token.replace(QLatin1Char('D'), QLatin1Char('E'));
            token.replace(QLatin1Char('d'), QLatin1Char('E'));
            result << token;
        }
        return result;
    };

    QStringList headerTokens = numericTokens(lines.first());
    if (headerTokens.size() < 2)
    {
        if (error)
        {
            *error = QStringLiteral("IMAT.DAT 文件头格式错误，未读取到 NMAT/NCON。");
        }
        return false;
    }

    bool okNmat = false;
    bool okNcon = false;
    const int nmat = headerTokens.at(0).toInt(&okNmat);
    const int ncon = headerTokens.at(1).toInt(&okNcon);
    if (!okNmat || !okNcon || nmat <= 0 || ncon <= 0)
    {
        if (error)
        {
            *error = QStringLiteral("IMAT.DAT 文件头中的 NMAT/NCON 无效。");
        }
        return false;
    }

    QVector<QVector<double>> values(ncon, QVector<double>(nmat, 0.0));
    int currentGroup = -1;
    bool hasData = false;
    static const QRegularExpression groupRe(QStringLiteral("GROUP\\s+NUM\\s*=\\s*(\\d+)"),
                                            QRegularExpression::CaseInsensitiveOption);

    for (int lineIndex = 1; lineIndex < lines.size(); ++lineIndex)
    {
        const QString line = lines.at(lineIndex);
        const QRegularExpressionMatch groupMatch = groupRe.match(line);
        if (groupMatch.hasMatch())
        {
            currentGroup = groupMatch.captured(1).toInt() - 1;
            continue;
        }
        if (currentGroup < 0 || line.contains(QStringLiteral("IMAT"), Qt::CaseInsensitive))
        {
            continue;
        }

        const QStringList nums = numericTokens(line);
        if (nums.size() < 2)
        {
            continue;
        }

        bool okRow = false;
        const int row = nums.last().toInt(&okRow);
        if (!okRow || row < 1 || row > ncon)
        {
            continue;
        }

        const int valueCount = qMin(10, nums.size() - 1);
        for (int k = 0; k < valueCount; ++k)
        {
            const int matIndex = currentGroup * 10 + k;
            if (matIndex < 0 || matIndex >= nmat)
            {
                continue;
            }
            bool okValue = false;
            const double value = nums.at(k).toDouble(&okValue);
            if (okValue)
            {
                values[row - 1][matIndex] = value;
                hasData = true;
            }
        }
    }

    if (!hasData)
    {
        if (error)
        {
            *error = QStringLiteral("未在 IMAT.DAT 中读取到 CONMAT 数据。");
        }
        return false;
    }

    auto rowValue = [&](int matIndex, int row) -> double
    {
        if (row < 1 || row > ncon || matIndex < 0 || matIndex >= nmat)
        {
            return 0.0;
        }
        return values[row - 1][matIndex];
    };
    auto nearlyEqual = [](double lhs, double rhs) -> bool
    {
        return std::fabs(lhs - rhs) <= 1.0e-8 * qMax(1.0, qMax(std::fabs(lhs), std::fabs(rhs)));
    };
    auto isNonZero = [&](int matIndex, int row) -> bool
    {
        return !nearlyEqual(rowValue(matIndex, row), 0.0);
    };
    auto formatValue = [](double value) -> QString
    {
        if (std::fabs(value) < 1.0e-12)
        {
            value = 0.0;
        }
        return QString::number(value, 'g', 12);
    };
    auto setProperty = [&](MaterialEntry& entry, const QString& key, double value)
    {
        entry.properties.insert(key, formatValue(value));
    };
    auto setSeepageProperty = [&](MaterialEntry& entry, const QString& key, double value)
    {
        entry.seepageProperties.insert(key, formatValue(value));
    };

    auto looksLikeForwardDefaultVoid = [&]() -> bool
    {
        if (nmat <= 0)
        {
            return false;
        }
        const int m = 0;
        return nearlyEqual(rowValue(m, 2), 100.0) && nearlyEqual(rowValue(m, 3), 100.0) &&
               nearlyEqual(rowValue(m, 4), 0.30) && nearlyEqual(rowValue(m, 5), 100.0) &&
               nearlyEqual(rowValue(m, 8), 1.0) && nearlyEqual(rowValue(m, 9), 2000.0e6) &&
               nearlyEqual(rowValue(m, 10), 200.0e6) && nearlyEqual(rowValue(m, 11), 78.0) &&
               nearlyEqual(rowValue(m, 12), 200.0e6);
    };

    auto hasAnyTypeSignature = [&](int matIndex) -> bool
    {
        const int ib = static_cast<int>(std::llround(rowValue(matIndex, 1)));
        if (ib == -1 || ib == -2 || ib == -3 || ib == -4 || ib == -5 || ib == -6 || ib == 1 ||
            ib == 2 || ib == 3 || ib == 4 || ib == 5 || ib == 6 || ib == 7 || ib == 10 || ib == 11)
        {
            return true;
        }
        const int creepType = static_cast<int>(std::llround(rowValue(matIndex, 102)));
        return creepType >= 1 && creepType <= 4;
    };

    const QSet<int> meshMatypes = availableMatypes();
    QSet<int> targetMatypes;
    QSet<int> appliedTargetMatypes;
    const bool defaultVoid = looksLikeForwardDefaultVoid();
    auto isImportableMatypeColumn = [&](int matype) -> bool
    {
        const int matIndex = matype - 1;
        if (!isComputableMatype(matype) || matIndex < 0 || matIndex >= nmat)
        {
            return false;
        }
        if (defaultVoid && matIndex == 0)
        {
            return false;
        }
        return hasAnyTypeSignature(matIndex);
    };

    if (!meshMatypes.isEmpty())
    {
        for (int matype : meshMatypes)
        {
            if (isImportableMatypeColumn(matype))
            {
                targetMatypes.insert(matype);
                appliedTargetMatypes.insert(matype);
            }
        }
    }
    else
    {
        for (int matype = FIRST_COMPUTABLE_MATYPE; matype <= nmat; ++matype)
        {
            if (isImportableMatypeColumn(matype))
                targetMatypes.insert(matype);
        }
    }

    if (targetMatypes.isEmpty())
    {
        if (error)
        {
            *error = !meshMatypes.isEmpty()
                         ? QStringLiteral("当前网格 MATYPE 在 IMAT.DAT 中未找到可识别材料列，请确认 IMAT.DAT 与当前工程网格配置一致。")
                         : QStringLiteral("IMAT.DAT 中未找到可识别的材料列。");
        }
        return false;
    }

    auto modelForMatype = [&](int matIndex) -> MaterialModel*
    {
        const int ib = static_cast<int>(std::llround(rowValue(matIndex, 1)));
        if (ib == -1)
            return findModel(QStringLiteral("soil_eb"));
        if (ib == -2)
            return findModel(QStringLiteral("soil_kg"));
        if (ib == -3)
            return findModel(QStringLiteral("soil_emu"));
        if (ib == -4)
            return findModel(QStringLiteral("dam_dynamic"));
        if (ib == -5)
            return findModel(QStringLiteral("soil_nanshui"));
        if (ib == -6)
            return findModel(QStringLiteral("soil_emu_crush"));
        if (ib == 11)
            return findModel(QStringLiteral("hoek_brown_rock"));
        // Compatibility with the old solver: IBUCK=4..7,10 were D-P subtypes.
        if (ib == 3 || ib == 4 || ib == 5 || ib == 6 || ib == 7 || ib == 10)
            return findModel(QStringLiteral("drucker_prager_rock"));
        if (ib == 1 && (nearlyEqual(rowValue(matIndex, 13), 2.0) || isNonZero(matIndex, 38) ||
                        isNonZero(matIndex, 93)))
            return findModel(QStringLiteral("huangjinping_bar_element"));
        if (ib == 1 && isNonZero(matIndex, 46))
            return findModel(QStringLiteral("planar_elastic_beam"));
        if (ib == 2)
        {
            if (isNonZero(matIndex, 58) || isNonZero(matIndex, 59) || isNonZero(matIndex, 77) ||
                isNonZero(matIndex, 78) || isNonZero(matIndex, 100))
            {
                return findModel(QStringLiteral("huangjinping_underground_plant"));
            }
            return findModel(QStringLiteral("dagangshan_isotropic"));
        }

        const int creepType = static_cast<int>(std::llround(rowValue(matIndex, 102)));
        if (creepType == 1)
            return findModel(QStringLiteral("shenzhujiang_creep"));
        if (creepType == 2)
            return findModel(QStringLiteral("liguoying_creep"));
        if (creepType == 3)
            return findModel(QStringLiteral("nine_param_creep"));
        if (creepType == 4)
            return findModel(QStringLiteral("xiyuan_creep"));
        if (ib == 0)
            return findModel(QStringLiteral("linear_elastic"));
        return nullptr;
    };

    auto restoreSeepage = [&](MaterialEntry& entry, int matIndex)
    {
        const double k1 = rowValue(matIndex, 96);
        const double k2 = rowValue(matIndex, 97);
        const double k3 = rowValue(matIndex, 98);
        const double specificStorage = rowValue(matIndex, 101);
        const double rip = rowValue(matIndex, 60);
        const bool hasPermeability =
            !nearlyEqual(k1, 0.0) || !nearlyEqual(k2, 0.0) || !nearlyEqual(k3, 0.0);
        if (hasPermeability && nearlyEqual(k1, k2) && nearlyEqual(k1, k3))
        {
            entry.seepageModelKey = QStringLiteral("jk_corewall_steady_seepage");
            setSeepageProperty(entry, QStringLiteral("渗透系数K(Lu)"), k1 / 1.0e-7);
            setSeepageProperty(entry, QStringLiteral("比储量S"), specificStorage);
            setSeepageProperty(entry, QStringLiteral("允许比降"), rip);
        }
        else if (hasPermeability)
        {
            entry.seepageModelKey = QStringLiteral("jk_corewall_anisotropic_seepage");
            setSeepageProperty(entry, QStringLiteral("渗透系数Kx(Lu)"), k1 / 1.0e-7);
            setSeepageProperty(entry, QStringLiteral("渗透系数Ky(Lu)"), k2 / 1.0e-7);
            setSeepageProperty(entry, QStringLiteral("渗透系数Kz(Lu)"), k3 / 1.0e-7);
            setSeepageProperty(entry, QStringLiteral("比储量S"), specificStorage);
            setSeepageProperty(entry, QStringLiteral("允许比降"), rip);
        }

        const int creepType = qRound64(rowValue(matIndex, 102));
        if (creepType == 1)
        {
            entry.creepModelKey = QStringLiteral("shenzhujiang_creep");
            setSeepageProperty(entry, QStringLiteral("应变速率系数α"), rowValue(matIndex, 14));
            setSeepageProperty(entry, QStringLiteral("体积流变系数b"), rowValue(matIndex, 15));
            setSeepageProperty(entry, QStringLiteral("剪切流变系数d"), rowValue(matIndex, 16));
        }
        else if (creepType == 2)
        {
            entry.creepModelKey = QStringLiteral("liguoying_creep");
            setSeepageProperty(entry, QStringLiteral("应变速率系数α"), rowValue(matIndex, 14));
            setSeepageProperty(entry, QStringLiteral("体积流变系数b"), rowValue(matIndex, 15));
            setSeepageProperty(entry, QStringLiteral("体积流变系数c"), rowValue(matIndex, 16));
            setSeepageProperty(entry, QStringLiteral("剪切流变系数d"), rowValue(matIndex, 17));
            setSeepageProperty(entry, QStringLiteral("体积流变指数m1"), rowValue(matIndex, 18));
            setSeepageProperty(entry, QStringLiteral("体积流变指数m2"), rowValue(matIndex, 19));
            setSeepageProperty(entry, QStringLiteral("剪切流变指数m3"), rowValue(matIndex, 20));
        }
        else if (creepType == 3)
        {
            entry.creepModelKey = QStringLiteral("nine_param_creep");
            setSeepageProperty(entry, QStringLiteral("应变速率系数λv"), rowValue(matIndex, 14));
            setSeepageProperty(entry, QStringLiteral("体积流变系数Cα"), rowValue(matIndex, 15));
            setSeepageProperty(entry, QStringLiteral("体积流变系数Cβ"), rowValue(matIndex, 16));
            setSeepageProperty(entry, QStringLiteral("体积流变指数dα"), rowValue(matIndex, 17));
            setSeepageProperty(entry, QStringLiteral("体积流变指数dβ"), rowValue(matIndex, 18));
            setSeepageProperty(entry, QStringLiteral("主应变系数c"), rowValue(matIndex, 19));
            setSeepageProperty(entry, QStringLiteral("主应变系数d"), rowValue(matIndex, 20));
            setSeepageProperty(entry, QStringLiteral("主应变流变系数η"), rowValue(matIndex, 21));
            setSeepageProperty(entry, QStringLiteral("主应变流变系数m"), rowValue(matIndex, 22));
        }
        else if (creepType == 4)
        {
            entry.creepModelKey = QStringLiteral("xiyuan_creep");
            setSeepageProperty(entry, QStringLiteral("瞬时弹性模量E1（GPa）"),
                               rowValue(matIndex, 2) / 1.0e9);
            setSeepageProperty(entry, QStringLiteral("粘塑性模量E2（GPa）"),
                               rowValue(matIndex, 3) / 1.0e9);
            setSeepageProperty(entry, QStringLiteral("泊松比μ1"), rowValue(matIndex, 4));
            setSeepageProperty(entry, QStringLiteral("泊松比μ2"), rowValue(matIndex, 5));
            setSeepageProperty(entry, QStringLiteral("粘弹性系数η1"), rowValue(matIndex, 103));
            setSeepageProperty(entry, QStringLiteral("粘塑性系数η2"), rowValue(matIndex, 104));
        }
    };

    int count = 0;
    QList<int> sortedMatypes = targetMatypes.values();
    std::sort(sortedMatypes.begin(), sortedMatypes.end());
    for (int matype : sortedMatypes)
    {
        const int matIndex = matype - 1;
        if (matIndex < 0 || matIndex >= nmat)
        {
            continue;
        }

        MaterialModel* model = modelForMatype(matIndex);
        if (!model)
        {
            qWarning() << "Skip IMAT material because type is not recognized" << matype
                       << "IB" << rowValue(matIndex, 1) << "ICREEP" << rowValue(matIndex, 102);
            continue;
        }

        MaterialEntry entry;
        entry.id = QUuid::createUuid().toString();
        entry.name = QStringLiteral("IMAT_%1").arg(matype);
        for (const QString& column : model->columns)
        {
            entry.properties.insert(column, isAutoNumberColumn(column) ? QString::number(matype)
                                                                       : QStringLiteral("0"));
        }

        const int ib = static_cast<int>(std::llround(rowValue(matIndex, 1)));
        if (model->key == QStringLiteral("linear_elastic"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("变形模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
        }
        else if (model->key == QStringLiteral("hoek_brown_rock"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("变形模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("岩体常数mb"), rowValue(matIndex, 75));
            setProperty(entry, QStringLiteral("岩体常数a"), rowValue(matIndex, 70));
            setProperty(entry, QStringLiteral("岩体常数s"), rowValue(matIndex, 76));
            setProperty(entry, QStringLiteral("完整岩石单轴抗压强度σci（MPa）"),
                        rowValue(matIndex, 115) / 1.0e6);
        }
        else if (model->key == QStringLiteral("drucker_prager_rock"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("变形模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("内摩擦角φ（°）"), rowValue(matIndex, 11));
            setProperty(entry, QStringLiteral("凝聚力C‘（MPa）"), rowValue(matIndex, 12) / 1.0e6);
            setProperty(entry, QStringLiteral("剪胀角(°)"), rowValue(matIndex, 99));
            int dpType = static_cast<int>(std::llround(rowValue(matIndex, 126)));
            if (ib == 4) dpType = 2;
            else if (ib == 5) dpType = 3;
            else if (ib == 6) dpType = 4;
            else if (ib == 7) dpType = 5;
            else if (ib == 10) dpType = 6;
            else if (ib == 3 && (dpType < 1 || dpType > 6)) dpType = 1;
            setProperty(entry, QStringLiteral("DP子类型"), dpType);
            setProperty(entry, QStringLiteral("抗压强度Rc（MPa）"), rowValue(matIndex, 9) / 1.0e6);
            setProperty(entry, QStringLiteral("抗拉强度Rt（MPa）"), rowValue(matIndex, 10) / 1.0e6);
        }
        else if (model->key == QStringLiteral("huangjinping_underground_plant"))
        {
            setProperty(entry, QStringLiteral("编号"), matype);
            setProperty(entry, QStringLiteral("母岩材料编号"), rowValue(matIndex, 84));
            setProperty(entry, QStringLiteral("岩类分区"), rowValue(matIndex, 100));
            setProperty(entry, QStringLiteral("半开挖区荷载释放系数"), rowValue(matIndex, 58));
            setProperty(entry, QStringLiteral("再生时对应开挖级"), rowValue(matIndex, 59));
            setProperty(entry, QStringLiteral("变形模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("抗剪断强度内摩擦系数"),
                        std::tan(rowValue(matIndex, 11) * 3.141592653589793 / 180.0));
            setProperty(entry, QStringLiteral("凝聚力C‘（MPa）"), rowValue(matIndex, 12) / 1.0e6);
            setProperty(entry, QStringLiteral("剪胀角(°)"), rowValue(matIndex, 99));
            setProperty(entry, QStringLiteral("洞室边缘松弛系数α0"), rowValue(matIndex, 77));
            setProperty(entry, QStringLiteral("松弛距离d0"), rowValue(matIndex, 78));
        }
        else if (model->key == QStringLiteral("planar_elastic_beam"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("弹性模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("截面积A（m2）"), rowValue(matIndex, 8));
            setProperty(entry, QStringLiteral("截面惯性矩Iz（m4）"), rowValue(matIndex, 46));
        }
        else if (model->key == QStringLiteral("dagangshan_isotropic"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("变形模量E（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("抗剪断强度内摩擦系数"),
                        std::tan(rowValue(matIndex, 11) * 3.141592653589793 / 180.0));
            setProperty(entry, QStringLiteral("凝聚力C‘（MPa）"), rowValue(matIndex, 12) / 1.0e6);
            setProperty(entry, QStringLiteral("抗压强度Rc（MPa）"), rowValue(matIndex, 9) / 1.0e6);
            setProperty(entry, QStringLiteral("抗拉强度Rt（MPa）"), rowValue(matIndex, 10) / 1.0e6);
            setProperty(entry, QStringLiteral("残余抗剪断强度内摩擦系数"),
                        std::tan(rowValue(matIndex, 94) * 3.141592653589793 / 180.0));
            setProperty(entry, QStringLiteral("残余凝聚力C‘（MPa）"),
                        rowValue(matIndex, 95) / 1.0e6);
            // Preserve old IMAT semantics: zero residual row meant inherit peak.
            setProperty(entry, QStringLiteral("残余摩擦系数沿用峰值"),
                        (std::fabs(rowValue(matIndex, 94)) < 1.0e-12 &&
                         std::fabs(rowValue(matIndex, 11)) > 1.0e-12) ? 1 : 0);
            setProperty(entry, QStringLiteral("残余凝聚力沿用峰值"),
                        (std::fabs(rowValue(matIndex, 95)) < 1.0e-12 &&
                         std::fabs(rowValue(matIndex, 12)) > 1.0e-12) ? 1 : 0);
            setProperty(entry, QStringLiteral("剪胀角(°)"), rowValue(matIndex, 99));
        }
        else if (model->key == QStringLiteral("huangjinping_bar_element"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("变模E1,E2（GPa）"), rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("屈服后变模E2（GPa）"),
                        rowValue(matIndex, 3) / 1.0e9);
            setProperty(entry, QStringLiteral("容重t/m3"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("杆面积（m2）"), rowValue(matIndex, 8));
            setProperty(entry, QStringLiteral("抗拉（压）强度ft（MPa）"),
                        rowValue(matIndex, 10) / 1.0e6);
            setProperty(entry, QStringLiteral("极限抗压强度Rc（MPa）"),
                        rowValue(matIndex, 9) / 1.0e6);
            setProperty(entry, QStringLiteral("屈服抗拉强度Ryt（MPa）"),
                        rowValue(matIndex, 92) / 1.0e6);
            setProperty(entry, QStringLiteral("屈服抗压强度Ryc（MPa）"),
                        rowValue(matIndex, 91) / 1.0e6);
            setProperty(entry, QStringLiteral("间距(m)"), rowValue(matIndex, 38));
            setProperty(entry, QStringLiteral("排距(m)"), rowValue(matIndex, 37));
            setProperty(entry, QStringLiteral("预应力T（KN）"), rowValue(matIndex, 93) / 1.0e3);
            setProperty(entry, QStringLiteral("预应系数"), rowValue(matIndex, 89));
        }
        else if (model->key == QStringLiteral("dam_dynamic"))
        {
            setProperty(entry, QStringLiteral("编号"), matype);
            // CONMAT(84) 属于求解时的静动关联，不导入为材料固有属性。
            setProperty(entry, QStringLiteral("是否液化"), rowValue(matIndex, 13));
            setProperty(entry, QStringLiteral("DRYK1(KPa)"), rowValue(matIndex, 80));
            setProperty(entry, QStringLiteral("DRYN1"), rowValue(matIndex, 81));
            setProperty(entry, QStringLiteral("FAI(°)"), rowValue(matIndex, 88));
            setProperty(entry, QStringLiteral("CO(KPa)"), rowValue(matIndex, 83) / 1.0e3);
            setProperty(entry, QStringLiteral("PU"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("DRYDAM"), rowValue(matIndex, 82));
            setProperty(entry, QStringLiteral("RATIAO1(0.1)"), rowValue(matIndex, 85));
            setProperty(entry, QStringLiteral("RATIAO2(0.3)"), rowValue(matIndex, 86));
            setProperty(entry, QStringLiteral("RATIAO3(0.5)"), rowValue(matIndex, 87));
            setProperty(entry, QStringLiteral("Kv"), rowValue(matIndex, 105));
            setProperty(entry, QStringLiteral("Kp"), rowValue(matIndex, 106));
            setProperty(entry, QStringLiteral("nv"), rowValue(matIndex, 107));
            setProperty(entry, QStringLiteral("np"), rowValue(matIndex, 108));
        }
        else if (model->key == QStringLiteral("shenzhujiang_creep"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("应变速率系数α"), rowValue(matIndex, 14));
            setProperty(entry, QStringLiteral("体积流变系数b"), rowValue(matIndex, 15));
            setProperty(entry, QStringLiteral("剪切流变系数d"), rowValue(matIndex, 16));
        }
        else if (model->key == QStringLiteral("liguoying_creep"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("应变速率系数α"), rowValue(matIndex, 14));
            setProperty(entry, QStringLiteral("体积流变系数b"), rowValue(matIndex, 15));
            setProperty(entry, QStringLiteral("体积流变系数c"), rowValue(matIndex, 16));
            setProperty(entry, QStringLiteral("剪切流变系数d"), rowValue(matIndex, 17));
            setProperty(entry, QStringLiteral("体积流变指数m1"), rowValue(matIndex, 18));
            setProperty(entry, QStringLiteral("体积流变指数m2"), rowValue(matIndex, 19));
            setProperty(entry, QStringLiteral("剪切流变指数m3"), rowValue(matIndex, 20));
        }
        else if (model->key == QStringLiteral("nine_param_creep"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("应变速率系数λv"), rowValue(matIndex, 14));
            setProperty(entry, QStringLiteral("体积流变系数Cα"), rowValue(matIndex, 15));
            setProperty(entry, QStringLiteral("体积流变系数Cβ"), rowValue(matIndex, 16));
            setProperty(entry, QStringLiteral("体积流变指数dα"), rowValue(matIndex, 17));
            setProperty(entry, QStringLiteral("体积流变指数dβ"), rowValue(matIndex, 18));
            setProperty(entry, QStringLiteral("主应变系数c"), rowValue(matIndex, 19));
            setProperty(entry, QStringLiteral("主应变系数d"), rowValue(matIndex, 20));
            setProperty(entry, QStringLiteral("主应变流变系数η"), rowValue(matIndex, 21));
            setProperty(entry, QStringLiteral("主应变流变系数m"), rowValue(matIndex, 22));
        }
        else if (model->key == QStringLiteral("xiyuan_creep"))
        {
            setProperty(entry, QStringLiteral("序号"), matype);
            setProperty(entry, QStringLiteral("瞬时弹性模量E1（GPa）"),
                        rowValue(matIndex, 2) / 1.0e9);
            setProperty(entry, QStringLiteral("粘塑性模量E2（GPa）"),
                        rowValue(matIndex, 3) / 1.0e9);
            setProperty(entry, QStringLiteral("泊松比μ1"), rowValue(matIndex, 4));
            setProperty(entry, QStringLiteral("泊松比μ2"), rowValue(matIndex, 5));
            setProperty(entry, QStringLiteral("粘弹性系数η1"), rowValue(matIndex, 103));
            setProperty(entry, QStringLiteral("粘塑性系数η2"), rowValue(matIndex, 104));
        }
        else
        {
            const QString dryPhi = model->columns.contains(QStringLiteral("φ0"))
                                     ? QStringLiteral("φ0")
                                     : QStringLiteral("φ0(°)");
            const QString deltaPhi = model->columns.contains(QStringLiteral("Δφ"))
                                       ? QStringLiteral("Δφ")
                                       : QStringLiteral("Δφ(°)");
            setProperty(entry, QStringLiteral("材料编号"), matype);
            setProperty(entry, QStringLiteral("干容重(t/m3)"), rowValue(matIndex, 7) / 1.0e3);
            setProperty(entry, QStringLiteral("天然容重(t/m3)"), rowValue(matIndex, 90) / 1.0e3);
            setProperty(entry, QStringLiteral("饱和容重(t/m3)"), rowValue(matIndex, 89) / 1.0e3);
            setProperty(entry, dryPhi, rowValue(matIndex, 11));
            setProperty(entry, deltaPhi, rowValue(matIndex, 76));
            setProperty(entry, QStringLiteral("c(KPa)"), rowValue(matIndex, 12) / 1.0e3);

            if (ib == -2)
            {
                setProperty(entry, QStringLiteral("Ki,Kie(10^5Pa)"), rowValue(matIndex, 69));
                setProperty(entry, QStringLiteral("αk,αke"), rowValue(matIndex, 70));
                setProperty(entry, QStringLiteral("Gi,Gie(10^5Pa)"), rowValue(matIndex, 74));
                setProperty(entry, QStringLiteral("αG,αGe"), rowValue(matIndex, 71));
                setProperty(entry, QStringLiteral("βG"), rowValue(matIndex, 75));
                setProperty(entry, QStringLiteral("a(10^5Pa)"), rowValue(matIndex, 72));
                setProperty(entry, QStringLiteral("b"), rowValue(matIndex, 73));
                setProperty(entry, QStringLiteral("Kie(10^5Pa)"), rowValue(matIndex, 76));
                setProperty(entry, QStringLiteral("αke"), rowValue(matIndex, 78));
                setProperty(entry, QStringLiteral("Gie(10^5Pa)"), rowValue(matIndex, 77));
                setProperty(entry, QStringLiteral("αGe"), rowValue(matIndex, 79));
            }
            else
            {
                setProperty(entry, QStringLiteral("Rf"), rowValue(matIndex, 71));
                setProperty(entry, QStringLiteral("K"), rowValue(matIndex, 69));
                setProperty(entry, QStringLiteral("n"), rowValue(matIndex, 70));
                if (ib == -1)
                {
                    setProperty(entry, QStringLiteral("Kur"), rowValue(matIndex, 72));
                    setProperty(entry, QStringLiteral("Nur"), rowValue(matIndex, 73));
                    setProperty(entry, QStringLiteral("Kb"), rowValue(matIndex, 74));
                    setProperty(entry, QStringLiteral("m"), rowValue(matIndex, 75));
                }
                else if (ib == -3)
                {
                    setProperty(entry, QStringLiteral("Kur"), rowValue(matIndex, 72));
                    setProperty(entry, QStringLiteral("Nur"), rowValue(matIndex, 73));
                    setProperty(entry, QStringLiteral("G"), rowValue(matIndex, 77));
                    setProperty(entry, QStringLiteral("D"), rowValue(matIndex, 78));
                    setProperty(entry, QStringLiteral("F"), rowValue(matIndex, 79));
                }
                else if (ib == -5)
                {
                    setProperty(entry, QStringLiteral("Kur"), rowValue(matIndex, 72));
                    setProperty(entry, QStringLiteral("Nur"), rowValue(matIndex, 73));
                    setProperty(entry, QStringLiteral("cd"), rowValue(matIndex, 77));
                    setProperty(entry, QStringLiteral("nd"), rowValue(matIndex, 78));
                    setProperty(entry, QStringLiteral("Rd"), rowValue(matIndex, 79));
                }
                else if (ib == -6)
                {
                    setProperty(entry, QStringLiteral("Kur"), rowValue(matIndex, 72));
                    setProperty(entry, QStringLiteral("Nur"), rowValue(matIndex, 73));
                    setProperty(entry, QStringLiteral("C1"), rowValue(matIndex, 109));
                    setProperty(entry, QStringLiteral("n1"), rowValue(matIndex, 110));
                    setProperty(entry, QStringLiteral("C2"), rowValue(matIndex, 111));
                    setProperty(entry, QStringLiteral("n2"), rowValue(matIndex, 112));
                    setProperty(entry, QStringLiteral("C3"), rowValue(matIndex, 113));
                    setProperty(entry, QStringLiteral("n3"), rowValue(matIndex, 114));
                }
            }
        }

        restoreSeepage(entry, matIndex);
        for (auto& existingModel : m_models)
        {
            for (auto& existingEntry : existingModel.materials)
            {
                existingEntry.appliedMatypes.remove(matype);
            }
        }
        if (appliedTargetMatypes.contains(matype))
        {
            entry.appliedMatypes.insert(matype);
        }
        model->materials.append(entry);
        m_addedEntryIds.insert(entry.id);
        if (entry.appliedMatypes.contains(matype))
            m_matypeOwnerById.insert(matype, entry.id);
        ++count;
    }

    if (count <= 0)
    {
        if (error)
        {
            *error = QStringLiteral("IMAT.DAT 中未发现可识别且需要创建的材料列。");
        }
        return false;
    }

    ensureEntryIds();
    notifyLibraryChanged(true);
    if (importedCount)
    {
        *importedCount = count;
    }
    return true;
}

bool MaterialManager::generateIMATFile(
    const QMap<int, int>& damDynamicSourceMappings)
{
    return generateCaculateFile(damDynamicSourceMappings);
}

void MaterialManager::clearMatypeHighlight(const QString& entryId)
{
    if (entryId.isEmpty())
    {
        return;
    }

    m_previewMatypes.remove(entryId);
    m_preDeleteMatypes.remove(entryId);

    const QString domain = m_pickDomain.isEmpty() ? QStringLiteral("MaterialManager") : m_pickDomain;
    const QString uuid = m_pickUuid.isEmpty() ? entryId : m_pickUuid;
    PickManager::instance().clearDomainSelection(domain, uuid);
}

void MaterialManager::clearAllMatypeHighlight()
{
    m_previewMatypes.clear();
    m_preDeleteMatypes.clear();
    const QString domain = m_pickDomain.isEmpty() ? QStringLiteral("MaterialManager") : m_pickDomain;
    if (m_pickUuid.isEmpty())
        PickManager::instance().clearDomainSelection(domain);
    else
        PickManager::instance().clearDomainSelection(domain, m_pickUuid);
}

void MaterialManager::setPickContext(const QString& domain, const QString& uuid)
{
    m_pickDomain = domain;
    m_pickUuid = uuid;
}

void MaterialManager::clearPickContext()
{
    m_pickDomain.clear();
    m_pickUuid.clear();
}

int MaterialManager::previewMatypeForEntry(const QString& entryId) const
{
    return m_previewMatypes.value(entryId, -1);
}

void MaterialManager::setPreviewMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
        return;
    if (!isComputableMatype(matype))
    {
        m_previewMatypes.remove(entryId);
    }
    else
    {
        m_previewMatypes.insert(entryId, matype);
    }
}

int MaterialManager::preDeleteMatypeForEntry(const QString& entryId) const
{
    return m_preDeleteMatypes.value(entryId, -1);
}

void MaterialManager::setPreDeleteMatypeForEntry(const QString& entryId, int matype)
{
    if (entryId.isEmpty())
        return;
    if (!isComputableMatype(matype))
    {
        m_preDeleteMatypes.remove(entryId);
    }
    else
    {
        m_preDeleteMatypes.insert(entryId, matype);
    }
}
