#include "SlopeStandardMethodDialog.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>

#include <QCloseEvent>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMap>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCutter.h>
#include <vtkDataArray.h>
#include <vtkIntArray.h>
#include <vtkPlane.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace
{
struct PlotPoint
{
    double x = 0.0;
    double y = 0.0;
};

QVector<PlotPoint> jsonPoints(const QJsonValue& value)
{
    QVector<PlotPoint> points;
    const QJsonArray array = value.toArray();
    points.reserve(array.size());
    for (const QJsonValue& item : array)
    {
        const QJsonObject point = item.toObject();
        const double x = point.value(QStringLiteral("x")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        const double y = point.value(QStringLiteral("y")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(x) && std::isfinite(y))
            points.push_back({x, y});
    }
    return points;
}

QString methodDisplayName(const QString& method)
{
    if (method == QStringLiteral("swedish"))
        return QStringLiteral("瑞典圆弧法");
    if (method == QStringLiteral("bishop"))
        return QStringLiteral("毕肖普法");
    if (method == QStringLiteral("sarma"))
        return QStringLiteral("Sarma 法");
    return method;
}

QString modeDisplayName(const QString& mode)
{
    if (mode == QStringLiteral("fixed"))
        return QStringLiteral("固定圆弧");
    if (mode == QStringLiteral("search"))
        return QStringLiteral("自动搜索最危险滑弧");
    return mode;
}

QString displayNumber(double value)
{
    return QString::number(value, 'g', 12);
}

QString sanitizedUuid(QString uuid)
{
    uuid.remove(QLatin1Char('{'));
    uuid.remove(QLatin1Char('}'));
    for (QChar& ch : uuid)
    {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('-') && ch != QLatin1Char('_'))
            ch = QLatin1Char('_');
    }
    return uuid.isEmpty() ? QStringLiteral("slope") : uuid;
}

QString axisName(int axis)
{
    static const QStringList names{QStringLiteral("X"), QStringLiteral("Y"),
                                   QStringLiteral("Z")};
    return names.value(axis, QStringLiteral("?"));
}

struct SectionPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct SectionCellUse
{
    int material = 0;
    SectionPoint centroid;
};

struct SectionEdge
{
    int a = -1;
    int b = -1;
    int material = 0;
    bool surface = false;
};

struct SectionChain
{
    QVector<int> nodes;
    QVector<int> materials;
};

double pointSegmentDistance(const SectionPoint& point,
                            const SectionPoint& a,
                            const SectionPoint& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    if (length2 <= std::numeric_limits<double>::epsilon())
        return std::hypot(point.x - a.x, point.y - a.y);
    const double t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length2,
                                0.0, 1.0);
    return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}

void markRdpPoints(const QVector<int>& nodes,
                   const QVector<SectionPoint>& points,
                   int first,
                   int last,
                   double tolerance,
                   QVector<bool>* keep)
{
    if (!keep || last <= first + 1)
        return;
    double maximumDistance = -1.0;
    int split = -1;
    const SectionPoint& a = points.at(nodes.at(first));
    const SectionPoint& b = points.at(nodes.at(last));
    for (int i = first + 1; i < last; ++i)
    {
        const double distance = pointSegmentDistance(points.at(nodes.at(i)), a, b);
        if (distance > maximumDistance)
        {
            maximumDistance = distance;
            split = i;
        }
    }
    if (split >= 0 && maximumDistance > tolerance)
    {
        (*keep)[split] = true;
        markRdpPoints(nodes, points, first, split, tolerance, keep);
        markRdpPoints(nodes, points, split, last, tolerance, keep);
    }
}

QVector<SectionChain> traceSectionChains(const QVector<SectionEdge>& edges)
{
    QVector<SectionChain> chains;
    if (edges.isEmpty())
        return chains;

    QMap<int, QVector<int>> adjacency;
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
    {
        adjacency[edges.at(edgeIndex).a].append(edgeIndex);
        adjacency[edges.at(edgeIndex).b].append(edgeIndex);
    }
    QVector<bool> used(edges.size(), false);

    const auto trace = [&](int startNode, int firstEdge, QVector<bool>* edgeUsed)
    {
        SectionChain chain;
        int node = startNode;
        int edgeIndex = firstEdge;
        chain.nodes.append(node);
        while (edgeIndex >= 0 && !edgeUsed->at(edgeIndex))
        {
            (*edgeUsed)[edgeIndex] = true;
            const SectionEdge& edge = edges.at(edgeIndex);
            const int nextNode = edge.a == node ? edge.b : edge.a;
            chain.materials.append(edge.material);
            chain.nodes.append(nextNode);
            node = nextNode;
            edgeIndex = -1;
            for (int candidate : adjacency.value(node))
            {
                if (!edgeUsed->at(candidate))
                {
                    edgeIndex = candidate;
                    break;
                }
            }
            if (node == startNode)
                break;
        }
        return chain;
    };

    for (auto it = adjacency.constBegin(); it != adjacency.constEnd(); ++it)
    {
        if (it.value().size() == 2)
            continue;
        for (int edgeIndex : it.value())
        {
            if (!used.at(edgeIndex))
                chains.append(trace(it.key(), edgeIndex, &used));
        }
    }
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
    {
        if (!used.at(edgeIndex))
            chains.append(trace(edges.at(edgeIndex).a, edgeIndex, &used));
    }
    return chains;
}

QVector<SectionEdge> simplifySectionChain(const SectionChain& chain,
                                          const QVector<SectionPoint>& points,
                                          double tolerance,
                                          bool surface)
{
    QVector<SectionEdge> result;
    if (chain.nodes.size() < 2 || chain.materials.size() + 1 != chain.nodes.size())
        return result;

    int firstEdge = 0;
    while (firstEdge < chain.materials.size())
    {
        int lastEdge = firstEdge;
        while (lastEdge + 1 < chain.materials.size()
               && chain.materials.at(lastEdge + 1) == chain.materials.at(firstEdge))
            ++lastEdge;
        const int firstNode = firstEdge;
        const int lastNode = lastEdge + 1;
        QVector<bool> keep(chain.nodes.size(), false);
        keep[firstNode] = true;
        keep[lastNode] = true;
        markRdpPoints(chain.nodes, points, firstNode, lastNode,
                      std::max(0.0, tolerance), &keep);
        int previous = firstNode;
        for (int i = firstNode + 1; i <= lastNode; ++i)
        {
            if (!keep.at(i))
                continue;
            result.append({chain.nodes.at(previous), chain.nodes.at(i),
                           chain.materials.at(firstEdge), surface});
            previous = i;
        }
        firstEdge = lastEdge + 1;
    }
    return result;
}

double chainHorizontalSpan(const SectionChain& chain,
                           const QVector<SectionPoint>& points)
{
    if (chain.nodes.isEmpty())
        return 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (int node : chain.nodes)
    {
        minimum = std::min(minimum, points.at(node).x);
        maximum = std::max(maximum, points.at(node).x);
    }
    return maximum - minimum;
}
}

class SlopeStandardInputPreviewWidget final : public QWidget
{
  public:
    explicit SlopeStandardInputPreviewWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(400, 360);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setModel(const SlopeStandardDatModel& model)
    {
        m_model = model;
        m_error.clear();
        update();
    }

    void setError(const QString& error)
    {
        m_error = error;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());

        painter.setPen(palette().text().color());
        QFont titleFont = painter.font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(QRectF(rect()).adjusted(14, 10, -14, -10),
                         Qt::AlignTop | Qt::AlignLeft,
                         QStringLiteral("二维边坡断面预览"));
        painter.setFont(font());

        if (!m_error.isEmpty() || m_model.nodes.isEmpty())
        {
            painter.setPen(QColor(170, 75, 55));
            painter.drawText(QRectF(rect()).adjusted(28, 58, -28, -28),
                             Qt::AlignCenter | Qt::TextWordWrap,
                             m_error.isEmpty() ? QStringLiteral("填写节点和边界线后在此显示断面。")
                                               : QStringLiteral("暂时无法预览：\n%1").arg(m_error));
            return;
        }

        double minX = m_model.nodes.first().x;
        double maxX = minX;
        double minY = m_model.nodes.first().y;
        double maxY = minY;
        for (const auto& node : m_model.nodes)
        {
            minX = std::min(minX, node.x);
            maxX = std::max(maxX, node.x);
            minY = std::min(minY, node.y);
            maxY = std::max(maxY, node.y);
        }
        if (std::abs(maxX - minX) < 1.0e-12)
        {
            minX -= 1.0;
            maxX += 1.0;
        }
        if (std::abs(maxY - minY) < 1.0e-12)
        {
            minY -= 1.0;
            maxY += 1.0;
        }

        const int legendLines = std::min(4, static_cast<int>(m_model.soils.size()));
        const QRectF frame = QRectF(rect()).adjusted(48.0, 48.0, -22.0,
                                                     -62.0 - legendLines * 18.0);
        painter.setPen(QPen(palette().mid().color(), 1.0));
        painter.drawRect(frame);
        painter.setPen(QPen(palette().mid().color(), 0.8, Qt::DotLine));
        for (int i = 1; i < 5; ++i)
        {
            const double x = frame.left() + frame.width() * i / 5.0;
            const double y = frame.top() + frame.height() * i / 5.0;
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }

        const double scale = std::min(frame.width() / (maxX - minX),
                                      frame.height() / (maxY - minY));
        const double usedWidth = (maxX - minX) * scale;
        const double usedHeight = (maxY - minY) * scale;
        const double left = frame.left() + (frame.width() - usedWidth) * 0.5;
        const double top = frame.top() + (frame.height() - usedHeight) * 0.5;
        const auto mapNode = [&](int nodeId)
        {
            const auto& node = m_model.nodes.at(nodeId - 1);
            return QPointF(left + (node.x - minX) * scale,
                           top + (maxY - node.y) * scale);
        };
        const auto soilColor = [](int soilId)
        {
            static const QVector<QColor> colors = {
                QColor(200, 120, 55), QColor(105, 150, 70), QColor(145, 95, 175),
                QColor(180, 165, 60), QColor(80, 145, 165), QColor(185, 95, 105)};
            return colors.at(std::abs(soilId - 1) % colors.size());
        };

        QSet<int> surfaceSegmentIds;
        for (const auto& surface : m_model.surfaceLoads)
            surfaceSegmentIds.insert(surface.segmentId);

        for (int i = 0; i < m_model.segments.size(); ++i)
        {
            const auto& segment = m_model.segments.at(i);
            if (segment.startNode < 1 || segment.startNode > m_model.nodes.size()
                || segment.endNode < 1 || segment.endNode > m_model.nodes.size())
                continue;
            const QPointF a = mapNode(segment.startNode);
            const QPointF b = mapNode(segment.endNode);
            painter.setPen(QPen(soilColor(segment.soilId), 2.5));
            painter.drawLine(a, b);
            const QPointF middle = (a + b) * 0.5;
            painter.setPen(palette().text().color());
            painter.drawText(QRectF(middle.x() - 70.0, middle.y() - 22.0, 140.0, 18.0),
                             Qt::AlignCenter,
                             QStringLiteral("线%1 / 下方土层%2")
                                 .arg(i + 1)
                                 .arg(segment.soilId));
        }

        painter.setPen(QPen(QColor(35, 100, 205), 4.0));
        for (int segmentId : surfaceSegmentIds)
        {
            if (segmentId < 1 || segmentId > m_model.segments.size())
                continue;
            const auto& segment = m_model.segments.at(segmentId - 1);
            painter.drawLine(mapNode(segment.startNode), mapNode(segment.endNode));
        }
        painter.setPen(QPen(QColor(30, 175, 190), 3.0, Qt::DashLine));
        for (const auto& segment : m_model.phreaticSegments)
        {
            if (segment.startNode >= 1 && segment.startNode <= m_model.nodes.size()
                && segment.endNode >= 1 && segment.endNode <= m_model.nodes.size())
                painter.drawLine(mapNode(segment.startNode), mapNode(segment.endNode));
        }

        for (const auto& node : m_model.nodes)
        {
            const QPointF point = mapNode(node.id);
            painter.setBrush(palette().base());
            painter.setPen(QPen(palette().text().color(), 1.5));
            painter.drawEllipse(point, 4.0, 4.0);
            painter.drawText(QRectF(point.x() + 5.0, point.y() + 2.0, 80.0, 18.0),
                             QStringLiteral("点%1").arg(node.id));
        }

        painter.setPen(QPen(QColor(35, 100, 205), 4.0));
        painter.drawLine(QPointF(frame.left(), frame.bottom() + 18.0),
                         QPointF(frame.left() + 28.0, frame.bottom() + 18.0));
        painter.setPen(palette().text().color());
        painter.drawText(QPointF(frame.left() + 34.0, frame.bottom() + 22.0),
                         QStringLiteral("蓝色粗线：参与稳定计算的外坡面"));
        painter.setPen(QPen(QColor(30, 175, 190), 3.0, Qt::DashLine));
        painter.drawLine(QPointF(frame.left(), frame.bottom() + 38.0),
                         QPointF(frame.left() + 28.0, frame.bottom() + 38.0));
        painter.setPen(palette().text().color());
        painter.drawText(QPointF(frame.left() + 34.0, frame.bottom() + 42.0),
                         QStringLiteral("青色虚线：浸润线"));

