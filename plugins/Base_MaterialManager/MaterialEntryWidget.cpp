#include "MaterialEntryWidget.h"

#include <CustomComboBox.h>

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QDoubleValidator>
#include <QLocale>
#include <QPointer>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QMetaObject>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QRadioButton>
#include <QPaintEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSet>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <PickManager.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include "MaterialManager.h"

namespace
{



class AppliedRegionListWidget final : public QListWidget
{
  public:
    explicit AppliedRegionListWidget(QWidget* parent = nullptr) : QListWidget(parent) {}

  protected:
    void paintEvent(QPaintEvent* event) override
    {
        QListWidget::paintEvent(event);
        if (count() != 0)
            return;

        QPainter painter(viewport());
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::WindowText);
        const qreal weight = 0.48;
        const QColor muted = QColor::fromRgbF(
            base.redF() * (1.0 - weight) + text.redF() * weight,
            base.greenF() * (1.0 - weight) + text.greenF() * weight,
            base.blueF() * (1.0 - weight) + text.blueF() * weight);
        painter.setPen(muted);
        painter.drawText(viewport()->rect().adjusted(10, 0, -10, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("暂无区域"));
    }
};

constexpr int FIRST_COMPUTABLE_MATYPE = 2;
constexpr int FIRST_SUGGESTED_MATYPE = 3;
constexpr int MAX_SOLVER_MATYPE = 300;
constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double MAX_FRICTION_ANGLE_DEG = 89.999999;

bool isFrictionCoefficientColumn(const QString& column)
{
    return column == QStringLiteral("抗剪断强度内摩擦系数") ||
           column == QStringLiteral("残余抗剪断强度内摩擦系数");
}

bool isResidualFrictionCoefficientColumn(const QString& column)
{
    return column == QStringLiteral("残余抗剪断强度内摩擦系数");
}

bool isDirectFrictionAngleColumn(const QString& modelKey, const QString& column)
{
    if (column == QStringLiteral("φ0(°)") || column == QStringLiteral("FAI(°)"))
        return true;
    return modelKey == QStringLiteral("soil_nanshui") && column == QStringLiteral("φ0");
}

QString frictionCoefficientHelpText()
{
    return QStringLiteral("摩擦系数，无量纲。");
}

QString frictionAngleHelpText(const QString& coefficientColumn)
{
    if (isResidualFrictionCoefficientColumn(coefficientColumn))
        return QStringLiteral("残余内摩擦角，单位 °。");

    return QStringLiteral("内摩擦角，单位 °。");
}

QString formatLinkedNumber(double value, int decimals = 6)
{
    if (!std::isfinite(value))
        return QString();

    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    if (text == QStringLiteral("-0"))
        text = QStringLiteral("0");
    return text;
}

bool isComputableMatype(int matype)
{
    return matype >= FIRST_COMPUTABLE_MATYPE;
}

bool isDamStaticMaterialModel(const QString& modelKey)
{
    static const QSet<QString> keys = {
        QStringLiteral("soil_eb"), QStringLiteral("soil_kg"),
        QStringLiteral("soil_emu"), QStringLiteral("soil_nanshui"),
        QStringLiteral("soil_emu_crush"), QStringLiteral("linear_elastic")};
    return keys.contains(modelKey);
}

// “基本参数”区域只保留日常建模最常修改的主参数。
// 这里只控制界面分组，不改变 MaterialEntry 的存储，也不改变 IMAT/CONMAT 输出。
bool isBasicMaterialColumn(const QString& modelKey, const QString& column)
{
    if (modelKey == QStringLiteral("linear_elastic")) return true;
    if(modelKey==QStringLiteral("planar_elastic_beam"))return true;
    // Hoek-Brown 的七项求解器输入均为日常必填材料参数。
    // 仅调整表单分组，不改变属性键、数值、保存格式或 CONMAT 导出。
    if (modelKey == QStringLiteral("hoek_brown_rock"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("变形模量E（GPa）"), QStringLiteral("泊松比μ"),
            QStringLiteral("容重t/m3"), QStringLiteral("岩体常数mb"),
            QStringLiteral("岩体常数a"), QStringLiteral("岩体常数s"),
            QStringLiteral("完整岩石单轴抗压强度σci（MPa）")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("huangjinping_underground_plant"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("变形模量E（GPa）"), QStringLiteral("泊松比μ"),
            QStringLiteral("容重t/m3"), QStringLiteral("抗剪断强度内摩擦系数"),
            QStringLiteral("凝聚力C‘（MPa）")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("drucker_prager_rock"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("变形模量E（GPa）"), QStringLiteral("泊松比μ"),
            QStringLiteral("容重t/m3"), QStringLiteral("内摩擦角φ（°）"),
            QStringLiteral("凝聚力C‘（MPa）"), QStringLiteral("DP子类型")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("dagangshan_isotropic"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("变形模量E（GPa）"), QStringLiteral("泊松比μ"),
            QStringLiteral("容重t/m3"), QStringLiteral("抗剪断强度内摩擦系数"),
            QStringLiteral("凝聚力C‘（MPa）")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("huangjinping_bar_element"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("变模E1,E2（GPa）"), QStringLiteral("屈服后变模E2（GPa）"),
            QStringLiteral("容重t/m3"), QStringLiteral("杆面积（m2）"),
            QStringLiteral("抗拉（压）强度ft（MPa）"),
            QStringLiteral("极限抗压强度Rc（MPa）")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("soil_eb") ||
        modelKey == QStringLiteral("soil_emu") ||
        modelKey == QStringLiteral("soil_emu_crush"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("干容重(t/m3)"), QStringLiteral("天然容重(t/m3)"),
            QStringLiteral("饱和容重(t/m3)"), QStringLiteral("φ0(°)"),
            QStringLiteral("c(KPa)"), QStringLiteral("Rf"), QStringLiteral("K"),
            QStringLiteral("n")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("soil_nanshui"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("干容重(t/m3)"), QStringLiteral("天然容重(t/m3)"),
            QStringLiteral("饱和容重(t/m3)"), QStringLiteral("φ0"),
            QStringLiteral("c(KPa)"), QStringLiteral("Rf"), QStringLiteral("K"),
            QStringLiteral("n")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("soil_kg"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("干容重(t/m3)"), QStringLiteral("天然容重(t/m3)"),
            QStringLiteral("饱和容重(t/m3)"), QStringLiteral("Ki,Kie(10^5Pa)"),
            QStringLiteral("Gi,Gie(10^5Pa)"), QStringLiteral("Kie(10^5Pa)"),
            QStringLiteral("Gie(10^5Pa)")};
        return columns.contains(column);
    }

    if (modelKey == QStringLiteral("dam_dynamic"))
    {
        static const QSet<QString> columns = {
            QStringLiteral("DRYK1(KPa)"), QStringLiteral("DRYN1"),
            QStringLiteral("FAI(°)"), QStringLiteral("CO(KPa)"),
            QStringLiteral("PU"), QStringLiteral("DRYDAM")};
        return columns.contains(column);
    }

    return false;
}

void configureParameterFormLayout(QFormLayout* layout)
{
    if (!layout)
        return;
    layout->setContentsMargins(10, 10, 8, 10);
    layout->setHorizontalSpacing(14);
    layout->setVerticalSpacing(8);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFormAlignment(Qt::AlignTop);
}

QString parameterUnit(const QString& modelKey, const QString& column)
{
    if (isFrictionCoefficientColumn(column))
        return QStringLiteral("无量纲");

    if (column == QStringLiteral("变形模量E（GPa）") ||
        column == QStringLiteral("变模E1,E2（GPa）") ||
        column == QStringLiteral("屈服后变模E2（GPa）") ||
        column == QStringLiteral("瞬时弹性模量E1（GPa）") ||
        column == QStringLiteral("粘塑性模量E2（GPa）"))
        return QStringLiteral("GPa");

    if (column == QStringLiteral("干容重(t/m3)") ||
        column == QStringLiteral("天然容重(t/m3)") ||
        column == QStringLiteral("饱和容重(t/m3)") ||
        column == QStringLiteral("容重t/m3"))
        return QStringLiteral("t/m³");

    if (column == QStringLiteral("φ0(°)") || column == QStringLiteral("Δφ(°)") ||
        column == QStringLiteral("内摩擦角φ（°）") ||
        column == QStringLiteral("FAI(°)") || column == QStringLiteral("剪胀角(°)") ||
        ((column == QStringLiteral("φ0") || column == QStringLiteral("Δφ")) &&
         modelKey == QStringLiteral("soil_nanshui")))
        return QStringLiteral("°");

    if (column == QStringLiteral("c(KPa)") || column == QStringLiteral("CO(KPa)") ||
        column == QStringLiteral("DRYK1(KPa)"))
        return QStringLiteral("kPa");

    if (column == QStringLiteral("完整岩石单轴抗压强度σci（MPa）") ||
        column == QStringLiteral("凝聚力C‘（MPa）") ||
        column == QStringLiteral("抗拉（压）强度ft（MPa）") ||
        column == QStringLiteral("极限抗压强度Rc（MPa）") ||
        column == QStringLiteral("屈服抗拉强度Ryt（MPa）") ||
        column == QStringLiteral("屈服抗压强度Ryc（MPa）") ||
        column == QStringLiteral("抗压强度Rc（MPa）") ||
        column == QStringLiteral("抗拉强度Rt（MPa）") ||
        column == QStringLiteral("残余凝聚力C‘（MPa）"))
        return QStringLiteral("MPa");

    if (column == QStringLiteral("杆面积（m2）"))
        return QStringLiteral("m²");

    if (column == QStringLiteral("间距(m)") || column == QStringLiteral("排距(m)"))
        return QStringLiteral("m");

    if (column == QStringLiteral("预应力T（KN）"))
        return QStringLiteral("kN");

    if (column == QStringLiteral("Ki,Kie(10^5Pa)") ||
        column == QStringLiteral("Gi,Gie(10^5Pa)") ||
        column == QStringLiteral("Kie(10^5Pa)") ||
        column == QStringLiteral("Gie(10^5Pa)") ||
        column == QStringLiteral("a(10^5Pa)"))
        return QStringLiteral("10⁵ Pa");

    if (column == QStringLiteral("渗透系数K(Lu)") ||
        column == QStringLiteral("渗透系数Kx(Lu)") ||
        column == QStringLiteral("渗透系数Ky(Lu)") ||
        column == QStringLiteral("渗透系数Kz(Lu)"))
        return QStringLiteral("Lu");

    if (column == QStringLiteral("比储量S"))
        return QStringLiteral("1/m");

    return QString();
}

QString parameterDisplayName(const QString& displayName, const QString& unit)
{
    if (unit.isEmpty())
        return displayName;

    const QString suffix = QStringLiteral("（%1）").arg(unit);
    if (displayName.endsWith(suffix))
        return displayName.left(displayName.size() - suffix.size());
    return displayName;
}

QWidget* createParameterFieldWidget(QLineEdit* editor, const QString& unit, QWidget* parent)
{
    auto* field = new QWidget(parent);
    auto* layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    editor->setMinimumHeight(30);
    layout->addWidget(editor, 1);

    auto* unitLabel = new QLabel(unit, field);
    unitLabel->setObjectName(QStringLiteral("materialUnitLabel"));
    unitLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    unitLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // 单位属于辅助信息，但不能淡到看不清。根据当前主题在正文色和底色之间取中间色。
    const QColor textColor = unitLabel->palette().color(QPalette::WindowText);
    const QColor baseColor = unitLabel->palette().color(QPalette::Base);
    const qreal weight = 0.62;
    const QColor muted = QColor::fromRgbF(
        baseColor.redF() * (1.0 - weight) + textColor.redF() * weight,
        baseColor.greenF() * (1.0 - weight) + textColor.greenF() * weight,
        baseColor.blueF() * (1.0 - weight) + textColor.blueF() * weight);
    QPalette unitPalette = unitLabel->palette();
    unitPalette.setColor(QPalette::WindowText, muted);
    unitLabel->setPalette(unitPalette);

    QFont unitFont = unitLabel->font();
    unitFont.setPointSizeF(qMax<qreal>(8.0, unitFont.pointSizeF() - 0.5));
    unitLabel->setFont(unitFont);
    layout->addWidget(unitLabel, 0);

    return field;
}

QWidget* createFrictionFieldWidget(QLineEdit* editor, const QString& unit, QWidget* parent)
{
    auto* field = new QWidget(parent);
    auto* layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    editor->setMinimumHeight(30);
    layout->addWidget(editor, 1);

    auto* unitLabel = new QLabel(unit, field);
    unitLabel->setObjectName(QStringLiteral("materialUnitLabel"));
    unitLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    unitLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(unitLabel, 0);

    const QColor textColor = unitLabel->palette().color(QPalette::WindowText);
    const QColor baseColor = unitLabel->palette().color(QPalette::Base);
    const qreal weight = 0.58;
    const QColor muted = QColor::fromRgbF(
        baseColor.redF() * (1.0 - weight) + textColor.redF() * weight,
        baseColor.greenF() * (1.0 - weight) + textColor.greenF() * weight,
        baseColor.blueF() * (1.0 - weight) + textColor.blueF() * weight);
    QPalette unitPalette = unitLabel->palette();
    unitPalette.setColor(QPalette::WindowText, muted);
    unitLabel->setPalette(unitPalette);

    return field;
}

QRadioButton* createFrictionChoice(const QString& text, const QString& toolTip, QWidget* parent)
{
    auto* choice = new QRadioButton(text, parent);
    choice->setToolTip(toolTip);
    choice->setCursor(Qt::PointingHandCursor);
    choice->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return choice;
}

void setFrictionEditorReadOnly(QLineEdit* editor, bool readOnly)
{
    if (!editor)
        return;

    editor->setReadOnly(readOnly);

    // 仅 setReadOnly/QPalette 会被应用程序级 QSS 的 QLineEdit 白底覆盖。
    // 对换算值直接设置局部背景/文字颜色，让“可输入”和“换算结果”一眼可区分。
    if (readOnly)
    {
        const QPalette sourcePalette = editor->parentWidget() ? editor->parentWidget()->palette()
                                                              : editor->palette();
        const QColor base = sourcePalette.color(QPalette::Base);
        const QColor mid = sourcePalette.color(QPalette::Mid);
        const qreal shadeWeight = 0.14;
        const QColor mutedBase = QColor::fromRgbF(
            base.redF() * (1.0 - shadeWeight) + mid.redF() * shadeWeight,
            base.greenF() * (1.0 - shadeWeight) + mid.greenF() * shadeWeight,
            base.blueF() * (1.0 - shadeWeight) + mid.blueF() * shadeWeight);
        const QColor mutedText = sourcePalette.color(QPalette::Disabled, QPalette::Text);

        editor->setStyleSheet(
            QStringLiteral("QLineEdit { background-color: %1; color: %2; }")
                .arg(mutedBase.name(QColor::HexRgb), mutedText.name(QColor::HexRgb)));
    }
    else
    {
        editor->setStyleSheet(QString());
    }
}

void setFrictionInputState(QLineEdit* angleEdit, QRadioButton* angleChoice,
                           QLineEdit* coefficientEdit, QRadioButton* coefficientChoice,
                           bool angleInput, bool editable = true)
{
    if (!angleEdit || !coefficientEdit || !angleChoice || !coefficientChoice)
        return;

    angleChoice->setEnabled(editable);
    coefficientChoice->setEnabled(editable);
    setFrictionEditorReadOnly(angleEdit, !editable || !angleInput);
    setFrictionEditorReadOnly(coefficientEdit, !editable || angleInput);

    if (!editable)
    {
        angleEdit->setToolTip(QStringLiteral("沿用峰值。"));
        coefficientEdit->setToolTip(QStringLiteral("沿用峰值。"));
        return;
    }

    const QString coefficientColumn =
        angleEdit->property("frictionCoefficientColumn").toString();
    const bool residual = isResidualFrictionCoefficientColumn(coefficientColumn);
    angleEdit->setToolTip(angleInput ? frictionAngleHelpText(coefficientColumn)
                                    : QStringLiteral("自动换算。"));
    coefficientEdit->setToolTip(
        angleInput ? QStringLiteral("自动换算。")
                   : (residual ? QStringLiteral("残余摩擦系数，无量纲。")
                               : frictionCoefficientHelpText()));
}

QLabel* createSubsectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("materialSubsectionLabel"));
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setContentsMargins(0, 8, 0, 3);
    return label;
}

QColor blendColor(const QColor& base, const QColor& accent, qreal accentWeight)
{
    const qreal w = qBound<qreal>(0.0, accentWeight, 1.0);
    return QColor::fromRgbF(base.redF() * (1.0 - w) + accent.redF() * w,
                            base.greenF() * (1.0 - w) + accent.greenF() * w,
                            base.blueF() * (1.0 - w) + accent.blueF() * w);
}

QDoubleValidator* createMaterialNumberValidator(QObject* parent)
{
    auto* validator = new QDoubleValidator(parent);
    validator->setNotation(QDoubleValidator::ScientificNotation);

    // IMAT.DAT / Fortran 输入使用小数点；固定 C locale，避免系统区域设置导致
    // 界面允许“1,25”而导出阶段无法识别。
    QLocale locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    validator->setLocale(locale);
    return validator;
}

QString parameterHelpText(const QString& modelKey, const QString& column)
{
    QString text;

    if (column.contains(QStringLiteral("干容重")))
        text = QStringLiteral("用于干燥状态下的自重计算。");
    else if (column.contains(QStringLiteral("天然容重")))
        text = QStringLiteral("用于天然状态下的自重计算。");
    else if (column.contains(QStringLiteral("饱和容重")))
        text = QStringLiteral("用于饱和状态下的自重计算。");
    else if (column == QStringLiteral("容重t/m3"))
        text = QStringLiteral("用于材料自重和质量计算。");
    else if (column == QStringLiteral("变模E1,E2（GPa）"))
        text = QStringLiteral("锚杆未屈服阶段的初始弹性模量 E1，对应求解器 CONMAT(:,2)。");
    else if (column == QStringLiteral("屈服后变模E2（GPa）"))
        text = QStringLiteral("锚杆进入屈服区后的切线弹性模量 E2，对应求解器 CONMAT(:,3)。");
    else if (column.contains(QStringLiteral("变形模量")))
        text = QStringLiteral("反映材料整体变形刚度。");
    else if (column.contains(QStringLiteral("瞬时弹性模量")))
        text = QStringLiteral("控制加载瞬间的弹性变形刚度。");
    else if (column.contains(QStringLiteral("粘塑性模量")))
        text = QStringLiteral("控制长期黏塑性变形刚度。");
    else if (column == QStringLiteral("泊松比μ") || column == QStringLiteral("PU"))
        text = QStringLiteral("控制横向变形与轴向变形的关系。");
    else if (column == QStringLiteral("泊松比μ1") || column == QStringLiteral("泊松比μ2"))
        text = QStringLiteral("控制对应流变阶段的横向变形。");
    else if (isDirectFrictionAngleColumn(modelKey, column))
        text = QStringLiteral("内摩擦角，单位 °。");
    else if (column == QStringLiteral("Δφ(°)") || column == QStringLiteral("Δφ"))
        text = QStringLiteral("控制摩擦角随应力状态的变化。");
    else if (column == QStringLiteral("c(KPa)") || column == QStringLiteral("CO(KPa)"))
        text = QStringLiteral("反映材料的黏聚抗剪能力。");
    else if (column == QStringLiteral("抗剪断强度内摩擦系数"))
        text = QStringLiteral("内摩擦系数，无量纲。");
    else if (column == QStringLiteral("凝聚力C‘（MPa）"))
        text = QStringLiteral("反映岩体的黏聚抗剪能力。");
    else if (column == QStringLiteral("抗压强度Rc（MPa）"))
        text = QStringLiteral("独立抗压强度 Rc；填 0 时仍按旧逻辑由 c、φ 自动推导。");
    else if (column == QStringLiteral("抗拉强度Rt（MPa）"))
        text = QStringLiteral("独立抗拉强度 Rt；填 0 时仍按旧逻辑由 c、φ 自动推导。");
    else if (column == QStringLiteral("残余抗剪断强度内摩擦系数"))
        text = QStringLiteral("残余摩擦系数，无量纲。");
    else if (column == QStringLiteral("残余凝聚力C‘（MPa）"))
        text = QStringLiteral("破坏后的残余凝聚力；是否沿用峰值由上方选项决定，单独设置可输入0。");
    else if (column == QStringLiteral("剪胀角(°)"))
        text = QStringLiteral("控制剪切破坏时的体积膨胀程度。");
    else if (column == QStringLiteral("Rf"))
        text = QStringLiteral("控制应力-应变曲线接近破坏的程度。");
    else if (column == QStringLiteral("K"))
        text = QStringLiteral("控制材料应力相关的刚度水平。");
    else if (column == QStringLiteral("n"))
        text = QStringLiteral("控制刚度随应力状态变化的程度。");
    else if (column == QStringLiteral("Kur"))
        text = QStringLiteral("控制卸载和重新加载阶段的刚度系数，对应求解器 CONMAT(:,72)。");
    else if (column == QStringLiteral("Nur"))
        text = QStringLiteral("控制卸载和重新加载刚度随应力变化的指数，对应求解器 CONMAT(:,73)。");
    else if (column == QStringLiteral("Kb"))
        text = QStringLiteral("控制材料体积变形相关的刚度。");
    else if (column == QStringLiteral("m") && modelKey == QStringLiteral("soil_eb"))
        text = QStringLiteral("控制体积刚度随应力状态变化的程度。");
    else if (column == QStringLiteral("G") || column == QStringLiteral("D") ||
             column == QStringLiteral("F"))
        text = QStringLiteral("E-μ 模型中与泊松比变化相关的拟合参数。");
    else if ((column == QStringLiteral("C1") || column == QStringLiteral("C2") ||
              column == QStringLiteral("C3")) &&
             modelKey == QStringLiteral("soil_emu_crush"))
        text = QStringLiteral("E-μ 破碎模型的破碎拟合系数。");
    else if ((column == QStringLiteral("n1") || column == QStringLiteral("n2") ||
              column == QStringLiteral("n3")) &&
             modelKey == QStringLiteral("soil_emu_crush"))
        text = QStringLiteral("E-μ 破碎模型的破碎拟合指数。");
    else if (column == QStringLiteral("Ki,Kie(10^5Pa)"))
        text = QStringLiteral("K-G 模型加载阶段体积刚度常数 Ki，对应 CONMAT(:,69)。");
    else if (column == QStringLiteral("αk,αke"))
        text = QStringLiteral("K-G 模型加载阶段体积刚度压力系数 αk，对应 CONMAT(:,70)。");
    else if (column == QStringLiteral("Gi,Gie(10^5Pa)"))
        text = QStringLiteral("K-G 模型加载阶段剪切刚度常数 Gi，对应 CONMAT(:,74)。");
    else if (column == QStringLiteral("αG,αGe"))
        text = QStringLiteral("K-G 模型加载阶段剪切刚度压力系数 αG，对应 CONMAT(:,71)。");
    else if (column == QStringLiteral("βG"))
        text = QStringLiteral("K-G 模型加载阶段剪切刚度的广义剪应力修正系数 βG，对应 CONMAT(:,75)。");
    else if (column == QStringLiteral("a(10^5Pa)") || column == QStringLiteral("b"))
        text = QStringLiteral("K-G 模型强度/应力水平计算参数，对应 CONMAT(:,72~73)。");
    else if (column == QStringLiteral("Kie(10^5Pa)"))
        text = QStringLiteral("K-G 模型卸载/再加载阶段体积刚度常数 Kie，对应 CONMAT(:,76)。");
    else if (column == QStringLiteral("αke"))
        text = QStringLiteral("K-G 模型卸载/再加载阶段体积刚度压力系数 αke，对应 CONMAT(:,78)。");
    else if (column == QStringLiteral("Gie(10^5Pa)"))
        text = QStringLiteral("K-G 模型卸载/再加载阶段剪切刚度常数 Gie，对应 CONMAT(:,77)。");
    else if (column == QStringLiteral("αGe"))
        text = QStringLiteral("K-G 模型卸载/再加载阶段剪切刚度压力系数 αGe，对应 CONMAT(:,79)。");
    else if (column == QStringLiteral("DRYK1(KPa)"))
        text = QStringLiteral("坝料动力模型的刚度系数。");
    else if (column == QStringLiteral("DRYN1"))
        text = QStringLiteral("坝料动力模型的刚度指数。");
    else if (column == QStringLiteral("DRYDAM"))
        text = QStringLiteral("控制材料的最大动力阻尼水平。");
    else if (column.startsWith(QStringLiteral("RATIAO")))
        text = QStringLiteral("用于液化判定的循环次数参数。");
    else if (column == QStringLiteral("Kv") || column == QStringLiteral("Kp"))
        text = QStringLiteral("坝料动力计算的回归系数。");
    else if (column == QStringLiteral("nv") || column == QStringLiteral("np"))
        text = QStringLiteral("坝料动力计算的回归指数。");
    else if (column == QStringLiteral("杆面积（m2）"))
        text = QStringLiteral("决定锚杆轴向刚度和承载面积。");
    else if (column == QStringLiteral("抗拉（压）强度ft（MPa）"))
        text = QStringLiteral("锚杆极限抗拉强度 Rt，对应求解器 CONMAT(:,10)。");
    else if (column == QStringLiteral("极限抗压强度Rc（MPa）"))
        text = QStringLiteral("锚杆极限抗压强度 Rc，对应求解器 CONMAT(:,9)。");
    else if (column == QStringLiteral("屈服抗拉强度Ryt（MPa）"))
        text = QStringLiteral("锚杆进入拉伸屈服段的强度 Ryt，对应求解器 CONMAT(:,92)。");
    else if (column == QStringLiteral("屈服抗压强度Ryc（MPa）"))
        text = QStringLiteral("锚杆进入压缩屈服段的强度 Ryc，对应求解器 CONMAT(:,91)。");
    else if (column == QStringLiteral("间距(m)"))
        text = QStringLiteral("设置同排锚杆之间的间距。");
    else if (column == QStringLiteral("排距(m)"))
        text = QStringLiteral("设置相邻锚杆排之间的距离。");
    else if (column == QStringLiteral("预应力T（KN）"))
        text = QStringLiteral("设置锚杆或锚索的初始预应力。");
    else if (column == QStringLiteral("预应系数"))
        text = QStringLiteral("用于修正实际施加的预应力水平。");
    else if (column == QStringLiteral("母岩材料编号"))
        text = QStringLiteral("指定开挖或再生材料对应的原始母岩。");
    else if (column == QStringLiteral("岩类分区"))
        text = QStringLiteral("用于区分普通、开挖、再生和加固材料。");
    else if (column == QStringLiteral("半开挖区荷载释放系数"))
        text = QStringLiteral("荷载释放比例，范围0～1，全释放取1。"
                              "二维按开挖面计算时也直接使用此系数，取0将不产生卸荷。");
    else if (column == QStringLiteral("再生时对应开挖级"))
        text = QStringLiteral("指定再生材料对应的开挖阶段。");
    else if (column == QStringLiteral("洞室边缘松弛系数α0"))
        text = QStringLiteral("控制洞室边缘岩体的松弛程度。");
    else if (column == QStringLiteral("松弛距离d0"))
        text = QStringLiteral("控制洞室边缘松弛的影响距离。");
    else if (column == QStringLiteral("cd"))
        text = QStringLiteral("南水模型的拟合系数。");
    else if (column == QStringLiteral("nd"))
        text = QStringLiteral("南水模型的拟合指数。");
    else if (column == QStringLiteral("Rd"))
        text = QStringLiteral("南水模型的修正系数。");
    else if (column == QStringLiteral("渗透系数K(Lu)"))
        text = QStringLiteral("各向同性渗透系数；导出到求解器 CONMAT(:,96~98)。");
    else if (column == QStringLiteral("渗透系数Kx(Lu)"))
        text = QStringLiteral("全局 X 方向渗透系数，对应求解器 CONMAT(:,96)。");
    else if (column == QStringLiteral("渗透系数Ky(Lu)"))
        text = QStringLiteral("全局 Y 方向渗透系数，对应求解器 CONMAT(:,97)。");
    else if (column == QStringLiteral("渗透系数Kz(Lu)"))
        text = QStringLiteral("全局 Z 方向渗透系数，对应求解器 CONMAT(:,98)。");
    else if (column == QStringLiteral("比储量S"))
        text = QStringLiteral("非稳态渗流的储水参数，对应求解器 CONMAT(:,101)，控制水头随时间变化的快慢。");
    else if (column == QStringLiteral("允许比降"))
        text = QStringLiteral("用于判断水力比降是否超过允许值。");
    else if (column.contains(QStringLiteral("应变速率系数")))
        text = QStringLiteral("控制材料变形随时间发展的速度。");
    else if (column.contains(QStringLiteral("体积流变系数")))
        text = QStringLiteral("控制材料长期体积变形的幅度。");
    else if (column.contains(QStringLiteral("剪切流变系数")))
        text = QStringLiteral("控制材料长期剪切变形的幅度。");
    else if (column.contains(QStringLiteral("体积流变指数")))
        text = QStringLiteral("控制体积流变随应力和时间的非线性。");
    else if (column.contains(QStringLiteral("剪切流变指数")))
        text = QStringLiteral("控制剪切流变随应力和时间的非线性。");
    else if (column.contains(QStringLiteral("主应变")))
        text = QStringLiteral("用于拟合主应变随时间的发展规律。");
    else if (column.contains(QStringLiteral("粘弹性系数")))
        text = QStringLiteral("控制可恢复的时间相关变形。");
    else if (column.contains(QStringLiteral("粘塑性系数")))
        text = QStringLiteral("控制不可恢复的时间相关变形。");

    if (text.isEmpty())
        text = QStringLiteral("用于该材料模型的计算参数。");

    return text;
}

QLabel* createParameterLabel(const QString& text, const QString& toolTip, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setToolTip(toolTip);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

void updateConditionalParameterVisibility(const QString& modelKey, const MaterialEntry* entry,
                                          QFormLayout* advancedLayout,
                                          const QMap<QString, QLineEdit*>& valueEdits)
{
    if (!entry || !advancedLayout ||
        modelKey != QStringLiteral("huangjinping_underground_plant"))
    {
        return;
    }

    // 与 MaterialManager 写 CONMAT 的条件保持一致：
    // ITAG_EX=-1 才写荷载释放系数；ITAG_EX=1 才写再生开挖级。
    const QString excavationState =
        entry->properties.value(QStringLiteral("岩类分区"), QStringLiteral("0"));

    const auto setVisible = [advancedLayout, &valueEdits](const QString& column, bool visible) {
        const auto it = valueEdits.constFind(column);
        if (it == valueEdits.constEnd() || !it.value())
            return;
        QWidget* fieldWidget = it.value()->parentWidget();
        advancedLayout->setRowVisible(fieldWidget ? fieldWidget : it.value(), visible);
    };

    setVisible(QStringLiteral("半开挖区荷载释放系数"),
               excavationState == QStringLiteral("-1"));
    setVisible(QStringLiteral("再生时对应开挖级"),
               excavationState == QStringLiteral("1"));
}

QString matypeDisplayText(int matype, const MshPickMap* pickMap = nullptr)
{
    if (!isComputableMatype(matype))
        return QString();
    QString display;
    if (pickMap)
    {
        QString name = pickMap->physicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = pickMap->physicalName(matype).trimmed();
        if (!name.isEmpty())
            display = name;
    }
    else if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        QString name = meshService->matypePhysicalName(matype, 3).trimmed();
        if (name.isEmpty())
            name = meshService->matypePhysicalName(matype).trimmed();
        if (!name.isEmpty())
            display = name;
    }
    if (display.isEmpty())
        display = QString::number(matype);
    if (matype == 2)
        display += QStringLiteral("（保留编号，固定线弹性）");
    return display;
}
}

MaterialEntryWidget::MaterialEntryWidget(MaterialManager* manager, QWidget* parent)
    : BasePage("MaterialEntryWidget"), m_materialManager(manager), m_isUpdating(false),
      m_formLayout(new QFormLayout), m_nameEdit(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);

    m_formLayout->setContentsMargins(9, 9, 9, 9);
    m_formLayout->setHorizontalSpacing(14);
    m_formLayout->setVerticalSpacing(9);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    m_formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_formLayout->setFormAlignment(Qt::AlignTop);

    // 与约束、荷载和分析页面共用原生分组框；折叠只影响可见性。
    setStyleSheet(QStringLiteral(
        "QLabel#materialSectionTitle {"
        "  font-weight: 600;"
        "}"
        "QLabel#materialSubsectionLabel {"
        "  font-weight: 600;"
        "}"
        "QLabel#materialUnitLabel {"
        "  padding-left: 1px;"
        "}"
        "QListWidget#appliedMatypesList {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 2px;"
        "  background: palette(base);"
        "  padding: 2px;"
        "}"));

    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("materialEntryContent"));
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setLayout(m_formLayout);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    // Dock 外层已经统一提供滚动区域。材料页本身只负责内容布局，
    // 避免嵌套滚动条和 QFormLayout 将可扩展行拉出大块空白。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container, 0, Qt::AlignTop);
    layout->addStretch(1);
}

MaterialEntryWidget::~MaterialEntryWidget()
{
    beforeUnload();
}

void MaterialEntryWidget::beforeLoad()
{
    m_beforeUnloadHandled = false;
    if (m_materialManager)
    {
        m_materialManager->setPickContext(getPageType(), m_uuid);
        if (!m_entryId.isEmpty())
        {
            MaterialEntry* entry = lookupEntry();
            const int preview = m_materialManager->previewMatypeForEntry(m_entryId);
            const int preDelete = m_materialManager->preDeleteMatypeForEntry(m_entryId);
            const bool hasHighlightState =
                entry && (!entry->appliedMatypes.isEmpty() || preview >= 0 || preDelete >= 0);
            // 即使是尚未分配任何网格组的新材料，也要启用视图域点选。
            // 否则只有已施加材料才会初始化拾取，表现为新材料无法点击网格。
            ensurePickInteractionReady();
            if (hasHighlightState)
            {
                refreshMatypeHighlightSafe();
            }
        }
    }
}

void MaterialEntryWidget::beforeUnload()
{
    if (m_beforeUnloadHandled)
        return;
    m_beforeUnloadHandled = true;

    // 离开材料页面时必须清掉本页面在 PickManager 中留下的 Domain 高亮状态。
    // 只 deactivate 不会清 setAppliedDomains / preview / preDelete，容易导致颜色残留。
    m_previewMatype = -1;
    m_preDeleteMatype = -1;

    auto& pm = PickManager::instance();

    disconnect(&pm, &PickManager::domainSelected, this,
               &MaterialEntryWidget::onDomainPicked);

    pm.setAppliedDomains(getPageType(), m_uuid, QSet<DomainKey>{});
    pm.setSelectedDomain(getPageType(), m_uuid, DomainKey{});
    pm.setPreviewDomain(getPageType(), m_uuid, DomainKey{});
    pm.setPreDeleteDomain(getPageType(), m_uuid, DomainKey{});
    pm.clearDomainSelection(getPageType(), m_uuid);
    pm.deactivate(getPageType(), m_uuid, PickMode::Domain);

    m_pickInteractionReady = false;

    if (m_materialManager)
    {
        m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        m_materialManager->clearPickContext();
    }

    if (m_matypeCombo && m_lastHoveredIndex >= 0)
    {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
        m_lastHoveredIndex = -1;
    }

    if (m_appliedMatypesList)
    {
        for (int i = 0; i < m_appliedMatypesList->count(); ++i)
            m_appliedMatypesList->item(i)->setBackground(QBrush());
    }
}

void MaterialEntryWidget::setEntry(const QString& modelKey, const QString& entryId)
{
    // 工程树刷新时，大多数材料并没有发生变化。避免重复销毁并重建所有材料页，
    // 否则材料较多时切换模型会出现明显卡顿。
    if (m_modelKey == modelKey && m_entryId == entryId && m_nameEdit)
        return;

    m_modelKey = modelKey;
    m_entryId = entryId;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    rebuildForm(model, entry);
}

void MaterialEntryWidget::refreshEntryName()
{
    if (!m_nameEdit)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry || m_nameEdit->text() == entry->name)
        return;

    const QSignalBlocker blocker(m_nameEdit);
    m_nameEdit->setText(entry->name);
}

void MaterialEntryWidget::onNameEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    entry->name = text;
    m_materialManager->notifyEntryUpdated(m_entryId, true);
}

void MaterialEntryWidget::onNameEditingFinished()
{
    if (m_isUpdating || !m_nameEdit || !m_materialManager)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    const QString uniqueName =
        m_materialManager->makeUniqueMaterialName(m_nameEdit->text(), m_entryId);
    if (entry->name == uniqueName && m_nameEdit->text() == uniqueName)
        return;

    entry->name = uniqueName;
    {
        const QSignalBlocker blocker(m_nameEdit);
        m_nameEdit->setText(uniqueName);
    }
    m_materialManager->notifyEntryUpdated(m_entryId, true);
}

void MaterialEntryWidget::onMaterialModelActivated(int)
{
    if (m_isUpdating || !m_modelCombo || !m_materialManager)
        return;

    const QString targetModelKey = m_modelCombo->currentData().toString();
    if (targetModelKey.isEmpty() || targetModelKey == m_modelKey)
        return;

    // 切换会重建当前材料页。排到本次下拉框信号处理结束后执行，
    // 避免在 QComboBox 正在发射 activated 时删除它自身。
    m_modelCombo->setEnabled(false);
    QMetaObject::invokeMethod(
        this,
        [this, targetModelKey]()
        {
            QString error;
            if (!m_materialManager->changeEntryModel(m_entryId, targetModelKey, &error))
            {
                if (m_modelCombo)
                {
                    const QSignalBlocker blocker(m_modelCombo);
                    const int oldIndex = m_modelCombo->findData(m_modelKey);
                    m_modelCombo->setCurrentIndex(oldIndex >= 0 ? oldIndex : 0);
                    m_modelCombo->setEnabled(true);
                }
                QMessageBox::warning(this, QStringLiteral("切换材料模型"),
                                     error.isEmpty() ? QStringLiteral("切换材料模型失败。")
                                                     : error);
            }
        },
        Qt::QueuedConnection);
}

void MaterialEntryWidget::onValueEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    auto* lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    const QString column = lineEdit->property("columnName").toString();
    entry->properties.insert(column, text);
    // 残余凝聚力沿用峰值时，显示值跟随峰值变化，但不覆盖独立保存的残余值。
    if (model->key == QStringLiteral("dagangshan_isotropic") &&
        column == QStringLiteral("凝聚力C‘（MPa）") &&
        entry->properties.value(QStringLiteral("残余凝聚力沿用峰值"),
                                QStringLiteral("1")) != QStringLiteral("0"))
    {
        QLineEdit* residual = m_valueEdits.value(QStringLiteral("残余凝聚力C‘（MPa）"), nullptr);
        if (residual)
        {
            const QSignalBlocker blocker(residual);
            residual->setText(text);
        }
    }
    if (isFrictionCoefficientColumn(column))
        refreshFrictionAngleFields(entry);
    if (isDirectFrictionAngleColumn(model->key, column))
        refreshFrictionCoefficientFields(model, entry);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onOptionValueChanged(int)
{
    if (m_isUpdating)
        return;

    auto* combo = qobject_cast<QComboBox*>(sender());
    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!combo || !entry || !model)
        return;

