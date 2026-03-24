#include "SlopeStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

SlopeStabilityWidget::SlopeStabilityWidget(QWidget* parent)
    : BasePage("SlopeStabilityAnalysis")
{
    auto* layout = new QVBoxLayout(this);

    m_summaryLabel = new QLabel(
        "边坡稳定分析将导出 ISLOP.DAT（20 行参数，默认按强度折减法配置）。", this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_computeButton = new QPushButton("导出计算文件", this);
    layout->addWidget(m_computeButton);
    layout->addStretch();

    connect(m_computeButton, &QPushButton::clicked, this, &SlopeStabilityWidget::computeRequested);
}

void SlopeStabilityWidget::beforeLoad()
{
}

void SlopeStabilityWidget::beforeUnload()
{
}

bool SlopeStabilityWidget::exportIslopDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("ISLOP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "'F:\\FEMCLASS\\Z08_SLOP3D\\PREP\\'\n";
    os << "'MACB.DAT'\n";
    os << "'3'\n";
    os << "'0.0   -9.81   0.0'\n";
    os << "'1'\n";
    os << "'500'\n";
    os << "'0.0001'\n";
    os << "'1'\n";
    os << "'IIWL.DAT'\n";
    os << "' -------------- '\n";
    os << "' -------------- '\n";
    os << "'JDTRACE.DAT'\n";
    os << "'KETRACE.DAT'\n";
    os << "'TDTRACE.DAT'\n";
    os << "'0'\n";
    os << "'0'\n";
    os << "'0.05'\n";
    os << "'0.0'\n";
    os << "'1'\n";
    os << "'------------'\n";

    return true;
}

QJsonObject SlopeStabilityWidget::saveJson()
{
    return QJsonObject();
}

void SlopeStabilityWidget::loadJson(const QJsonObject&)
{
}