        double legendY = frame.bottom() + 60.0;
        for (int i = 0; i < legendLines; ++i)
        {
            const auto& soil = m_model.soils.at(i);
            painter.setPen(QPen(soilColor(soil.id), 4.0));
            painter.drawLine(QPointF(frame.left(), legendY - 4.0),
                             QPointF(frame.left() + 28.0, legendY - 4.0));
            painter.setPen(palette().text().color());
            painter.drawText(QPointF(frame.left() + 34.0, legendY),
                             QStringLiteral("土层%1：φ=%2，c=%3，γ=%4，γsat=%5")
                                 .arg(soil.id)
                                 .arg(displayNumber(soil.frictionAngle),
                                      displayNumber(soil.cohesion),
                                      displayNumber(soil.naturalUnitWeight),
                                      displayNumber(soil.saturatedUnitWeight)));
            legendY += 18.0;
        }
    }

  private:
    SlopeStandardDatModel m_model;
    QString m_error;
};

class SlopeStandardPlotWidget final : public QWidget
{
  public:
    explicit SlopeStandardPlotWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPlot(const QJsonObject& plot)
    {
        m_outline = jsonPoints(plot.value(QStringLiteral("outline")));
        m_extendedOutline = jsonPoints(plot.value(QStringLiteral("extended_outline")));
        m_slipArc = jsonPoints(plot.value(QStringLiteral("slip_arc")));
        m_factor = plot.value(QStringLiteral("factor")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
        update();
    }

    void clearPlot()
    {
        m_outline.clear();
        m_extendedOutline.clear();
        m_slipArc.clear();
        m_factor = std::numeric_limits<double>::quiet_NaN();
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());

        const QRectF frame = QRectF(rect()).adjusted(48.0, 24.0, -24.0, -42.0);
        painter.setPen(QPen(palette().mid().color(), 1.0));
        painter.drawRect(frame);

        QVector<PlotPoint> all = m_extendedOutline;
        all += m_outline;
        all += m_slipArc;
        if (all.size() < 2)
        {
            painter.setPen(palette().text().color());
            painter.drawText(frame, Qt::AlignCenter,
                             QStringLiteral("计算完成后在此显示边坡外轮廓和临界滑弧"));
            return;
        }

        double minX = all.first().x;
        double maxX = all.first().x;
        double minY = all.first().y;
        double maxY = all.first().y;
        for (const PlotPoint& point : std::as_const(all))
        {
            minX = std::min(minX, point.x);
            maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
        if (std::abs(maxX - minX) < 1.0e-12)
        {
            minX -= 1.0;
            maxX += 1.0;
        }
        if (std::abs(maxY - minY) < 1.0e-12)
        {
            minY -= 1.0;
            maxY += 1.0;
        }

        const double scale = std::min(frame.width() / (maxX - minX),
                                      frame.height() / (maxY - minY));
        const double usedWidth = (maxX - minX) * scale;
        const double usedHeight = (maxY - minY) * scale;
        const double left = frame.left() + (frame.width() - usedWidth) * 0.5;
        const double top = frame.top() + (frame.height() - usedHeight) * 0.5;
        const auto mapPoint = [&](const PlotPoint& point)
        {
            return QPointF(left + (point.x - minX) * scale,
                           top + (maxY - point.y) * scale);
        };

        painter.setPen(QPen(palette().mid().color(), 1.0, Qt::DotLine));
        for (int i = 1; i < 5; ++i)
        {
            const double x = frame.left() + frame.width() * i / 5.0;
            const double y = frame.top() + frame.height() * i / 5.0;
            painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }

        const auto drawPolyline = [&](const QVector<PlotPoint>& points, const QPen& pen)
        {
            if (points.size() < 2)
                return;
            QPainterPath path(mapPoint(points.first()));
            for (int i = 1; i < points.size(); ++i)
                path.lineTo(mapPoint(points.at(i)));
            painter.setPen(pen);
            painter.drawPath(path);
        };

        drawPolyline(m_extendedOutline, QPen(QColor(125, 125, 125), 1.2, Qt::DashLine));
        drawPolyline(m_outline, QPen(QColor(40, 100, 190), 2.2));
        drawPolyline(m_slipArc, QPen(QColor(210, 60, 55), 2.5));

        painter.setPen(palette().text().color());
        painter.drawText(QRectF(frame.left(), frame.bottom() + 8.0, frame.width(), 24.0),
                         Qt::AlignCenter, QStringLiteral("水平坐标 X"));
        painter.save();
        painter.translate(18.0, frame.center().y());
        painter.rotate(-90.0);
        painter.drawText(QRectF(-frame.height() / 2.0, -12.0, frame.height(), 24.0),
                         Qt::AlignCenter, QStringLiteral("高程 Y"));
        painter.restore();
        if (std::isfinite(m_factor))
            painter.drawText(frame.adjusted(8.0, 6.0, -8.0, -6.0),
                             Qt::AlignTop | Qt::AlignRight,
                             QStringLiteral("安全系数 Fs = %1").arg(displayNumber(m_factor)));
    }

  private:
    QVector<PlotPoint> m_outline;
    QVector<PlotPoint> m_extendedOutline;
    QVector<PlotPoint> m_slipArc;
    double m_factor{std::numeric_limits<double>::quiet_NaN()};
};

SlopeStandardMethodDialog::SlopeStandardMethodDialog(const QString& analysisUuid,
                                                     const QString& datText,
                                                     const QString& lastJobDirectory,
                                                     QWidget* parent)
    : QDialog(parent), m_analysisUuid(sanitizedUuid(analysisUuid)), m_process(new QProcess(this))
{
    buildUi();
    const QString initialDat = datText.trimmed().isEmpty() ? defaultDatText() : datText;
    m_datEditor->setPlainText(initialDat);
    QString formError;
    if (!loadFormFromDat(initialDat, &formError))
    {
        m_datEditor->setPlainText(defaultDatText());
        loadFormFromDat(defaultDatText());
        QTimer::singleShot(0, this, [this, formError]() {
            QMessageBox::warning(
                this, QStringLiteral("规范法输入已恢复为示例"),
                QStringLiteral("工程中保存的规范法 DAT 无法转换为参数表单：\n%1\n"
                               "已载入可运行的默认示例，请检查后再计算。")
                    .arg(formError));
        });
    }
    m_datEditor->document()->setModified(false);

    connect(m_datEditor, &QPlainTextEdit::textChanged, this, [this]() {
        emit datTextChanged(m_datEditor->toPlainText());
    });
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SlopeStandardMethodDialog::consumeProcessOutput);
    connect(m_process, &QProcess::errorOccurred,
            this, &SlopeStandardMethodDialog::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SlopeStandardMethodDialog::handleProcessFinished);

    if (!lastJobDirectory.trimmed().isEmpty())
        loadExistingResult(lastJobDirectory);
}

QString SlopeStandardMethodDialog::defaultDatText()
{
    return QStringLiteral(
        "TWO_LAYER_SLOPE\n"
        "1 0.2\n"
        "2\n"
        "12.8 5 12\n"
        "9.81\n"
        "50\n"
        "4\n"
        "1 2 2\n"
        "2 3 1\n"
        "3 4 1\n"
        "2 5 2\n"
        "9999.0\n"
        "2\n"
        "1 23.0 5.3 19.5 19.5 -1 0.0\n"
        "2 20.0 7.2 19.5 19.5 -1 0.0\n"
        "5\n"
        "1 0.0 0.0\n"
        "2 5.2 4.0\n"
        "3 10.4 8.0\n"
        "4 20.0 8.0\n"
        "5 20.0 4.0\n"
        "3\n"
        "1 0.0\n"
        "2 0.0\n"
        "3 0.0\n"
        "8.0 0 0 0 0 0\n"
        "0\n");
}

QString SlopeStandardMethodDialog::datText() const
{
    SlopeStandardDatModel model;
    return collectFormModel(&model) ? model.serialize()
                                    : (m_datEditor ? m_datEditor->toPlainText() : QString());
}