    const QString column = combo->property("columnName").toString();
    entry->properties.insert(column, combo->currentData().toString());
    if (column == QStringLiteral("残余摩擦系数沿用峰值"))
        refreshFrictionAngleFields(entry);
    updateConditionalParameterVisibility(model->key, entry, m_advancedLayout, m_valueEdits);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onSeepageModelChanged(int)
{
    if (m_isUpdating || !m_seepageCombo)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    const QString oldKey = entry->seepageModelKey;
    const QString newKey = m_seepageCombo->currentData().toString();
    if (!newKey.isEmpty() && !entry->creepModelKey.isEmpty())
    {
        const QSignalBlocker blocker(m_seepageCombo);
        const int previousIndex = m_seepageCombo->findData(oldKey);
        m_seepageCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        QMessageBox::warning(this, QStringLiteral("附加模型冲突"),
                             QStringLiteral("当前流变与渗流不能同时设置在同一材料中。"));
        return;
    }
    const QString isotropicKey = QStringLiteral("jk_corewall_steady_seepage");
    const QString anisotropicKey = QStringLiteral("jk_corewall_anisotropic_seepage");

    // 第一次由各向同性切换为各向异性时，沿用当前 K，避免三个方向突然回到默认值。
    if (oldKey == isotropicKey && newKey == anisotropicKey)
    {
        const QString k = entry->seepageProperties.value(
            QStringLiteral("渗透系数K(Lu)"),
            defaultMaterialPropertyValue(isotropicKey, QStringLiteral("渗透系数K(Lu)")));
        const QStringList directionalKeys = {QStringLiteral("渗透系数Kx(Lu)"),
                                             QStringLiteral("渗透系数Ky(Lu)"),
                                             QStringLiteral("渗透系数Kz(Lu)")};
        for (const QString& key : directionalKeys)
        {
            if (!entry->seepageProperties.contains(key))
                entry->seepageProperties.insert(key, k);
        }
    }
    // 若材料最初就是各向异性且从未设置过各向同性 K，切回时以 Kx 作为兼容值。
    else if (oldKey == anisotropicKey && newKey == isotropicKey &&
             !entry->seepageProperties.contains(QStringLiteral("渗透系数K(Lu)")))
    {
        entry->seepageProperties.insert(
            QStringLiteral("渗透系数K(Lu)"),
            entry->seepageProperties.value(
                QStringLiteral("渗透系数Kx(Lu)"),
                defaultMaterialPropertyValue(anisotropicKey,
                                             QStringLiteral("渗透系数Kx(Lu)"))));
    }

    entry->seepageModelKey = newKey;
    rebuildSeepageFields(entry);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onCreepModelChanged(int)
{
    if (m_isUpdating || !m_creepCombo)
        return;

    MaterialModel* model = nullptr;
    MaterialEntry* entry = lookupEntry(&model);
    if (!entry || !model)
        return;

    const QString newKey = m_creepCombo->currentData().toString();
    if (!newKey.isEmpty() && !entry->seepageModelKey.isEmpty())
    {
        const QSignalBlocker blocker(m_creepCombo);
        const int previousIndex = m_creepCombo->findData(entry->creepModelKey);
        m_creepCombo->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
        QMessageBox::warning(this, QStringLiteral("附加模型冲突"),
                             QStringLiteral("当前流变与渗流不能同时设置在同一材料中。"));
        return;
    }

    entry->creepModelKey = newKey;
    rebuildCreepFields(entry);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::onSeepageValueEdited(const QString& text)
{
    if (m_isUpdating)
        return;

    auto* lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit)
        return;

    MaterialEntry* entry = lookupEntry();
    if (!entry)
        return;

    const QString column = lineEdit->property("columnName").toString();
    entry->seepageProperties.insert(column, text);
    m_materialManager->notifyEntryUpdated(m_entryId, false);
}

void MaterialEntryWidget::rebuildSeepageFields(MaterialEntry* entry)
{
    if (!m_seepageFieldsLayout || !entry)
        return;

    while (m_seepageFieldsLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_seepageFieldsLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_seepageFieldsLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_seepageFieldsLayout->removeRow(0);
    }
    m_seepageValueEdits.clear();

    const QString seepageModelKey = m_seepageCombo ? m_seepageCombo->currentData().toString()
                                                    : entry->seepageModelKey;
    const auto& seepageModels = m_materialManager->seepageModels();
    const MaterialModel* selectedSeepageModel = nullptr;
    for (const auto& seepageModel : seepageModels)
    {
        if (seepageModel.key == seepageModelKey)
        {
            selectedSeepageModel = &seepageModel;
            break;
        }
    }
    if (m_seepageFieldsWidget && m_advancedLayout)
        m_advancedLayout->setRowVisible(m_seepageFieldsWidget, selectedSeepageModel != nullptr);
    if (!selectedSeepageModel)
        return;

    for (int columnIndex = 0; columnIndex < selectedSeepageModel->columns.size(); ++columnIndex)
    {
        const QString& column = selectedSeepageModel->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
        {
            continue;
        }
        if (!entry->seepageProperties.contains(column))
            entry->seepageProperties.insert(
                column, defaultMaterialPropertyValue(selectedSeepageModel->key, column));
        auto* seepageValueEdit = new QLineEdit(
            entry->seepageProperties.value(
                column, defaultMaterialPropertyValue(selectedSeepageModel->key, column)),
            this);
        seepageValueEdit->setMinimumWidth(10);
        seepageValueEdit->setValidator(createMaterialNumberValidator(seepageValueEdit));
        seepageValueEdit->setProperty("columnName", column);
        connect(seepageValueEdit, &QLineEdit::textEdited, this,
                &MaterialEntryWidget::onSeepageValueEdited);
        const QString displayLabel = columnIndex < selectedSeepageModel->displayColumns.size()
                                         ? selectedSeepageModel->displayColumns.at(columnIndex)
                                         : column;
        const QString unit = parameterUnit(selectedSeepageModel->key, column);
        const QString label = parameterDisplayName(displayLabel, unit);
        const QString toolTip = parameterHelpText(selectedSeepageModel->key, column);
        seepageValueEdit->setToolTip(toolTip);
        QWidget* fieldWidget =
            createParameterFieldWidget(seepageValueEdit, unit, m_seepageFieldsWidget);
        m_seepageFieldsLayout->addRow(
            createParameterLabel(label, toolTip, m_seepageFieldsWidget), fieldWidget);
        m_seepageValueEdits.insert(column, seepageValueEdit);
    }
}

void MaterialEntryWidget::rebuildCreepFields(MaterialEntry* entry)
{
    if (!m_creepFieldsLayout || !entry)
        return;

    while (m_creepFieldsLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_creepFieldsLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_creepFieldsLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_creepFieldsLayout->removeRow(0);
    }
    m_creepValueEdits.clear();

    const QString creepModelKey =
        m_creepCombo ? m_creepCombo->currentData().toString() : entry->creepModelKey;
    const MaterialModel* selectedModel = nullptr;
    const auto& supplementalModels = m_materialManager->seepageModels();
    for (const auto& model : supplementalModels)
    {
        if (model.key == creepModelKey &&
            model.key != QStringLiteral("jk_corewall_steady_seepage") &&
            model.key != QStringLiteral("jk_corewall_anisotropic_seepage"))
        {
            selectedModel = &model;
            break;
        }
    }
    if (m_creepFieldsWidget && m_advancedLayout)
        m_advancedLayout->setRowVisible(m_creepFieldsWidget, selectedModel != nullptr);
    if (!selectedModel)
        return;

    for (int columnIndex = 0; columnIndex < selectedModel->columns.size(); ++columnIndex)
    {
        const QString& column = selectedModel->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
            continue;
        if (!entry->seepageProperties.contains(column))
            entry->seepageProperties.insert(
                column, defaultMaterialPropertyValue(selectedModel->key, column));

        auto* valueEdit = new QLineEdit(
            entry->seepageProperties.value(
                column, defaultMaterialPropertyValue(selectedModel->key, column)),
            this);
        valueEdit->setMinimumWidth(10);
        valueEdit->setValidator(createMaterialNumberValidator(valueEdit));
        valueEdit->setProperty("columnName", column);
        connect(valueEdit, &QLineEdit::textEdited, this,
                &MaterialEntryWidget::onSeepageValueEdited);
        const QString displayLabel = columnIndex < selectedModel->displayColumns.size()
                                         ? selectedModel->displayColumns.at(columnIndex)
                                         : column;
        const QString unit = parameterUnit(selectedModel->key, column);
        const QString label = parameterDisplayName(displayLabel, unit);
        const QString toolTip = parameterHelpText(selectedModel->key, column);
        valueEdit->setToolTip(toolTip);
        QWidget* fieldWidget = createParameterFieldWidget(valueEdit, unit, m_creepFieldsWidget);
        m_creepFieldsLayout->addRow(
            createParameterLabel(label, toolTip, m_creepFieldsWidget), fieldWidget);
        m_creepValueEdits.insert(column, valueEdit);
    }
}

void MaterialEntryWidget::rebuildForm(MaterialModel* model, MaterialEntry* entry)
{
    m_isUpdating = true;
    clearForm();

    if (!model || !entry)
    {
        m_isUpdating = false;
        return;
    }

    auto* infoGroup=new QGroupBox(QStringLiteral("材料信息"),this);
    infoGroup->setObjectName(QStringLiteral("materialInfoGroup"));
    auto* infoForm=new QFormLayout(infoGroup);
    configureParameterFormLayout(infoForm);
    m_formLayout->addRow(infoGroup);

    // ===== 通用信息：不属于“基本/高级参数”，始终显示 =====
    m_nameEdit = new QLineEdit(entry->name, this);
    m_nameEdit->setMinimumWidth(10);
    connect(m_nameEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onNameEdited);
    connect(m_nameEdit, &QLineEdit::editingFinished, this,
            &MaterialEntryWidget::onNameEditingFinished);
    infoForm->addRow(QStringLiteral("材料名称"), m_nameEdit);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setMinimumWidth(10);
    m_modelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_modelCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modelCombo->setMinimumContentsLength(0);
    for (const MaterialModel& availableModel : m_materialManager->models())
        m_modelCombo->addItem(availableModel.displayName, availableModel.key);
    const int modelIndex = m_modelCombo->findData(model->key);
    m_modelCombo->setCurrentIndex(modelIndex >= 0 ? modelIndex : 0);
    m_modelCombo->setToolTip(
        QStringLiteral("切换材料本构模型；已有材料名称和关联关系保持不变。"));
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MaterialEntryWidget::onMaterialModelActivated);
    infoForm->addRow(QStringLiteral("材料模型"), m_modelCombo);

    QList<int> appliedList = entry->appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());

