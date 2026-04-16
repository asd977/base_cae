#include "CurvePlotWidget.h"

#include <QFile>
#include <QLabel>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>

#include "qcustomplot.h"

CurvePlotWidget::CurvePlotWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel("等待加载曲线数据", this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_plot = new QCustomPlot(this);
    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QColor(255, 255, 255, 180));
    m_plot->xAxis->setLabel("X");
    m_plot->yAxis->setLabel("Y");
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);

    for (int i = 0; i < 4; ++i)
        m_plot->addGraph();

    m_plot->graph(0)->setName("FOS");
    m_plot->graph(0)->setPen(QPen(QColor(31, 119, 180), 2));

    m_plot->graph(1)->setName("PTOT");
    m_plot->graph(1)->setPen(QPen(QColor(255, 127, 14), 2));

    m_plot->graph(2)->setName("MAX UR");
    m_plot->graph(2)->setPen(QPen(QColor(44, 160, 44), 2));

    m_plot->graph(3)->setName("LOOP");
    m_plot->graph(3)->setPen(QPen(QColor(214, 39, 40), 2));

    layout->addWidget(m_plot, 1);
}

bool CurvePlotWidget::loadDataFromTraceFile(const QString& filePath, const QString& xField,
                                            const QSet<QString>& yFields, QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString msg = QString("打开 TRACE 文件失败：%1").arg(filePath);
        m_statusLabel->setText(msg);
        if (errMsg)
            *errMsg = msg;
        return false;
    }

    const QRegularExpression pattern(
        "ISTEP=\\s*(\\d+)\\s+FOS=\\s*([+-]?\\d*\\.?\\d+(?:[Ee][+-]?\\d+)?)\\s+ptot=\\s*([+-]?\\d*\\.?\\d+(?:[Ee][+-]?\\d+)?)\\s+MAX\\s+UR=\\s*([+-]?\\d*\\.?\\d+(?:[Ee][+-]?\\d+)?)\\s+LOOP=\\s*(\\d+)");

    QVector<TracePoint> parsed;
    while (!file.atEnd())
    {
        const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;

        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;

        TracePoint point;
        point.istep = match.captured(1).toInt();
        point.fos = match.captured(2).toDouble();
        point.ptot = match.captured(3).toDouble();
        point.maxUr = match.captured(4).toDouble();
        point.loop = match.captured(5).toDouble();
        parsed.push_back(point);
    }

    if (parsed.isEmpty())
    {
        const QString msg = QString("TRACE 文件没有可解析数据：%1").arg(filePath);
        m_statusLabel->setText(msg);
        if (errMsg)
            *errMsg = msg;
        return false;
    }

    if (yFields.isEmpty())
    {
        const QString msg = "至少选择一条Y曲线";
        m_statusLabel->setText(msg);
        if (errMsg)
            *errMsg = msg;
        return false;
    }

    m_tracePath = filePath;
    m_points = parsed;
    renderPlot(xField, yFields);

    const QString ok = QString("已加载 %1 条记录：%2").arg(m_points.size()).arg(filePath);
    m_statusLabel->setText(ok);
    if (errMsg)
        errMsg->clear();
    return true;
}

void CurvePlotWidget::renderPlot(const QString& xField, const QSet<QString>& yFields)
{
    QVector<double> x;
    QVector<double> fos;
    QVector<double> ptot;
    QVector<double> maxUr;
    QVector<double> loop;

    x.reserve(m_points.size());
    fos.reserve(m_points.size());
    ptot.reserve(m_points.size());
    maxUr.reserve(m_points.size());
    loop.reserve(m_points.size());

    for (const TracePoint& p : qAsConst(m_points))
    {
        x.push_back(xField == "LOOP" ? p.loop : p.istep);
        fos.push_back(p.fos);
        ptot.push_back(p.ptot);
        maxUr.push_back(p.maxUr);
        loop.push_back(p.loop);
    }

    m_plot->xAxis->setLabel(xField);
    m_plot->yAxis->setLabel("Value");

    m_plot->graph(0)->setVisible(yFields.contains("FOS"));
    m_plot->graph(0)->setData(x, fos);

    m_plot->graph(1)->setVisible(yFields.contains("PTOT"));
    m_plot->graph(1)->setData(x, ptot);

    m_plot->graph(2)->setVisible(yFields.contains("MAX UR"));
    m_plot->graph(2)->setData(x, maxUr);

    m_plot->graph(3)->setVisible(yFields.contains("LOOP"));
    m_plot->graph(3)->setData(x, loop);

    const auto xMinMax = std::minmax_element(x.constBegin(), x.constEnd());
    const double minX = *xMinMax.first;
    const double maxX = *xMinMax.second;
    m_plot->xAxis->setRange(minX, maxX > minX ? maxX : minX + 1.0);

    m_plot->rescaleAxes(true);
    m_plot->replot();
}