void SlopeStandardMethodDialog::buildUi()
{
    setWindowTitle(QStringLiteral("边坡规范法验算"));
    resize(1080, 820);
    setModal(false);

    auto* rootLayout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        QStringLiteral("按计算方法、材料、水位和边坡断面填写参数，软件会自动生成求解器输入。"
                       "通常只需检查计算设置和材料参数；节点、边界线可由既有 DAT 导入后修改。"),
        this);
    intro->setWordWrap(true);
    rootLayout->addWidget(intro);

    m_tabs = new QTabWidget(this);
    rootLayout->addWidget(m_tabs, 1);

    const auto makeRealSpin = [](QWidget* parent, double minimum, double maximum,
                                 double value, int decimals = 6)
    {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setDecimals(decimals);
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
        return spin;
    };

    auto* settingsPage = new QScrollArea(m_tabs);
    settingsPage->setWidgetResizable(true);
    auto* settingsContent = new QWidget(settingsPage);
    auto* settingsLayout = new QHBoxLayout(settingsContent);
    auto* settingsLeft = new QVBoxLayout();
    auto* settingsRight = new QVBoxLayout();
    settingsLayout->addLayout(settingsLeft, 1);
    settingsLayout->addLayout(settingsRight, 1);

    auto* calculationGroup = new QGroupBox(QStringLiteral("计算设置"), settingsContent);
    auto* calculationForm = new QFormLayout(calculationGroup);
    m_titleEdit = new QLineEdit(calculationGroup);
    m_titleEdit->setPlaceholderText(QStringLiteral("例如：左岸边坡正常蓄水工况"));
    m_methodCombo = new QComboBox(calculationGroup);
    m_methodCombo->addItem(QStringLiteral("瑞典圆弧法"), 1);
    m_methodCombo->addItem(QStringLiteral("毕肖普法（同时给出瑞典法结果）"), 2);
    m_methodCombo->addItem(QStringLiteral("Sarma 法"), 3);
    m_modeCombo = new QComboBox(calculationGroup);
    m_modeCombo->addItem(QStringLiteral("固定圆弧"), 0);
    m_modeCombo->addItem(QStringLiteral("自动搜索最危险滑弧"), 1);
    m_searchStepSpin = makeRealSpin(calculationGroup, 0.000001, 1000000.0, 0.2);
    m_sliceCountSpin = new QSpinBox(calculationGroup);
    m_sliceCountSpin->setRange(1, 200);
    m_sliceCountSpin->setValue(50);
    m_sliceCountSpin->setSuffix(QStringLiteral(" 条"));
    calculationForm->addRow(QStringLiteral("算例名称"), m_titleEdit);
    calculationForm->addRow(QStringLiteral("计算方法"), m_methodCombo);
    calculationForm->addRow(QStringLiteral("滑弧模式"), m_modeCombo);
    calculationForm->addRow(QStringLiteral("搜索步长 LTH"), m_searchStepSpin);
    calculationForm->addRow(QStringLiteral("竖向条分数"), m_sliceCountSpin);
    settingsLeft->addWidget(calculationGroup);

    auto* circleGroup = new QGroupBox(QStringLiteral("滑弧设置"), settingsContent);
    auto* circleForm = new QFormLayout(circleGroup);
    m_radiusSpin = makeRealSpin(circleGroup, 0.000001, 1000000000.0, 12.8);
    m_circleXSpin = makeRealSpin(circleGroup, -1000000000.0, 1000000000.0, 5.0);
    m_circleYSpin = makeRealSpin(circleGroup, -1000000000.0, 1000000000.0, 12.0);
    auto* circleHint = new QLabel(
        QStringLiteral("固定圆弧时为实际圆心和半径；自动搜索时作为搜索初始值。"), circleGroup);
    circleHint->setWordWrap(true);
    circleForm->addRow(QStringLiteral("半径 R"), m_radiusSpin);
    circleForm->addRow(QStringLiteral("圆心 X"), m_circleXSpin);
    circleForm->addRow(QStringLiteral("圆心 Y"), m_circleYSpin);
    circleForm->addRow(circleHint);
    settingsLeft->addWidget(circleGroup);
    settingsLeft->addStretch();

    auto* waterGroup = new QGroupBox(QStringLiteral("水位与孔隙水压力"), settingsContent);
    auto* waterForm = new QFormLayout(waterGroup);
    m_waterWeightSpin = makeRealSpin(waterGroup, 0.000001, 1000000.0, 9.81);
    m_outsideWaterCheck = new QCheckBox(QStringLiteral("考虑坡外水位"), waterGroup);
    m_outsideWaterSpin = makeRealSpin(waterGroup, -1000000000.0, 1000000000.0, 0.0);
    m_poreMethodCombo = new QComboBox(waterGroup);
    m_poreMethodCombo->addItem(QStringLiteral("总应力法（不使用孔压）"), -1);
    m_poreMethodCombo->addItem(QStringLiteral("浸润线法"), 0);
    m_poreMethodCombo->addItem(QStringLiteral("孔压系数 Ru 法"), 2);
    auto* waterHint = new QLabel(
        QStringLiteral("采用浸润线法时，请在“材料与几何 → 浸润线”中定义线段；"
                       "采用 Ru 法时，在材料表中填写各层 Ru。"), waterGroup);
    waterHint->setWordWrap(true);
    waterForm->addRow(QStringLiteral("水的容重 RW"), m_waterWeightSpin);
    waterForm->addRow(m_outsideWaterCheck);
    waterForm->addRow(QStringLiteral("坡外水面绝对高程"), m_outsideWaterSpin);
    waterForm->addRow(QStringLiteral("孔压计算方法"), m_poreMethodCombo);
    waterForm->addRow(waterHint);
    settingsRight->addWidget(waterGroup);

    auto* earthquakeGroup = new QGroupBox(QStringLiteral("地震工况"), settingsContent);
    auto* earthquakeForm = new QFormLayout(earthquakeGroup);
    m_earthquakeCheck = new QCheckBox(QStringLiteral("考虑地震作用"), earthquakeGroup);
    m_slopeHeightSpin = makeRealSpin(earthquakeGroup, 0.0, 1000000000.0, 8.0);
    m_earthquakeIntensitySpin = new QSpinBox(earthquakeGroup);
    m_earthquakeIntensitySpin->setRange(0, 20);
    m_earthquakeModeCombo = new QComboBox(earthquakeGroup);
    m_earthquakeModeCombo->addItem(QStringLiteral("仅水平向 H"), 0);
    m_earthquakeModeCombo->addItem(QStringLiteral("仅竖向 V"), 1);
    m_earthquakeModeCombo->addItem(QStringLiteral("水平向 + 竖向"), 2);
    const auto fillDirection = [](QComboBox* combo)
    {
        combo->addItem(QStringLiteral("负方向"), -1);
        combo->addItem(QStringLiteral("不考虑"), 0);
        combo->addItem(QStringLiteral("正方向"), 1);
        combo->setCurrentIndex(1);
    };
    m_earthquakeDirectionHCombo = new QComboBox(earthquakeGroup);
    m_earthquakeDirectionVCombo = new QComboBox(earthquakeGroup);
    fillDirection(m_earthquakeDirectionHCombo);
    fillDirection(m_earthquakeDirectionVCombo);
    earthquakeForm->addRow(m_earthquakeCheck);
    earthquakeForm->addRow(QStringLiteral("实际边坡高度"), m_slopeHeightSpin);
    earthquakeForm->addRow(QStringLiteral("地震烈度 LD"), m_earthquakeIntensitySpin);
    earthquakeForm->addRow(QStringLiteral("方向组合"), m_earthquakeModeCombo);
    earthquakeForm->addRow(QStringLiteral("水平向方向"), m_earthquakeDirectionHCombo);
    earthquakeForm->addRow(QStringLiteral("竖向方向"), m_earthquakeDirectionVCombo);
    settingsRight->addWidget(earthquakeGroup);
    settingsRight->addStretch();
    settingsPage->setWidget(settingsContent);
    m_tabs->addTab(settingsPage, QStringLiteral("计算设置"));

    auto* dataPage = new QWidget(m_tabs);
    auto* dataLayout = new QVBoxLayout(dataPage);
    auto* dataHint = new QLabel(
        QStringLiteral(
            "<b>这里描述的是二维边坡断面，不是三维实体。</b><br>"
            "填写顺序：①定义土层强度和容重 → ②用坐标点画出断面 → "
            "③用边界线连接点，并指定每条线<b>下方是哪一层土</b> → "
            "④从边界线中选出真正暴露在外的坡面 → ⑤有地下水时再定义浸润线。"),
        dataPage);
    dataHint->setTextFormat(Qt::RichText);
    dataHint->setWordWrap(true);
    dataLayout->addWidget(dataHint);
    auto* geometryActions = new QHBoxLayout();
    auto* extractSectionButton =
        new QPushButton(QStringLiteral("从当前三维网格提取剖面"), dataPage);
    auto* importGeometryButton = new QPushButton(QStringLiteral("导入已有断面 DAT"), dataPage);
    auto* exampleGeometryButton = new QPushButton(QStringLiteral("载入两层边坡示例"), dataPage);
    geometryActions->addWidget(extractSectionButton);
    geometryActions->addWidget(importGeometryButton);
    geometryActions->addWidget(exampleGeometryButton);
    geometryActions->addStretch();
    dataLayout->addLayout(geometryActions);

    auto* dataSplitter = new QSplitter(Qt::Horizontal, dataPage);
    auto* dataTabs = new QTabWidget(dataSplitter);
    auto* previewPane = new QWidget(dataSplitter);
    auto* previewLayout = new QVBoxLayout(previewPane);
    auto* previewHint = new QLabel(
        QStringLiteral("图中“下方土层”决定该边界以下采用哪组 φ、c 和容重。"
                       "蓝色粗线是滑弧与坡面求交时使用的外坡面；内部层间线不要选为外坡面。"),
        previewPane);
    previewHint->setWordWrap(true);
    previewLayout->addWidget(previewHint);
    m_inputPreview = new SlopeStandardInputPreviewWidget(previewPane);
    previewLayout->addWidget(m_inputPreview, 1);
    dataSplitter->addWidget(dataTabs);
    dataSplitter->addWidget(previewPane);
    dataSplitter->setStretchFactor(0, 3);
    dataSplitter->setStretchFactor(1, 4);
    dataLayout->addWidget(dataSplitter, 1);

    const auto createTablePage = [this, dataTabs](QTableWidget** target,
                                                   const QStringList& headers,
                                                   const QString& title,
                                                   const QString& hint)
    {
        auto* page = new QWidget(dataTabs);
        auto* layout = new QVBoxLayout(page);
        auto* label = new QLabel(hint, page);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto* table = new QTableWidget(page);
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        layout->addWidget(table, 1);
        auto* buttons = new QHBoxLayout();
        auto* addButton = new QPushButton(QStringLiteral("添加一行"), page);
        auto* removeButton = new QPushButton(QStringLiteral("删除选中行"), page);
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addStretch();
        layout->addLayout(buttons);
        *target = table;
        connect(addButton, &QPushButton::clicked, this, [this, table]() {
            addTableRow(table);
            emit datTextChanged(datText());
        });
        connect(removeButton, &QPushButton::clicked, this, [this, table]() {
            removeSelectedRows(table);
            emit datTextChanged(datText());
        });
        dataTabs->addTab(page, title);
    };

    createTablePage(&m_soilTable,
                    {QStringLiteral("土层（自动编号）"), QStringLiteral("内摩擦角 φ (°)"),
                     QStringLiteral("黏聚力 c"), QStringLiteral("天然容重 γ"),
                     QStringLiteral("饱和容重 γsat"), QStringLiteral("孔压系数 Ru")},
                    QStringLiteral("① 土层参数"),
                    QStringLiteral("一行代表一种土或岩体。φ、c 决定抗剪强度；γ/γsat 用来计算土条自重；"
                                   "Ru 仅在“孔压系数法”中使用。要求 φ、c、Ru≥0，γ>0，且 γsat≥γ。"));
    createTablePage(&m_nodeTable,
                    {QStringLiteral("ID"), QStringLiteral("X 坐标"), QStringLiteral("Y 高程")},
                    QStringLiteral("② 断面坐标点"),
                    QStringLiteral("每个点是二维断面上的一个折点。X 是水平距离，Y 是高程；ID 自动生成。"));
    createTablePage(&m_segmentTable,
                    {QStringLiteral("起点 ID"), QStringLiteral("终点 ID"),
                     QStringLiteral("线段下方土层 ID")},
                    QStringLiteral("③ 边界与分层线"),
                    QStringLiteral("连接两个坐标点形成坡面或层间界线。第三列非常重要："
                                   "它表示从这条线向下进入哪一层土，不是线本身的材料。"));
    createTablePage(&m_surfaceTable,
                    {QStringLiteral("外坡面对应的边界线 ID"), QStringLiteral("坡面荷载")},
                    QStringLiteral("④ 计算外坡面"),
                    QStringLiteral("只列出与空气或坡外水接触的实际坡面线段；内部土层分界线不能选。"
                                   "没有坡面荷载时填 0，选中的线会在右图显示为蓝色粗线。"));
    createTablePage(&m_phreaticTable,
                    {QStringLiteral("起点 ID"), QStringLiteral("终点 ID"),
                     QStringLiteral("线段下方土层 ID")},
                    QStringLiteral("⑤ 地下水浸润线"),
                    QStringLiteral("仅“浸润线法”需要。用已有坐标点连线描述地下水自由面，"
                                   "右图以青色虚线显示。"));
    m_tabs->addTab(dataPage, QStringLiteral("土层与断面"));

    auto* advancedPage = new QWidget(m_tabs);
    auto* advancedLayout = new QVBoxLayout(advancedPage);
    auto* inputTools = new QHBoxLayout();
    auto* importButton = new QPushButton(QStringLiteral("导入 DAT"), advancedPage);
    auto* exportButton = new QPushButton(QStringLiteral("导出 DAT"), advancedPage);
    auto* applyDatButton = new QPushButton(QStringLiteral("将 DAT 应用到参数表单"), advancedPage);
    auto* sampleButton = new QPushButton(QStringLiteral("载入毕肖普示例"), advancedPage);
    inputTools->addWidget(importButton);
    inputTools->addWidget(exportButton);
    inputTools->addWidget(applyDatButton);
    inputTools->addWidget(sampleButton);
    inputTools->addStretch();
    advancedLayout->addLayout(inputTools);
    auto* formatHint = new QLabel(
        QStringLiteral("高级功能：兼容客户旧算例或排查输入。普通用户无需编辑 DAT；"
                       "手工修改后请先点击“将 DAT 应用到参数表单”。"), advancedPage);
    formatHint->setWordWrap(true);
    advancedLayout->addWidget(formatHint);
    m_datEditor = new QPlainTextEdit(advancedPage);
    m_datEditor->setObjectName(QStringLiteral("standardMethodDatEditor"));
    m_datEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_datEditor->setFont(fixedFont);
    advancedLayout->addWidget(m_datEditor, 1);
    m_tabs->addTab(advancedPage, QStringLiteral("高级 DAT"));

    auto* resultPage = new QWidget(m_tabs);
    auto* resultLayout = new QVBoxLayout(resultPage);
    m_summaryLabel = new QLabel(QStringLiteral("尚未进行规范法计算。"), resultPage);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultLayout->addWidget(m_summaryLabel);

    auto* splitter = new QSplitter(Qt::Vertical, resultPage);
    m_plotWidget = new SlopeStandardPlotWidget(splitter);
    m_resultViewer = new QPlainTextEdit(splitter);
    m_resultViewer->setReadOnly(true);
    m_resultViewer->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_resultViewer->setFont(fixedFont);
    m_resultViewer->setPlaceholderText(QStringLiteral("result.dat 详细计算报告"));
    splitter->addWidget(m_plotWidget);
    splitter->addWidget(m_resultViewer);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    resultLayout->addWidget(splitter, 1);
    m_tabs->addTab(resultPage, QStringLiteral("验算结果"));

    auto* stateRow = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumWidth(180);
    m_stateLabel = new QLabel(QStringLiteral("就绪"), this);
    m_stateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stateRow->addWidget(m_progressBar);
    stateRow->addWidget(m_stateLabel, 1);
    rootLayout->addLayout(stateRow);

    auto* buttonRow = new QHBoxLayout();
    m_openDirectoryButton = new QPushButton(QStringLiteral("打开结果目录"), this);
    m_openDirectoryButton->setEnabled(false);
    m_validateButton = new QPushButton(QStringLiteral("校验输入"), this);
    m_calculateButton = new QPushButton(QStringLiteral("开始验算"), this);
    m_stopButton = new QPushButton(QStringLiteral("停止"), this);
    m_stopButton->setEnabled(false);
    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
    buttonRow->addWidget(m_openDirectoryButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_validateButton);
    buttonRow->addWidget(m_calculateButton);
    buttonRow->addWidget(m_stopButton);
    buttonRow->addWidget(closeButton);
    rootLayout->addLayout(buttonRow);

    connect(importButton, &QPushButton::clicked, this, &SlopeStandardMethodDialog::importDat);
    connect(extractSectionButton, &QPushButton::clicked,
            this, &SlopeStandardMethodDialog::extractSectionFromCurrentMesh);
    connect(importGeometryButton, &QPushButton::clicked,
            this, &SlopeStandardMethodDialog::importDat);
    connect(exportButton, &QPushButton::clicked, this, &SlopeStandardMethodDialog::exportDat);
    connect(applyDatButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!loadFormFromDat(m_datEditor->toPlainText(), &error))
        {
            QMessageBox::warning(this, QStringLiteral("DAT 无法应用"), error);
            return;
        }
        m_datEditor->document()->setModified(false);
        m_tabs->setCurrentIndex(0);
        m_stateLabel->setText(QStringLiteral("DAT 已转换为参数表单"));
        emit datTextChanged(datText());
    });
    const auto loadExample = [this]() {
        if (QMessageBox::question(this, QStringLiteral("载入示例"),
                                  QStringLiteral("当前规范法参数将被毕肖普自动搜索示例替换，是否继续？"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        m_datEditor->setPlainText(defaultDatText());
        loadFormFromDat(defaultDatText());
        m_datEditor->document()->setModified(false);
        m_tabs->setCurrentIndex(1);
        updateInputPreview();
    };
    connect(sampleButton, &QPushButton::clicked, this, loadExample);
    connect(exampleGeometryButton, &QPushButton::clicked, this, loadExample);
    connect(m_validateButton, &QPushButton::clicked, this,
            [this]() { startOperation(Operation::Validate); });
    connect(m_calculateButton, &QPushButton::clicked, this,
            [this]() { startOperation(Operation::Calculate); });
    connect(m_stopButton, &QPushButton::clicked, this, &SlopeStandardMethodDialog::stopProcess);
    connect(m_openDirectoryButton, &QPushButton::clicked,
            this, &SlopeStandardMethodDialog::openOutputDirectory);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    const auto formChanged = [this]() {
        if (m_stateLabel)
            m_stateLabel->setText(QStringLiteral("参数已修改，等待校验或计算"));
        updateInputPreview();
        emit datTextChanged(datText());
    };
    connect(m_titleEdit, &QLineEdit::textChanged, this, formChanged);
    connect(m_methodCombo, &QComboBox::currentIndexChanged, this, formChanged);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this, formChanged]() {
        updateControlAvailability();
        formChanged();
    });
    connect(m_searchStepSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_sliceCountSpin, &QSpinBox::valueChanged, this, formChanged);
    connect(m_radiusSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_circleXSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_circleYSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_waterWeightSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_outsideWaterCheck, &QCheckBox::toggled, this, [this, formChanged]() {
        updateControlAvailability();
        formChanged();
    });
    connect(m_outsideWaterSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_poreMethodCombo, &QComboBox::currentIndexChanged, this,
            [this, formChanged]() {
                updateControlAvailability();
                formChanged();
            });
    connect(m_earthquakeCheck, &QCheckBox::toggled, this, [this, formChanged]() {
        updateControlAvailability();
        formChanged();
    });
    connect(m_slopeHeightSpin, &QDoubleSpinBox::valueChanged, this, formChanged);
    connect(m_earthquakeIntensitySpin, &QSpinBox::valueChanged, this, formChanged);
    connect(m_earthquakeModeCombo, &QComboBox::currentIndexChanged, this, formChanged);
    connect(m_earthquakeDirectionHCombo, &QComboBox::currentIndexChanged, this, formChanged);
    connect(m_earthquakeDirectionVCombo, &QComboBox::currentIndexChanged, this, formChanged);
    for (QTableWidget* table : {m_soilTable, m_nodeTable, m_segmentTable,
                                m_surfaceTable, m_phreaticTable})
        connect(table, &QTableWidget::itemChanged, this, formChanged);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != 2 || m_datEditor->document()->isModified())
            return;
        QString error;
        if (!syncDatFromForm(&error))
            m_stateLabel->setText(QStringLiteral("参数尚不完整：%1").arg(error));
    });
    updateControlAvailability();
}