    // ===== 材料与网格关联 =====
    auto* assignmentCard = new QGroupBox(QStringLiteral("网格关联"),this);
    assignmentCard->setObjectName(QStringLiteral("materialAssignmentSection"));
    assignmentCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    assignmentCard->setToolTip(QStringLiteral("将当前材料与网格材料编号建立关联。"));
    m_assignmentWidget = assignmentCard;

    auto* assignmentLayout = new QVBoxLayout(assignmentCard);
    assignmentLayout->setContentsMargins(10, 10, 10, 10);
    assignmentLayout->setSpacing(8);

    auto* assignmentForm = new QFormLayout;
    assignmentForm->setContentsMargins(0, 0, 0, 0);
    assignmentForm->setHorizontalSpacing(14);
    assignmentForm->setVerticalSpacing(8);
    assignmentForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    assignmentForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    assignmentForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    assignmentForm->setFormAlignment(Qt::AlignTop);
    assignmentLayout->addLayout(assignmentForm);

    QWidget* actionRowWidget = new QWidget(assignmentCard);
    auto* rowLayout = new QHBoxLayout(actionRowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    m_matypeCombo = new CustomComboBox(assignmentCard);
    m_matypeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_matypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_matypeCombo->setMinimumContentsLength(4);
    m_matypeCombo->setToolTip(
        QStringLiteral("可在可视化窗口点击已有网格区域，或在这里选取材料编号。"
                       "点击后再按“施加到网格”确认。编号 1 不参加计算；编号 2 为保留编号。"));
    m_matypeCombo->addItem(QStringLiteral("未选择"), QVariant());
    m_matypeOptionsLoaded = false;
    for (int value : appliedList)
        appendMatypeOption(value);

    connect(m_matypeCombo, &CustomComboBox::itemHovered, this,
            &MaterialEntryWidget::onMatypeHovered);
    connect(m_matypeCombo, &CustomComboBox::dropdownOpened, this,
            [this]()
            {
                m_matypeDropdownOpen = true;
                ensureMatypeOptionsLoaded();
            });
    connect(m_matypeCombo, &CustomComboBox::dropdownClosed, this,
            &MaterialEntryWidget::onDropdownClosed);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onMatypeChanged);
    connect(m_matypeCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MaterialEntryWidget::onMatypeActivated);

