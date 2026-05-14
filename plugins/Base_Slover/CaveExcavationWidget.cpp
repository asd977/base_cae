#include "CaveExcavationWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include "ui_CaveExcavationWidget.h"

CaveExcavationWidget::CaveExcavationWidget(QWidget* parent)
    : BasePage("CaveExcavationAnalysis"), ui(new Ui::CaveExcavationWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        "QWidget#CaveExcavationWidget, QWidget#scrollAreaWidgetContents { background: white; }");
    ui->scrollArea->setStyleSheet("QScrollArea { background: white; border: 0; }");

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString projectAbs = QDir(projectDir).absolutePath();
    ui->line1Edit->setText(QDir::toNativeSeparators(QDir(projectAbs).filePath("PREP")) + QDir::separator());
    ui->line14Edit->setText(QDir::toNativeSeparators(QDir(projectAbs).filePath("GEOSTR")) + QDir::separator());

    const QList<QPair<QLabel*, QString>> labelTextMap = {
        {ui->label1,  QStringLiteral("洞室默认文件目录")},
        {ui->label2,  QStringLiteral("荷载释放文件（MAFACTOR）")},
        {ui->label3,  QStringLiteral("地下水影响开关（IFSEEP）")},
        {ui->label4,  QStringLiteral("地下水面点集文件")},
        {ui->label5,  QStringLiteral("洞内水位（HW_NEI）")},
        {ui->label6,  QStringLiteral("衬砌内表面文件")},
        {ui->label7,  QStringLiteral("衬砌外水位（HW_WAI）")},
        {ui->label8,  QStringLiteral("衬砌外表面文件")},
        {ui->label9,  QStringLiteral("吊车梁材料文件")},
        {ui->label10, QStringLiteral("吊车梁安装级")},
        {ui->label11, QStringLiteral("围岩已释放荷载 α")},
        {ui->label12, QStringLiteral("衬砌外表面文件（T11）")},
        {ui->label13, QStringLiteral("渗压场文件")},
        {ui->label14, QStringLiteral("地应力场目录")},
        {ui->label15, QStringLiteral("特征节点文件")},
        {ui->label16, QStringLiteral("特征单元文件")},
        {ui->label17, QStringLiteral("工况查看/编辑文件")},
        {ui->label18, QStringLiteral("阻尼开关（IF_BOP）")},
        {ui->label19, QStringLiteral("阻尼参数（IDAMP）")},
        {ui->label20, QStringLiteral("Rayleigh α（ALFAP）")},
        {ui->label21, QStringLiteral("Rayleigh β（BBETAP）")},
        {ui->label22, QStringLiteral("计算方式（ITYPE_JISUAN）")},
        {ui->label23, QStringLiteral("开挖荷载计算方式")},
        {ui->label24, QStringLiteral("迭代法与迭代数")},
        {ui->label25, QStringLiteral("加固对应荷载分级")},
    };
    for (const auto& item : labelTextMap)
        item.first->setText(item.second);

    const QList<QLineEdit*> fixedFileInputs = {
        ui->line1Edit, ui->line2Edit, ui->line4Edit, ui->line6Edit, ui->line8Edit,
        ui->line9Edit, ui->line12Edit, ui->line13Edit, ui->line14Edit, ui->line15Edit,
        ui->line16Edit, ui->line17Edit,
    };
    for (auto* edit : fixedFileInputs)
        edit->hide();

    connect(ui->btnCompute, &QPushButton::clicked, this, &CaveExcavationWidget::onComputeClicked);
}

CaveExcavationWidget::~CaveExcavationWidget()
{
    delete ui;
}

void CaveExcavationWidget::beforeLoad() {}
void CaveExcavationWidget::beforeUnload() {}

bool CaveExcavationWidget::exportICaveDatFile(const QString& supportCaseSection) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("ICAVE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QList<QLineEdit*> edits = {
        ui->line1Edit,  ui->line2Edit,  ui->line3Edit,  ui->line4Edit,  ui->line5Edit,
        ui->line6Edit,  ui->line7Edit,  ui->line8Edit,  ui->line9Edit,  ui->line10Edit,
        ui->line11Edit, ui->line12Edit, ui->line13Edit, ui->line14Edit, ui->line15Edit,
        ui->line16Edit, ui->line17Edit, ui->line18Edit, ui->line19Edit, ui->line20Edit,
        ui->line21Edit, ui->line22Edit, ui->line23Edit, ui->line24Edit, ui->line25Edit,
    };

    const QSet<int> quotedFields = {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int i = 0; i < edits.size(); ++i)
    {
        const int fieldNo = i + 1;
        const QString value = edits.at(i)->text().trimmed();
        if (quotedFields.contains(fieldNo))
            os << "'" << value << "'      " << fieldNo << "\n";
        else
            os << " " << value << "      " << fieldNo << "\n";
    }

    if (!supportCaseSection.trimmed().isEmpty())
        os << supportCaseSection.trimmed() << "\n";
    return true;
}

QJsonObject CaveExcavationWidget::saveJson()
{
    QJsonObject obj;
    for (int i = 1; i <= 25; ++i)
    {
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        obj[QString("line_%1").arg(i)] = edit ? edit->text().trimmed() : "";
    }
    return obj;
}

void CaveExcavationWidget::loadJson(const QJsonObject& obj)
{
    for (int i = 1; i <= 25; ++i)
    {
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        if (!edit)
            continue;
        edit->setText(obj.value(QString("line_%1").arg(i)).toString(edit->text()));
    }
}

void CaveExcavationWidget::onComputeClicked()
{
    emit computeRequested();
}
