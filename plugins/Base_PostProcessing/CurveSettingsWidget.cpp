#include "CurveSettingsWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

#include "qcustomplot.h"

CurveSettingsWidget::CurveSettingsWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessCurve)
{
    if (parent)
        setParent(parent);

    auto* mainLayout = new QVBoxLayout(this);
    auto* topLayout = new QHBoxLayout();

    m_reloadBtn = new QPushButton("刷新曲线", this);
    m_statusLabel = new QLabel("等待加载 TRACE.TXT", this);
    m_statusLabel->setWordWrap(true);

    topLayout->addWidget(m_reloadBtn);
    topLayout->addWidget(m_statusLabel, 1);

    m_plot = new QCustomPlot(this);
    m_plot->addGraph();
    m_plot->xAxis->setLabel("ISTEP");
    m_plot->yAxis->setLabel("MAX UR");
    m_plot->legend->setVisible(true);
    m_plot->graph(0)->setName("MAX UR - ISTEP");
    m_plot->graph(0)->setLineStyle(QCPGraph::lsLine);
    m_plot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 6));

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_plot, 1);

    connect(m_reloadBtn, &QPushButton::clicked, this, &CurveSettingsWidget::reloadFromDefaultPath);
}

void CurveSettingsWidget::beforeLoad()
{
    reloadFromDefaultPath();
}

void CurveSettingsWidget::beforeUnload()
{
}

QJsonObject CurveSettingsWidget::saveJson()
{
    QJsonObject obj;
    obj["tracePath"] = m_tracePath;
    return obj;
}

void CurveSettingsWidget::loadJson(const QJsonObject& obj)
{
    m_tracePath = obj.value("tracePath").toString();
    if (!m_tracePath.isEmpty())
    {
        loadTraceFile(m_tracePath);
        renderPlot();
    }
}

void CurveSettingsWidget::reloadFromDefaultPath()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    QStringList candidates;
    if (!projectDir.isEmpty())
    {
        candidates << QDir(projectDir).filePath("CASE01/TRACE.TXT");
    }
    candidates << QDir::current().filePath("CASE01/TRACE.TXT");

    for (const QString& candidate : candidates)
    {
        if (QFile::exists(candidate))
        {
            loadTraceFile(candidate);
            renderPlot();
            return;
        }
    }

    m_points.clear();
    renderPlot();
    m_statusLabel->setText("未找到 TRACE.TXT（已尝试：项目目录/CASE01 与当前目录/CASE01）");
}

bool CurveSettingsWidget::loadTraceFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_statusLabel->setText(QString("打开失败：%1").arg(filePath));
        return false;
    }

    const QRegularExpression pattern(
        "ISTEP=\\s*(\\d+)\\s+FOS=\\s*([+-]?\\d*\\.?\\d+(?:[Ee][+-]?\\d+)?)\\s+ptot=.*?MAX\\s+UR=\\s*([+-]?\\d*\\.?\\d+(?:[Ee][+-]?\\d+)?)");

    QVector<TracePoint> parsed;
    while (!file.atEnd())
    {
        const QString line = QString::fromLocal8Bit(file.readLine());
        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;

        TracePoint point;
        point.istep = match.captured(1).toInt();
        point.fos = match.captured(2).toDouble();
        point.maxUr = match.captured(3).toDouble();
        parsed.push_back(point);
    }

    m_tracePath = filePath;
    m_points = parsed;
    m_statusLabel->setText(QString("已加载 %1 个点：%2").arg(m_points.size()).arg(filePath));
    return !m_points.isEmpty();
}

void CurveSettingsWidget::renderPlot()
{
    QVector<double> x;
    QVector<double> y;
    x.reserve(m_points.size());
    y.reserve(m_points.size());

    for (const TracePoint& point : qAsConst(m_points))
    {
        x.push_back(point.istep);
        y.push_back(point.maxUr);
    }

    m_plot->graph(0)->setData(x, y);
    if (!x.isEmpty())
    {
        m_plot->xAxis->setRange(*std::min_element(x.constBegin(), x.constEnd()),
                                *std::max_element(x.constBegin(), x.constEnd()));
        m_plot->yAxis->rescale();
    }
    else
    {
        m_plot->xAxis->setRange(0, 1);
        m_plot->yAxis->setRange(0, 1);
    }
    m_plot->replot();
}