    m_applyButton = new QPushButton(QStringLiteral("施加到网格"), actionRowWidget);
    m_applyButton->setObjectName(QStringLiteral("materialCompactAction"));
    m_applyButton->setToolTip(QStringLiteral("将当前材料施加到所选网格区域。"));
    connect(m_applyButton, &QPushButton::clicked, this,
            &MaterialEntryWidget::onApplyMatypeClicked);

    assignmentForm->addRow(
        createParameterLabel(QStringLiteral("材料编号"),
                             QStringLiteral("对应求解器 MATYPE，用于把材料与网格区域建立关联。"),
                             assignmentCard),
        m_matypeCombo);

    m_viewPickCheck = new QCheckBox(QStringLiteral("启用视图点选"), assignmentCard);
    m_viewPickCheck->setChecked(m_viewPickEnabled);
    m_viewPickCheck->setToolTip(QStringLiteral("勾选后可在视图中点击一个网格材料区域，"
        "自动选中其 MATYPE；仍需点击“施加到网格”才能完成材料关联。"));
    rowLayout->addWidget(m_viewPickCheck);
    rowLayout->addStretch();
    rowLayout->addWidget(m_applyButton);
    assignmentForm->addRow(actionRowWidget);
    connect(m_viewPickCheck, &QCheckBox::toggled, this, [this](bool enabled)
    {
        m_viewPickEnabled = enabled;
        if (m_pickInteractionReady)
            PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, enabled);
    });

    QWidget* appliedRowWidget = new QWidget(assignmentCard);
    appliedRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* appliedLayout = new QHBoxLayout(appliedRowWidget);
    appliedLayout->setContentsMargins(0, 0, 0, 0);
    appliedLayout->setSpacing(8);

    m_appliedMatypesList = new AppliedRegionListWidget(appliedRowWidget);
    m_appliedMatypesList->setObjectName(QStringLiteral("appliedMatypesList"));
    m_appliedMatypesList->setFrameShape(QFrame::StyledPanel);
    m_appliedMatypesList->setFrameShadow(QFrame::Plain);
    m_appliedMatypesList->setLineWidth(1);
    // 这里既是当前分配结果，也是用户检查材料施加是否正确的主要区域。
    // 保留足够可视高度；数量较多时再在列表内部滚动。
    const int appliedListHeight =
        qBound(2,appliedList.size(),4)*(fontMetrics().lineSpacing()+6)+8;
    m_appliedMatypesList->setFixedHeight(appliedListHeight);
    m_appliedMatypesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_appliedMatypesList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_appliedMatypesList->setUniformItemSizes(true);
    m_appliedMatypesList->setToolTip(QStringLiteral("当前材料已经关联的网格区域。"));
    for (int value : appliedList)
    {
        auto* item = new QListWidgetItem(matypeDisplayText(value), m_appliedMatypesList);
        item->setData(Qt::UserRole, value);
    }
    connect(m_appliedMatypesList, &QListWidget::currentTextChanged, this,
            &MaterialEntryWidget::onAppliedMatypeSelected);

    m_removeButton = new QPushButton(QStringLiteral("移除"), appliedRowWidget);
    m_removeButton->setObjectName(QStringLiteral("materialCompactAction"));
    m_removeButton->setToolTip(QStringLiteral("移除当前选中的网格区域关联。"));
    m_removeButton->setEnabled(false);
    connect(m_removeButton, &QPushButton::clicked, this,
            &MaterialEntryWidget::onRemoveMatypeClicked);

    appliedLayout->addWidget(m_appliedMatypesList, 1);
    appliedLayout->addWidget(m_removeButton, 0, Qt::AlignTop);

    auto* appliedLabel = createParameterLabel(
        QStringLiteral("已施加区域"), QStringLiteral("显示当前材料已施加到哪些网格区域。"),
        assignmentCard);
    appliedLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    appliedLabel->setContentsMargins(0, 7, 0, 0);
    assignmentForm->addRow(appliedLabel, appliedRowWidget);

    m_formLayout->addRow(assignmentCard);

    // ===== 参数区域：使用与其他页面一致的原生 GroupBox =====
    auto* parameterContainer = new QWidget(this);
    parameterContainer->setObjectName(QStringLiteral("materialParameterContainer"));
    parameterContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto* parameterLayout = new QVBoxLayout(parameterContainer);
    parameterLayout->setContentsMargins(0, 4, 0, 0);
    parameterLayout->setSpacing(12);

    const auto createParameterGroup =
        [parameterContainer, parameterLayout](const QString& title, const QString& name,
                                              QWidget*& content, QFormLayout*& formLayout) {
            auto* group=new QGroupBox(title,parameterContainer);
            group->setObjectName(name);
            group->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);
            auto* outer=new QVBoxLayout(group);
            content=new QWidget(group);
            outer->addWidget(content);
            formLayout=new QFormLayout(content);
            configureParameterFormLayout(formLayout);
            formLayout->setContentsMargins(0,0,0,0);
            if(name==QStringLiteral("materialAdvancedGroup")) {
                group->setCheckable(true);
                QObject::connect(group,&QGroupBox::toggled,content,&QWidget::setVisible);
                group->setChecked(false);
                content->hide();
            }
            parameterLayout->addWidget(group);
            return group;
        };

    QWidget* basicPage = nullptr;
    QWidget* advancedPage = nullptr;
    auto* basicGroup = createParameterGroup(
        QStringLiteral("基本参数"), QStringLiteral("materialBasicGroup"), basicPage, m_basicLayout);
    auto* advancedGroup = createParameterGroup(
        QStringLiteral("高级参数"), QStringLiteral("materialAdvancedGroup"), advancedPage, m_advancedLayout);
    basicGroup->setToolTip(QStringLiteral("常用材料参数，主要控制强度、刚度和自重等基本响应。"));
    advancedGroup->setToolTip(
        QStringLiteral("勾选仅展开高级参数，不影响已有参数的使用。\n"
                       "主要用于特定本构、开挖、动力、渗流或流变计算。"));

    m_formLayout->addRow(parameterContainer);

    // ===== 计算阶段设置：属于求解控制，在高级区内单独分组 =====
    const bool isDynamicMaterial = model->key == QStringLiteral("dam_dynamic");
    const bool supportsCalculationStage =
        isDynamicMaterial || isDamStaticMaterialModel(model->key);
    if (supportsCalculationStage)
    {
        m_advancedLayout->addRow(createSubsectionLabel(QStringLiteral("计算设置"), advancedPage));

        const QString stageUseText =
            isDynamicMaterial ? QStringLiteral("作为计算阶段材料（动力及地震波动）")
                              : QStringLiteral("作为计算阶段材料（浸润）");
        const QString stageUseTip =
            isDynamicMaterial
                ? QStringLiteral("仅用于土石坝动力及地震波动计算的材料对应关系，不直接施加到网格。")
                : QStringLiteral("仅用于土石坝浸润计算的材料对应关系，不直接施加到网格。");

        m_transitionOnlyCheck = new QCheckBox(stageUseText, advancedPage);
        m_transitionOnlyCheck->setChecked(entry->transitionOnly);
        m_transitionOnlyCheck->setToolTip(stageUseTip);
        // 复选项本身已经表达用途，不再额外占一列显示“材料用途”。
        m_advancedLayout->addRow(m_transitionOnlyCheck);

        m_transitionMatypeSpin = new QSpinBox(advancedPage);
        m_transitionMatypeSpin->setMinimumWidth(10);
        m_transitionMatypeSpin->setRange(FIRST_SUGGESTED_MATYPE, MAX_SOLVER_MATYPE);
        m_transitionMatypeSpin->setValue(isComputableMatype(entry->transitionMatype)
                                             ? entry->transitionMatype
                                             : FIRST_SUGGESTED_MATYPE);
        m_transitionMatypeSpin->setToolTip(QStringLiteral("指定该计算阶段材料对应的材料编号。"));
        m_transitionMatypeSpin->setEnabled(entry->transitionOnly);
        m_advancedLayout->addRow(QStringLiteral("阶段材料编号"), m_transitionMatypeSpin);

        connect(m_transitionOnlyCheck, &QCheckBox::stateChanged, this,
                &MaterialEntryWidget::onTransitionModeChanged);
        connect(m_transitionMatypeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                &MaterialEntryWidget::onTransitionMatypeChanged);
    }

    setAssignmentControlsEnabled(!(m_transitionOnlyCheck && entry->transitionOnly));

    bool hasAdvancedModelParameters = false;
    for (const QString& column : model->columns)
    {
        if (MaterialManager::isAutoNumberColumn(column))
            continue;
        if (model->key == QStringLiteral("dam_dynamic") &&
            column == QStringLiteral("静力计算编号"))
            continue;
        if (!isBasicMaterialColumn(model->key, column))
        {
            hasAdvancedModelParameters = true;
            break;
        }
    }
    if (hasAdvancedModelParameters)
        m_advancedLayout->addRow(createSubsectionLabel(QStringLiteral("模型参数"), advancedPage));

    // ===== 本构参数：常用参数进“基本参数”，其余进“高级参数” =====
    const auto addResidualMode = [this, entry](QFormLayout* layout, QWidget* parent,
                                                const QString& flag, const QString& title)
    {
        auto* option = new QComboBox(parent);
        option->addItem(QStringLiteral("沿用峰值"), QStringLiteral("1"));
        option->addItem(QStringLiteral("单独设置"), QStringLiteral("0"));
        if (!entry->properties.contains(flag))
            entry->properties.insert(flag, QStringLiteral("1"));
        const int index = option->findData(entry->properties.value(flag));
        option->setCurrentIndex(index >= 0 ? index : 0);
        option->setProperty("columnName", flag);
        option->setToolTip(QStringLiteral("单独设置时可输入0；沿用峰值时不修改已存储的残余值。"));
        // 信号在创建具体输入框后连接，先更新控件状态，再通知外部数据模型。
        layout->addRow(title, option);
        return option;
    };
    for (int columnIndex = 0; columnIndex < model->columns.size(); ++columnIndex)
    {
        const QString& column = model->columns.at(columnIndex);
        if (MaterialManager::isAutoNumberColumn(column))
            continue;

        // 静力－动力材料转换由具体土石坝计算工况设置，不在材料参数页重复编辑。
        if (model->key == QStringLiteral("dam_dynamic") &&
            column == QStringLiteral("静力计算编号"))
        {
            continue;
        }

        QFormLayout* targetLayout =
            isBasicMaterialColumn(model->key, column) ? m_basicLayout : m_advancedLayout;
        QWidget* targetParent =
            (targetLayout == m_basicLayout) ? basicPage : advancedPage;

        const QString displayLabel = columnIndex < model->displayColumns.size()
                                         ? model->displayColumns.at(columnIndex)
                                         : column;
        const QString unit = parameterUnit(model->key, column);
        const QString label = parameterDisplayName(displayLabel, unit);

        // 继承选项与对应残余值放在一起展示，避免原来一项出现两套开关。
        // 数据字段仍保留，以兼容已经保存的材料库/IMAT导入。
        if (model->key == QStringLiteral("dagangshan_isotropic") &&
            (column == QStringLiteral("残余摩擦系数沿用峰值") ||
             column == QStringLiteral("残余凝聚力沿用峰值")))
            continue;

        if (model->key == QStringLiteral("drucker_prager_rock") &&
            column == QStringLiteral("DP子类型"))
        {
            auto* optionBox = new QComboBox(targetParent);
            optionBox->addItem(QStringLiteral("内接圆"), QStringLiteral("1"));
            optionBox->addItem(QStringLiteral("DPAMC"), QStringLiteral("2"));
            optionBox->addItem(QStringLiteral("扩展锥"), QStringLiteral("3"));
            optionBox->addItem(QStringLiteral("压缩锥"), QStringLiteral("4"));
            optionBox->addItem(QStringLiteral("平均锥"), QStringLiteral("5"));
            optionBox->addItem(QStringLiteral("平面应变"), QStringLiteral("6"));
            int index = optionBox->findData(entry->properties.value(column));
            if (index < 0)
            {
                entry->properties.insert(column, QStringLiteral("1"));
                index = 0;
            }
            optionBox->setCurrentIndex(index);
            optionBox->setProperty("columnName", column);
            connect(optionBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
            targetLayout->addRow(QStringLiteral("屈服面"), optionBox);
            continue;
        }

        if (model->key == QStringLiteral("dam_dynamic") &&
            column == QStringLiteral("是否液化"))
        {
            auto* optionBox = new QComboBox(targetParent);
            optionBox->addItem(QStringLiteral("否"), QStringLiteral("0"));
            optionBox->addItem(QStringLiteral("是"), QStringLiteral("1"));
            const int optionIndex = optionBox->findData(entry->properties.value(column));
            if (optionIndex < 0)
                entry->properties.insert(
                    column, defaultMaterialPropertyValue(model->key, column));
            optionBox->setCurrentIndex(optionIndex >= 0 ? optionIndex : 0);
            optionBox->setProperty("columnName", column);
            optionBox->setToolTip(QStringLiteral("是否启用该材料的液化相关动力参数。"));
            connect(optionBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
            targetLayout->addRow(QStringLiteral("是否液化"), optionBox);
            continue;
        }

        if (model->key == QStringLiteral("huangjinping_underground_plant") &&
            column == QStringLiteral("岩类分区"))
        {
            auto* optionBox = new QComboBox(targetParent);
            optionBox->addItem(QStringLiteral("普通围岩"), QStringLiteral("0"));
            optionBox->addItem(QStringLiteral("半开挖区"), QStringLiteral("-1"));
            optionBox->addItem(QStringLiteral("开挖区"), QStringLiteral("-2"));
            optionBox->addItem(QStringLiteral("再生材料"), QStringLiteral("1"));
            optionBox->addItem(QStringLiteral("加固材料"), QStringLiteral("2"));
            const QString storedValue = entry->properties.value(column);
            int optionIndex = optionBox->findData(storedValue);
            if (optionIndex < 0 && !storedValue.trimmed().isEmpty())
            {
                optionBox->addItem(QStringLiteral("原工程设置（%1）").arg(storedValue),
                                   storedValue);
                optionIndex = optionBox->count() - 1;
            }
            optionBox->setCurrentIndex(optionIndex >= 0 ? optionIndex : 0);
            optionBox->setProperty("columnName", column);
            optionBox->setToolTip(QStringLiteral("用于区分普通围岩、半开挖、开挖、再生及加固材料。"));
            connect(optionBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
            targetLayout->addRow(QStringLiteral("开挖状态"), optionBox);
            continue;
        }

        if (!entry->properties.contains(column))
            entry->properties.insert(column, defaultMaterialPropertyValue(model->key, column));

        const bool directAngleColumn = isDirectFrictionAngleColumn(model->key, column);
        const bool coefficientColumn = isFrictionCoefficientColumn(column);
        const bool frictionPair = directAngleColumn || coefficientColumn;

        auto* valueEdit = new QLineEdit(entry->properties.value(column), targetParent);
        valueEdit->setMinimumWidth(10);
        auto* valueValidator = createMaterialNumberValidator(valueEdit);
        if (coefficientColumn)
            valueValidator->setBottom(0.0);
        if (directAngleColumn)
        {
            valueValidator->setBottom(0.0);
            valueValidator->setTop(MAX_FRICTION_ANGLE_DEG);
            valueValidator->setDecimals(6);
            valueValidator->setNotation(QDoubleValidator::StandardNotation);
        }
        valueEdit->setValidator(valueValidator);
        valueEdit->setProperty("columnName", column);
        const QString toolTip = parameterHelpText(model->key, column);
        valueEdit->setToolTip(toolTip);
        connect(valueEdit, &QLineEdit::textEdited, this, &MaterialEntryWidget::onValueEdited);
        m_valueEdits.insert(column, valueEdit);

        if (!frictionPair)
        {
            if (model->key == QStringLiteral("dagangshan_isotropic") &&
                column == QStringLiteral("残余凝聚力C‘（MPa）"))
            {
                QComboBox* option = addResidualMode(
                    targetLayout, targetParent, QStringLiteral("残余凝聚力沿用峰值"),
                    QStringLiteral("残余凝聚力"));
                const auto syncResidualCohesion = [valueEdit, option, entry]()
                {
                    const bool independent =
                        option->currentData().toString() == QStringLiteral("0");
                    const QString displayColumn = independent
                        ? QStringLiteral("残余凝聚力C‘（MPa）")
                        : QStringLiteral("凝聚力C‘（MPa）");
                    const QSignalBlocker blocker(valueEdit);
                    valueEdit->setText(entry->properties.value(displayColumn));
                    valueEdit->setEnabled(independent);
                };
                connect(option, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [syncResidualCohesion](int) { syncResidualCohesion(); });
                connect(option, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        &MaterialEntryWidget::onOptionValueChanged);
                syncResidualCohesion();
            }
            targetLayout->addRow(createParameterLabel(label, toolTip, targetParent),
                                 createParameterFieldWidget(valueEdit, unit, targetParent));
            continue;
        }

        // 两个摩擦参数始终同时显示。单选项表示当前输入值，另一项只读显示换算结果。
        if (directAngleColumn)
        {
            auto* coefficientEdit = new QLineEdit(targetParent);
            coefficientEdit->setMinimumWidth(10);
            auto* coefficientValidator = createMaterialNumberValidator(coefficientEdit);
            coefficientValidator->setBottom(0.0);
            coefficientEdit->setValidator(coefficientValidator);
            coefficientEdit->setProperty("frictionAngleColumn", column);
            coefficientEdit->setToolTip(frictionCoefficientHelpText());

            connect(coefficientEdit, &QLineEdit::textEdited, this,
                    [this, coefficientEdit](const QString& text)
                    {
                        if (m_isUpdating || !coefficientEdit)
                            return;

                        MaterialModel* linkedModel = nullptr;
                        MaterialEntry* linkedEntry = lookupEntry(&linkedModel);
                        if (!linkedEntry || !linkedModel)
                            return;

                        bool ok = false;
                        const double coefficient = QLocale::c().toDouble(text, &ok);
                        if (!ok || coefficient < 0.0)
                            return;

                        const QString angleColumn =
                            coefficientEdit->property("frictionAngleColumn").toString();
                        if (!isDirectFrictionAngleColumn(linkedModel->key, angleColumn))
                            return;

                        const double angleDeg = std::atan(coefficient) * 180.0 / PI_VALUE;
                        const QString angleText = formatLinkedNumber(angleDeg);
                        linkedEntry->properties.insert(angleColumn, angleText);

                        const auto angleIt = m_valueEdits.constFind(angleColumn);
                        if (angleIt != m_valueEdits.constEnd() && angleIt.value())
                        {
                            const QSignalBlocker blocker(angleIt.value());
                            angleIt.value()->setText(angleText);
                        }

                        m_materialManager->notifyEntryUpdated(m_entryId, false);
                    });

            auto* angleChoice = createFrictionChoice(QStringLiteral("内摩擦角 φ"),
                                                      QStringLiteral("按内摩擦角输入。"),
                                                      targetParent);
            auto* coefficientChoice = createFrictionChoice(QStringLiteral("内摩擦系数 f"),
                                                            QStringLiteral("按摩擦系数输入。"),
                                                            targetParent);
            auto* choiceGroup = new QButtonGroup(targetParent);
            choiceGroup->setExclusive(true);
            choiceGroup->addButton(angleChoice);
            choiceGroup->addButton(coefficientChoice);
            angleChoice->setChecked(true);

            targetLayout->addRow(angleChoice,
                                 createFrictionFieldWidget(valueEdit, QStringLiteral("°"),
                                                           targetParent));
            targetLayout->addRow(coefficientChoice,
                                 createFrictionFieldWidget(coefficientEdit,
                                                           QStringLiteral("无量纲"), targetParent));
            m_frictionCoefficientEdits.insert(column, coefficientEdit);

            valueEdit->setProperty("frictionCoefficientColumn", QString());
            setFrictionInputState(valueEdit, angleChoice, coefficientEdit, coefficientChoice,
                                  true);
            connect(angleChoice, &QRadioButton::toggled, this,
                    [valueEdit, angleChoice, coefficientEdit, coefficientChoice](bool checked)
                    {
                        if (!checked)
                            return;
                        setFrictionInputState(valueEdit, angleChoice, coefficientEdit,
                                              coefficientChoice, true);
                    });
            connect(coefficientChoice, &QRadioButton::toggled, this,
                    [valueEdit, angleChoice, coefficientEdit, coefficientChoice](bool checked)
                    {
                        if (!checked)
                            return;
                        setFrictionInputState(valueEdit, angleChoice, coefficientEdit,
                                              coefficientChoice, false);
                    });
            continue;
        }

        // 原模型保存摩擦系数：界面仍优先按工程上更直观的内摩擦角输入，
        // 换算后的系数继续写回原字段，求解器接口保持不变。
        auto* angleEdit = new QLineEdit(targetParent);
        angleEdit->setMinimumWidth(10);
        auto* angleValidator =
            new QDoubleValidator(0.0, MAX_FRICTION_ANGLE_DEG, 6, angleEdit);
        angleValidator->setNotation(QDoubleValidator::StandardNotation);
        QLocale angleLocale = QLocale::c();
        angleLocale.setNumberOptions(QLocale::RejectGroupSeparator);
        angleValidator->setLocale(angleLocale);
        angleEdit->setValidator(angleValidator);
        angleEdit->setProperty("frictionCoefficientColumn", column);
        angleEdit->setToolTip(frictionAngleHelpText(column));

        const bool residualFriction = isResidualFrictionCoefficientColumn(column);
        QComboBox* residualMode = nullptr;
        if (model->key == QStringLiteral("dagangshan_isotropic") && residualFriction)
        {
            residualMode = addResidualMode(
                targetLayout, targetParent, QStringLiteral("残余摩擦系数沿用峰值"),
                QStringLiteral("残余摩擦参数"));
        }

        connect(angleEdit, &QLineEdit::textEdited, this,
                [this, angleEdit](const QString& text)
                {
                    if (m_isUpdating || !angleEdit)
                        return;

                    MaterialModel* linkedModel = nullptr;
                    MaterialEntry* linkedEntry = lookupEntry(&linkedModel);
                    if (!linkedEntry || !linkedModel)
                        return;

                    const QString coefficientColumn =
                        angleEdit->property("frictionCoefficientColumn").toString();
                    if (!isFrictionCoefficientColumn(coefficientColumn))
                        return;

                    bool ok = false;
                    const double angleDeg = QLocale::c().toDouble(text, &ok);
                    if (!ok || angleDeg < 0.0 || angleDeg >= 90.0)
                        return;

                    const double coefficient = std::tan(angleDeg * PI_VALUE / 180.0);
                    const QString coefficientText = QString::number(coefficient, 'g', 12);
                    linkedEntry->properties.insert(coefficientColumn, coefficientText);

                    const auto coefficientIt = m_valueEdits.constFind(coefficientColumn);
                    if (coefficientIt != m_valueEdits.constEnd() && coefficientIt.value())
                    {
                        const QSignalBlocker blocker(coefficientIt.value());
                        coefficientIt.value()->setText(coefficientText);
                    }

                    if (coefficientColumn == QStringLiteral("抗剪断强度内摩擦系数"))
                        refreshFrictionAngleFields(linkedEntry);

                    m_materialManager->notifyEntryUpdated(m_entryId, false);
                });

        auto* angleChoice = createFrictionChoice(
            residualFriction ? QStringLiteral("残余内摩擦角 φr")
                             : QStringLiteral("内摩擦角 φ"),
            residualFriction ? QStringLiteral("按残余内摩擦角输入。")
                             : QStringLiteral("按内摩擦角输入。"),
            targetParent);
        auto* coefficientChoice = createFrictionChoice(
            residualFriction ? QStringLiteral("残余摩擦系数 fr")
                             : QStringLiteral("内摩擦系数 f"),
            residualFriction ? QStringLiteral("按残余摩擦系数输入。")
                             : QStringLiteral("按摩擦系数输入。"),
            targetParent);
        auto* choiceGroup = new QButtonGroup(targetParent);
        choiceGroup->setExclusive(true);
        choiceGroup->addButton(angleChoice);
        choiceGroup->addButton(coefficientChoice);
        angleChoice->setChecked(true);

        targetLayout->addRow(angleChoice,
                             createFrictionFieldWidget(angleEdit, QStringLiteral("°"),
                                                       targetParent));
        targetLayout->addRow(coefficientChoice,
                             createFrictionFieldWidget(valueEdit, QStringLiteral("无量纲"),
                                                       targetParent));
        m_frictionAngleEdits.insert(column, angleEdit);

        const auto applyInputState =
            [angleEdit, angleChoice, valueEdit, coefficientChoice, residualMode]()
            {
                const bool inherited = residualMode &&
                    residualMode->currentData().toString() == QStringLiteral("1");
                setFrictionInputState(angleEdit, angleChoice, valueEdit, coefficientChoice,
                                      angleChoice->isChecked(), !inherited);
            };

        connect(angleChoice, &QRadioButton::toggled, this,
                [applyInputState](bool checked)
                {
                    if (checked)
                        applyInputState();
                });
        connect(coefficientChoice, &QRadioButton::toggled, this,
                [applyInputState](bool checked)
                {
                    if (checked)
                        applyInputState();
                });

        if (residualMode)
        {
            connect(residualMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [applyInputState](int) { applyInputState(); });
            connect(residualMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    &MaterialEntryWidget::onOptionValueChanged);
        }

        applyInputState();
    }

    refreshFrictionAngleFields(entry);
    refreshFrictionCoefficientFields(model, entry);

    updateConditionalParameterVisibility(model->key, entry, m_advancedLayout, m_valueEdits);

    // ===== 附加渗流 / 流变模型属于专业扩展，统一放在“高级参数” =====
    m_advancedLayout->addRow(createSubsectionLabel(QStringLiteral("附加模型"), advancedPage));

    m_seepageCombo = new CustomComboBox(advancedPage);
    m_seepageCombo->setMinimumWidth(10);
    m_seepageCombo->setToolTip(QStringLiteral("选择附加渗流模型；选中后可调整下方渗流参数。"));
    m_seepageCombo->addItem(QStringLiteral("无"), QString());
    const auto& seepageModels = m_materialManager->seepageModels();
    for (const auto& seepageModel : seepageModels)
    {
        if (seepageModel.key == QStringLiteral("jk_corewall_steady_seepage") ||
            seepageModel.key == QStringLiteral("jk_corewall_anisotropic_seepage"))
        {
            m_seepageCombo->addItem(seepageModel.displayName, seepageModel.key);
        }
    }
    const int seepageIndex = m_seepageCombo->findData(entry->seepageModelKey);
    m_seepageCombo->setCurrentIndex(seepageIndex >= 0 ? seepageIndex : 0);
    connect(m_seepageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onSeepageModelChanged);
    m_advancedLayout->addRow(QStringLiteral("渗流模型"), m_seepageCombo);

    m_seepageFieldsWidget = new QWidget(advancedPage);
    m_seepageFieldsLayout = new QFormLayout(m_seepageFieldsWidget);
    configureParameterFormLayout(m_seepageFieldsLayout);
    m_seepageFieldsLayout->setContentsMargins(16, 2, 0, 6);
    m_advancedLayout->addRow(m_seepageFieldsWidget);
    rebuildSeepageFields(entry);

    m_creepCombo = new CustomComboBox(advancedPage);
    m_creepCombo->setMinimumWidth(10);
    m_creepCombo->setToolTip(QStringLiteral("选择附加流变模型；选中后可调整下方流变参数。"));
    m_creepCombo->addItem(QStringLiteral("无"), QString());
    for (const auto& creepModel : seepageModels)
    {
        if (creepModel.key != QStringLiteral("jk_corewall_steady_seepage") &&
            creepModel.key != QStringLiteral("jk_corewall_anisotropic_seepage") &&
            creepModel.key != QStringLiteral("xiyuan_creep"))
        {
            m_creepCombo->addItem(creepModel.displayName, creepModel.key);
        }
    }
    const int creepIndex = m_creepCombo->findData(entry->creepModelKey);
    m_creepCombo->setCurrentIndex(creepIndex >= 0 ? creepIndex : 0);
    connect(m_creepCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MaterialEntryWidget::onCreepModelChanged);
    m_advancedLayout->addRow(QStringLiteral("流变模型"), m_creepCombo);

    m_creepFieldsWidget = new QWidget(advancedPage);
    m_creepFieldsLayout = new QFormLayout(m_creepFieldsWidget);
    configureParameterFormLayout(m_creepFieldsLayout);
    m_creepFieldsLayout->setContentsMargins(16, 2, 0, 6);
    m_advancedLayout->addRow(m_creepFieldsWidget);
    rebuildCreepFields(entry);

    // ===== 初始化选中 MATYPE =====
    int initialMatype = m_materialManager->selectedMatypeForEntry(m_entryId);
    if (initialMatype < 0 && !appliedList.isEmpty())
        initialMatype = appliedList.first();

    int comboIndex = (initialMatype >= 0) ? m_matypeCombo->findData(initialMatype) : -1;
    m_matypeCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    m_materialManager->setSelectedMatypeForEntry(m_entryId, initialMatype);

    m_previewMatype = -1;
    m_preDeleteMatype = -1;
    m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    m_isUpdating = false;
    updateAppliedListColors();
    // 这里只重建控件，不主动刷新网格高亮；高亮仍由 beforeLoad()/用户交互触发。
}

void MaterialEntryWidget::clearForm()
{
    while (m_formLayout->rowCount() > 0)
    {
        QLayoutItem* labelItem = m_formLayout->itemAt(0, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = m_formLayout->itemAt(0, QFormLayout::FieldRole);
        if (labelItem)
            delete labelItem->widget();
        if (fieldItem)
            delete fieldItem->widget();
        m_formLayout->removeRow(0);
    }

    m_nameEdit = nullptr;
    m_modelCombo = nullptr;
    m_basicLayout = nullptr;
    m_advancedLayout = nullptr;
    m_valueEdits.clear();
    m_frictionAngleEdits.clear();
    m_frictionCoefficientEdits.clear();
    m_seepageCombo = nullptr;
    m_seepageFieldsWidget = nullptr;
    m_seepageFieldsLayout = nullptr;
    m_seepageValueEdits.clear();
    m_creepCombo = nullptr;
    m_creepFieldsWidget = nullptr;
    m_creepFieldsLayout = nullptr;
    m_creepValueEdits.clear();

    m_matypeCombo = nullptr;
    m_viewPickCheck = nullptr;
    m_applyButton = nullptr;

    m_appliedMatypesList = nullptr;
    m_removeButton = nullptr;
    m_transitionOnlyCheck = nullptr;
    m_transitionMatypeSpin = nullptr;
    m_assignmentWidget = nullptr;

    m_lastHoveredIndex = -1;
    m_matypeDropdownOpen = false;
    m_matypeOptionsLoaded = false;
}

void MaterialEntryWidget::refreshFrictionAngleFields(const MaterialEntry* entry)
{
    if (!entry)
        return;

    bool peakOk = false;
    const double peakCoefficient =
        entry->properties.value(QStringLiteral("抗剪断强度内摩擦系数")).toDouble(&peakOk);
    const double peakAngleDeg = peakOk
                                    ? std::atan(qMax(0.0, peakCoefficient)) * 180.0 / PI_VALUE
                                    : 0.0;

    for (auto it = m_frictionAngleEdits.constBegin(); it != m_frictionAngleEdits.constEnd(); ++it)
    {
        QLineEdit* angleEdit = it.value();
        if (!angleEdit)
            continue;

        const QString coefficientColumn = it.key();
        bool ok = false;
        const double coefficient = entry->properties.value(coefficientColumn).toDouble(&ok);
        const QSignalBlocker blocker(angleEdit);

        if (!ok)
        {
            angleEdit->clear();
            continue;
        }

        if (isResidualFrictionCoefficientColumn(coefficientColumn) &&
            entry->properties.value(QStringLiteral("残余摩擦系数沿用峰值"),
                                    QStringLiteral("1")) != QStringLiteral("0"))
        {
            angleEdit->setText(peakOk ? formatLinkedNumber(peakAngleDeg) : QString());
            angleEdit->setPlaceholderText(QStringLiteral("沿用峰值"));

            const auto coefficientIt = m_valueEdits.constFind(coefficientColumn);
            if (coefficientIt != m_valueEdits.constEnd() && coefficientIt.value())
            {
                const QSignalBlocker coefficientBlocker(coefficientIt.value());
                coefficientIt.value()->setText(peakOk ? formatLinkedNumber(peakCoefficient)
                                                       : QString());
            }
            continue;
        }

        // 从“沿用峰值”切换为“单独设置”时恢复原来存储的值，包括真实的0。
        if (isResidualFrictionCoefficientColumn(coefficientColumn))
        {
            const auto coefficientIt = m_valueEdits.constFind(coefficientColumn);
            if (coefficientIt != m_valueEdits.constEnd() && coefficientIt.value())
            {
                const QSignalBlocker coefficientBlocker(coefficientIt.value());
                coefficientIt.value()->setText(formatLinkedNumber(coefficient));
            }
        }

        const double angleDeg = std::atan(qMax(0.0, coefficient)) * 180.0 / PI_VALUE;
        angleEdit->setText(formatLinkedNumber(angleDeg));
        angleEdit->setPlaceholderText(QString());
    }
}

void MaterialEntryWidget::refreshFrictionCoefficientFields(const MaterialModel* model,
                                                            const MaterialEntry* entry)
{
    if (!model || !entry)
        return;

    for (auto it = m_frictionCoefficientEdits.constBegin();
         it != m_frictionCoefficientEdits.constEnd(); ++it)
    {
        QLineEdit* coefficientEdit = it.value();
        if (!coefficientEdit)
            continue;

        const QString angleColumn = it.key();
        if (!isDirectFrictionAngleColumn(model->key, angleColumn))
            continue;

        bool ok = false;
        const double angleDeg = entry->properties.value(angleColumn).toDouble(&ok);
        const QSignalBlocker blocker(coefficientEdit);
        if (!ok || angleDeg < 0.0 || angleDeg >= 90.0)
        {
            coefficientEdit->clear();
            continue;
        }

        const double coefficient = std::tan(angleDeg * PI_VALUE / 180.0);
        coefficientEdit->setText(QString::number(coefficient, 'g', 12));
    }
}

MaterialEntry* MaterialEntryWidget::lookupEntry(MaterialModel** model) const
{
    return m_materialManager->findEntry(m_entryId, model);
}

void MaterialEntryWidget::ensurePickInteractionReady()
{
    if (!m_materialManager || m_pickInteractionReady)
        return;

    // 材料页直接点选可见网格域：只选择已有 MATYPE，不立即修改网格分组或材料关联。
    // 仍由“施加到网格”按钮确认关联，避免点击时误修改求解器数据。
    auto& picker = PickManager::instance();
    picker.activate(getPageType(), m_uuid, PickMode::Domain);
    picker.setBoxSelectEnabled(getPageType(), m_uuid, false);
    picker.setPreferredDomainCategory(getPageType(), m_uuid, QStringLiteral("MATYPE"));
    picker.setClickPickEnabled(getPageType(), m_uuid, m_viewPickEnabled);

    connect(&picker, &PickManager::domainSelected, this,
            &MaterialEntryWidget::onDomainPicked, Qt::UniqueConnection);

    m_pickInteractionReady = true;
}

void MaterialEntryWidget::refreshMatypeHighlightSafe()
{
    ensurePickInteractionReady();
    if (m_materialManager)
        m_materialManager->refreshMatypeHighlight(m_entryId);
}

void MaterialEntryWidget::appendMatypeOption(int matype)
{
    if (!m_matypeCombo || !isComputableMatype(matype))
        return;
    if (m_matypeCombo->findData(matype) >= 0)
        return;
    m_matypeCombo->addItem(matypeDisplayText(matype), matype);
}

void MaterialEntryWidget::ensureMatypeOptionsLoaded()
{
    if (!m_matypeCombo || m_matypeOptionsLoaded || !m_materialManager)
        return;

    const QVariant currentValue = m_matypeCombo->currentData();
    const QSignalBlocker blocker(m_matypeCombo);
    QList<int> matypes = m_materialManager->availableMatypes().values();
    std::sort(matypes.begin(), matypes.end());
    for (int value : matypes)
        appendMatypeOption(value);

    int currentIndex = currentValue.isValid() ? m_matypeCombo->findData(currentValue) : 0;
    if (currentIndex < 0)
        currentIndex = 0;
    m_matypeCombo->setCurrentIndex(currentIndex);
    m_matypeOptionsLoaded = true;
}

void MaterialEntryWidget::refreshAppliedMatypes(const QSet<int>& appliedMatypes, int selectedMatype)
{
    if (!m_appliedMatypesList || !m_matypeCombo)
        return;

    m_isUpdating = true;

    m_appliedMatypesList->clear();
    QList<int> appliedList = appliedMatypes.values();
    std::sort(appliedList.begin(), appliedList.end());
    for (int value : appliedList)
    {
        auto* item = new QListWidgetItem(matypeDisplayText(value), m_appliedMatypesList);
        item->setData(Qt::UserRole, value);
    }
    m_appliedMatypesList->setFixedHeight(
        appliedList.isEmpty() ? 42 : qMin(78, 30 + appliedList.size() * 24));

    int comboIndex = (selectedMatype >= 0) ? m_matypeCombo->findData(selectedMatype) : 0;
    if (comboIndex < 0)
        comboIndex = 0;
    m_matypeCombo->setCurrentIndex(comboIndex);

    // 数据刷新后，预删除/预览都清空（避免旧状态影响）
    m_preDeleteMatype = -1;
    m_previewMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
    m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);

    if (m_removeButton)
        m_removeButton->setEnabled(false);

    m_isUpdating = false;

    updateAppliedListColors();
}

void MaterialEntryWidget::onMatypeChanged(int)
{
    if (m_isUpdating || !m_matypeCombo)
        return;

    // 下拉关闭时某些实现会额外发一次
    // currentIndexChanged（并非真实提交），直接忽略。
    if (m_ignoreNextMatypeChanged)
    {
        updateAppliedListColors();
        return;
    }

    // 下拉弹层可见时，currentIndexChanged 可能由“悬浮预览”触发，
    // 不能把该值写成 Selected（否则关闭下拉后会残留预选色/选中色）。
    if (m_matypeDropdownOpen || (m_matypeCombo->view() && m_matypeCombo->view()->isVisible()))
    {
        updateAppliedListColors();
        return;
    }

    const QVariant data = m_matypeCombo->currentData();
    const int matype = data.isValid() ? data.toInt() : -1;

    // 仅同步材料页下拉框当前值，不触发三维高亮刷新。
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
}

void MaterialEntryWidget::onMatypeActivated(int)
{
    if (m_isUpdating || !m_matypeCombo)
        return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    // 选中态：写入 Selected
    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);

    // 改选中时通常清掉预删除（更符合直觉）
    m_preDeleteMatype = -1;
    if (m_appliedMatypesList)
        m_appliedMatypesList->clearSelection();
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onApplyMatypeClicked()
{
    if (!m_matypeCombo)
        return;

    QVariant data = m_matypeCombo->currentData();
    int matype = data.isValid() ? data.toInt() : -1;

    m_materialManager->applyMatypeToEntry(m_entryId, matype);
}

void MaterialEntryWidget::onAppliedMatypeSelected(const QString& text)
{
    Q_UNUSED(text);
    if (m_isUpdating)
        return;

    auto* item = m_appliedMatypesList ? m_appliedMatypesList->currentItem() : nullptr;
    if (m_removeButton)
        m_removeButton->setEnabled(item != nullptr &&
                                   (!m_assignmentWidget || m_assignmentWidget->isEnabled()));
    if (!item)
    {
        // 清选择 => 取消预删除
        m_preDeleteMatype = -1;
        m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
        updateAppliedListColors();
        refreshMatypeHighlightSafe();
        return;
    }
    const int matype = item->data(Qt::UserRole).toInt();
    if (!isComputableMatype(matype))
        return;

    // ★ 只设置“预删除”，不改变 Selected
    m_preDeleteMatype = matype;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, matype);

    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onTransitionModeChanged(int state)
{
    if (m_isUpdating || m_transitionChangeInProgress)
        return;
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck || !m_transitionMatypeSpin)
        return;
    QScopedValueRollback<bool> guard(m_transitionChangeInProgress, true);
    const bool enabled = (state == Qt::Checked);
    if (!enabled)
    {
        entry->transitionOnly = false;
        entry->transitionMatype = m_transitionMatypeSpin->value();
        m_transitionMatypeSpin->setEnabled(false);
        setAssignmentControlsEnabled(true);
        m_materialManager->notifyEntryUpdated(entry->id, false);
        return;
    }
    QString error;
    const int matype = m_transitionMatypeSpin->value();
    if (!m_materialManager->validateTransitionMatype(m_entryId, matype, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        QSignalBlocker blocker(m_transitionOnlyCheck);
        m_transitionOnlyCheck->setCheckState(Qt::Unchecked);
        m_transitionOnlyCheck->setEnabled(true);
        m_transitionMatypeSpin->setEnabled(false);
        entry->transitionOnly = false;
        setAssignmentControlsEnabled(true);
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = matype;
    m_transitionOnlyCheck->setEnabled(true);
    m_transitionMatypeSpin->setEnabled(true);
    setAssignmentControlsEnabled(false);
    m_materialManager->notifyEntryUpdated(entry->id, false);
}

void MaterialEntryWidget::onTransitionMatypeChanged(int value)
{
    if (m_isUpdating || m_transitionChangeInProgress)
        return;
    MaterialEntry* entry = lookupEntry();
    if (!entry || !m_transitionOnlyCheck || !m_transitionMatypeSpin)
        return;
    if (!m_transitionOnlyCheck->isChecked())
    {
        m_transitionOnlyCheck->setEnabled(true);
        return;
    }
    QScopedValueRollback<bool> guard(m_transitionChangeInProgress, true);
    const int previousMatype = entry->transitionMatype;
    QString error;
    if (!m_materialManager->validateTransitionMatype(m_entryId, value, &error))
    {
        QMessageBox::warning(this, QStringLiteral("提示"), error);
        entry->transitionMatype = previousMatype;
        QSignalBlocker blocker(m_transitionMatypeSpin);
        m_transitionMatypeSpin->setValue(isComputableMatype(previousMatype)
                                             ? previousMatype
                                             : FIRST_SUGGESTED_MATYPE);
        return;
    }
    entry->transitionOnly = true;
    entry->transitionMatype = value;
    m_materialManager->notifyEntryUpdated(entry->id, false);
}

void MaterialEntryWidget::onMatypeHovered(int index)
{
    if (!m_matypeCombo)
        return;

    // UI：下拉项背景淡蓝（你原逻辑保留）
    if (m_lastHoveredIndex >= 0)
    {
        m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(), Qt::BackgroundRole);
    }
    if (index >= 0)
    {
        const QColor hoverColor =
            blendColor(m_matypeCombo->palette().color(QPalette::Base),
                       m_matypeCombo->palette().color(QPalette::Highlight), 0.16);
        m_matypeCombo->setItemData(index, hoverColor, Qt::BackgroundRole);
        m_lastHoveredIndex = index;
    }
    else
    {
        m_lastHoveredIndex = -1;
    }

    // 业务：Preview matype
    QVariant data = m_matypeCombo->itemData(index);
    int matype = data.isValid() ? data.toInt() : -1;

    m_previewMatype = matype;
    m_materialManager->setPreviewMatypeForEntry(m_entryId, matype);
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onDropdownClosed()
{
    m_matypeDropdownOpen = false;
    // 注意：CustomComboBox 在 hidePopup() 里先 emit dropdownClosed，再调用 QComboBox::hidePopup()。
    // 因此这里必须把清理/恢复放到下一轮事件循环，避免被基类后续的 index 提交覆盖。
    m_ignoreNextMatypeChanged = true;
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           if (m_matypeCombo && m_lastHoveredIndex >= 0)
                           {
                               m_matypeCombo->setItemData(m_lastHoveredIndex, QVariant(),
                                                          Qt::BackgroundRole);
                               m_lastHoveredIndex = -1;
                           }

                           // 下拉关闭：无条件取消 Preview，避免悬浮高亮残留。
                           m_previewMatype = -1;
                           if (m_materialManager)
                               m_materialManager->setPreviewMatypeForEntry(m_entryId, -1);

                           updateAppliedListColors();

                           // 卸载过程中只做状态清理，不回刷高亮，避免离开页面后颜色被重新点亮
                           if (!m_beforeUnloadHandled)
                           {
                               // 下拉关闭后取消悬浮预览，并恢复当前选中域及已施加域的显示状态。
                               refreshMatypeHighlightSafe();
                           }

                           m_ignoreNextMatypeChanged = false;
                       });
}

void MaterialEntryWidget::onRemoveMatypeClicked()
{
    if (!m_appliedMatypesList)
        return;

    auto* item = m_appliedMatypesList->currentItem();
    if (!item)
        return;

    bool ok = false;
    int matype = item->data(Qt::UserRole).toInt(&ok);
    if (!ok)
        return;

    m_materialManager->removeMatypeFromEntry(m_entryId, matype);

    // 移除后清掉预删除
    m_preDeleteMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);
    updateAppliedListColors();
    refreshMatypeHighlightSafe();
}

