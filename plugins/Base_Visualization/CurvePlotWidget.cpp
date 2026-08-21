#include "CurvePlotWidget.h"

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkAxis.h>
#include <vtkChart.h>
#include <vtkChartXY.h>
#include <vtkChartLegend.h>
#include <vtkContextScene.h>
#include <vtkContextView.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkPlot.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkTable.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const QStringList& supportedFields()
{
    static const QStringList fields = {
        QStringLiteral("FOS"),
        QStringLiteral("PTOT"),
        QStringLiteral("MAX UR"),
        QStringLiteral("LOOP"),
    };
    return fields;
}

QColor seriesColor(const QString& field)
{
    const QString key = field.trimmed().toUpper();
    if (key == QStringLiteral("FOS"))
        return QColor(31, 119, 180);
    if (key == QStringLiteral("PTOT"))
        return QColor(230, 126, 34);
    if (key == QStringLiteral("MAX UR") || key == QStringLiteral("MAXUR"))
        return QColor(39, 174, 96);
    if (key == QStringLiteral("LOOP"))
        return QColor(192, 57, 43);
    return QColor(90, 100, 110);
}


QString findMicrosoftYaHeiFontFile()
{
#ifdef Q_OS_WIN
    const QString fontDir =
        QDir(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows"))).filePath("Fonts");
    const QStringList candidates = {
        QStringLiteral("msyh.ttc"),
        QStringLiteral("msyh.ttf"),
        QStringLiteral("msyhui.ttf"),
    };
    for (const QString& name : candidates)
    {
        const QString path = QDir(fontDir).filePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
#endif
    return QString();
}

void applyChartTextStyle(vtkTextProperty* property, int fontSize, bool bold = false)
{
    if (!property)
        return;

    property->SetFontSize(fontSize);
    property->SetColor(0.12, 0.14, 0.16);
    property->SetBold(bold ? 1 : 0);
    property->SetItalic(0);
    property->SetShadow(0);

    const QString fontFile = findMicrosoftYaHeiFontFile();
    if (!fontFile.isEmpty())
    {
        const QByteArray bytes = fontFile.toLocal8Bit();
        property->SetFontFamily(VTK_FONT_FILE);
        property->SetFontFile(bytes.constData());
    }
    else
    {
        property->SetFontFamilyToArial();
    }
}

QString canonicalFieldName(QString field)
{
    field = field.trimmed().toUpper();
    field.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    if (field == QStringLiteral("MAXUR"))
        return QStringLiteral("MAX UR");
    return field;
}
} // namespace

CurvePlotWidget::CurvePlotWidget(QWidget* parent) : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("curvePlotToolbar"));
    toolbar->setAttribute(Qt::WA_StyledBackground, true);

    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 5, 12, 5);
    toolbarLayout->setSpacing(6);

    m_titleLabel = new QLabel(tr("计算过程曲线"), toolbar);
    m_titleLabel->setObjectName(QStringLiteral("curvePlotTitleLabel"));
    toolbarLayout->addWidget(m_titleLabel);
    toolbarLayout->addStretch(1);

    auto makeButton = [toolbar](const QString& text, const QString& tooltip)
    {
        auto* button = new QToolButton(toolbar);
        button->setObjectName(QStringLiteral("curvePlotToolButton"));
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        return button;
    };

    m_normalizeButton =
        makeButton(tr("趋势对比"), tr("将每条曲线按自身最小值和最大值映射到 0–1，只比较变化趋势"));
    m_normalizeButton->setCheckable(true);
    connect(m_normalizeButton, &QToolButton::toggled, this, &CurvePlotWidget::setNormalized);
    toolbarLayout->addWidget(m_normalizeButton);

    auto* fitButton = makeButton(tr("适应视图"), tr("恢复完整曲线范围"));
    connect(fitButton, &QToolButton::clicked, this, &CurvePlotWidget::resetChartView);
    toolbarLayout->addWidget(fitButton);

    rootLayout->addWidget(toolbar);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setObjectName(QStringLiteral("curvePlotVtkWidget"));
    m_vtkWidget->setEnableHiDPI(true);
    m_vtkWidget->setUnscaledDPI(96);
    rootLayout->addWidget(m_vtkWidget, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("curvePlotStatusLabel"));
    m_statusLabel->setContentsMargins(12, 4, 12, 6);
    rootLayout->addWidget(m_statusLabel);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderWindow->SetMultiSamples(4);
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_contextView = vtkSmartPointer<vtkContextView>::New();
    m_contextView->SetRenderWindow(m_renderWindow);
    m_contextView->GetRenderer()->SetBackground(0.965, 0.973, 0.980);

    m_chart = vtkSmartPointer<vtkChartXY>::New();
    m_chart->SetShowLegend(false);
    m_chart->SetBorders(62, 48, 22, 18);
    m_contextView->GetScene()->AddItem(m_chart);

    vtkAxis* bottomAxis = m_chart->GetAxis(vtkAxis::BOTTOM);
    vtkAxis* leftAxis = m_chart->GetAxis(vtkAxis::LEFT);
    bottomAxis->SetTitle("ISTEP");
    leftAxis->SetTitle("Value");
    applyChartTextStyle(bottomAxis->GetLabelProperties(), 10, false);
    applyChartTextStyle(bottomAxis->GetTitleProperties(), 11, true);
    applyChartTextStyle(leftAxis->GetLabelProperties(), 10, false);
    applyChartTextStyle(leftAxis->GetTitleProperties(), 11, true);
    if (vtkChartLegend* legend = m_chart->GetLegend())
        applyChartTextStyle(legend->GetLabelProperties(), 10, false);

    updateStatus(tr("请选择可用的计算过程结果。"));
}

