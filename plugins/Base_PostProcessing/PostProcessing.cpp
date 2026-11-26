
#include "PostProcessing.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/UiFrameService.h>
#include <vtkAutoInit.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkTextProperty.h>
#include <vtkActor.h>
#include <vtkArrowSource.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

#include <QDebug>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <cmath>
#include <limits>
#include <QHash>
#include <QDesktopServices>
#include <QFile>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPointF>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QSaveFile>
#include <QSharedPointer>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVector>
#include <QVariant>

#include "AnimationAviWidget.h"
#include "CurveSettingsWidget.h"
#include "DerivedValueSettingWidget.h"
#include "IsosurfaceSegmentationWidget.h"
#include "IsosurfaceSettingsWidget.h"
#include "ProbeSettingsWidget.h"
#include "PostContourReaderWidget.h"
#include "PostDisplayControlWidget.h"
#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingContextOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle);

namespace
{
struct PostNodeSpec
{
    const char* title;
    const char* type;
};

const QVector<PostNodeSpec>& postNodeSpecs()
{
    static const QVector<PostNodeSpec> specs = {
        {"云图", PublicConstants::kPagePostProcessNephogram},
        {"矢量", PublicConstants::kPagePostProcessVector},
        {"切面", PublicConstants::kPagePostProcessSlice},
        {"切块", PublicConstants::kPagePostProcessBlock},
        {"等值面", PublicConstants::kPagePostProcessIsosurface},
        {"等值面分割", PublicConstants::kPagePostProcessIsosurfaceSegmentation},
        {"计算曲线", PublicConstants::kPagePostProcessCurve},
        {"动画设置", PublicConstants::kPagePostProcessAnimation},
        {"派生值", PublicConstants::kPagePostProcessDerivative},
        {"洞室变形轮廓图", PublicConstants::kPagePostProcessContourReader},
    };
    return specs;
}

constexpr char kSlopeLimitResultPageType[] = "SlopeLimitEquilibriumResult";
constexpr char kSlopeLimitResultReadyEvent[] = "slope.limit.equilibrium.result.ready";
constexpr char kCaveBlockResultPageType[] = "CaveBlockStabilityResult";

QString normalizedSpecialResultRoot(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath()).toLower();
}

QString specialResultTitle(const QString& analysisName, const QString& fallback)
{
    QString name = analysisName.trimmed();
    if (name.isEmpty())
        return fallback;
    if (name.endsWith(QStringLiteral("结果")))
        return name;
    if (name.endsWith(QStringLiteral("分析")))
        name.chop(2);
    else if (name.endsWith(QStringLiteral("计算")))
        name.chop(2);
    return name + QStringLiteral("结果");
}

QString specialResultKey(const QString& type, const QJsonObject& state)
{
    if (type != QString::fromLatin1(kSlopeLimitResultPageType)
        && type != QString::fromLatin1(kCaveBlockResultPageType))
        return {};

    const QString analysisUuid = state.value(QStringLiteral("analysis_uuid")).toString().trimmed();
    if (!analysisUuid.isEmpty())
        return type + QStringLiteral("|uuid|") + analysisUuid;

    const QString resultRoot =
        normalizedSpecialResultRoot(state.value(QStringLiteral("result_root")).toString());
    if (!resultRoot.isEmpty())
        return type + QStringLiteral("|root|") + resultRoot;

    return {};
}

bool specialResultMatches(ProjectNode* node, const QString& type,
                          const QString& analysisUuid, const QString& resultRoot)
{
    if (!node || node->data(0, ProjectNode::RoleKind).toString() != type)
        return false;
    BasePage* page = node->getPage();
    if (!page)
        return false;

    const QJsonObject state = page->saveJsonForPersistence();
    const QString nodeAnalysisUuid =
        state.value(QStringLiteral("analysis_uuid")).toString().trimmed();
    if (!analysisUuid.trimmed().isEmpty() && nodeAnalysisUuid == analysisUuid.trimmed())
        return true;

    const QString wantedRoot = normalizedSpecialResultRoot(resultRoot);
    const QString nodeRoot =
        normalizedSpecialResultRoot(state.value(QStringLiteral("result_root")).toString());
    return !wantedRoot.isEmpty() && wantedRoot == nodeRoot;
}

ProjectNode* findSpecialResultChild(ProjectNode* root, const QString& type,
                                    const QString& analysisUuid, const QString& resultRoot)
{
    if (!root)
        return nullptr;
    for (int i = 0; i < root->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(root->child(i));
        if (specialResultMatches(node, type, analysisUuid, resultRoot))
            return node;
    }
    return nullptr;
}

class SlopeLimitEquilibriumResultPage final : public BasePage
{
  public:
    explicit SlopeLimitEquilibriumResultPage(VisualWidgetService* visualService)
        : BasePage(QString::fromLatin1(kSlopeLimitResultPageType)),
          m_visualService(visualService)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto* resultGroup = new QGroupBox(QStringLiteral("结果"), this);
        auto* resultForm = new QFormLayout(resultGroup);
        resultForm->setContentsMargins(10, 8, 10, 8);
        resultForm->setHorizontalSpacing(10);
        resultForm->setVerticalSpacing(6);

        m_factorLabel = new QLabel(QStringLiteral("—"), resultGroup);
        QFont factorFont = m_factorLabel->font();
        factorFont.setBold(true);
        factorFont.setPointSize(factorFont.pointSize() + 3);
        m_factorLabel->setFont(factorFont);
        m_methodLabel = new QLabel(QStringLiteral("—"), resultGroup);
        m_circleLabel = new QLabel(QStringLiteral("—"), resultGroup);
        m_circleLabel->setWordWrap(true);

        resultForm->addRow(QStringLiteral("安全系数"), m_factorLabel);
        resultForm->addRow(QStringLiteral("方法"), m_methodLabel);
        resultForm->addRow(QStringLiteral("临界滑弧"), m_circleLabel);
        root->addWidget(resultGroup);

        auto* displayGroup = new QGroupBox(QStringLiteral("显示控制"), this);
        auto* displayLayout = new QHBoxLayout(displayGroup);
        displayLayout->setContentsMargins(10, 6, 10, 6);
        displayLayout->setSpacing(12);

        m_showBodyCheck = new QCheckBox(QStringLiteral("滑体区域"), displayGroup);
        m_showOutlineCheck = new QCheckBox(QStringLiteral("边坡断面"), displayGroup);
        m_showArcCheck = new QCheckBox(QStringLiteral("临界滑弧"), displayGroup);
        for (QCheckBox* check : {m_showBodyCheck, m_showOutlineCheck, m_showArcCheck})
        {
            check->setChecked(true);
            QObject::connect(check, &QCheckBox::toggled, this,
                             [this](bool) { applyVisibility(); });
            displayLayout->addWidget(check);
        }
        displayLayout->addStretch();
        root->addWidget(displayGroup);

        auto* tools = new QHBoxLayout();
        tools->setSpacing(6);
        m_openDirectoryButton = new QPushButton(QStringLiteral("结果目录"), this);
        tools->addStretch();
        tools->addWidget(m_openDirectoryButton);
        root->addLayout(tools);
        root->addStretch();