bool SlopeStandardMethodDialog::loadFormFromDat(const QString& text, QString* errorMessage)
{
    SlopeStandardDatModel model;
    if (!SlopeStandardDatModel::parse(text, &model, errorMessage))
        return false;

    const QList<QObject*> objects = {
        m_titleEdit, m_methodCombo, m_modeCombo, m_searchStepSpin, m_sliceCountSpin,
        m_radiusSpin, m_circleXSpin, m_circleYSpin, m_waterWeightSpin,
        m_outsideWaterCheck, m_outsideWaterSpin, m_poreMethodCombo, m_earthquakeCheck,
        m_slopeHeightSpin, m_earthquakeIntensitySpin, m_earthquakeModeCombo,
        m_earthquakeDirectionHCombo, m_earthquakeDirectionVCombo, m_soilTable,
        m_nodeTable, m_segmentTable, m_surfaceTable, m_phreaticTable, m_datEditor};
    for (QObject* object : objects)
        object->blockSignals(true);

    const auto selectData = [](QComboBox* combo, int value)
    {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    const auto setCell = [](QTableWidget* table, int row, int column,
                            const QString& value, bool editable = true)
    {
        auto* item = new QTableWidgetItem(value);
        if (!editable)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    };

    m_titleEdit->setText(model.projectTitle);
    selectData(m_methodCombo, model.method);
    selectData(m_modeCombo, model.mode);
    m_searchStepSpin->setValue(model.searchStep);
    m_sliceCountSpin->setValue(model.sliceCount);
    m_radiusSpin->setValue(model.radius);
    m_circleXSpin->setValue(model.circleX);
    m_circleYSpin->setValue(model.circleY);
    m_waterWeightSpin->setValue(model.waterUnitWeight);
    const bool hasOutsideWater = std::abs(model.outsideWaterLevel - 9999.0) > 1.0e-8;
    m_outsideWaterCheck->setChecked(hasOutsideWater);
    m_outsideWaterSpin->setValue(hasOutsideWater ? model.outsideWaterLevel : 0.0);
    selectData(m_poreMethodCombo, model.poreMethod());
    m_earthquakeCheck->setChecked(model.earthquakeEnabled != 0);
    m_slopeHeightSpin->setValue(model.slopeHeight);
    m_earthquakeIntensitySpin->setValue(model.earthquakeIntensity);
    selectData(m_earthquakeModeCombo, model.earthquakeMode);
    selectData(m_earthquakeDirectionHCombo, model.earthquakeDirectionH);
    selectData(m_earthquakeDirectionVCombo, model.earthquakeDirectionV);

    m_soilTable->setRowCount(model.soils.size());
    for (int row = 0; row < model.soils.size(); ++row)
    {
        const auto& item = model.soils.at(row);
        setCell(m_soilTable, row, 0, QStringLiteral("土层 %1").arg(row + 1), false);
        setCell(m_soilTable, row, 1, displayNumber(item.frictionAngle));
        setCell(m_soilTable, row, 2, displayNumber(item.cohesion));
        setCell(m_soilTable, row, 3, displayNumber(item.naturalUnitWeight));
        setCell(m_soilTable, row, 4, displayNumber(item.saturatedUnitWeight));
        setCell(m_soilTable, row, 5, displayNumber(item.ru));
    }
    m_nodeTable->setRowCount(model.nodes.size());
    for (int row = 0; row < model.nodes.size(); ++row)
    {
        const auto& item = model.nodes.at(row);
        setCell(m_nodeTable, row, 0, QString::number(row + 1), false);
        setCell(m_nodeTable, row, 1, displayNumber(item.x));
        setCell(m_nodeTable, row, 2, displayNumber(item.y));
    }
    m_segmentTable->setRowCount(model.segments.size());
    for (int row = 0; row < model.segments.size(); ++row)
    {
        const auto& item = model.segments.at(row);
        setCell(m_segmentTable, row, 0, QString::number(item.startNode));
        setCell(m_segmentTable, row, 1, QString::number(item.endNode));
        setCell(m_segmentTable, row, 2, QString::number(item.soilId));
    }
    m_surfaceTable->setRowCount(model.surfaceLoads.size());
    for (int row = 0; row < model.surfaceLoads.size(); ++row)
    {
        const auto& item = model.surfaceLoads.at(row);
        setCell(m_surfaceTable, row, 0, QString::number(item.segmentId));
        setCell(m_surfaceTable, row, 1, displayNumber(item.force));
    }
    m_phreaticTable->setRowCount(model.phreaticSegments.size());
    for (int row = 0; row < model.phreaticSegments.size(); ++row)
    {
        const auto& item = model.phreaticSegments.at(row);
        setCell(m_phreaticTable, row, 0, QString::number(item.startNode));
        setCell(m_phreaticTable, row, 1, QString::number(item.endNode));
        setCell(m_phreaticTable, row, 2, QString::number(item.soilId));
    }
    m_datEditor->setPlainText(model.serialize());
    m_datEditor->document()->setModified(false);

    for (QObject* object : objects)
        object->blockSignals(false);
    updateControlAvailability();
    updateInputPreview();
    return true;
}

bool SlopeStandardMethodDialog::collectFormModel(SlopeStandardDatModel* model,
                                                 QString* errorMessage) const
{
    if (!model)
        return false;
    SlopeStandardDatModel value;
    value.projectTitle = m_titleEdit->text().trimmed();
    value.method = m_methodCombo->currentData().toInt();
    value.mode = m_modeCombo->currentData().toInt();
    value.searchStep = m_searchStepSpin->value();
    value.sliceCount = m_sliceCountSpin->value();
    value.radius = m_radiusSpin->value();
    value.circleX = m_circleXSpin->value();
    value.circleY = m_circleYSpin->value();
    value.waterUnitWeight = m_waterWeightSpin->value();
    value.outsideWaterLevel = m_outsideWaterCheck->isChecked()
                                  ? m_outsideWaterSpin->value()
                                  : 9999.0;
    value.slopeHeight = m_slopeHeightSpin->value();
    value.earthquakeEnabled = m_earthquakeCheck->isChecked() ? 1 : 0;
    value.earthquakeIntensity = m_earthquakeIntensitySpin->value();
    value.earthquakeMode = m_earthquakeModeCombo->currentData().toInt();
    value.earthquakeDirectionH = m_earthquakeDirectionHCombo->currentData().toInt();
    value.earthquakeDirectionV = m_earthquakeDirectionVCombo->currentData().toInt();
    const int poreMethod = m_poreMethodCombo->currentData().toInt();

    const auto cellText = [](const QTableWidget* table, int row, int column)
    {
        const QTableWidgetItem* item = table->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    const auto readInt = [&](const QTableWidget* table, int row, int column,
                             const QString& field, int* result)
    {
        const QString text = cellText(table, row, column);
        bool ok = false;
        const int parsed = text.toInt(&ok);
        if (!ok)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("%1第 %2 行“%3”必须填写整数。")
                                    .arg(table == m_soilTable ? QStringLiteral("土层参数表")
                                         : table == m_nodeTable ? QStringLiteral("节点表")
                                         : table == m_segmentTable ? QStringLiteral("边界线表")
                                         : table == m_surfaceTable ? QStringLiteral("外坡面表")
                                                                   : QStringLiteral("浸润线表"))
                                    .arg(row + 1)
                                    .arg(field);
            return false;
        }
        *result = parsed;
        return true;
    };
    const auto readReal = [&](const QTableWidget* table, int row, int column,
                              const QString& field, double* result)
    {
        QString text = cellText(table, row, column);
        text.replace(QLatin1Char('D'), QLatin1Char('E'), Qt::CaseInsensitive);
        bool ok = false;
        const double parsed = text.toDouble(&ok);
        if (!ok || !std::isfinite(parsed))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("第 %1 行“%2”必须填写有效数值。")
                                    .arg(row + 1)
                                    .arg(field);
            return false;
        }
        *result = parsed;
        return true;
    };

    value.soils.reserve(m_soilTable->rowCount());
    for (int row = 0; row < m_soilTable->rowCount(); ++row)
    {
        SlopeStandardSoil item;
        item.id = row + 1;
        item.poreMethod = poreMethod;
        if (!readReal(m_soilTable, row, 1, QStringLiteral("内摩擦角"), &item.frictionAngle)
            || !readReal(m_soilTable, row, 2, QStringLiteral("黏聚力"), &item.cohesion)
            || !readReal(m_soilTable, row, 3, QStringLiteral("天然容重"), &item.naturalUnitWeight)
            || !readReal(m_soilTable, row, 4, QStringLiteral("饱和容重"), &item.saturatedUnitWeight)
            || !readReal(m_soilTable, row, 5, QStringLiteral("Ru"), &item.ru))
            return false;
        value.soils.append(item);
    }
    value.nodes.reserve(m_nodeTable->rowCount());
    for (int row = 0; row < m_nodeTable->rowCount(); ++row)
    {
        SlopeStandardNode item;
        item.id = row + 1;
        if (!readReal(m_nodeTable, row, 1, QStringLiteral("X 坐标"), &item.x)
            || !readReal(m_nodeTable, row, 2, QStringLiteral("Y 高程"), &item.y))
            return false;
        value.nodes.append(item);
    }
    value.segments.reserve(m_segmentTable->rowCount());
    for (int row = 0; row < m_segmentTable->rowCount(); ++row)
    {
        SlopeStandardSegment item;
        if (!readInt(m_segmentTable, row, 0, QStringLiteral("起点 ID"), &item.startNode)
            || !readInt(m_segmentTable, row, 1, QStringLiteral("终点 ID"), &item.endNode)
            || !readInt(m_segmentTable, row, 2, QStringLiteral("材料 ID"), &item.soilId))
            return false;
        value.segments.append(item);
    }
    value.surfaceLoads.reserve(m_surfaceTable->rowCount());
    for (int row = 0; row < m_surfaceTable->rowCount(); ++row)
    {
        SlopeStandardSurfaceLoad item;
        if (!readInt(m_surfaceTable, row, 0, QStringLiteral("边界线 ID"), &item.segmentId)
            || !readReal(m_surfaceTable, row, 1, QStringLiteral("坡面荷载"), &item.force))
            return false;
        value.surfaceLoads.append(item);
    }
    value.phreaticSegments.reserve(m_phreaticTable->rowCount());
    for (int row = 0; row < m_phreaticTable->rowCount(); ++row)
    {
        SlopeStandardSegment item;
        if (!readInt(m_phreaticTable, row, 0, QStringLiteral("起点 ID"), &item.startNode)
            || !readInt(m_phreaticTable, row, 1, QStringLiteral("终点 ID"), &item.endNode)
            || !readInt(m_phreaticTable, row, 2, QStringLiteral("材料 ID"), &item.soilId))
            return false;
        value.phreaticSegments.append(item);
    }

    if (!value.validate(errorMessage))
        return false;
    *model = value;
    return true;
}