void MaterialEntryWidget::onDomainPicked(const QString& domain, const QString& uuid, const DomainKey& key)
{
    if (!m_materialManager || !m_matypeCombo || m_beforeUnloadHandled || !m_viewPickEnabled)
        return;
    if (domain != getPageType() || uuid != m_uuid)
        return;
    if (m_assignmentWidget && !m_assignmentWidget->isEnabled())
        return;
    if (key.category.compare(QStringLiteral("MATYPE"), Qt::CaseInsensitive) != 0 ||
        !isComputableMatype(key.id))
        return;

    // 选项采用延迟加载，必须先补齐，否则用户点击未曾展开下拉框的材料组会被丢弃。
    ensureMatypeOptionsLoaded();
    const int matype = key.id;
    const int comboIndex = m_matypeCombo->findData(matype);
    if (comboIndex < 0)
        return;

    if (m_appliedMatypesList)
        m_appliedMatypesList->clearSelection();
    m_preDeleteMatype = -1;
    m_materialManager->setPreDeleteMatypeForEntry(m_entryId, -1);

    {
        QSignalBlocker blocker(m_matypeCombo);
        m_matypeCombo->setCurrentIndex(comboIndex);
    }

    m_materialManager->setSelectedMatypeForEntry(m_entryId, matype);
    refreshMatypeHighlightSafe();
    updateAppliedListColors();
}