        QObject::connect(m_openDirectoryButton, &QPushButton::clicked, this,
                         [this]() { openResultDirectory(); });
    }

    void setResultContext(const QString& analysisUuid, const QString& analysisName,
                          const QString& resultRoot, double requiredFactor,
                          bool horizontalReversed, double horizontalMinimum,
                          double horizontalMaximum)
    {
        m_analysisUuid = analysisUuid;
        m_analysisName = analysisName;
        m_resultRoot = QDir(resultRoot).absolutePath();
        m_requiredFactor = requiredFactor;
        m_horizontalReversed = horizontalReversed;
        m_horizontalMinimum = horizontalMinimum;
        m_horizontalMaximum = horizontalMaximum;
        loadResult();
        if (property("postDisplayActive").toBool())
            displayPlot(true);
    }

    QString analysisUuid() const { return m_analysisUuid; }
    QString resultRoot() const { return m_resultRoot; }

    void refreshResult()
    {
        loadResult();
        if (property("postDisplayActive").toBool())
            displayPlot(false);
    }

    void fitView()
    {
        if (!m_visualService)
            m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (m_visualService && m_visualService->renderer())
        {
            m_visualService->renderer()->ResetCamera();
            m_visualService->render();
        }
    }

    void openResultDirectory()
    {
        if (!m_resultRoot.isEmpty() && QDir(m_resultRoot).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_resultRoot));
    }

    QJsonObject saveJson() override
    {
        return QJsonObject{
            {QStringLiteral("analysis_uuid"), m_analysisUuid},
            {QStringLiteral("analysis_name"), m_analysisName},
            {QStringLiteral("result_root"), m_resultRoot},
            {QStringLiteral("required_factor"), m_requiredFactor},
            {QStringLiteral("horizontal_reversed"), m_horizontalReversed},
            {QStringLiteral("horizontal_minimum"), m_horizontalMinimum},
            {QStringLiteral("horizontal_maximum"), m_horizontalMaximum},
            {QStringLiteral("show_body"), m_showBodyCheck ? m_showBodyCheck->isChecked() : true},
            {QStringLiteral("show_outline"), m_showOutlineCheck ? m_showOutlineCheck->isChecked() : true},
            {QStringLiteral("show_arc"), m_showArcCheck ? m_showArcCheck->isChecked() : true}};
    }

    void loadJson(const QJsonObject& object) override
    {
        m_analysisUuid = object.value(QStringLiteral("analysis_uuid")).toString();
        m_analysisName = object.value(QStringLiteral("analysis_name")).toString();
        m_resultRoot = object.value(QStringLiteral("result_root")).toString();
        m_requiredFactor = object.value(QStringLiteral("required_factor")).toDouble(1.30);
        m_horizontalReversed = object.value(QStringLiteral("horizontal_reversed")).toBool(false);
        m_horizontalMinimum = object.value(QStringLiteral("horizontal_minimum")).toDouble(0.0);
        m_horizontalMaximum = object.value(QStringLiteral("horizontal_maximum")).toDouble(0.0);
        if (m_showBodyCheck)
            m_showBodyCheck->setChecked(object.value(QStringLiteral("show_body")).toBool(true));
        if (m_showOutlineCheck)
            m_showOutlineCheck->setChecked(object.value(QStringLiteral("show_outline")).toBool(true));
        if (m_showArcCheck)
            m_showArcCheck->setChecked(object.value(QStringLiteral("show_arc")).toBool(true));
        loadResult();
    }

    void beforeLoad() override
    {
        setProperty("postDisplayActive", true);
        clearPostDisplayContext();
        if (!m_visualService)
            m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (m_visualService)
            showPostProcessingOnly(m_visualService, false);
        loadResult();
        displayPlot(true);
    }

    void beforeUnload() override
    {
        setProperty("postDisplayActive", false);
        setPostViewActive(false);
        clearPostDisplayContext();
        removeActors();
    }

  private:
    static QJsonObject readObject(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        return doc.isObject() ? doc.object() : QJsonObject();
    }

    static QString methodName(const QString& method)
    {
        if (method == QStringLiteral("swedish")) return QStringLiteral("瑞典圆弧法");
        if (method == QStringLiteral("bishop")) return QStringLiteral("毕肖普法");
        if (method == QStringLiteral("sarma")) return QStringLiteral("Sarma 法");
        return method;
    }

    static QPointF plotPoint(const QJsonValue& value, bool horizontalReversed,
                             double horizontalMinimum, double horizontalMaximum)
    {
        const QJsonObject p = value.toObject();
        double x = p.value(QStringLiteral("x")).toDouble();
        if (horizontalReversed)
            x = horizontalMinimum + horizontalMaximum - x;
        return QPointF(x, p.value(QStringLiteral("y")).toDouble());
    }

    static vtkSmartPointer<vtkActor> polylineActor(const QJsonArray& pointsArray,
                                                    double r, double g, double b,
                                                    double lineWidth, bool horizontalReversed,
                                                    double horizontalMinimum,
                                                    double horizontalMaximum)
    {
        if (pointsArray.size() < 2)
            return nullptr;
        auto points = vtkSmartPointer<vtkPoints>::New();
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        vtkIdType previous = -1;
        for (const QJsonValue& value : pointsArray)
        {
            const QPointF p = plotPoint(value, horizontalReversed,
                                        horizontalMinimum, horizontalMaximum);
            const vtkIdType current = points->InsertNextPoint(p.x(), p.y(), 0.0);
            if (previous >= 0)
            {
                vtkIdType ids[2]{previous, current};
                lines->InsertNextCell(2, ids);
            }
            previous = current;
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetLineWidth(lineWidth);
        return actor;
    }

    static vtkSmartPointer<vtkActor> slidingBodyActor(const QJsonArray& outlineArray,
                                                       const QJsonArray& arcArray,
                                                       bool horizontalReversed,
                                                       double horizontalMinimum,
                                                       double horizontalMaximum)
    {
        if (outlineArray.size() < 2 || arcArray.size() < 3)
            return nullptr;

        QVector<QPointF> outline;
        QVector<QPointF> arc;
        outline.reserve(outlineArray.size());
        arc.reserve(arcArray.size());
        for (const QJsonValue& value : outlineArray)
            outline.push_back(plotPoint(value, horizontalReversed,
                                        horizontalMinimum, horizontalMaximum));
        for (const QJsonValue& value : arcArray)
            arc.push_back(plotPoint(value, horizontalReversed,
                                    horizontalMinimum, horizontalMaximum));

        const auto nearestIndex = [](const QVector<QPointF>& values, const QPointF& target)
        {
            int best = 0;
            double bestD2 = std::numeric_limits<double>::max();
            for (int i = 0; i < values.size(); ++i)
            {
                const double dx = values[i].x() - target.x();
                const double dy = values[i].y() - target.y();
                const double d2 = dx * dx + dy * dy;
                if (d2 < bestD2)
                {
                    bestD2 = d2;
                    best = i;
                }
            }
            return best;
        };

        const int startIndex = nearestIndex(outline, arc.first());
        const int endIndex = nearestIndex(outline, arc.last());
        QVector<QPointF> polygon = arc;
        if (endIndex >= startIndex)
        {
            for (int i = endIndex; i >= startIndex; --i)
                polygon.push_back(outline[i]);
        }
        else
        {
            for (int i = endIndex; i <= startIndex; ++i)
                polygon.push_back(outline[i]);
        }
        if (polygon.size() < 3)
            return nullptr;

        auto points = vtkSmartPointer<vtkPoints>::New();
        auto polys = vtkSmartPointer<vtkCellArray>::New();
        polys->InsertNextCell(polygon.size());
        for (int i = 0; i < polygon.size(); ++i)
        {
            points->InsertNextPoint(polygon[i].x(), polygon[i].y(), -0.01);
            polys->InsertCellPoint(i);
        }
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetPolys(polys);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(0.95, 0.64, 0.18);
        actor->GetProperty()->SetOpacity(0.22);
        return actor;
    }

    void loadResult()
    {
        m_status = readObject(QDir(m_resultRoot).filePath(QStringLiteral("status.json")));
        m_plot = readObject(QDir(m_resultRoot).filePath(QStringLiteral("plot.json")));
        const QJsonObject result = m_status.value(QStringLiteral("result")).toObject();
        const double factor = result.value(QStringLiteral("factor")).toDouble();
        const bool valid = m_status.value(QStringLiteral("success")).toBool() && factor > 0.0;

        m_factorLabel->setText(valid ? QString::number(factor, 'f', 3) : QStringLiteral("—"));
        const QString method = m_status.value(QStringLiteral("method")).toString();
        m_methodLabel->setText(method.isEmpty() ? QStringLiteral("—") : methodName(method));

        const QJsonObject circle = result.value(QStringLiteral("critical_circle")).toObject();
        if (circle.isEmpty())
            m_circleLabel->setText(QStringLiteral("—"));
        else
            m_circleLabel->setText(
                QStringLiteral("圆心 (%1, %2)，R = %3")
                    .arg(QString::number(circle.value(QStringLiteral("cx")).toDouble(), 'g', 7),
                         QString::number(circle.value(QStringLiteral("cy")).toDouble(), 'g', 7),
                         QString::number(circle.value(QStringLiteral("radius")).toDouble(), 'g', 7)));
        m_openDirectoryButton->setEnabled(QDir(m_resultRoot).exists());
    }

    void applyVisibility()
    {
        if (!m_visualService || !property("postDisplayActive").toBool())
            return;
        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("slope-body")),
            m_showBodyCheck ? m_showBodyCheck->isChecked() : true, false);
        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("slope-outline")),
            m_showOutlineCheck ? m_showOutlineCheck->isChecked() : true, false);
        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("slope-arc")),
            m_showArcCheck ? m_showArcCheck->isChecked() : true, false);
        m_visualService->render();
    }

    void removeActors()
    {
        if (!m_visualService)
            return;
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("slope-body")), false);
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("slope-outline")), false);
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("slope-arc")), false);
        m_bodyActor = nullptr;
        m_outlineActor = nullptr;
        m_arcActor = nullptr;
        m_visualService->render();
    }

    void displayPlot(bool resetCamera)
    {
        if (!m_visualService || m_plot.isEmpty())
            return;
        removeActors();
        const QJsonArray outline = m_plot.value(QStringLiteral("outline")).toArray();
        const QJsonArray arc = m_plot.value(QStringLiteral("slip_arc")).toArray();
        m_bodyActor = slidingBodyActor(outline, arc,
                                       m_horizontalReversed, m_horizontalMinimum,
                                       m_horizontalMaximum);
        m_outlineActor = polylineActor(outline, 0.12, 0.12, 0.12, 2.4,
                                       m_horizontalReversed, m_horizontalMinimum,
                                       m_horizontalMaximum);
        m_arcActor = polylineActor(arc, 0.86, 0.18, 0.16, 3.5,
                                   m_horizontalReversed, m_horizontalMinimum,
                                   m_horizontalMaximum);
        if (m_bodyActor)
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("slope-body")),
                              m_bodyActor, QStringLiteral("滑体区域"), false);
        if (m_outlineActor)
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("slope-outline")),
                              m_outlineActor, QStringLiteral("边坡断面"), false);
        if (m_arcActor)
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("slope-arc")),
                              m_arcActor, QStringLiteral("临界滑弧"), false);
        applyVisibility();
        if (resetCamera && m_visualService->renderer())
            m_visualService->renderer()->ResetCamera();
        m_visualService->render();
    }

    VisualWidgetService* m_visualService{nullptr};
    QString m_analysisUuid;
    QString m_analysisName;
    QString m_resultRoot;
    double m_requiredFactor{1.30};
    bool m_horizontalReversed{false};
    double m_horizontalMinimum{0.0};
    double m_horizontalMaximum{0.0};
    QLabel* m_factorLabel{nullptr};
    QLabel* m_methodLabel{nullptr};
    QLabel* m_circleLabel{nullptr};
    QCheckBox* m_showBodyCheck{nullptr};
    QCheckBox* m_showOutlineCheck{nullptr};
    QCheckBox* m_showArcCheck{nullptr};
    QPushButton* m_openDirectoryButton{nullptr};
    QJsonObject m_status;
    QJsonObject m_plot;
    vtkSmartPointer<vtkActor> m_bodyActor;
    vtkSmartPointer<vtkActor> m_outlineActor;
    vtkSmartPointer<vtkActor> m_arcActor;
};