bool CurvePlotWidget::loadDataFromTraceFile(const QString& filePath, const QString& xField,
                                            const QSet<QString>& yFields, QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString msg = tr("打开 TRACE 文件失败：%1").arg(QDir::toNativeSeparators(filePath));
        if (errMsg)
            *errMsg = msg;
        updateStatus(msg, true);
        return false;
    }

    QVector<TracePoint> parsed;
    parsed.reserve(1024);

    while (!file.atEnd())
    {
        const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;

        TracePoint point;
        if (parseTraceLine(line, point))
            parsed.push_back(point);
    }

    if (parsed.isEmpty())
    {
        const QString msg = tr("TRACE 文件中没有识别到 ISTEP、FOS、PTOT、MAX UR 或 LOOP 数据。");
        if (errMsg)
            *errMsg = msg;
        updateStatus(msg, true);
        return false;
    }

    const QString canonicalX = canonicalFieldName(xField);
    if (canonicalX != QStringLiteral("ISTEP") && canonicalX != QStringLiteral("LOOP") &&
        canonicalX != QStringLiteral("FOS"))
    {
        const QString msg = tr("不支持的横轴字段：%1").arg(xField);
        if (errMsg)
            *errMsg = msg;
        updateStatus(msg, true);
        return false;
    }

    m_showLegend = yFields.contains(QStringLiteral("__SHOW_LEGEND__"));
    QStringList orderedFields = normalizedFieldOrder(yFields);
    orderedFields.removeAll(canonicalX);
    if (orderedFields.isEmpty())
    {
        const QString msg = tr("横轴已使用%1，请再选择其他纵轴曲线。")
                                .arg(fieldDisplayName(canonicalX));
        if (errMsg)
            *errMsg = msg;
        updateStatus(msg, true);
        return false;
    }

    m_tracePath = filePath;
    m_xField = canonicalX;
    m_yFields = orderedFields;
    m_points = parsed;

    renderPlot();

    if (errMsg)
        errMsg->clear();
    return true;
}

bool CurvePlotWidget::parseTraceLine(const QString& line, TracePoint& point) const
{
    static const QRegularExpression pairPattern(
        QStringLiteral("(ISTEP|FOS|PTOT|MAX\\s*UR|LOOP)\\s*=\\s*"
                       "([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[Ee][+-]?\\d+)?)"),
        QRegularExpression::CaseInsensitiveOption);

    bool foundAny = false;
    QRegularExpressionMatchIterator it = pairPattern.globalMatch(line);
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const QString key = canonicalFieldName(match.captured(1));
        bool ok = false;
        const double value = match.captured(2).toDouble(&ok);
        if (!ok)
            continue;

        foundAny = true;
        if (key == QStringLiteral("ISTEP"))
        {
            point.istep = value;
            point.hasIstep = true;
        }
        else if (key == QStringLiteral("FOS"))
        {
            point.fos = value;
            point.hasFos = true;
        }
        else if (key == QStringLiteral("PTOT"))
        {
            point.ptot = value;
            point.hasPtot = true;
        }
        else if (key == QStringLiteral("MAX UR"))
        {
            point.maxUr = value;
            point.hasMaxUr = true;
        }
        else if (key == QStringLiteral("LOOP"))
        {
            point.loop = value;
            point.hasLoop = true;
        }
    }

    // 保留任何识别到的 TRACE 数据。具体横轴是否有值在 buildPlotTable() 中按
    // 当前选择判断，这样 FOS 可以直接作为横轴。
    return foundAny;
}

void CurvePlotWidget::renderPlot()
{
    if (!m_chart || m_points.isEmpty() || m_yFields.isEmpty())
        return;

    m_chart->ClearPlots();
    m_chart->SetShowLegend(m_showLegend);
    m_table = buildPlotTable(m_yFields);
    if (!m_table || m_table->GetNumberOfRows() == 0)
    {
        updateStatus(tr("当前选择没有可绘制的有效数值。"), true);
        return;
    }

    for (int i = 0; i < m_yFields.size(); ++i)
    {
        vtkPlot* plot = m_chart->AddPlot(vtkChart::LINE);
        if (!plot)
            continue;

        plot->SetInputData(m_table, 0, i + 1);
        const QColor color = seriesColor(m_yFields.at(i));
        plot->SetColor(static_cast<unsigned char>(color.red()),
                       static_cast<unsigned char>(color.green()),
                       static_cast<unsigned char>(color.blue()), 255);
        plot->SetWidth(2.0f);
    }

    const QString xTitle = fieldDisplayName(m_xField);
    m_chart->GetAxis(vtkAxis::BOTTOM)->SetTitle(xTitle.toUtf8().constData());
    m_chart->GetAxis(vtkAxis::LEFT)
        ->SetTitle((m_normalized ? tr("相对变化（0–1）") : tr("数值")).toUtf8().constData());

    m_chart->RecalculateBounds();
    m_renderWindow->Render();

    updateStatus(QString());
}