bool SlopeStandardMethodDialog::syncDatFromForm(QString* errorMessage)
{
    SlopeStandardDatModel model;
    if (!collectFormModel(&model, errorMessage))
        return false;
    const QSignalBlocker blocker(m_datEditor);
    m_datEditor->setPlainText(model.serialize());
    m_datEditor->document()->setModified(false);
    emit datTextChanged(model.serialize());
    return true;
}

void SlopeStandardMethodDialog::updateControlAvailability()
{
    const bool searching = m_modeCombo->currentData().toInt() == 1;
    m_searchStepSpin->setEnabled(searching);
    m_outsideWaterSpin->setEnabled(m_outsideWaterCheck->isChecked());

    const bool earthquake = m_earthquakeCheck->isChecked();
    for (QWidget* widget : {static_cast<QWidget*>(m_earthquakeIntensitySpin),
                            static_cast<QWidget*>(m_earthquakeModeCombo),
                            static_cast<QWidget*>(m_earthquakeDirectionHCombo),
                            static_cast<QWidget*>(m_earthquakeDirectionVCombo)})
        widget->setEnabled(earthquake);

    const int poreMethod = m_poreMethodCombo->currentData().toInt();
    m_phreaticTable->setEnabled(poreMethod == 0);
    for (int row = 0; row < m_soilTable->rowCount(); ++row)
    {
        QTableWidgetItem* ruItem = m_soilTable->item(row, 5);
        if (!ruItem)
            continue;
        Qt::ItemFlags flags = ruItem->flags();
        if (poreMethod == 2)
            flags |= Qt::ItemIsEditable | Qt::ItemIsEnabled;
        else
            flags &= ~Qt::ItemIsEditable;
        ruItem->setFlags(flags);
        ruItem->setBackground(poreMethod == 2 ? QBrush() : palette().alternateBase());
    }
}

void SlopeStandardMethodDialog::updateInputPreview()
{
    if (!m_inputPreview)
        return;
    SlopeStandardDatModel model;
    QString error;
    if (collectFormModel(&model, &error))
        m_inputPreview->setModel(model);
    else
        m_inputPreview->setError(error);
}

void SlopeStandardMethodDialog::addTableRow(QTableWidget* table)
{
    if (!table)
        return;
    const QSignalBlocker blocker(table);
    const int row = table->rowCount();
    table->insertRow(row);
    QStringList defaults;
    if (table == m_soilTable)
        defaults = {QStringLiteral("土层 %1").arg(row + 1), QStringLiteral("20"), QStringLiteral("5"),
                    QStringLiteral("19"), QStringLiteral("20"), QStringLiteral("0")};
    else if (table == m_nodeTable)
    {
        double x = 0.0;
        double y = 0.0;
        if (row > 0)
        {
            bool xOk = false;
            bool yOk = false;
            x = table->item(row - 1, 1)->text().toDouble(&xOk);
            y = table->item(row - 1, 2)->text().toDouble(&yOk);
            x = xOk ? x + 1.0 : 0.0;
            y = yOk ? y : 0.0;
        }
        defaults = {QString::number(row + 1), displayNumber(x), displayNumber(y)};
    }
    else if (table == m_surfaceTable)
        defaults = {QString::number(std::max(1, std::min(row + 1, m_segmentTable->rowCount()))),
                    QStringLiteral("0")};
    else
    {
        const int endNode = std::max(1, m_nodeTable->rowCount());
        const int startNode = std::max(1, endNode - 1);
        defaults = {QString::number(startNode), QString::number(endNode), QStringLiteral("1")};
    }
    for (int column = 0; column < table->columnCount(); ++column)
    {
        auto* item = new QTableWidgetItem(defaults.value(column));
        if ((table == m_soilTable || table == m_nodeTable) && column == 0)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }
    renumberIdentityColumn(table);
    updateControlAvailability();
    updateInputPreview();
}

void SlopeStandardMethodDialog::removeSelectedRows(QTableWidget* table)
{
    if (!table)
        return;
    QList<int> rows;
    for (const QModelIndex& index : table->selectionModel()->selectedRows())
        rows.append(index.row());
    if (rows.isEmpty())
        return;
    QSet<int> removedIds;
    for (int row : rows)
        removedIds.insert(row + 1);
    const auto cellInt = [](const QTableWidget* source, int row, int column)
    {
        const QTableWidgetItem* item = source->item(row, column);
        return item ? item->text().trimmed().toInt() : 0;
    };
    const auto referenced = [&](const QTableWidget* source, const QList<int>& columns)
    {
        for (int row = 0; row < source->rowCount(); ++row)
            for (int column : columns)
                if (removedIds.contains(cellInt(source, row, column)))
                    return true;
        return false;
    };
    if (table == m_nodeTable
        && (referenced(m_segmentTable, {0, 1}) || referenced(m_phreaticTable, {0, 1})))
    {
        QMessageBox::information(
            this, QStringLiteral("节点仍在使用"),
            QStringLiteral("选中的节点仍被边界线或浸润线引用。请先删除或修改相应线段，再删除节点。"));
        return;
    }
    if (table == m_soilTable
        && (referenced(m_segmentTable, {2}) || referenced(m_phreaticTable, {2})))
    {
        QMessageBox::information(
            this, QStringLiteral("土层仍在使用"),
            QStringLiteral("选中的土层仍被边界线或浸润线引用。请先修改线段的“下方土层”，再删除土层。"));
        return;
    }
    if (table == m_segmentTable && referenced(m_surfaceTable, {0}))
    {
        QMessageBox::information(
            this, QStringLiteral("边界线仍在使用"),
            QStringLiteral("选中的边界线仍被“计算外坡面”引用。请先从外坡面列表中删除对应行。"));
        return;
    }

    std::sort(rows.begin(), rows.end(), std::greater<int>());
    const QSignalBlocker soilBlocker(m_soilTable);
    const QSignalBlocker nodeBlocker(m_nodeTable);
    const QSignalBlocker segmentBlocker(m_segmentTable);
    const QSignalBlocker surfaceBlocker(m_surfaceTable);
    const QSignalBlocker phreaticBlocker(m_phreaticTable);
    for (int row : rows)
        table->removeRow(row);

    const auto remapReferences = [&](QTableWidget* target, const QList<int>& columns)
    {
        for (int row = 0; row < target->rowCount(); ++row)
        {
            for (int column : columns)
            {
                QTableWidgetItem* item = target->item(row, column);
                if (!item)
                    continue;
                bool ok = false;
                const int oldId = item->text().trimmed().toInt(&ok);
                if (!ok)
                    continue;
                int shift = 0;
                for (int removedId : removedIds)
                    if (removedId < oldId)
                        ++shift;
                item->setText(QString::number(oldId - shift));
            }
        }
    };
    if (table == m_nodeTable)
    {
        remapReferences(m_segmentTable, {0, 1});
        remapReferences(m_phreaticTable, {0, 1});
    }
    else if (table == m_soilTable)
    {
        remapReferences(m_segmentTable, {2});
        remapReferences(m_phreaticTable, {2});
    }
    else if (table == m_segmentTable)
        remapReferences(m_surfaceTable, {0});
    renumberIdentityColumn(table);
    updateControlAvailability();
    updateInputPreview();
}

void SlopeStandardMethodDialog::renumberIdentityColumn(QTableWidget* table)
{
    if (table != m_soilTable && table != m_nodeTable)
        return;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QTableWidgetItem* item = table->item(row, 0);
        if (!item)
        {
            item = new QTableWidgetItem();
            table->setItem(row, 0, item);
        }
        item->setText(table == m_soilTable ? QStringLiteral("土层 %1").arg(row + 1)
                                           : QString::number(row + 1));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }
}

