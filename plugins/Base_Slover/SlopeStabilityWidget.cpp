#include "SlopeStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QStringList>
#include <QVBoxLayout>

SlopeStabilityWidget::SlopeStabilityWidget(QWidget* parent)
    : BasePage("SlopeStabilityAnalysis")
{
    Q_UNUSED(parent);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* titleLabel = new QLabel("边坡稳定分析", this);
    root->addWidget(titleLabel);

    auto* sectionTitleButton = new QPushButton("功能设置", this);
    sectionTitleButton->setEnabled(false);
    root->addWidget(sectionTitleButton);

    auto* group = new QGroupBox("ISLOP.DAT 参数", this);
    auto* form = new QFormLayout(group);

    auto addLineEdit = [this, form](int lineNo, const QString& labelText)
    {
        auto* edit = new QLineEdit(defaultValueByLine(lineNo), this);
        m_lineEditors[lineNo] = edit;
        form->addRow(labelText, edit);
    };
    auto addCombo = [this, form](int lineNo, const QString& labelText, const QList<QPair<QString, QString>>& items)
    {
        auto* combo = new QComboBox(this);
        for (const auto& item : items)
            combo->addItem(item.second, item.first);
        const int idx = combo->findData(defaultValueByLine(lineNo));
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        m_lineEditors[lineNo] = combo;
        form->addRow(labelText, combo);
    };

    addLineEdit(1, "1. 计算默认文件目录");
    addLineEdit(2, "2. 材料集文件");
    addCombo(3, "3. 求解方法", {{"0", "0 荷载增量法"},
                                {"1", "1 强度储备法(降C)"},
                                {"2", "2 强度储备法(降F)"},
                                {"3", "3 强度储备法(同时降C/F)"},
                                {"4", "4 强度储备法(同时降E/C/F)"}});
    addLineEdit(4, "4. 三方向重力加速度");
    addCombo(5, "5. 弹塑性分析迭代方法", {{"1", "1 初应变法"}, {"2", "2 初应力法"}, {"3", "3 变刚度法"}});
    addLineEdit(6, "6. 最大迭代次数");
    addLineEdit(7, "7. 迭代误差阈值");
    addCombo(8, "8. 施加荷载方式", {{"1", "1 仅施加重力"}, {"2", "2 按 IIWL.DAT 施加"}});
    addLineEdit(9, "9. 荷载文件名");
    addLineEdit(10, "10. 备用");
    addLineEdit(11, "11. 备用");
    addLineEdit(12, "12. 特征节点集文件");
    addLineEdit(13, "13. 特征单元集文件");
    addLineEdit(14, "14. 特征时刻文件");
    addCombo(15, "15. 系统阻尼/分材料阻尼", {{"0", "0 系统阻尼"}, {"1", "1 分材料阻尼"}});
    addCombo(16, "16. 瑞雷阻尼", {{"0", "0 非瑞雷"}, {"1", "1 瑞雷"}});
    addLineEdit(17, "17. 阻尼比或质量阻尼系数 fm");
    addLineEdit(18, "18. 刚度阻尼系数 fk");
    addCombo(19, "19. 计算类型", {{"1", "1 仅静力"}, {"2", "2 仅动力"}, {"3", "3 静力+动力"}});
    addLineEdit(20, "20. 备用");

    root->addWidget(group);

    auto* reserveGroup = new QGroupBox("强度储备系数", this);
    auto* reserveLayout = new QVBoxLayout(reserveGroup);
    m_strengthReserveTable = new QTableWidget(this);
    initStrengthReserveTable();
    reserveLayout->addWidget(m_strengthReserveTable);
    root->addWidget(reserveGroup);

    auto* dynamicGroup = new QGroupBox("动力计算参数定义", this);
    auto* dynamicForm = new QFormLayout(dynamicGroup);
    m_caseDirEdit = new QLineEdit("F:\\FEMCLASS\\Z08_SLOP3D\\CASE01\\", this);
    m_dynamicArgsEdit = new QLineEdit("0.010000 0.000000 30.000000 1 1", this);
    m_accXEdit = new QLineEdit("ACCEX.DAT", this);
    m_accYEdit = new QLineEdit("ACCEY.DAT", this);
    m_accZEdit = new QLineEdit("ACCEZ.DAT", this);
    dynamicForm->addRow("初始目录", m_caseDirEdit);
    dynamicForm->addRow("DT TSTA TEND IFMODE IFWAVE", m_dynamicArgsEdit);
    dynamicForm->addRow("X方向地震波文件", m_accXEdit);
    dynamicForm->addRow("Y方向地震波文件", m_accYEdit);
    dynamicForm->addRow("Z方向地震波文件", m_accZEdit);
    root->addWidget(dynamicGroup);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* btnCompute = new QPushButton("计算", this);
    connect(btnCompute, &QPushButton::clicked, this, &SlopeStabilityWidget::onComputeClicked);
    btnRow->addWidget(btnCompute);
    root->addLayout(btnRow);
    root->addStretch();
}

void SlopeStabilityWidget::beforeLoad()
{
}

void SlopeStabilityWidget::beforeUnload()
{
}

QString SlopeStabilityWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString SlopeStabilityWidget::lineValue(int lineNo) const
{
    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
        return combo->currentData().toString();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    return defaultValueByLine(lineNo);
}