class CaveBlockStabilityResultPage final : public BasePage
{
  public:
    explicit CaveBlockStabilityResultPage(VisualWidgetService* visualService)
        : BasePage(QString::fromLatin1(kCaveBlockResultPageType)),
          m_visualService(visualService)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto* resultGroup = new QGroupBox(QStringLiteral("结果"), this);
        auto* resultForm = new QFormLayout(resultGroup);
        resultForm->setContentsMargins(10, 8, 10, 8);
        resultForm->setHorizontalSpacing(10);
        resultForm->setVerticalSpacing(6);

        m_beforeLabel = new QLabel(QStringLiteral("—"), resultGroup);
        m_afterLabel = new QLabel(QStringLiteral("—"), resultGroup);
        m_modeLabel = new QLabel(QStringLiteral("—"), resultGroup);
        QFont factorFont = m_beforeLabel->font();
        factorFont.setBold(true);
        factorFont.setPointSize(factorFont.pointSize() + 2);
        m_beforeLabel->setFont(factorFont);
        m_afterLabel->setFont(factorFont);
        resultForm->addRow(QStringLiteral("支护前 FOS"), m_beforeLabel);
        resultForm->addRow(QStringLiteral("支护后 FOS"), m_afterLabel);
        resultForm->addRow(QStringLiteral("滑移模式"), m_modeLabel);
        root->addWidget(resultGroup);

        m_summaryLabel = new QLabel(this);
        m_summaryLabel->setWordWrap(true);
        m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(m_summaryLabel);

        auto* displayGroup = new QGroupBox(QStringLiteral("显示控制"), this);
        auto* displayLayout = new QHBoxLayout(displayGroup);
        displayLayout->setContentsMargins(10, 6, 10, 6);
        displayLayout->setSpacing(12);

        m_showBlockCheck = new QCheckBox(QStringLiteral("滑块"), displayGroup);
        m_showPlanesCheck = new QCheckBox(QStringLiteral("滑块面"), displayGroup);
        m_showDirectionCheck = new QCheckBox(QStringLiteral("滑移方向"), displayGroup);
        m_showBackgroundCheck = new QCheckBox(QStringLiteral("原模型"), displayGroup);
        for (QCheckBox* check : {m_showBlockCheck, m_showPlanesCheck,
                                 m_showDirectionCheck, m_showBackgroundCheck})
        {
            check->setChecked(true);
            QObject::connect(check, &QCheckBox::toggled, this,
                             [this](bool) { applyVisibility(); });
            displayLayout->addWidget(check);
        }
        displayLayout->addStretch();
        root->addWidget(displayGroup);

        auto* tools = new QHBoxLayout();
        tools->setSpacing(6);
        m_openDirectoryButton = new QPushButton(QStringLiteral("结果目录"), this);
        tools->addStretch();
        tools->addWidget(m_openDirectoryButton);
        root->addLayout(tools);
        root->addStretch();