vtkSmartPointer<vtkTable> CurvePlotWidget::buildPlotTable(const QStringList& fields) const
{
    vtkSmartPointer<vtkTable> table = vtkSmartPointer<vtkTable>::New();

    vtkSmartPointer<vtkDoubleArray> xValues = vtkSmartPointer<vtkDoubleArray>::New();
    xValues->SetName("X");
    xValues->SetNumberOfValues(m_points.size());

    for (int row = 0; row < m_points.size(); ++row)
    {
        bool valid = false;
        const double value = fieldValue(m_points.at(row), m_xField, &valid);
        xValues->SetValue(row, valid ? value : std::numeric_limits<double>::quiet_NaN());
    }
    table->AddColumn(xValues);

    for (const QString& field : fields)
    {
        vtkSmartPointer<vtkDoubleArray> values = vtkSmartPointer<vtkDoubleArray>::New();
        values->SetName(fieldDisplayName(field).toUtf8().constData());
        values->SetNumberOfValues(m_points.size());

        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::lowest();
        if (m_normalized)
        {
            for (const TracePoint& point : m_points)
            {
                bool valid = false;
                const double value = fieldValue(point, field, &valid);
                if (!valid || !std::isfinite(value))
                    continue;
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }

        const bool canNormalize = m_normalized && minValue <= maxValue;
        const double range = canNormalize ? maxValue - minValue : 0.0;

        for (int row = 0; row < m_points.size(); ++row)
        {
            bool valid = false;
            double value = fieldValue(m_points.at(row), field, &valid);
            if (!valid || !std::isfinite(value))
            {
                value = std::numeric_limits<double>::quiet_NaN();
            }
            else if (canNormalize)
            {
                value = range > std::numeric_limits<double>::epsilon()
                            ? (value - minValue) / range
                            : 0.0;
            }
            values->SetValue(row, value);
        }
        table->AddColumn(values);
    }

    return table;
}

QStringList CurvePlotWidget::normalizedFieldOrder(const QSet<QString>& fields) const
{
    QSet<QString> canonical;
    for (const QString& field : fields)
        canonical.insert(canonicalFieldName(field));

    QStringList ordered;
    for (const QString& field : supportedFields())
    {
        if (canonical.contains(field))
            ordered.push_back(field);
    }
    return ordered;
}

double CurvePlotWidget::fieldValue(const TracePoint& point, const QString& field, bool* valid) const
{
    const QString canonical = canonicalFieldName(field);
    double value = 0.0;
    bool hasValue = false;

    if (canonical == QStringLiteral("ISTEP"))
    {
        value = point.istep;
        hasValue = point.hasIstep;
    }
    else if (canonical == QStringLiteral("FOS"))
    {
        value = point.fos;
        hasValue = point.hasFos;
    }
    else if (canonical == QStringLiteral("PTOT"))
    {
        value = point.ptot;
        hasValue = point.hasPtot;
    }
    else if (canonical == QStringLiteral("MAX UR"))
    {
        value = point.maxUr;
        hasValue = point.hasMaxUr;
    }
    else if (canonical == QStringLiteral("LOOP"))
    {
        value = point.loop;
        hasValue = point.hasLoop;
    }

    if (valid)
        *valid = hasValue;
    return value;
}

QString CurvePlotWidget::fieldDisplayName(const QString& field) const
{
    const QString canonical = canonicalFieldName(field);
    if (canonical == QStringLiteral("FOS"))
        return tr("安全系数");
    if (canonical == QStringLiteral("PTOT"))
        return tr("总荷载");
    if (canonical == QStringLiteral("MAX UR"))
        return tr("最大位移比");
    if (canonical == QStringLiteral("LOOP"))
        return tr("迭代次数");
    if (canonical == QStringLiteral("ISTEP"))
        return tr("计算步");
    return field;
}

void CurvePlotWidget::resetChartView()
{
    if (!m_chart || !m_renderWindow)
        return;
    m_chart->RecalculateBounds();
    m_renderWindow->Render();
}

void CurvePlotWidget::setNormalized(bool normalized)
{
    if (m_normalized == normalized)
        return;
    m_normalized = normalized;
    renderPlot();
}

void CurvePlotWidget::updateStatus(const QString& text, bool error)
{
    if (!m_statusLabel)
        return;
    m_statusLabel->setVisible(error && !text.trimmed().isEmpty());
    m_statusLabel->setText(error ? text : QString());
    m_statusLabel->setProperty("error", error);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}