void SlopeStabilityWidget::setLineValue(int lineNo, const QString& value)
{
    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
    {
        const int idx = combo->findData(value.trimmed());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        edit->setText(value);
}

QString SlopeStabilityWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {"F:\\FEMCLASS\\Z08_SLOP3D\\PREP\\", "MACB.DAT", "3",
                                         "0.0   -9.81   0.0", "1", "500", "0.0001", "1", "IIWL.DAT",
                                         "--------------", "--------------", "JDTRACE.DAT", "KETRACE.DAT",
                                         "TDTRACE.DAT", "0", "0", "0.05", "0.0", "1", "------------"};
    return defaults.value(lineNo - 1);
}

void SlopeStabilityWidget::initStrengthReserveTable()
{
    if (!m_strengthReserveTable)
        return;

    m_strengthReserveTable->setColumnCount(2);
    m_strengthReserveTable->setHorizontalHeaderLabels({"强储倍数", "是否输出"});
    m_strengthReserveTable->setRowCount(12);

    const QStringList factors = {"1.0000", "1.2000", "1.4000", "1.5000", "1.5500", "1.6000",
                                 "1.6200", "1.6400", "1.6600", "1.6800", "1.7000", "1.7200"};
    const QStringList ifpr = {"1", "1", "1", "1", "1", "1", "1", "1", "1", "1", "1", "0"};
    for (int row = 0; row < factors.size(); ++row)
    {
        m_strengthReserveTable->setItem(row, 0, new QTableWidgetItem(factors.at(row)));
        m_strengthReserveTable->setItem(row, 1, new QTableWidgetItem(ifpr.at(row)));
    }
    m_strengthReserveTable->horizontalHeader()->setStretchLastSection(true);
}

bool SlopeStabilityWidget::exportIslopDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString projectDirAbs = QDir(projectDir).absolutePath();

    QDir wd(projectDirAbs);
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    QFile file(wd.filePath("ISLOP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    for (int lineNo = 1; lineNo <= 20; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = ensureTrailingSep(wd.filePath("PREP"));
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    int nsrf = 0;
    for (int row = 0; row < m_strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = m_strengthReserveTable->item(row, 0);
        if (factorItem && !factorItem->text().trimmed().isEmpty())
            ++nsrf;
    }
    os << nsrf << "    NSRF\n";
    for (int row = 0; row < m_strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = m_strengthReserveTable->item(row, 0);
        if (!factorItem)
            continue;
        const QString factor = factorItem->text().trimmed();
        if (factor.isEmpty())
            continue;
        const auto* ifprItem = m_strengthReserveTable->item(row, 1);
        const QString ifpr = ifprItem ? ifprItem->text().trimmed() : "1";
        os << factor << " " << (ifpr.isEmpty() ? "1" : ifpr) << "\n";
    }

    os << quoteValue(ensureTrailingSep(m_caseDirEdit->text().trimmed())) << "    CASE_DIR\n";
    os << m_dynamicArgsEdit->text().trimmed() << "    DT TSTA TEND IFMODE IFWAVE\n";
    os << quoteValue(m_accXEdit->text()) << "  " << quoteValue(m_accYEdit->text()) << "  "
       << quoteValue(m_accZEdit->text()) << "    ACCEX ACCEY ACCEZ\n";

    return true;
}

QJsonObject SlopeStabilityWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 20; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    QJsonArray reserve;
    for (int row = 0; row < m_strengthReserveTable->rowCount(); ++row)
    {
        QJsonObject one;
        const auto* factorItem = m_strengthReserveTable->item(row, 0);
        const auto* ifprItem = m_strengthReserveTable->item(row, 1);
        one["factor"] = factorItem ? factorItem->text().trimmed() : "";
        one["ifpr"] = ifprItem ? ifprItem->text().trimmed() : "";
        reserve.append(one);
    }
    obj["strength_reserve"] = reserve;
    obj["case_dir"] = m_caseDirEdit->text().trimmed();
    obj["dynamic_args"] = m_dynamicArgsEdit->text().trimmed();
    obj["acc_x"] = m_accXEdit->text().trimmed();
    obj["acc_y"] = m_accYEdit->text().trimmed();
    obj["acc_z"] = m_accZEdit->text().trimmed();
    return obj;
}

void SlopeStabilityWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 20; ++lineNo)
    {
        const QString value =
            (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }

    const QJsonArray reserve = obj.value("strength_reserve").toArray();
    const int rowCount = qMax(m_strengthReserveTable->rowCount(), reserve.size());
    m_strengthReserveTable->setRowCount(rowCount);
    for (int row = 0; row < rowCount; ++row)
    {
        QString factor;
        QString ifpr;
        if (row < reserve.size())
        {
            const QJsonObject one = reserve.at(row).toObject();
            factor = one.value("factor").toString();
            ifpr = one.value("ifpr").toString();
        }
        m_strengthReserveTable->setItem(
            row, 0, new QTableWidgetItem(factor.isEmpty() ? QString() : factor));
        m_strengthReserveTable->setItem(
            row, 1, new QTableWidgetItem(ifpr.isEmpty() ? QStringLiteral("1") : ifpr));
    }

    m_caseDirEdit->setText(obj.value("case_dir").toString(m_caseDirEdit->text()));
    m_dynamicArgsEdit->setText(obj.value("dynamic_args").toString(m_dynamicArgsEdit->text()));
    m_accXEdit->setText(obj.value("acc_x").toString(m_accXEdit->text()));
    m_accYEdit->setText(obj.value("acc_y").toString(m_accYEdit->text()));
    m_accZEdit->setText(obj.value("acc_z").toString(m_accZEdit->text()));
}

void SlopeStabilityWidget::onComputeClicked()
{
    emit computeRequested();
}