void SlopeStandardMethodDialog::extractSectionFromCurrentMesh()
{
    auto* meshService =
        ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
    {
        QMessageBox::warning(this, QStringLiteral("无法提取剖面"),
                             QStringLiteral("当前工程没有可用的网格服务。"));
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> gridHolder =
        meshService->getVtkUnstructuredGridSolid();
    if (!gridHolder || gridHolder->GetNumberOfCells() <= 0)
        gridHolder = meshService->getVtkUnstructuredGrid();
    vtkUnstructuredGrid* grid = gridHolder;
    if (!grid || grid->GetNumberOfCells() <= 0 || grid->GetNumberOfPoints() <= 0)
    {
        QMessageBox::information(this, QStringLiteral("尚无网格"),
                                 QStringLiteral("请先导入或生成三维实体网格，再提取规范法剖面。"));
        return;
    }

    const MshPickMap pickMap = meshService->mshPickMapSolid();
    if (pickMap.meshDim > 0 && pickMap.meshDim < 3)
    {
        QMessageBox::information(
            this, QStringLiteral("当前不是三维实体网格"),
            QStringLiteral("当前网格本身已经是二维网格，无需再用平面剖切。"
                           "本功能用于从三维实体网格生成二维规范法断面。"));
        return;
    }

    double bounds[6]{};
    grid->GetBounds(bounds);
    // MeshModuleControllerService follows the IIIN convention: 1=X, 2=Y, 3=Z.
    // VTK coordinate arrays are zero based. Keeping these conventions separate is
    // essential; treating IIIN value 2 as VTK axis 2 turns an X-Y slope into X-Z.
    int reportedVerticalAxis = meshService->verticalAxis() - 1;
    if (reportedVerticalAxis < 0 || reportedVerticalAxis > 2)
        reportedVerticalAxis = 1;

    QDialog options(this);
    options.setWindowTitle(QStringLiteral("从当前网格提取二维剖面"));
    options.resize(560, 0);
    auto* optionsLayout = new QVBoxLayout(&options);
    auto* explanation = new QLabel(
        QStringLiteral("选择一个竖直剖面。程序会截取实体网格与该平面的交线，"
                       "自动识别外坡面和材料分界，并投影为“水平距离—高程”的二维断面。"),
        &options);
    explanation->setWordWrap(true);
    optionsLayout->addWidget(explanation);
    auto* form = new QFormLayout();
    auto* verticalCombo = new QComboBox(&options);
    for (int axis = 0; axis < 3; ++axis)
    {
        verticalCombo->addItem(
            axis == reportedVerticalAxis
                ? QStringLiteral("%1（工程设置，推荐）").arg(axisName(axis))
                : axisName(axis),
            axis);
    }
    verticalCombo->setCurrentIndex(reportedVerticalAxis);
    verticalCombo->setToolTip(
        QStringLiteral("高程增加的方向。IIIN 中 1/2/3 分别对应 X/Y/Z。"
                       "如果预览把模型宽度分成了水平土层，请检查这里。"));
    auto* planeCombo = new QComboBox(&options);
    auto* positionSpin = new QDoubleSpinBox(&options);
    positionSpin->setDecimals(8);
    positionSpin->setKeyboardTracking(false);
    auto* materialCombo = new QComboBox(&options);

    QString materialCategory = meshService->materialGroupCategory().trimmed();
    const QVector<MeshTagSet> domainSets = meshService->domainTagSets();
    const auto categoryHasDomains = [&](const QString& category)
    {
        for (const MeshTagSet& set : domainSets)
            if (set.key.category.compare(category, Qt::CaseInsensitive) == 0
                && set.key.id != 0 && !set.cellIds.isEmpty())
                return true;
        return false;
    };
    if (materialCategory.isEmpty() || !categoryHasDomains(materialCategory))
    {
        for (const QString& candidate : {QStringLiteral("MATYPE"),
                                         QStringLiteral("REGION"),
                                         QStringLiteral("Physical")})
        {
            if (categoryHasDomains(candidate))
            {
                materialCategory = candidate;
                break;
            }
        }
    }
    if (!materialCategory.isEmpty() && categoryHasDomains(materialCategory))
    {
        materialCombo->addItem(
            QStringLiteral("工程材料分组：%1（推荐）").arg(materialCategory),
            QStringLiteral("tag:%1").arg(materialCategory));
    }

    QSet<QString> addedArrays;
    vtkCellData* cellData = grid->GetCellData();
    const QStringList preferredArrays{materialCategory, QStringLiteral("MATYPE"),
                                      QStringLiteral("REGION"), QStringLiteral("Physical"),
                                      QStringLiteral("RegionId"), QStringLiteral("src_part")};
    for (const QString& wanted : preferredArrays)
    {
        if (wanted.trimmed().isEmpty() || !cellData)
            continue;
        for (int i = 0; i < cellData->GetNumberOfArrays(); ++i)
        {
            vtkDataArray* array = vtkDataArray::SafeDownCast(cellData->GetArray(i));
            const QString name = array && array->GetName()
                                     ? QString::fromUtf8(array->GetName()).trimmed()
                                     : QString();
            if (!array || array->GetNumberOfComponents() != 1
                || name.compare(wanted, Qt::CaseInsensitive) != 0
                || addedArrays.contains(name.toLower()))
                continue;
            addedArrays.insert(name.toLower());
            materialCombo->addItem(QStringLiteral("网格单元数组：%1").arg(name),
                                   QStringLiteral("array:%1").arg(name));
        }
    }
    materialCombo->addItem(QStringLiteral("不区分材料（全部作为土层 1）"),
                           QStringLiteral("uniform"));

    auto* toleranceSpin = new QDoubleSpinBox(&options);
    toleranceSpin->setDecimals(8);
    toleranceSpin->setKeyboardTracking(false);
    toleranceSpin->setToolTip(
        QStringLiteral("用于减少网格剖切产生的密集小折点；不会跨越材料分界简化。"));

    const auto updatePlaneRange = [=]()
    {
        const int axis = planeCombo->currentData().toInt();
        if (axis < 0 || axis > 2)
            return;
        const double minimum = bounds[2 * axis];
        const double maximum = bounds[2 * axis + 1];
        positionSpin->setRange(minimum, maximum);
        positionSpin->setValue((minimum + maximum) * 0.5);

        const int verticalAxis = verticalCombo->currentData().toInt();
        const int horizontalAxis = 3 - verticalAxis - axis;
        const double sectionWidth = bounds[2 * horizontalAxis + 1]
                                    - bounds[2 * horizontalAxis];
        const double sectionHeight = bounds[2 * verticalAxis + 1]
                                     - bounds[2 * verticalAxis];
        const double sectionSpan =
            std::max(std::abs(sectionWidth), std::abs(sectionHeight));
        toleranceSpin->setRange(0.0, std::max(1.0, sectionSpan));
        toleranceSpin->setValue(std::max(sectionSpan * 1.0e-4, 1.0e-8));
    };
    const auto rebuildPlaneChoices = [=]()
    {
        const int verticalAxis = verticalCombo->currentData().toInt();
        const QSignalBlocker blocker(planeCombo);
        planeCombo->clear();
        int recommendedNormal = -1;
        double recommendedSpan = std::numeric_limits<double>::max();
        for (int normalAxis = 0; normalAxis < 3; ++normalAxis)
        {
            if (normalAxis == verticalAxis)
                continue;
            const int horizontalAxis = 3 - verticalAxis - normalAxis;
            const double normalSpan =
                std::abs(bounds[2 * normalAxis + 1] - bounds[2 * normalAxis]);
            if (normalSpan < recommendedSpan)
            {
                recommendedSpan = normalSpan;
                recommendedNormal = normalAxis;
            }
            planeCombo->addItem(
                QStringLiteral("%1 = 常数（断面横轴为 %2）")
                    .arg(axisName(normalAxis), axisName(horizontalAxis)),
                normalAxis);
        }
        const int recommendedIndex = planeCombo->findData(recommendedNormal);
        if (recommendedIndex >= 0)
            planeCombo->setCurrentIndex(recommendedIndex);
        updatePlaneRange();
    };
    rebuildPlaneChoices();
    connect(planeCombo, &QComboBox::currentIndexChanged, &options,
            [updatePlaneRange](int) { updatePlaneRange(); });
    connect(verticalCombo, &QComboBox::currentIndexChanged, &options,
            [rebuildPlaneChoices](int) { rebuildPlaneChoices(); });
    form->addRow(QStringLiteral("高程方向"), verticalCombo);
    form->addRow(QStringLiteral("剖面方向"), planeCombo);
    form->addRow(QStringLiteral("剖面位置"), positionSpin);
    form->addRow(QStringLiteral("土层识别依据"), materialCombo);
    form->addRow(QStringLiteral("折线简化容差"), toleranceSpin);
    optionsLayout->addLayout(form);
    auto* caution = new QLabel(
        QStringLiteral("说明：自动提取会替换当前节点、边界线、外坡面、浸润线和土层行。"
                       "网格中的材料编号只用于划分土层；φ、c 和容重将在提取后标为待确认。"),
        &options);
    caution->setWordWrap(true);
    optionsLayout->addWidget(caution);
    auto* optionButtons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &options);
    optionButtons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("提取剖面"));
    optionButtons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(optionButtons, &QDialogButtonBox::accepted, &options, &QDialog::accept);
    connect(optionButtons, &QDialogButtonBox::rejected, &options, &QDialog::reject);
    optionsLayout->addWidget(optionButtons);
    if (options.exec() != QDialog::Accepted)
        return;

    const int verticalAxis = verticalCombo->currentData().toInt();
    const int normalAxis = planeCombo->currentData().toInt();
    const int horizontalAxis = 3 - verticalAxis - normalAxis;
    const QString source = materialCombo->currentData().toString();
    QMap<int, QString> materialLabels;
    auto materialArray = vtkSmartPointer<vtkIntArray>::New();
    materialArray->SetName("__SlopeSectionMaterial");
    materialArray->SetNumberOfTuples(grid->GetNumberOfCells());
    materialArray->FillValue(0);

    if (source == QStringLiteral("uniform"))
    {
        materialArray->FillValue(1);
        materialLabels.insert(1, QStringLiteral("统一土层"));
    }
    else if (source.startsWith(QStringLiteral("tag:")))
    {
        const QString category = source.mid(4);
        for (const MeshTagSet& set : domainSets)
        {
            if (set.key.category.compare(category, Qt::CaseInsensitive) != 0
                || set.key.id == 0)
                continue;
            const QVector<MeshIdType> cellIds = meshService->cellIdsOfTag(set.key);
            for (MeshIdType cellId : cellIds)
                if (cellId >= 0 && cellId < grid->GetNumberOfCells())
                    materialArray->SetValue(static_cast<vtkIdType>(cellId), set.key.id);
            QString label = set.name.trimmed();
            if (label.isEmpty())
                label = meshService->displayTextOfTag(set.key).trimmed();
            materialLabels.insert(set.key.id,
                                  label.isEmpty() ? QStringLiteral("材料 %1").arg(set.key.id)
                                                  : label);
        }
    }
    else if (source.startsWith(QStringLiteral("array:")))
    {
        const QString arrayName = source.mid(6);
        vtkDataArray* sourceArray = vtkDataArray::SafeDownCast(
            grid->GetCellData()->GetArray(arrayName.toUtf8().constData()));
        if (sourceArray)
        {
            const vtkIdType count = std::min(grid->GetNumberOfCells(),
                                             sourceArray->GetNumberOfTuples());
            for (vtkIdType cellId = 0; cellId < count; ++cellId)
            {
                const int material = qRound(sourceArray->GetComponent(cellId, 0));
                materialArray->SetValue(cellId, material);
                if (material > 0 && !materialLabels.contains(material))
                {
                    QString label = meshService->matypePhysicalName(material, 3).trimmed();
                    materialLabels.insert(
                        material, label.isEmpty() ? QStringLiteral("材料 %1").arg(material)
                                                  : label);
                }
            }
        }
    }

    bool hasMaterial = false;
    for (vtkIdType cellId = 0; cellId < materialArray->GetNumberOfTuples(); ++cellId)
    {
        if (materialArray->GetValue(cellId) > 0)
        {
            hasMaterial = true;
            break;
        }
    }
    if (!hasMaterial)
    {
        QMessageBox::warning(
            this, QStringLiteral("没有可用材料分组"),
            QStringLiteral("所选材料分组没有覆盖实体单元。请重新选择材料识别依据，"
                           "或选择“不区分材料”。"));
        return;
    }

    auto workingGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    workingGrid->ShallowCopy(grid);
    workingGrid->GetCellData()->AddArray(materialArray);
    auto plane = vtkSmartPointer<vtkPlane>::New();
    double origin[3]{(bounds[0] + bounds[1]) * 0.5,
                     (bounds[2] + bounds[3]) * 0.5,
                     (bounds[4] + bounds[5]) * 0.5};
    double normal[3]{0.0, 0.0, 0.0};
    origin[normalAxis] = positionSpin->value();
    normal[normalAxis] = 1.0;
    plane->SetOrigin(origin);
    plane->SetNormal(normal);
    auto cutter = vtkSmartPointer<vtkCutter>::New();
    cutter->SetCutFunction(plane);
    cutter->SetInputData(workingGrid);
    cutter->Update();
    vtkPolyData* cut = cutter->GetOutput();
    vtkDataArray* cutMaterials = cut && cut->GetCellData()
                                     ? vtkDataArray::SafeDownCast(
                                           cut->GetCellData()->GetArray("__SlopeSectionMaterial"))
                                     : nullptr;
    if (!cut || cut->GetNumberOfCells() <= 0 || !cutMaterials)
    {
        QMessageBox::warning(
            this, QStringLiteral("剖面没有截到实体"),
            QStringLiteral("该位置没有得到有效的实体截面。请调整剖面位置，"
                           "并避免让剖面恰好贴在实体边界面上。"));
        return;
    }

    const double horizontalMinimum = bounds[2 * horizontalAxis];
    const double verticalMinimum = bounds[2 * verticalAxis];
    const double span = std::max(std::abs(bounds[2 * horizontalAxis + 1]
                                          - horizontalMinimum),
                                 std::abs(bounds[2 * verticalAxis + 1]
                                          - verticalMinimum));
    const double mergeTolerance = std::max(span * 1.0e-9, 1.0e-9);
    QVector<SectionPoint> points;
    std::map<std::pair<long long, long long>, int> pointLookup;
    const auto pointIndex = [&](const double coordinates[3])
    {
        const SectionPoint point{coordinates[horizontalAxis], coordinates[verticalAxis]};
        const auto key = std::make_pair(
            std::llround((point.x - horizontalMinimum) / mergeTolerance),
            std::llround((point.y - verticalMinimum) / mergeTolerance));
        const auto found = pointLookup.find(key);
        if (found != pointLookup.end())
            return found->second;
        const int index = points.size();
        points.append(point);
        pointLookup.emplace(key, index);
        return index;
    };

    std::map<std::pair<int, int>, QVector<SectionCellUse>> edgeUses;
    for (vtkIdType cellId = 0; cellId < cut->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = cut->GetCell(cellId);
        if (!cell || cell->GetNumberOfPoints() < 3
            || cellId >= cutMaterials->GetNumberOfTuples())
            continue;
        const int material = qRound(cutMaterials->GetComponent(cellId, 0));
        if (material <= 0)
            continue;
        QVector<int> polygon;
        SectionPoint centroid;
        for (vtkIdType local = 0; local < cell->GetNumberOfPoints(); ++local)
        {
            double coordinates[3]{};
            cut->GetPoint(cell->GetPointId(local), coordinates);
            polygon.append(pointIndex(coordinates));
            centroid.x += coordinates[horizontalAxis];
            centroid.y += coordinates[verticalAxis];
        }
        centroid.x /= static_cast<double>(polygon.size());
        centroid.y /= static_cast<double>(polygon.size());
        for (int i = 0; i < polygon.size(); ++i)
        {
            const int a = polygon.at(i);
            const int b = polygon.at((i + 1) % polygon.size());
            if (a == b)
                continue;
            edgeUses[std::minmax(a, b)].append({material, centroid});
        }
    }

    QVector<SectionEdge> rawSurfaceEdges;
    QVector<SectionEdge> rawInterfaceEdges;
    for (const auto& entry : edgeUses)
    {
        const int a = entry.first.first;
        const int b = entry.first.second;
        const SectionPoint& p1 = points.at(a);
        const SectionPoint& p2 = points.at(b);
        const double dx = p2.x - p1.x;
        const double dy = p2.y - p1.y;
        const double length = std::hypot(dx, dy);
        if (length <= mergeTolerance)
            continue;
        double nx = -dy / length;
        double ny = dx / length;
        if (ny < 0.0)
        {
            nx = -nx;
            ny = -ny;
        }
        const SectionPoint midpoint{(p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5};
        QMap<int, SectionCellUse> materialUses;
        for (const SectionCellUse& use : entry.second)
            if (!materialUses.contains(use.material))
                materialUses.insert(use.material, use);

        if (entry.second.size() == 1)
        {
            const SectionCellUse& use = entry.second.first();
            const double signedDistance =
                (use.centroid.x - midpoint.x) * nx + (use.centroid.y - midpoint.y) * ny;
            // A true outer slope has material on the downward side. Exact vertical
            // side walls are deliberately omitted because the legacy engine expects y(x).
            if (std::abs(dx) > mergeTolerance && signedDistance < -mergeTolerance)
                rawSurfaceEdges.append({a, b, use.material, true});
            continue;
        }
        if (materialUses.size() <= 1)
            continue; // Same-material tetrahedral subdivision inside the section.

        int lowerMaterial = 0;
        double bestSide = std::numeric_limits<double>::max();
        double bestHeight = std::numeric_limits<double>::max();
        for (auto it = materialUses.constBegin(); it != materialUses.constEnd(); ++it)
        {
            const double side = (it.value().centroid.x - midpoint.x) * nx
                                + (it.value().centroid.y - midpoint.y) * ny;
            if (side < bestSide - mergeTolerance
                || (std::abs(side - bestSide) <= mergeTolerance
                    && it.value().centroid.y < bestHeight))
            {
                bestSide = side;
                bestHeight = it.value().centroid.y;
                lowerMaterial = it.key();
            }
        }
        if (lowerMaterial > 0)
            rawInterfaceEdges.append({a, b, lowerMaterial, false});
    }

    QVector<SectionChain> surfaceChains = traceSectionChains(rawSurfaceEdges);
    if (surfaceChains.isEmpty())
    {
        QMessageBox::warning(
            this, QStringLiteral("未识别到外坡面"),
            QStringLiteral("截面存在，但没有识别到材料位于其下方的连续外坡面。"
                           "请检查竖直轴设置、材料分组，或换一个剖面位置。"));
        return;
    }
    int selectedChain = 0;
    for (int i = 1; i < surfaceChains.size(); ++i)
        if (chainHorizontalSpan(surfaceChains.at(i), points)
            > chainHorizontalSpan(surfaceChains.at(selectedChain), points))
            selectedChain = i;
    SectionChain surfaceChain = surfaceChains.at(selectedChain);
    if (surfaceChain.nodes.size() >= 2
        && points.at(surfaceChain.nodes.first()).x > points.at(surfaceChain.nodes.last()).x)
    {
        std::reverse(surfaceChain.nodes.begin(), surfaceChain.nodes.end());
        std::reverse(surfaceChain.materials.begin(), surfaceChain.materials.end());
    }

    // The legacy slope engine treats the first outline point as the toe and
    // expects the principal slope to rise from left to right.  A section cut
    // from a 3-D mesh can have either horizontal direction, so mirror the
    // complete section when its left end is higher than its right end.  This
    // keeps all material interfaces aligned with the surface while presenting
    // the orientation expected by the engine.
    bool horizontalAxisReversed = false;
    if (surfaceChain.nodes.size() >= 2)
    {
        const SectionPoint& leftEnd = points.at(surfaceChain.nodes.first());
        const SectionPoint& rightEnd = points.at(surfaceChain.nodes.last());
        if (leftEnd.y > rightEnd.y + mergeTolerance)
        {
            double sectionMinX = leftEnd.x;
            double sectionMaxX = leftEnd.x;
            for (const int node : surfaceChain.nodes)
            {
                sectionMinX = std::min(sectionMinX, points.at(node).x);
                sectionMaxX = std::max(sectionMaxX, points.at(node).x);
            }
            for (SectionPoint& point : points)
                point.x = sectionMinX + sectionMaxX - point.x;
            std::reverse(surfaceChain.nodes.begin(), surfaceChain.nodes.end());
            std::reverse(surfaceChain.materials.begin(), surfaceChain.materials.end());
            horizontalAxisReversed = true;
        }
    }

    double simplificationTolerance = toleranceSpin->value();
    QVector<SectionEdge> surfaceEdges =
        simplifySectionChain(surfaceChain, points, simplificationTolerance, true);
    bool toleranceRaised = false;
    for (int attempt = 0; surfaceEdges.size() > 50 && attempt < 20; ++attempt)
    {
        simplificationTolerance = std::max(simplificationTolerance * 1.8,
                                           std::max(span * 1.0e-8, 1.0e-8));
        surfaceEdges =
            simplifySectionChain(surfaceChain, points, simplificationTolerance, true);
        toleranceRaised = true;
    }
    if (surfaceEdges.isEmpty() || surfaceEdges.size() > 50)
    {
        QMessageBox::warning(
            this, QStringLiteral("外坡面过于复杂"),
            QStringLiteral("简化后外坡面仍超过求解器的 50 条线限制。"
                           "请增大折线简化容差，或选择更规则的剖面。"));
        return;
    }

    QVector<SectionEdge> interfaceEdges;
    QMap<int, QVector<SectionEdge>> interfacesByMaterial;
    for (const SectionEdge& edge : rawInterfaceEdges)
        interfacesByMaterial[edge.material].append(edge);
    for (auto it = interfacesByMaterial.constBegin(); it != interfacesByMaterial.constEnd(); ++it)
    {
        for (const SectionChain& chain : traceSectionChains(it.value()))
            interfaceEdges += simplifySectionChain(chain, points,
                                                   simplificationTolerance, false);
    }

    QVector<SectionEdge> allEdges = surfaceEdges;
    allEdges += interfaceEdges;
    QSet<int> sourceMaterials;
    for (const SectionEdge& edge : allEdges)
        sourceMaterials.insert(edge.material);
    QList<int> orderedMaterials = sourceMaterials.values();
    std::sort(orderedMaterials.begin(), orderedMaterials.end());
    if (orderedMaterials.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("剖面为空"),
                             QStringLiteral("没有得到可供规范法使用的边界线。"));
        return;
    }
    QMap<int, int> materialToSoil;
    for (int i = 0; i < orderedMaterials.size(); ++i)
        materialToSoil.insert(orderedMaterials.at(i), i + 1);

    SlopeStandardDatModel model;
    QString currentError;
    if (!collectFormModel(&model, &currentError)
        && !SlopeStandardDatModel::parse(m_datEditor->toPlainText(), &model, nullptr))
        SlopeStandardDatModel::parse(defaultDatText(), &model, nullptr);
    model.projectTitle = QStringLiteral("%1 - %2=%3 剖面")
                             .arg(model.projectTitle.isEmpty() ? QStringLiteral("边坡规范法")
                                                               : model.projectTitle,
                                  axisName(normalAxis), displayNumber(positionSpin->value()));
    model.soils.clear();
    const int poreMethod = m_poreMethodCombo->currentData().toInt();
    for (int i = 0; i < orderedMaterials.size(); ++i)
    {
        SlopeStandardSoil soil;
        soil.id = i + 1;
        soil.frictionAngle = 20.0;
        soil.cohesion = 5.0;
        soil.naturalUnitWeight = 19.0;
        soil.saturatedUnitWeight = 20.0;
        soil.poreMethod = poreMethod;
        soil.ru = 0.0;
        model.soils.append(soil);
    }
    model.nodes.clear();
    model.segments.clear();
    model.surfaceLoads.clear();
    model.phreaticSegments.clear();
    QMap<int, int> nodeIds;
    const auto ensureNode = [&](int sourceNode, SlopeStandardDatModel* target)
    {
        if (nodeIds.contains(sourceNode))
            return nodeIds.value(sourceNode);
        const int id = nodeIds.size() + 1;
        const SectionPoint& point = points.at(sourceNode);
        target->nodes.append({id, point.x, point.y});
        nodeIds.insert(sourceNode, id);
        return id;
    };
    for (const SectionEdge& edge : allEdges)
    {
        model.segments.append({ensureNode(edge.a, &model), ensureNode(edge.b, &model),
                               materialToSoil.value(edge.material)});
    }
    for (int i = 0; i < surfaceEdges.size(); ++i)
        model.surfaceLoads.append({i + 1, 0.0});

    QString validationError;
    if (!model.validate(&validationError))
    {
        QMessageBox::warning(this, QStringLiteral("提取结果无法使用"), validationError);
        return;
    }
    if (!loadFormFromDat(model.serialize(), &validationError))
    {
        QMessageBox::warning(this, QStringLiteral("提取结果无法载入"), validationError);
        return;
    }

    for (int row = 0; row < orderedMaterials.size(); ++row)
    {
        const int sourceMaterial = orderedMaterials.at(row);
        QString label = materialLabels.value(sourceMaterial).trimmed();
        if (label.isEmpty())
            label = QStringLiteral("材料 %1").arg(sourceMaterial);
        if (QTableWidgetItem* nameItem = m_soilTable->item(row, 0))
        {
            nameItem->setText(QStringLiteral("土层 %1 ← %2（待确认）").arg(row + 1).arg(label));
            nameItem->setToolTip(
                QStringLiteral("规范法土层 %1 对应网格材料编号 %2：%3")
                    .arg(row + 1).arg(sourceMaterial).arg(label));
        }
        for (int column = 1; column <= 4; ++column)
        {
            if (QTableWidgetItem* item = m_soilTable->item(row, column))
            {
                item->setBackground(QColor(255, 247, 214));
                item->setToolTip(QStringLiteral("这是通用占位值，请按勘察或设计参数确认后再计算。"));
            }
        }
    }
    m_tabs->setCurrentIndex(1);
    updateInputPreview();
    m_stateLabel->setText(
        QStringLiteral("已从网格提取剖面；请确认黄色标记的土层强度与容重参数"));
    emit datTextChanged(model.serialize());

    QString message =
        QStringLiteral("已提取 %1 个断面点、%2 条外坡面线和 %3 条材料分界线，"
                       "识别为 %4 个规范法土层。\n\n"
                       "请在“① 土层参数”中确认黄色标记的 φ、c、天然容重和饱和容重。"
                       "地下水浸润线不会从实体网格推测，需要时请单独填写。")
            .arg(model.nodes.size())
            .arg(surfaceEdges.size())
            .arg(interfaceEdges.size())
            .arg(model.soils.size());
    if (surfaceChains.size() > 1)
        message += QStringLiteral("\n\n截面中检测到多个不相连的外露轮廓，已采用水平跨度最大的主边坡轮廓。");
    if (horizontalAxisReversed)
        message += QStringLiteral("\n\n原断面由高处向低处排列，已自动反转水平坐标，使坡脚位于左侧，符合求解器的断面方向约定。");
    if (toleranceRaised)
        message += QStringLiteral("\n\n为满足求解器外坡面最多 50 条线的限制，程序已自动提高简化容差至 %1。")
                       .arg(displayNumber(simplificationTolerance));
    QMessageBox::information(this, QStringLiteral("剖面提取完成"), message);
}