void MaterialEntryWidget::updateAppliedListColors()
{
    if (!m_appliedMatypesList)
        return;

    // 预删除色基于当前主题底色轻度混合红色，避免暗色主题出现刺眼亮块。
    const QColor preDeleteColor =
        blendColor(m_appliedMatypesList->palette().color(QPalette::Base), QColor(210, 72, 72),
                   0.18);

    for (int i = 0; i < m_appliedMatypesList->count(); ++i)
    {
        auto* it = m_appliedMatypesList->item(i);
        bool ok = false;
        int v = it->data(Qt::UserRole).toInt(&ok);
        if (!ok)
        {
            it->setBackground(QBrush());
            continue;
        }

        if (m_preDeleteMatype >= 0 && v == m_preDeleteMatype)
        {
            it->setBackground(preDeleteColor);
        }
        else
        {
            it->setBackground(QBrush());
        }
    }
}

void MaterialEntryWidget::setAssignmentControlsEnabled(bool enabled)
{
    if (m_assignmentWidget)
    {
        m_assignmentWidget->setEnabled(enabled);
    }
    else
    {
        if (m_matypeCombo)
            m_matypeCombo->setEnabled(enabled);
        if (m_applyButton)
            m_applyButton->setEnabled(enabled);
        if (m_appliedMatypesList)
            m_appliedMatypesList->setEnabled(enabled);
    }

    if (m_removeButton)
    {
        const bool hasSelection = m_appliedMatypesList && m_appliedMatypesList->currentItem();
        m_removeButton->setEnabled(enabled && hasSelection);
    }
}