        QObject::connect(m_openDirectoryButton, &QPushButton::clicked, this,
                         [this]() { openResultDirectory(); });
    }

    void setResultContext(const QString& analysisUuid, const QString& analysisName,
                          const QString& resultRoot)
    {
        m_analysisUuid = analysisUuid;
        m_analysisName = analysisName;
        m_resultRoot = QDir(resultRoot).absolutePath();
        loadResult();
        if (property("postDisplayActive").toBool())
            displayResult(true);
    }

    QString analysisUuid() const { return m_analysisUuid; }
    QString resultRoot() const { return m_resultRoot; }

    void refreshResult()
    {
        loadResult();
        if (property("postDisplayActive").toBool())
            displayResult(false);
    }

    void fitView()
    {
        if (!m_visualService)
            m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (m_visualService && m_visualService->renderer())
        {
            m_visualService->renderer()->ResetCamera();
            m_visualService->render();
        }
    }

    void openResultDirectory()
    {
        const QFileInfo resultInfo(resultFilePath());
        const QString dir = resultInfo.exists() ? resultInfo.absolutePath() : m_resultRoot;
        if (!dir.isEmpty() && QDir(dir).exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }

    QJsonObject saveJson() override
    {
        return QJsonObject{
            {QStringLiteral("analysis_uuid"), m_analysisUuid},
            {QStringLiteral("analysis_name"), m_analysisName},
            {QStringLiteral("result_root"), m_resultRoot},
            {QStringLiteral("show_block"), m_showBlockCheck ? m_showBlockCheck->isChecked() : true},
            {QStringLiteral("show_planes"), m_showPlanesCheck ? m_showPlanesCheck->isChecked() : true},
            {QStringLiteral("show_direction"), m_showDirectionCheck ? m_showDirectionCheck->isChecked() : true},
            {QStringLiteral("show_background"), m_showBackgroundCheck ? m_showBackgroundCheck->isChecked() : true}};
    }

    void loadJson(const QJsonObject& object) override
    {
        m_analysisUuid = object.value(QStringLiteral("analysis_uuid")).toString();
        m_analysisName = object.value(QStringLiteral("analysis_name")).toString();
        m_resultRoot = object.value(QStringLiteral("result_root")).toString();
        if (m_showBlockCheck)
            m_showBlockCheck->setChecked(object.value(QStringLiteral("show_block")).toBool(true));
        if (m_showPlanesCheck)
            m_showPlanesCheck->setChecked(object.value(QStringLiteral("show_planes")).toBool(true));
        if (m_showDirectionCheck)
            m_showDirectionCheck->setChecked(object.value(QStringLiteral("show_direction")).toBool(true));
        if (m_showBackgroundCheck)
            m_showBackgroundCheck->setChecked(object.value(QStringLiteral("show_background")).toBool(true));
        loadResult();
    }

    void beforeLoad() override
    {
        setProperty("postDisplayActive", true);
        clearPostDisplayContext();
        if (!m_visualService)
            m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (m_visualService)
            showPostProcessingOnly(m_visualService, false);
        loadResult();
        displayResult(true);
    }

    void beforeUnload() override
    {
        setProperty("postDisplayActive", false);
        setPostViewActive(false);
        clearPostDisplayContext();
        removeActors();
    }

  private:
    struct PlaneTriangle
    {
        double p[3][3]{};
    };

    struct PlaneSegment
    {
        double p[2][3]{};
    };

    static QString numberPattern()
    {
        return QStringLiteral(R"(([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?))");
    }

    static QString capturedNumber(const QString& text, const QString& prefix)
    {
        const QRegularExpression re(prefix + QStringLiteral(R"(\s*=\s*)") + numberPattern(),
                                    QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
    }

    QString workingFilePath(const QString& fileName) const
    {
        if (m_resultRoot.isEmpty() || fileName.trimmed().isEmpty())
            return {};
        const QString nested =
            QDir(m_resultRoot).filePath(QStringLiteral("SliderCase/") + fileName);
        if (QFileInfo::exists(nested))
            return nested;
        return QDir(m_resultRoot).filePath(fileName);
    }

    QString configuredMesg227FileName(int lineIndex, const QString& fallback) const
    {
        QFile mesg(workingFilePath(QStringLiteral("MESG227.DAT")));
        if (!mesg.open(QIODevice::ReadOnly | QIODevice::Text))
            return fallback;
        const QStringList lines = QString::fromLocal8Bit(mesg.readAll())
                                      .split(QRegularExpression(QStringLiteral("\\r?\\n")));
        if (lineIndex < 0 || lineIndex >= lines.size())
            return fallback;
        QString val = lines.at(lineIndex).trimmed();
        if (val.size() >= 2 && val.startsWith(QLatin1Char('\'')) && val.endsWith(QLatin1Char('\'')))
            val = val.mid(1, val.size() - 2).trimmed();
        return (val.isEmpty() || val == QStringLiteral("---")) ? fallback : val;
    }

    QString resultFilePath() const
    {
        return workingFilePath(configuredMesg227FileName(2, QStringLiteral("SAFE_CAVEBLK.DAT")));
    }

    static QString formatFactor(const QString& value)
    {
        bool ok = false;
        const double factor = value.toDouble(&ok);
        return ok ? QString::number(factor, 'f', 4) : QStringLiteral("—");
    }

    static QVector<double> numbersFromLine(const QString& line)
    {
        static const QRegularExpression re(
            QStringLiteral(R"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?)"));
        QVector<double> values;
        auto it = re.globalMatch(line);
        while (it.hasNext())
        {
            bool ok = false;
            const double value = it.next().captured(0).toDouble(&ok);
            if (ok)
                values.push_back(value);
        }
        return values;
    }

    QVector<int> readBlockElementIds() const
    {
        QVector<int> ids;
        QFile file(workingFilePath(QStringLiteral("JME_CUT.DAT")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return ids;

        const QStringList lines =
            QString::fromLocal8Bit(file.readAll()).split(
                QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
        for (int i = 1; i < lines.size(); ++i)
        {
            const QVector<double> values = numbersFromLine(lines[i]);
            if (!values.isEmpty())
            {
                const int id = qRound64(values[0]);
                if (id > 0)
                    ids.push_back(id);
            }
        }
        return ids;
    }

    QVector<PlaneSegment> readPlaneSegments() const
    {
        QVector<PlaneSegment> segments;
        QFile file(workingFilePath(configuredMesg227FileName(1, QStringLiteral("ICUT.DAT"))));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return segments;

        const QStringList lines = QString::fromUtf8(file.readAll()).split(
            QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
        for (int i = 2; i < lines.size(); ++i)
        {
            const QVector<double> values = numbersFromLine(lines[i]);
            if (values.size() < 5 || values.size() >= 9)
                continue;
            PlaneSegment segment;
            segment.p[0][0] = values[0];
            segment.p[0][1] = values[1];
            segment.p[0][2] = 0.0;
            segment.p[1][0] = values[2];
            segment.p[1][1] = values[3];
            segment.p[1][2] = 0.0;
            segments.push_back(segment);
        }
        return segments;
    }

    QVector<PlaneTriangle> readPlaneTriangles() const
    {
        QVector<PlaneTriangle> planes;
        QFile file(workingFilePath(configuredMesg227FileName(1, QStringLiteral("ICUT.DAT"))));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return planes;

        const QStringList lines =
            QString::fromUtf8(file.readAll()).split(
                QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
        // 前两行分别是 NCUT/METHOD 和列名。METHOD=1 时每个滑块面由三点定义。
        for (int i = 2; i < lines.size(); ++i)
        {
            const QVector<double> values = numbersFromLine(lines[i]);
            if (values.size() < 9)
                continue;
            PlaneTriangle plane;
            for (int p = 0; p < 3; ++p)
                for (int c = 0; c < 3; ++c)
                    plane.p[p][c] = values[p * 3 + c];
            planes.push_back(plane);
        }
        return planes;
    }

    vtkSmartPointer<vtkActor> gridActor(vtkUnstructuredGrid* grid,
                                        double r, double g, double b,
                                        double opacity, bool edges) const
    {
        if (!grid || grid->GetNumberOfCells() <= 0)
            return nullptr;
        auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
        mapper->SetInputData(grid);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetOpacity(opacity);
        actor->GetProperty()->SetInterpolationToPhong();
        if (edges)
        {
            actor->GetProperty()->EdgeVisibilityOn();
            actor->GetProperty()->SetEdgeColor(0.12, 0.12, 0.12);
            actor->GetProperty()->SetLineWidth(1.5);
        }
        return actor;
    }

    vtkSmartPointer<vtkActor> segmentActor(const PlaneSegment& segment, int index) const
    {
        auto points = vtkSmartPointer<vtkPoints>::New();
        points->InsertNextPoint(segment.p[0]);
        points->InsertNextPoint(segment.p[1]);
        vtkIdType ids[2]{0, 1};
        auto lines = vtkSmartPointer<vtkCellArray>::New();
        lines->InsertNextCell(2, ids);
        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        static const double colors[][3] = {
            {0.90, 0.24, 0.18},
            {0.96, 0.58, 0.10},
            {0.55, 0.28, 0.82},
            {0.13, 0.55, 0.76}};
        const auto& color = colors[index % 4];
        actor->GetProperty()->LightingOff();
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);
        actor->GetProperty()->SetLineWidth(4.0);
        return actor;
    }

    vtkSmartPointer<vtkActor> planeActor(const PlaneTriangle& plane, int index,
                                         double offset) const
    {
        double p1[3]{plane.p[0][0], plane.p[0][1], plane.p[0][2]};
        double p2[3]{plane.p[1][0], plane.p[1][1], plane.p[1][2]};
        double p3[3]{plane.p[2][0], plane.p[2][1], plane.p[2][2]};

        double v1[3]{p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
        double v2[3]{p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]};
        double normal[3]{};
        vtkMath::Cross(v1, v2, normal);
        if (vtkMath::Normalize(normal) > 0.0 && offset > 0.0)
        {
            // ICUT 三点顺序约定法向指向块体内部；显示时向外轻微偏移，
            // 避免与滑块表面完全重合造成闪烁。
            for (int c = 0; c < 3; ++c)
            {
                p1[c] -= normal[c] * offset;
                p2[c] -= normal[c] * offset;
                p3[c] -= normal[c] * offset;
            }
        }

        auto points = vtkSmartPointer<vtkPoints>::New();
        points->InsertNextPoint(p1);
        points->InsertNextPoint(p2);
        points->InsertNextPoint(p3);

        vtkIdType ids[3]{0, 1, 2};
        auto polys = vtkSmartPointer<vtkCellArray>::New();
        polys->InsertNextCell(3, ids);

        auto poly = vtkSmartPointer<vtkPolyData>::New();
        poly->SetPoints(points);
        poly->SetPolys(polys);

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(poly);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        static const double colors[][3] = {
            {0.90, 0.24, 0.18},
            {0.96, 0.58, 0.10},
            {0.55, 0.28, 0.82},
            {0.13, 0.55, 0.76}};
        const auto& color = colors[index % 4];
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);
        actor->GetProperty()->SetOpacity(0.55);
        actor->GetProperty()->EdgeVisibilityOn();
        actor->GetProperty()->SetEdgeColor(color[0], color[1], color[2]);
        actor->GetProperty()->SetLineWidth(3.0);
        return actor;
    }

    vtkSmartPointer<vtkActor> directionActor(const double center[3],
                                             double length) const
    {
        if (!m_hasSlipDirection || length <= 0.0)
            return nullptr;

        double direction[3]{m_slipDirection[0], m_slipDirection[1], m_slipDirection[2]};
        if (vtkMath::Normalize(direction) <= 0.0)
            return nullptr;

        double helper[3]{0.0, 0.0, 1.0};
        if (std::abs(vtkMath::Dot(direction, helper)) > 0.95)
        {
            helper[0] = 0.0;
            helper[1] = 1.0;
            helper[2] = 0.0;
        }
        double yAxis[3]{};
        vtkMath::Cross(helper, direction, yAxis);
        if (vtkMath::Normalize(yAxis) <= 0.0)
            return nullptr;
        double zAxis[3]{};
        vtkMath::Cross(direction, yAxis, zAxis);
        vtkMath::Normalize(zAxis);

        auto matrix = vtkSmartPointer<vtkMatrix4x4>::New();
        matrix->Identity();
        for (int row = 0; row < 3; ++row)
        {
            matrix->SetElement(row, 0, direction[row] * length);
            matrix->SetElement(row, 1, yAxis[row] * length);
            matrix->SetElement(row, 2, zAxis[row] * length);
            matrix->SetElement(row, 3, center[row]);
        }

        auto arrow = vtkSmartPointer<vtkArrowSource>::New();
        arrow->SetShaftRadius(0.025);
        arrow->SetTipRadius(0.075);
        arrow->SetTipLength(0.28);

        auto transform = vtkSmartPointer<vtkTransform>::New();
        transform->SetMatrix(matrix);

        auto transformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
        transformFilter->SetInputConnection(arrow->GetOutputPort());
        transformFilter->SetTransform(transform);
        transformFilter->Update();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(transformFilter->GetOutputPort());
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(0.10, 0.55, 0.20);
        return actor;
    }

    static QString modeName(int mode)
    {
        switch (mode)
        {
        case 0: return QStringLiteral("稳定");
        case 1: return QStringLiteral("单面滑动");
        case 2: return QStringLiteral("多面滑动");
        case 3: return QStringLiteral("悬吊");
        case 4: return QStringLiteral("稳定");
        case 5: return QStringLiteral("指定方向滑动");
        default: return mode > 0 ? QStringLiteral("模式 %1").arg(mode) : QStringLiteral("—");
        }
    }

    void loadResult()
    {
        m_hasSlipDirection = false;
        const QString path = resultFilePath();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            m_beforeLabel->setText(QStringLiteral("—"));
            m_afterLabel->setText(QStringLiteral("—"));
            m_modeLabel->setText(QStringLiteral("—"));
            m_summaryLabel->setText(QStringLiteral("暂无可用结果。"));
            if (m_openDirectoryButton)
                m_openDirectoryButton->setEnabled(QDir(m_resultRoot).exists());
            return;
        }

        const QString text = QString::fromLocal8Bit(file.readAll());
        file.close();

        QString before;
        QString after;
        QString mode;
        QString resisting;
        QString driving;
        const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
        for (const QString& rawLine : lines)
        {
            const QString line = rawLine.trimmed();
            if (line.isEmpty())
                continue;
            if (line.contains(QStringLiteral("支护前安全系数")))
                before = capturedNumber(line, QStringLiteral("FOS"));
            else if (line.contains(QStringLiteral("支护后安全系数")))
                after = capturedNumber(line, QStringLiteral("FOS"));

            if (line.contains(QStringLiteral("滑移模式KTMODE")))
            {
                const QRegularExpression re(
                    QStringLiteral(R"(KTMODE\s*=\s*(\d+)\s*(.*)$)"),
                    QRegularExpression::CaseInsensitiveOption);
                const auto match = re.match(line);
                if (match.hasMatch())
                    mode = modeName(match.captured(1).toInt());
            }
            if (line.contains(QStringLiteral("总抗滑力")))
                resisting = capturedNumber(line, QStringLiteral("总抗滑力"));
            if (line.contains(QStringLiteral("总滑动力")))
                driving = capturedNumber(line, QStringLiteral("总滑动力"));

            if (line.contains(QStringLiteral("滑移方向"))
                || line.contains(QStringLiteral("滑动方向")))
            {
                const int equalPos = line.indexOf(QLatin1Char('='));
                const QVector<double> values =
                    numbersFromLine(equalPos >= 0 ? line.mid(equalPos + 1) : line);
                if (values.size() >= 2)
                {
                    m_slipDirection[0] = values.value(0);
                    m_slipDirection[1] = values.value(1);
                    m_slipDirection[2] = values.size() >= 3 ? values.value(2) : 0.0;
                    m_hasSlipDirection = true;
                }
            }
        }

        // 兼容仅保留 FOS=... 而没有中文前缀的旧输出。
        if (before.isEmpty() && after.isEmpty())
        {
            const QRegularExpression re(QStringLiteral(R"(FOS\s*=\s*)") + numberPattern(),
                                        QRegularExpression::CaseInsensitiveOption);
            auto it = re.globalMatch(text);
            QStringList values;
            while (it.hasNext())
                values.push_back(it.next().captured(1));
            if (!values.isEmpty()) before = values.value(0);
            if (values.size() > 1) after = values.value(1);
        }

        m_beforeLabel->setText(formatFactor(before));
        m_afterLabel->setText(formatFactor(after));
        m_afterLabel->setVisible(!after.isEmpty());
        m_modeLabel->setText(mode.isEmpty() ? QStringLiteral("—") : mode);

        QStringList summary;
        if (!resisting.isEmpty())
            summary << QStringLiteral("抗滑力 %1 kN").arg(resisting);
        if (!driving.isEmpty())
            summary << QStringLiteral("滑动力 %1 kN").arg(driving);
        m_summaryLabel->setText(summary.join(QStringLiteral("  ·  ")));
        if (m_openDirectoryButton)
            m_openDirectoryButton->setEnabled(QDir(m_resultRoot).exists());
    }

    void applyVisibility()
    {
        if (!m_visualService || !property("postDisplayActive").toBool())
            return;

        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("cave-block-background")),
            m_showBackgroundCheck ? m_showBackgroundCheck->isChecked() : true, false);
        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("cave-block-body")),
            m_showBlockCheck ? m_showBlockCheck->isChecked() : true, false);
        m_visualService->setVisible(
            postActorId(getUuid(), QStringLiteral("cave-block-direction")),
            m_showDirectionCheck ? m_showDirectionCheck->isChecked() : true, false);
        const bool showPlanes = m_showPlanesCheck ? m_showPlanesCheck->isChecked() : true;
        for (int i = 0; i < m_planeActorCount; ++i)
        {
            m_visualService->setVisible(
                postActorId(getUuid(), QStringLiteral("cave-block-plane-%1").arg(i + 1)),
                showPlanes, false);
        }
        for (int i = 0; i < m_segmentActorCount; ++i)
        {
            m_visualService->setVisible(
                postActorId(getUuid(), QStringLiteral("cave-block-segment-%1").arg(i + 1)),
                showPlanes, false);
        }
        m_visualService->render();
    }

    void removeActors()
    {
        if (!m_visualService)
            return;
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("cave-block-background")), false);
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("cave-block-body")), false);
        removeActorByService(m_visualService,
                             postActorId(getUuid(), QStringLiteral("cave-block-direction")), false);
        for (int i = 0; i < m_planeActorCount; ++i)
        {
            removeActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("cave-block-plane-%1").arg(i + 1)), false);
        }
        for (int i = 0; i < m_segmentActorCount; ++i)
        {
            removeActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("cave-block-segment-%1").arg(i + 1)), false);
        }
        m_backgroundActor = nullptr;
        m_blockActor = nullptr;
        m_directionActor = nullptr;
        m_planeActors.clear();
        m_segmentActors.clear();
        m_planeActorCount = 0;
        m_segmentActorCount = 0;
        m_visualService->render();
    }

    void displayResult(bool resetCamera)
    {
        if (!m_visualService)
            return;

        removeActors();

        auto* meshService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkSmartPointer<vtkUnstructuredGrid> grid =
            meshService ? meshService->getVtkUnstructuredGrid() : nullptr;
        if (!grid || grid->GetNumberOfCells() <= 0)
        {
            m_visualService->render();
            return;
        }

        const QVector<int> blockElementIds = readBlockElementIds();
        if (blockElementIds.isEmpty())
        {
            // Step1 未完成时不伪造滑块，只保持结果页文字信息。
            m_visualService->render();
            return;
        }

        int targetDim =
            PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
        if (targetDim <= 0)
        {
            for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
            {
                if (vtkCell* cell = grid->GetCell(cellId))
                    targetDim = qMax(targetDim, cell->GetCellDimension());
            }
        }
        const QFileInfo sourceInfo(meshService->sourceMeshFilePath());
        const bool keepSourceElementIds =
            sourceInfo.exists()
            && sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;

        vtkDataArray* elemIdArray =
            vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

        QHash<int, vtkIdType> calculationIdToCell;
        int exportedElementIndex = 0;
        int fullDimensionCellCount = 0;
        for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
        {
            vtkCell* cell = grid->GetCell(cellId);
            if (!cell)
                continue;
            const int cellDim = cell->GetCellDimension();
            if (cellDim != targetDim && cellDim != 1)
                continue;

            ++exportedElementIndex;
            if (cellDim != targetDim)
                continue;

            ++fullDimensionCellCount;
            const int sourceElemId =
                elemIdArray ? int(elemIdArray->GetTuple1(cellId)) : int(cellId + 1);
            const int calculationElemId =
                keepSourceElementIds ? sourceElemId : exportedElementIndex;
            if (calculationElemId > 0 && !calculationIdToCell.contains(calculationElemId))
                calculationIdToCell.insert(calculationElemId, cellId);
        }

        auto selectedIds = vtkSmartPointer<vtkIdList>::New();
        for (int elementId : blockElementIds)
        {
            const auto it = calculationIdToCell.constFind(elementId);
            if (it != calculationIdToCell.constEnd())
                selectedIds->InsertNextId(it.value());
        }
        if (selectedIds->GetNumberOfIds() <= 0)
        {
            qWarning() << "块体稳定后处理：JME_CUT.DAT 中的单元号无法映射到当前网格";
            m_visualService->render();
            return;
        }

        auto extract = vtkSmartPointer<vtkExtractCells>::New();
        extract->SetInputData(grid);
        extract->SetCellList(selectedIds);
        extract->Update();

        auto blockGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        blockGrid->ShallowCopy(extract->GetOutput());

        if (selectedIds->GetNumberOfIds() < fullDimensionCellCount)
        {
            m_backgroundActor = gridActor(grid, 0.72, 0.74, 0.78, 0.16, false);
            if (m_backgroundActor)
            {
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("cave-block-background")),
                    m_backgroundActor, QStringLiteral("原模型"), false);
            }
        }

        m_blockActor = gridActor(blockGrid, 0.18, 0.58, 0.86, 0.72, true);
        if (m_blockActor)
        {
            addActorByService(m_visualService,
                              postActorId(getUuid(), QStringLiteral("cave-block-body")),
                              m_blockActor, QStringLiteral("滑块"), false);
        }

        double bounds[6]{};
        blockGrid->GetBounds(bounds);
        const double center[3]{
            0.5 * (bounds[0] + bounds[1]),
            0.5 * (bounds[2] + bounds[3]),
            0.5 * (bounds[4] + bounds[5])};
        const double dx = bounds[1] - bounds[0];
        const double dy = bounds[3] - bounds[2];
        const double dz = bounds[5] - bounds[4];
        const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double faceOffset = diagonal > 0.0 ? diagonal * 0.002 : 0.0;

        const QVector<PlaneSegment> segments = readPlaneSegments();
        m_segmentActorCount = segments.size();
        m_segmentActors.reserve(segments.size());
        for (int i = 0; i < segments.size(); ++i)
        {
            auto actor = segmentActor(segments[i], i);
            m_segmentActors.push_back(actor);
            if (actor)
            {
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("cave-block-segment-%1").arg(i + 1)),
                    actor, QStringLiteral("滑块面 %1").arg(i + 1), false);
            }
        }

        const QVector<PlaneTriangle> planes = readPlaneTriangles();
        m_planeActorCount = planes.size();
        m_planeActors.reserve(planes.size());
        for (int i = 0; i < planes.size(); ++i)
        {
            auto actor = planeActor(planes[i], i, faceOffset);
            m_planeActors.push_back(actor);
            if (actor)
            {
                addActorByService(
                    m_visualService,
                    postActorId(getUuid(), QStringLiteral("cave-block-plane-%1").arg(i + 1)),
                    actor, QStringLiteral("滑块面 %1").arg(i + 1), false);
            }
        }

        m_directionActor = directionActor(center, diagonal * 0.45);
        if (m_directionActor)
        {
            addActorByService(
                m_visualService,
                postActorId(getUuid(), QStringLiteral("cave-block-direction")),
                m_directionActor, QStringLiteral("滑移方向"), false);
        }

        applyVisibility();
        if (resetCamera && m_visualService->renderer())
            m_visualService->renderer()->ResetCamera();
        m_visualService->render();
    }

    VisualWidgetService* m_visualService{nullptr};
    QString m_analysisUuid;
    QString m_analysisName;
    QString m_resultRoot;
    QLabel* m_beforeLabel{nullptr};
    QLabel* m_afterLabel{nullptr};
    QLabel* m_modeLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QCheckBox* m_showBlockCheck{nullptr};
    QCheckBox* m_showPlanesCheck{nullptr};
    QCheckBox* m_showDirectionCheck{nullptr};
    QCheckBox* m_showBackgroundCheck{nullptr};
    QPushButton* m_openDirectoryButton{nullptr};

    bool m_hasSlipDirection{false};
    double m_slipDirection[3]{0.0, 0.0, 0.0};
    int m_planeActorCount{0};
    int m_segmentActorCount{0};
    vtkSmartPointer<vtkActor> m_backgroundActor;
    vtkSmartPointer<vtkActor> m_blockActor;
    vtkSmartPointer<vtkActor> m_directionActor;
    QVector<vtkSmartPointer<vtkActor>> m_planeActors;
    QVector<vtkSmartPointer<vtkActor>> m_segmentActors;
};
} // namespace