QString SlopeStandardMethodDialog::engineExecutable() const
{
    return QDir::toNativeSeparators(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("solver/SlopeEngine/SlopeEngine.exe")));
}

QString SlopeStandardMethodDialog::createJobDirectory(QString* errorMessage) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString().trimmed();
    if (projectDir.isEmpty() || !QDir(projectDir).exists())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("请先创建或保存工程，再进行规范法验算。");
        return {};
    }

    const QString root = QDir(projectDir).filePath(
        QStringLiteral("规范法验算/%1").arg(m_analysisUuid));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString job = QDir(root).filePath(stamp);
    if (!QDir().mkpath(job))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建规范法任务目录：%1").arg(job);
        return {};
    }
    return QDir(job).absolutePath();
}

bool SlopeStandardMethodDialog::writeInputDat(const QString& path, QString* errorMessage) const
{
    const QString input = m_datEditor->toPlainText().trimmed();
    if (input.isEmpty())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("规范法 DAT 输入不能为空。");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法写入输入文件：%1").arg(path);
        return false;
    }
    file.write(input.toLocal8Bit());
    file.write("\r\n");
    if (!file.commit())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("输入文件保存失败：%1").arg(path);
        return false;
    }
    return true;
}

void SlopeStandardMethodDialog::startOperation(Operation operation)
{
    if (m_process->state() != QProcess::NotRunning)
        return;

    QString error;
    if (m_datEditor->document()->isModified())
    {
        if (!loadFormFromDat(m_datEditor->toPlainText(), &error))
        {
            m_tabs->setCurrentIndex(2);
            QMessageBox::warning(
                this, QStringLiteral("请先修正高级 DAT"),
                QStringLiteral("高级 DAT 已被手工修改，但无法转换成参数表单：\n%1").arg(error));
            return;
        }
    }
    if (!syncDatFromForm(&error))
    {
        m_tabs->setCurrentIndex(error.contains(QStringLiteral("材料"))
                                    || error.contains(QStringLiteral("土层"))
                                    || error.contains(QStringLiteral("节点"))
                                    || error.contains(QStringLiteral("边界"))
                                    || error.contains(QStringLiteral("坡面"))
                                    || error.contains(QStringLiteral("浸润"))
                                ? 1
                                : 0);
        QMessageBox::warning(this, QStringLiteral("请完善规范法参数"), error);
        return;
    }

    const QString executable = engineExecutable();
    if (!QFileInfo::exists(executable))
    {
        QMessageBox::warning(
            this, QStringLiteral("规范法求解器未安装"),
            QStringLiteral("未找到 SlopeEngine.exe：\n%1\n请重新构建或修复软件安装。")
                .arg(executable));
        return;
    }

    m_jobDirectory = createJobDirectory(&error);
    if (m_jobDirectory.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("无法开始验算"), error);
        return;
    }

    const QString inputPath = QDir(m_jobDirectory).filePath(QStringLiteral("case.dat"));
    m_statusPath = QDir(m_jobDirectory).filePath(QStringLiteral("status.json"));
    m_plotPath = QDir(m_jobDirectory).filePath(QStringLiteral("plot.json"));
    m_resultPath = QDir(m_jobDirectory).filePath(QStringLiteral("result.dat"));
    if (!writeInputDat(inputPath, &error))
    {
        QMessageBox::warning(this, QStringLiteral("无法开始验算"), error);
        return;
    }

    const QStringList paths = {inputPath, m_statusPath, m_plotPath, m_resultPath, m_jobDirectory};
    for (const QString& path : paths)
    {
        if (QDir::toNativeSeparators(path).size() >= 260)
        {
            QMessageBox::warning(
                this, QStringLiteral("任务路径过长"),
                QStringLiteral("SlopeEngine 的路径上限为 259 个字符，请将工程保存到更短的位置。\n%1")
                    .arg(path));
            return;
        }
    }

    // The delivered engine writes command-line path values back to status.json.
    // On Windows those arguments use the active ANSI code page, so an absolute
    // path containing Chinese characters would make the JSON invalid UTF-8.
    // The process already runs in the job directory; keep all command-line path
    // values ASCII-only and relative to that directory.
    const QString inputArgument = QStringLiteral("case.dat");
    const QString statusArgument = QStringLiteral("status.json");
    const QString plotArgument = QStringLiteral("plot.json");
    const QString resultArgument = QStringLiteral("result.dat");

    QStringList arguments;
    if (operation == Operation::Validate)
    {
        arguments << QStringLiteral("validate") << QStringLiteral("--input") << inputArgument
                  << QStringLiteral("--status") << statusArgument
                  << QStringLiteral("--job-dir") << QStringLiteral(".");
    }
    else
    {
        arguments << QStringLiteral("calculate") << QStringLiteral("--input") << inputArgument
                  << QStringLiteral("--result") << resultArgument
                  << QStringLiteral("--status") << statusArgument
                  << QStringLiteral("--plot") << plotArgument
                  << QStringLiteral("--job-dir") << QStringLiteral(".");
    }

    m_operation = operation;
    m_stopRequested = false;
    m_processOutput.clear();
    m_plotWidget->clearPlot();
    m_resultViewer->clear();
    m_process->setWorkingDirectory(m_jobDirectory);
    m_process->setProgram(executable);
    m_process->setArguments(arguments);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    setRunning(true);
    m_stateLabel->setText(operation == Operation::Validate
                              ? QStringLiteral("正在校验规范法输入……")
                              : QStringLiteral("正在进行规范法验算……"));
    m_process->start();
}