VisualWidgetService* PostProcessing::visualService()
{
    if (!m_visualService)
    {
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (!m_visualService)
        {
            qWarning() << "VisualWidgetService is not registered.";
        }
    }
    return m_visualService;
}

void PostProcessing::initPageFactory()
{
    m_pageFactory.clear();
    m_pageFactory[PublicConstants::kPagePostProcessNephogram] = [this](VisualWidgetService* host)
    {
        return new NephogramSettingWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessVector] = [this](VisualWidgetService* host)
    {
        return new VectorSettingWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessSlice] = [this](VisualWidgetService* host)
    {
        return new SliceSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessBlock] = [this](VisualWidgetService* host)
    {
        return new BlockSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessIsosurface] = [this](VisualWidgetService* host)
    {
        return new IsosurfaceSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessIsosurfaceSegmentation] =
        [this](VisualWidgetService* host)
    {
        return new IsosurfaceSegmentationWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessAnimation] = [this](VisualWidgetService* host)
    {
        return new AnimationAviWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessDerivative] =
        [this](VisualWidgetService* /*host*/)
    {
        return new DerivedValueSettingWidget();
    };
    m_pageFactory[PublicConstants::kPagePostProcessCurve] =
        [this](VisualWidgetService* host)
    {
        return new CurveSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessContourReader] =
        [this](VisualWidgetService* /*host*/)
    {
        return new PostContourReaderWidget();
    };
    m_pageFactory[QString::fromLatin1(kSlopeLimitResultPageType)] =
        [](VisualWidgetService* host)
    {
        return new SlopeLimitEquilibriumResultPage(host);
    };
    m_pageFactory[QString::fromLatin1(kCaveBlockResultPageType)] =
        [](VisualWidgetService* host)
    {
        return new CaveBlockStabilityResultPage(host);
    };
}

BasePage* PostProcessing::createPageByType(const QString& type)
{
    auto it = m_pageFactory.find(type);
    return it == m_pageFactory.end() ? nullptr : it.value()(visualService());
}

void PostProcessing::configureNode(ProjectNode* node, const QString& type)
{
    if (!node)
        return;

    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, QStringLiteral("PostProcessing"));
    node->setData(0, ProjectNode::RoleKind, type);
}

ProjectNode* PostProcessing::createPostNode(const QString& type, const QString& title,
                                            const QString& uuid, bool select)
{
    if (!m_root)
        return nullptr;

    BasePage* page = createPageByType(type);
    if (!page)
    {
        qWarning() << "Unsupported post-processing page type:" << type;
        return nullptr;
    }

    const QString id = uuid.isEmpty() ? QUuid::createUuid().toString() : uuid;
    ProjectNode* node = new ProjectNode(title, id, page);
    configureNode(node, type);
    m_root->addChild(node);

    if (select)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }

    return node;
}

ProjectNode* PostProcessing::findChildByUuid(const QString& uuid) const
{
    if (uuid.isEmpty() || !m_root)
        return nullptr;

    for (int i = 0; i < m_root->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        if (nodeUuid == uuid)
            return node;
    }

    return nullptr;
}

ProjectNode* PostProcessing::findChildByType(const QString& type) const
{
    if (type.isEmpty() || !m_root)
        return nullptr;

    for (int i = 0; i < m_root->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;
        if (node->data(0, ProjectNode::RoleKind).toString() == type)
            return node;
    }

    return nullptr;
}

PostProcessing::PostProcessing()
{
    m_visualService = nullptr;

    ServiceRegistry::instance().registerService<PostProcessingService>(this);
    UiFrameService* uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();

    ProbeSettingsWidget* tmp = new ProbeSettingsWidget(visualService());
    if (uiFrameService)
        uiFrameService->addWidget(tmp, "物理量");

    m_displayControlWidget = new PostDisplayControlWidget(visualService());
    if (uiFrameService)
    {
        uiFrameService->addWidget(m_displayControlWidget, QStringLiteral("结果显示"));
        uiFrameService->setWidgetVisible(m_displayControlWidget, false);
    }

    initPageFactory();
    initAction();
    initTreeMenu();
}

void PostProcessing::newNephogramSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessNephogram, QStringLiteral("云图"));
}

void PostProcessing::newVectorSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessVector, QStringLiteral("矢量"));
}

void PostProcessing::newSliceSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessSlice, QStringLiteral("切面"));
}

void PostProcessing::newBlockSettingsWidget()
{
    createPostNode(PublicConstants::kPagePostProcessBlock, QStringLiteral("切块"));
}

void PostProcessing::newIsosurfaceSettingsWidget()
{
    createPostNode(PublicConstants::kPagePostProcessIsosurface, QStringLiteral("等值面"));
}

void PostProcessing::newIsosurfaceSegmentationWidget()
{
    createPostNode(PublicConstants::kPagePostProcessIsosurfaceSegmentation,
                   QStringLiteral("等值面分割"));
}

void PostProcessing::newAnimationAviWidget()
{
    createPostNode(PublicConstants::kPagePostProcessAnimation, QStringLiteral("动画设置"));
}

// void PostProcessing::newProbeSettingsWidget()
//{

//}

void PostProcessing::newDerivedValueSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessDerivative, QStringLiteral("派生值"));
}

void PostProcessing::newCurveSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessCurve, QStringLiteral("计算曲线"));
}

void PostProcessing::newPostContourReaderWidget()
{
    createPostNode(PublicConstants::kPagePostProcessContourReader, QStringLiteral("洞室变形轮廓图"));
}

void PostProcessing::newProject()
{
    if (m_root)
        closeProject();

    PostResultSelectionUtils::clearSharedResultSelection();
    postScalarBarStyle() = PostScalarBarStyle{};
    postContourStyle() = PostContourStyle{};
    postSceneStyle() = PostSceneStyle{};
    postDisplaySettingsAuthoritative() = true;
    postFieldRangeStyles().clear();
    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    m_root = new ProjectNode("后处理");
    m_root->setData(0, ProjectNode::RoleUuid, m_root->getUuid());
    m_root->setData(0, ProjectNode::RoleModule, "PostProcessing");
    m_root->setData(0, ProjectNode::RoleKind, "PostProcessingRoot");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    if (projectManagerService)
        projectManagerService->addTreeItem(m_root);
}