void SlopeStandardMethodDialog::setRunning(bool running)
{
    m_validateButton->setEnabled(!running);
    m_calculateButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    m_progressBar->setVisible(running);
    emit runningChanged(running);
}

void SlopeStandardMethodDialog::consumeProcessOutput()
{
    m_processOutput += QString::fromLocal8Bit(m_process->readAllStandardOutput());
    constexpr qsizetype kMaximumCapturedCharacters = 1024 * 1024;
    if (m_processOutput.size() > kMaximumCapturedCharacters)
        m_processOutput = m_processOutput.right(kMaximumCapturedCharacters);
}

void SlopeStandardMethodDialog::handleProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    setRunning(false);
    m_operation = Operation::None;
    m_stateLabel->setText(QStringLiteral("求解器启动失败"));
    QMessageBox::warning(this, QStringLiteral("规范法求解器未启动"), m_process->errorString());
}

QJsonObject SlopeStandardMethodDialog::readJsonObject(const QString& path,
                                                      QString* errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取文件：%1").arg(path);
        return {};
    }
    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject())
    {
        // Compatibility with existing jobs created by the delivered Fortran
        // engine: Chinese absolute paths were emitted using the Windows local
        // code page instead of UTF-8.
        QJsonParseError localParseError;
        const QByteArray converted = QString::fromLocal8Bit(data).toUtf8();
        const QJsonDocument localDocument =
            QJsonDocument::fromJson(converted, &localParseError);
        if (localDocument.isObject())
            document = localDocument;
    }
    if (!document.isObject())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("JSON 格式错误：%1（文件：%2）")
                                .arg(parseError.errorString(), path);
        return {};
    }
    return document.object();
}

QString SlopeStandardMethodDialog::errorDescription(int exitCode,
                                                    const QJsonObject& status) const
{
    QString detail = status.value(QStringLiteral("message")).toString().trimmed();
    const QString field = status.value(QStringLiteral("field")).toString().trimmed();
    const QString key = status.value(QStringLiteral("message_key")).toString().trimmed();
    if (!field.isEmpty())
        detail += QStringLiteral("\n字段：%1").arg(field);
    if (!key.isEmpty())
        detail += QStringLiteral("\n错误标识：%1").arg(key);
    if (detail.trimmed().isEmpty())
    {
        QStringList usefulOutput;
        const QString progressLine =
            QStringLiteral("THE PROGRAM OF SLOPE IS RUNNING NOW, PLEASE WAIT!");
        const QStringList lines = m_processOutput.split(QLatin1Char('\n'));
        for (QString line : lines)
        {
            line = line.trimmed();
            if (!line.isEmpty() && line.compare(progressLine, Qt::CaseInsensitive) != 0
                && !usefulOutput.contains(line))
                usefulOutput.append(line);
        }
        detail = usefulOutput.join(QLatin1Char('\n'));
    }
    if (detail.trimmed().isEmpty())
    {
        static const QMap<int, QString> descriptions = {
            {1, QStringLiteral("命令行参数错误")},
            {2, QStringLiteral("输入文件或任务目录读写错误")},
            {3, QStringLiteral("DAT 字段校验错误")},
            {4, QStringLiteral("数值计算没有有效解")},
            {5, QStringLiteral("输出文件写入错误")},
            {6, QStringLiteral("求解器内部错误")}};
        detail = descriptions.value(exitCode, QStringLiteral("未知错误"));
    }
    return QStringLiteral("SlopeEngine 退出码：%1\n%2").arg(exitCode).arg(detail.trimmed());
}

void SlopeStandardMethodDialog::handleProcessFinished(int exitCode,
                                                      QProcess::ExitStatus exitStatus)
{
    consumeProcessOutput();
    const Operation finishedOperation = m_operation;
    m_operation = Operation::None;
    setRunning(false);

    if (m_stopRequested)
    {
        m_stopRequested = false;
        m_stateLabel->setText(QStringLiteral("规范法验算已停止"));
        m_openDirectoryButton->setEnabled(QDir(m_jobDirectory).exists());
        return;
    }

    QString jsonError;
    const QJsonObject status = readJsonObject(m_statusPath, &jsonError);
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0
                         && status.value(QStringLiteral("success")).toBool();
    if (!success)
    {
        m_stateLabel->setText(QStringLiteral("规范法验算未完成"));
        m_openDirectoryButton->setEnabled(QDir(m_jobDirectory).exists());
        QString description;
        if (exitStatus == QProcess::CrashExit)
            description = QStringLiteral("SlopeEngine 进程异常退出。\n%1")
                              .arg(m_processOutput.trimmed());
        else if (exitCode == 0 && status.isEmpty())
            description = QStringLiteral(
                              "SlopeEngine 已成功结束，但无法读取结构化结果。\n%1\n任务目录：%2")
                              .arg(jsonError, m_jobDirectory);
        else
            description = errorDescription(exitCode, status);
        QMessageBox::warning(
            this, QStringLiteral("规范法验算未完成"), description);
        return;
    }

    const bool calculation = finishedOperation == Operation::Calculate;
    showStatus(status, m_jobDirectory, calculation);
    m_stateLabel->setText(calculation ? QStringLiteral("规范法验算完成")
                                     : QStringLiteral("输入校验通过"));
    m_openDirectoryButton->setEnabled(true);
    if (calculation)
    {
        emit lastJobDirectoryChanged(m_jobDirectory);
        m_tabs->setCurrentIndex(m_tabs->count() - 1);
    }
    else
        QMessageBox::information(this, QStringLiteral("输入校验"),
                                 QStringLiteral("规范法 DAT 输入校验通过。"));
}

void SlopeStandardMethodDialog::showStatus(const QJsonObject& status,
                                           const QString& jobDirectory,
                                           bool calculation)
{
    if (!calculation)
    {
        m_summaryLabel->setText(QStringLiteral("输入校验通过。"));
        return;
    }

    const QJsonObject result = status.value(QStringLiteral("result")).toObject();
    const QJsonObject circle = result.value(QStringLiteral("critical_circle")).toObject();
    QStringList summary;
    summary << QStringLiteral("方法：%1").arg(
                   methodDisplayName(status.value(QStringLiteral("method")).toString()))
            << QStringLiteral("模式：%1").arg(
                   modeDisplayName(status.value(QStringLiteral("mode")).toString()))
            << QStringLiteral("安全系数：%1").arg(
                   displayNumber(result.value(QStringLiteral("factor")).toDouble()))
            << QStringLiteral("临界圆弧：圆心 (%1, %2)，半径 %3")
                   .arg(displayNumber(circle.value(QStringLiteral("cx")).toDouble()),
                        displayNumber(circle.value(QStringLiteral("cy")).toDouble()),
                        displayNumber(circle.value(QStringLiteral("radius")).toDouble()))
            << QStringLiteral("搜索次数：%1")
                   .arg(result.value(QStringLiteral("search_iterations")).toInt());

    const auto appendOptional = [&](const QString& key, const QString& label)
    {
        const QJsonValue value = result.value(key);
        if (!value.isNull() && !value.isUndefined())
            summary << QStringLiteral("%1：%2").arg(label, displayNumber(value.toDouble()));
    };
    appendOptional(QStringLiteral("swedish_factor"), QStringLiteral("瑞典法安全系数"));
    appendOptional(QStringLiteral("bishop_factor"), QStringLiteral("毕肖普法安全系数"));
    appendOptional(QStringLiteral("sarma_factor"), QStringLiteral("Sarma 法安全系数"));
    appendOptional(QStringLiteral("sarma_kc"), QStringLiteral("Sarma Kc"));
    const QJsonValue physical = result.value(QStringLiteral("sarma_physical_acceptable"));
    if (!physical.isNull() && !physical.isUndefined())
        summary << QStringLiteral("Sarma 物理可接受性：%1")
                       .arg(physical.toBool() ? QStringLiteral("是") : QStringLiteral("否"));
    m_summaryLabel->setText(summary.join(QStringLiteral("　　")));

    QString plotError;
    const QJsonObject plot = readJsonObject(QDir(jobDirectory).filePath(QStringLiteral("plot.json")),
                                            &plotError);
    if (!plot.isEmpty())
        m_plotWidget->setPlot(plot);

    QFile resultFile(QDir(jobDirectory).filePath(QStringLiteral("result.dat")));
    if (resultFile.open(QIODevice::ReadOnly))
        m_resultViewer->setPlainText(QString::fromLocal8Bit(resultFile.readAll()));
}

void SlopeStandardMethodDialog::loadExistingResult(const QString& jobDirectory)
{
    const QDir job(jobDirectory);
    const QString statusPath = job.filePath(QStringLiteral("status.json"));
    if (!job.exists() || !QFileInfo::exists(statusPath))
        return;
    const QJsonObject status = readJsonObject(statusPath);
    if (!status.value(QStringLiteral("success")).toBool())
        return;
    m_jobDirectory = job.absolutePath();
    m_statusPath = statusPath;
    m_plotPath = job.filePath(QStringLiteral("plot.json"));
    m_resultPath = job.filePath(QStringLiteral("result.dat"));
    const bool calculation = status.contains(QStringLiteral("result"));
    showStatus(status, m_jobDirectory, calculation);
    m_openDirectoryButton->setEnabled(true);
    if (calculation)
        m_tabs->setCurrentIndex(m_tabs->count() - 1);
}

void SlopeStandardMethodDialog::stopProcess()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    m_stateLabel->setText(QStringLiteral("正在停止规范法验算……"));
    m_process->terminate();
    QTimer::singleShot(1500, m_process, [process = m_process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void SlopeStandardMethodDialog::importDat()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入规范法 DAT"), QString(),
        QStringLiteral("DAT 文件 (*.dat *.DAT);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, QStringLiteral("导入失败"), file.errorString());
        return;
    }
    const QString imported = QString::fromLocal8Bit(file.readAll());
    QString error;
    if (!loadFormFromDat(imported, &error))
    {
        QMessageBox::warning(this, QStringLiteral("DAT 导入失败"), error);
        return;
    }
    m_datEditor->setPlainText(imported);
    m_datEditor->document()->setModified(false);
    m_tabs->setCurrentIndex(0);
    m_stateLabel->setText(QStringLiteral("DAT 已导入并转换为参数表单"));
    emit datTextChanged(datText());
}

void SlopeStandardMethodDialog::exportDat()
{
    QString error;
    if (m_datEditor->document()->isModified()
        && !loadFormFromDat(m_datEditor->toPlainText(), &error))
    {
        QMessageBox::warning(this, QStringLiteral("无法导出 DAT"), error);
        return;
    }
    if (!syncDatFromForm(&error))
    {
        QMessageBox::warning(this, QStringLiteral("无法导出 DAT"), error);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出规范法 DAT"), QStringLiteral("case.dat"),
        QStringLiteral("DAT 文件 (*.dat);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    if (!writeInputDat(path, &error))
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
}

void SlopeStandardMethodDialog::openOutputDirectory()
{
    if (!m_jobDirectory.isEmpty() && QDir(m_jobDirectory).exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_jobDirectory));
}

void SlopeStandardMethodDialog::closeEvent(QCloseEvent* event)
{
    if (m_process->state() != QProcess::NotRunning)
    {
        if (QMessageBox::question(this, QStringLiteral("停止规范法验算"),
                                  QStringLiteral("计算仍在进行，是否停止并关闭窗口？"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        stopProcess();
        if (m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(1500);
        }
    }
    if (m_datEditor->document()->isModified())
    {
        QString error;
        if (!loadFormFromDat(m_datEditor->toPlainText(), &error)
            && QMessageBox::question(
                   this, QStringLiteral("高级 DAT 尚未应用"),
                   QStringLiteral("高级 DAT 无法转换为参数表单：\n%1\n是否放弃这部分修改并关闭？")
                       .arg(error),
                   QMessageBox::Yes | QMessageBox::No,
                   QMessageBox::No) != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
    }
    emit datTextChanged(datText());
    QDialog::closeEvent(event);
}