void PostProcessing::initAction()
{
    const QString initialProject =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString initialMatype =
        PublicVariables::instance().get(QStringLiteral("post.result.filter.matype.checked")).toString();
    const QString initialLayer =
        PublicVariables::instance().get(QStringLiteral("post.result.filter.layer.checked")).toString();

    EventManager::instance().registerHandler(
        QString::fromLatin1(kSlopeLimitResultReadyEvent), this,
        [this](const QVariantMap& data)
        {
            if (!m_root)
                return;
            const QString analysisUuid = data.value(QStringLiteral("analysisUuid")).toString();
            const QString analysisName = data.value(QStringLiteral("analysisName")).toString();
            const QString resultRoot = data.value(QStringLiteral("resultRoot")).toString();
            const double requiredFactor = data.value(QStringLiteral("requiredFactor"), QVariant(1.30)).toDouble();
            const bool horizontalReversed =
                data.value(QStringLiteral("horizontalReversed"), QVariant(false)).toBool();
            const double horizontalMinimum =
                data.value(QStringLiteral("horizontalMinimum"), QVariant(0.0)).toDouble();
            const double horizontalMaximum =
                data.value(QStringLiteral("horizontalMaximum"), QVariant(0.0)).toDouble();
            if (analysisUuid.isEmpty() || resultRoot.isEmpty())
                return;

            ProjectNode* resultNode = findSpecialResultChild(
                m_root, QString::fromLatin1(kSlopeLimitResultPageType),
                analysisUuid, resultRoot);
            auto* resultPage = resultNode
                                   ? dynamic_cast<SlopeLimitEquilibriumResultPage*>(resultNode->getPage())
                                   : nullptr;

            const QString nodeTitle = specialResultTitle(
                analysisName, QStringLiteral("极限平衡结果"));
            if (!resultNode)
            {
                resultNode = createPostNode(QString::fromLatin1(kSlopeLimitResultPageType),
                                            nodeTitle, QString(), false);
                resultPage = resultNode
                                 ? dynamic_cast<SlopeLimitEquilibriumResultPage*>(resultNode->getPage())
                                 : nullptr;
            }
            else
            {
                resultNode->setText(0, nodeTitle);
                resultNode->setName(nodeTitle);
                if (resultNode->getPage())
                    resultNode->getPage()->setDisplayName(nodeTitle);
            }

            if (!resultNode || !resultPage)
                return;
            resultPage->applyDeferredLoadJson();
            resultPage->setResultContext(analysisUuid, analysisName, resultRoot, requiredFactor,
                                         horizontalReversed, horizontalMinimum,
                                         horizontalMaximum);
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(resultNode);
        });

    EventManager::instance().registerHandler(
        postResultFilterChangedEvent(), this,
        [this, lastProject = initialProject, lastMatype = initialMatype,
         lastLayer = initialLayer](const QVariantMap& values) mutable
        {
            if (!postSceneStyle().followMeshFilter)
                return;

            // 后处理筛选状态来自结果自身 MATYPE/LAYER，不再由 Mesh 数据驱动。
            const QString project =
                PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
            const QString matype = values.value(QStringLiteral("matype")).toString();
            const QString layer = values.value(QStringLiteral("layer")).toString();
            if (project == lastProject && matype == lastMatype && layer == lastLayer)
                return;

            lastProject = project;
            lastMatype = matype;
            lastLayer = layer;
            notifyPostDisplaySettingsChanged(QStringLiteral("domain-filter"));
            if (m_displayControlWidget)
                m_displayControlWidget->syncFromState();
        });

    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessDisplayControl, this,
        [this](const QVariantMap&)
        {
            if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
                if (m_displayControlWidget)
                {
                    m_displayControlWidget->syncFromState();
                    uiFrame->setWidgetVisible(m_displayControlWidget, true);
                }
        });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessNephogram, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newNephogramSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessVector, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newVectorSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessSection, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newSliceSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDice, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newBlockSettingsWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessIsosurface, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newIsosurfaceSettingsWidget();
                                             });
    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessIsosurfaceSegmentation, this,
        [this](const QVariantMap&)
        {
            this->newIsosurfaceSegmentationWidget();
        });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessXyPlot, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newCurveSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessAnimation, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newAnimationAviWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessPickPhysical, this,
                                             [this](const QVariantMap&)
                                             {
                                                 //        this->ne();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDerivative, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newDerivedValueSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDataExport, this,
                                             [this](const QVariantMap&)
                                             {
                                                 //        this->newNephogramSettingWidget();
                                             });
    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessContourReader, this,
        [this](const QVariantMap&)
        {
            this->newPostContourReaderWidget();
        });
    EventManager::instance().registerHandler(
        PublicConstants::kEventSolverFinished, this,
        [this](const QVariantMap& data)
        {
            const QString analysisKind = data.value(QStringLiteral("analysisKind")).toString();
            if (analysisKind == QStringLiteral("CaveBlockStabilityAnalysis"))
            {
                if (!m_root)
                    return;
                const QString analysisUuid = data.value(QStringLiteral("analysisUuid")).toString();
                const QString analysisName = data.value(QStringLiteral("analysisName")).toString().trimmed();
                const QString resultRoot = data.value(QStringLiteral("resultRoot")).toString().trimmed();
                if (analysisUuid.isEmpty() || resultRoot.isEmpty())
                    return;

                ProjectNode* resultNode = findSpecialResultChild(
                    m_root, QString::fromLatin1(kCaveBlockResultPageType),
                    analysisUuid, resultRoot);
                auto* resultPage = resultNode
                                       ? dynamic_cast<CaveBlockStabilityResultPage*>(resultNode->getPage())
                                       : nullptr;

                const QString nodeTitle = specialResultTitle(
                    analysisName, QStringLiteral("块体稳定结果"));
                if (!resultNode)
                {
                    resultNode = createPostNode(QString::fromLatin1(kCaveBlockResultPageType),
                                                nodeTitle, QString(), false);
                    resultPage = resultNode
                                     ? dynamic_cast<CaveBlockStabilityResultPage*>(resultNode->getPage())
                                     : nullptr;
                }
                else
                {
                    resultNode->setText(0, nodeTitle);
                    resultNode->setName(nodeTitle);
                    if (resultNode->getPage())
                        resultNode->getPage()->setDisplayName(nodeTitle);
                }

                if (!resultNode || !resultPage)
                    return;
                resultPage->applyDeferredLoadJson();
                resultPage->setResultContext(analysisUuid, analysisName, resultRoot);
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(resultNode);
                return;
            }

            QString projectDir = data.value(QStringLiteral("projectDir")).toString().trimmed();
            if (projectDir.isEmpty())
            {
                projectDir = PublicVariables::instance()
                                 .get(PublicConstants::kProjectDirectoryKey)
                                 .toString()
                                 .trimmed();
            }

            projectDir = QDir(projectDir).absolutePath();
            QString resultRoot = data.value(QStringLiteral("resultRoot")).toString().trimmed();
            if (resultRoot.isEmpty())
                resultRoot = projectDir;
            resultRoot = QDir(resultRoot).absolutePath();
            if (projectDir.isEmpty() || resultRoot.isEmpty())
                return;
            prepareResultRootAsync(projectDir, resultRoot);
        });
}

void PostProcessing::prepareResultRootAsync(const QString& projectDir, const QString& resultRoot)
{
    const QString projectPath = QDir(projectDir).absolutePath();
    const QString rootPath = QDir(resultRoot).absolutePath();
    const QString key = QDir::fromNativeSeparators(rootPath).toCaseFolded();
    if (projectPath.isEmpty() || rootPath.isEmpty())
        return;

    if (m_preparingResultRoots.contains(key))
    {
        m_pendingResultRoots.insert(key);
        return;
    }

    m_preparingResultRoots.insert(key);
    const auto prepared = QSharedPointer<bool>::create(false);
    const auto summary = QSharedPointer<PostResultAutoPrepare::PreparedResultSummary>::create();
    const QString preferredResultFile =
        PostResultSelectionUtils::sharedResultSelection().resultFile;
    QThread* worker = QThread::create(
        [projectPath, rootPath, preferredResultFile, prepared, summary]()
        {
            *prepared = PostResultAutoPrepare::prepareResultRoot(projectPath, rootPath);
            if (!*prepared)
                return;

            *summary = PostResultAutoPrepare::preparedResultSummary(projectPath, rootPath);

            // 只在求解后的后处理结果已经完整生成以后预读；不在这里创建任何
            // 持久二进制副本，避免把异常/半成品结果缓存下来。
            QString warmFile = preferredResultFile;
            const QString foldedRoot =
                QDir::fromNativeSeparators(QDir(rootPath).absolutePath()).toCaseFolded();
            const QString foldedWarm =
                warmFile.isEmpty()
                    ? QString()
                    : QDir::fromNativeSeparators(QFileInfo(warmFile).absoluteFilePath())
                          .toCaseFolded();
            if (warmFile.isEmpty() || !QFileInfo::exists(warmFile) ||
                (foldedWarm != foldedRoot &&
                 !foldedWarm.startsWith(foldedRoot + QLatin1Char('/'))))
            {
                warmFile.clear();
                const auto groups = PostResultAutoPrepare::resultGroups(projectPath);
                for (const auto& group : groups)
                {
                    const QString foldedSource =
                        QDir::fromNativeSeparators(QDir(group.sourcePath).absolutePath())
                            .toCaseFolded();
                    if (foldedSource != foldedRoot &&
                        !foldedSource.startsWith(foldedRoot + QLatin1Char('/')))
                        continue;

                    const auto files = PostResultAutoPrepare::resultFiles(
                        projectPath, group.sourcePath, true);
                    if (!files.isEmpty())
                    {
                        warmFile = files.constFirst().filePath;
                        break;
                    }
                }
            }

            if (!warmFile.isEmpty())
            {
                // 内部只保留一份标准 vtkUnstructuredGrid。求解完成后低优先级预读
                // 这一个结果即可，云图/切面/裁剪都复用同一份内存缓存。
                QString gridWarmError;
                PostTecplotUtils::readGrid(warmFile, &gridWarmError);
                if (!gridWarmError.isEmpty())
                    qWarning().noquote()
                        << QStringLiteral("后处理结果预读失败：%1").arg(gridWarmError);
            }
        });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this,
            [this, projectPath, rootPath, key, prepared, summary]()
            {
                m_preparingResultRoots.remove(key);
                const bool prepareAgain = m_pendingResultRoots.remove(key);
                if (*prepared)
                {
                    const QString outputDir = QDir(rootPath).filePath(
                        QString::fromUtf8(PublicConstants::kPostResultDirectoryName));
                    QStringList logLines{QStringLiteral("__RAW__计算完成，后处理结果已生成。")};
                    if (summary->groupCount > 0)
                        logLines.push_back(QStringLiteral("生成内容：%1 组后处理结果")
                                               .arg(summary->groupCount));
                    if (!summary->resultTypes.isEmpty())
                        logLines.push_back(QStringLiteral("结果数据：%1")
                                               .arg(summary->resultTypes.join(QStringLiteral("、"))));
                    if (!summary->resultConditions.isEmpty())
                    {
                        QStringList conditions = summary->resultConditions;
                        if (summary->hasRegularResults)
                            conditions.prepend(QStringLiteral("常规"));
                        logLines.push_back(
                            QStringLiteral("结果工况：%1")
                                .arg(conditions.join(QStringLiteral("、"))));
                    }
                    logLines.push_back(
                        QStringLiteral("保存位置：%1")
                            .arg(QDir::toNativeSeparators(outputDir)));
                    qInfo().noquote() << logLines.join(QLatin1Char('\n'));
                }
                else
                {
                    qWarning().noquote() << QStringLiteral(
                        "__RAW__计算完成，但没有生成可用的后处理结果。");
                }
                if (prepareAgain)
                    prepareResultRootAsync(projectPath, rootPath);
            });
    worker->start();
}

void PostProcessing::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        QStringLiteral("project_tree.node"), this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value(QStringLiteral("uuid")).toString();
            const QString kind = ctx.value(QStringLiteral("kind")).toString();

            ContextMenuHub::addSeparator(menu);

            if (kind == QStringLiteral("PostProcessingRoot"))
            {
                for (const auto& spec : postNodeSpecs())
                {
                    const QString title = QString::fromUtf8(spec.title);
                    const QString type = QString::fromLatin1(spec.type);
                    ContextMenuHub::addAction(menu, this, QStringLiteral("新增%1").arg(title),
                                              [this, type, title]
                                              {
                                                  createPostNode(type, title);
                                              });
                }
                return;
            }

            if (kind == QString::fromLatin1(kSlopeLimitResultPageType)
                || kind == QString::fromLatin1(kCaveBlockResultPageType))
            {
                ContextMenuHub::addAction(menu, this, QStringLiteral("打开结果目录"),
                                          [this, uuid, kind]
                                          {
                                              ProjectNode* node = findChildByUuid(uuid);
                                              if (!node || !node->getPage())
                                                  return;
                                              node->getPage()->applyDeferredLoadJson();
                                              if (kind == QString::fromLatin1(kSlopeLimitResultPageType))
                                              {
                                                  if (auto* page = dynamic_cast<SlopeLimitEquilibriumResultPage*>(node->getPage()))
                                                      page->openResultDirectory();
                                              }
                                              else if (auto* page = dynamic_cast<CaveBlockStabilityResultPage*>(node->getPage()))
                                                  page->openResultDirectory();
                                          });
                ContextMenuHub::addSeparator(menu);
            }

            ContextMenuHub::addAction(menu, this, QStringLiteral("重命名"),
                                      [this, uuid]
                                      {
                                          renameNode(uuid);
                                      });
            ContextMenuHub::addAction(menu, this, QStringLiteral("删除"),
                                      [this, uuid]
                                      {
                                          deleteNode(uuid);
                                      });
        },
        80,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value(QStringLiteral("module")).toString() ==
                   QStringLiteral("PostProcessing");
        });
}

void PostProcessing::renameNode(const QString& uuid)
{
    ProjectNode* node = findChildByUuid(uuid);
    if (!node)
        return;

    const QString currentName = node->text(0);
    bool ok = false;
    const QString newName = QInputDialog::getText(nullptr, tr("重命名后处理结果"),
                                                  tr("请输入新名称："), QLineEdit::Normal,
                                                  currentName, &ok);
    if (!ok)
        return;

    const QString trimmedName = newName.trimmed();
    if (trimmedName.isEmpty() || trimmedName == currentName)
        return;

    node->setText(0, trimmedName);
    node->setName(trimmedName);
    if (BasePage* page = node->getPage())
        page->setDisplayName(trimmedName);
}

void PostProcessing::deleteNode(const QString& uuid)
{
    ProjectNode* node = findChildByUuid(uuid);
    if (!node || !m_root)
        return;

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(m_root);

    if (BasePage* page = node->getPage())
    {
        if (!page->hasDeferredLoadJson())
            page->beforeUnload();
        page->deleteLater();
        node->setPage(nullptr);
    }

    m_root->removeChild(node);
    delete node;
}

void PostProcessing::removeRootFromTree()
{
    if (!m_root)
        return;

    if (auto* parent = dynamic_cast<ProjectNode*>(m_root->parent()))
    {
        parent->removeChild(m_root);
    }
    else if (QTreeWidget* tree = m_root->treeWidget())
    {
        const int index = tree->indexOfTopLevelItem(m_root);
        if (index >= 0)
            tree->takeTopLevelItem(index);
    }
}

bool PostProcessing::saveProject()
{
    if (!m_root)
        return true;
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPostProcessingProjectFileName);

    QJsonObject root;
    root["module"] = "PostProcessing";
    root["root_title"] = "后处理";
    root["result_context"] = PostResultSelectionUtils::saveSharedResultSelection();
    root["display_settings"] = savePostDisplaySettings();

    QJsonArray items;
    QSet<QString> savedSpecialResults;
    const int n = m_root->childCount();
    for (int i = 0; i < n; ++i)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;
        BasePage* page = node->getPage();
        if (!page)
            continue;

        const QString type = page->getPageType();
        const QJsonObject state = page->saveJsonForPersistence();
        const QString specialKey = specialResultKey(type, state);
        if (!specialKey.isEmpty())
        {
            if (savedSpecialResults.contains(specialKey))
                continue;
            savedSpecialResults.insert(specialKey);
        }

        QJsonObject item;
        item["uuid"] = node->getUuid();
        item["title"] = node->text(0);
        item["type"] = type;
        item["state"] = state;
        items.append(item);
    }
    root["items"] = items;
    return writeJsonFile(filePath, root);
}

bool PostProcessing::loadProject()
{
    if (m_root)
        closeProject();

    PostResultSelectionUtils::clearSharedResultSelection();
    postScalarBarStyle() = PostScalarBarStyle{};
    postContourStyle() = PostContourStyle{};
    postSceneStyle() = PostSceneStyle{};
    postDisplaySettingsAuthoritative() = false;
    postFieldRangeStyles().clear();
    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    m_root = new ProjectNode("后处理");
    m_root->setData(0, ProjectNode::RoleUuid, m_root->getUuid());
    m_root->setData(0, ProjectNode::RoleModule, "PostProcessing");
    m_root->setData(0, ProjectNode::RoleKind, "PostProcessingRoot");
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (pm)
        pm->addTreeItem(m_root);

    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPostProcessingProjectFileName);
    QJsonObject root;
    if (!readJsonFile(filePath, root))
    {
        postDisplaySettingsAuthoritative() = true;
        if (m_displayControlWidget)
            m_displayControlWidget->syncFromState();
        return true;
    }

    if (root.value(QStringLiteral("result_context")).isObject())
        PostResultSelectionUtils::loadSharedResultSelection(root.value(QStringLiteral("result_context")).toObject());
    if (root.value(QStringLiteral("display_settings")).isObject())
    {
        loadPostDisplaySettings(root.value(QStringLiteral("display_settings")).toObject());
        postDisplaySettingsAuthoritative() = true;
    }
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();
    if (m_visualService)
        m_visualService->setGroupVisible(QStringLiteral("post"), postSceneStyle().visible, false);

    const QJsonArray items = root.value("items").toArray();
    QSet<QString> loadedSpecialResults;
    for (const auto& v : items)
    {
        const auto obj = v.toObject();
        const QString id = obj.value("uuid").toString(QUuid::createUuid().toString());
        QString title = obj.value("title").toString("未命名");
        const QString type = obj.value("type").toString();
        const QJsonObject state = obj.value("state").toObject();

        const QString specialKey = specialResultKey(type, state);
        if (!specialKey.isEmpty())
        {
            if (loadedSpecialResults.contains(specialKey))
                continue;
            loadedSpecialResults.insert(specialKey);
        }

        const QString analysisName =
            state.value(QStringLiteral("analysis_name")).toString().trimmed();
        if (type == QString::fromLatin1(kSlopeLimitResultPageType))
            title = specialResultTitle(analysisName, QStringLiteral("极限平衡结果"));
        else if (type == QString::fromLatin1(kCaveBlockResultPageType))
            title = specialResultTitle(analysisName, QStringLiteral("块体稳定结果"));

        ProjectNode* node = createPostNode(type, title, id, false);
        if (!node)
            continue;

        if (BasePage* page = node->getPage())
        {
            // 后处理结果页可能在 loadJson() 中扫描结果目录、准备结果文件甚至读取
            // Tecplot/VTU。工程打开阶段只保存状态，首次真正进入页面时再恢复。
            page->deferLoadJson(state);
        }
    }
    postDisplaySettingsAuthoritative() = true;
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    return true;
}

void PostProcessing::closeProject()
{
    PostMeshEdges::clear(m_visualService);
    if (!m_root)
        return;

    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
    {
        m_visualService->showScalarBar(false, false);
        m_visualService->setGroupVisible(QStringLiteral("post"), false, false);
        m_visualService->render();
    }
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    while (m_root->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(0));
        if (node)
        {
            if (auto* page = node->getPage())
            {
                if (!page->hasDeferredLoadJson())
                    page->beforeUnload();
                page->deleteLater();

                node->setPage(nullptr);
            }
            m_root->removeChild(node);
            delete node;
        }
    }

    removeRootFromTree();
    if (auto* page = m_root->getPage())
    {
        page->beforeUnload();
        page->deleteLater();
        m_root->setPage(nullptr);
    }
    delete m_root;
    m_root = nullptr;
}

bool PostProcessing::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QJsonDocument document(obj);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0)
        return false;

    return file.commit();
}

bool PostProcessing::readJsonFile(const QString& filePath, QJsonObject& out)
{
    QFile f(filePath);
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}
