#pragma once

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkAlgorithmOutput.h>
#include <vtkContourFilter.h>
#include <vtkDataObject.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkStripper.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>

#include <QByteArray>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QJsonObject>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

struct PostScalarBarStyle
{
    bool visible{true};
    int titleFont{14};
    int labelFont{11};
    int precision{4};
    int tickCount{7};
    int widthPercent{8};
    int heightPercent{62};
    bool hasCustomPosition{false};
    double positionX{0.0};
    double positionY{0.0};
};

struct PostContourStyle
{
    bool enabled{false};
    int levels{10};
    bool labels{false};
    int labelFont{10};
};

struct PostFieldRangeStyle
{
    bool automatic{true};
    bool initialized{false};
    double minimum{0.0};
    double maximum{1.0};
};

inline PostScalarBarStyle& postScalarBarStyle()
{
    static PostScalarBarStyle style;
    return style;
}

inline PostContourStyle& postContourStyle()
{
    static PostContourStyle style;
    return style;
}

inline QHash<QString, PostFieldRangeStyle>& postFieldRangeStyles()
{
    static QHash<QString, PostFieldRangeStyle> styles;
    return styles;
}

inline PostFieldRangeStyle& postFieldRangeStyle(const QString& fieldName,
                                                 double dataMin = 0.0,
                                                 double dataMax = 1.0)
{
    PostFieldRangeStyle& style = postFieldRangeStyles()[fieldName];
    if (!style.initialized && dataMax > dataMin)
    {
        style.minimum = dataMin;
        style.maximum = dataMax;
        style.initialized = true;
    }
    return style;
}

inline void resolvePostDisplayRange(const QString& fieldName, double dataMin, double dataMax,
                                    double& minV, double& maxV)
{
    minV = dataMin;
    maxV = dataMax;
    if (fieldName.isEmpty() || !(dataMax > dataMin))
        return;
    PostFieldRangeStyle& style = postFieldRangeStyle(fieldName, dataMin, dataMax);
    if (!style.automatic && style.initialized && style.maximum > style.minimum)
    {
        minV = style.minimum;
        maxV = style.maximum;
    }
}

inline QJsonObject savePostDisplaySettings()
{
    const PostScalarBarStyle& bar = postScalarBarStyle();
    const PostContourStyle& contour = postContourStyle();
    QJsonObject obj;
    obj[QStringLiteral("scalarBarVisible")] = bar.visible;
    obj[QStringLiteral("scalarBarTitleFont")] = bar.titleFont;
    obj[QStringLiteral("scalarBarLabelFont")] = bar.labelFont;
    obj[QStringLiteral("scalarBarPrecision")] = bar.precision;
    obj[QStringLiteral("scalarBarTickCount")] = bar.tickCount;
    obj[QStringLiteral("scalarBarWidth")] = bar.widthPercent;
    obj[QStringLiteral("scalarBarHeight")] = bar.heightPercent;
    obj[QStringLiteral("scalarBarHasPosition")] = bar.hasCustomPosition;
    obj[QStringLiteral("scalarBarPositionX")] = bar.positionX;
    obj[QStringLiteral("scalarBarPositionY")] = bar.positionY;
    obj[QStringLiteral("contourEnabled")] = contour.enabled;
    obj[QStringLiteral("contourLevels")] = contour.levels;
    obj[QStringLiteral("contourLabels")] = contour.labels;
    obj[QStringLiteral("contourLabelFont")] = contour.labelFont;
    QJsonObject ranges;
    const auto& rangeStyles = postFieldRangeStyles();
    for (auto it = rangeStyles.constBegin(); it != rangeStyles.constEnd(); ++it)
    {
        QJsonObject range;
        range[QStringLiteral("automatic")] = it.value().automatic;
        range[QStringLiteral("initialized")] = it.value().initialized;
        range[QStringLiteral("minimum")] = it.value().minimum;
        range[QStringLiteral("maximum")] = it.value().maximum;
        ranges[it.key()] = range;
    }
    obj[QStringLiteral("fieldRanges")] = ranges;
    return obj;
}

inline void loadPostDisplaySettings(const QJsonObject& obj)
{
    PostScalarBarStyle& bar = postScalarBarStyle();
    PostContourStyle& contour = postContourStyle();
    bar.visible = obj.value(QStringLiteral("scalarBarVisible")).toBool(bar.visible);
    bar.titleFont = qBound(8, obj.value(QStringLiteral("scalarBarTitleFont")).toInt(bar.titleFont), 36);
    bar.labelFont = qBound(8, obj.value(QStringLiteral("scalarBarLabelFont")).toInt(bar.labelFont), 32);
    bar.precision = qBound(1, obj.value(QStringLiteral("scalarBarPrecision")).toInt(bar.precision), 10);
    bar.tickCount = qBound(2, obj.value(QStringLiteral("scalarBarTickCount")).toInt(bar.tickCount), 64);
    bar.widthPercent = qBound(5, obj.value(QStringLiteral("scalarBarWidth")).toInt(bar.widthPercent), 25);
    bar.heightPercent = qBound(25, obj.value(QStringLiteral("scalarBarHeight")).toInt(bar.heightPercent), 90);
    bar.hasCustomPosition = obj.value(QStringLiteral("scalarBarHasPosition")).toBool(bar.hasCustomPosition);
    bar.positionX = obj.value(QStringLiteral("scalarBarPositionX")).toDouble(bar.positionX);
    bar.positionY = obj.value(QStringLiteral("scalarBarPositionY")).toDouble(bar.positionY);
    contour.enabled = obj.value(QStringLiteral("contourEnabled")).toBool(contour.enabled);
    contour.levels = qBound(2, obj.value(QStringLiteral("contourLevels")).toInt(contour.levels), 30);
    contour.labels = obj.value(QStringLiteral("contourLabels")).toBool(contour.labels);
    contour.labelFont = qBound(8, obj.value(QStringLiteral("contourLabelFont")).toInt(contour.labelFont), 28);
    postFieldRangeStyles().clear();
    const QJsonObject ranges = obj.value(QStringLiteral("fieldRanges")).toObject();
    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
    {
        const QJsonObject range = it.value().toObject();
        PostFieldRangeStyle style;
        style.automatic = range.value(QStringLiteral("automatic")).toBool(true);
        style.initialized = range.value(QStringLiteral("initialized")).toBool(false);
        style.minimum = range.value(QStringLiteral("minimum")).toDouble(0.0);
        style.maximum = range.value(QStringLiteral("maximum")).toDouble(1.0);
        if (style.maximum <= style.minimum)
            style.automatic = true;
        postFieldRangeStyles().insert(it.key(), style);
    }
}

inline void applyStandardPostScalarBar(vtkScalarBarActor* scalarBar)
{
    if (!scalarBar)
        return;

    const PostScalarBarStyle& style = postScalarBarStyle();
    const double width = style.widthPercent / 100.0;
    const double height = style.heightPercent / 100.0;
    const double defaultX = qBound(0.0, 0.965 - width, 1.0 - width);
    const double defaultY = qBound(0.0, 0.5 - height * 0.5, 1.0 - height);
    const double x = style.hasCustomPosition
                         ? qBound(0.0, style.positionX, 1.0 - width)
                         : defaultX;
    const double y = style.hasCustomPosition
                         ? qBound(0.0, style.positionY, 1.0 - height)
                         : defaultY;

    scalarBar->SetOrientationToVertical();
    scalarBar->UnconstrainedFontSizeOn();
    scalarBar->SetWidth(width);
    scalarBar->SetHeight(height);
    scalarBar->SetPosition(x, y);
    scalarBar->SetMaximumWidthInPixels(1000);
    scalarBar->SetMaximumHeightInPixels(2000);
    scalarBar->UseCustomLabelsOff();
    scalarBar->SetCustomLabels(nullptr);
    scalarBar->SetNumberOfLabels(style.tickCount);
    const QByteArray labelFormat = QStringLiteral("%.%1g").arg(style.precision).toLatin1();
    scalarBar->SetLabelFormat(labelFormat.constData());
    scalarBar->DrawBackgroundOff();
    scalarBar->DrawFrameOff();

    if (vtkTextProperty* title = scalarBar->GetTitleTextProperty())
    {
        title->SetFontSize(style.titleFont);
        title->SetColor(0.08, 0.08, 0.08);
        title->SetBold(1);
    }
    if (vtkTextProperty* label = scalarBar->GetLabelTextProperty())
    {
        label->SetFontSize(style.labelFont);
        label->SetColor(0.08, 0.08, 0.08);
        label->SetBold(0);
    }
    scalarBar->Modified();
}

inline void applyStandardPostScalarBar(VisualWidgetService* service, const QString& title)
{
    if (!service)
        return;
    vtkScalarBarActor* scalarBar = service->scalarBarActor();
    if (!scalarBar)
        return;
    applyStandardPostScalarBar(scalarBar);
    scalarBar->SetTitle(title.toUtf8().constData());
    scalarBar->Modified();
}

inline bool editPostDisplaySettings(QWidget* parent, VisualWidgetService* service,
                                    bool supportsContours, bool supportsContourLabels = false,
                                    const QString& fieldName = QString(),
                                    double dataMin = 0.0, double dataMax = 1.0)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("显示设置"));
    dialog.setModal(true);

    auto* rootLayout = new QVBoxLayout(&dialog);

    QCheckBox* automaticRange = nullptr;
    QDoubleSpinBox* rangeMinSpin = nullptr;
    QDoubleSpinBox* rangeMaxSpin = nullptr;
    if (!fieldName.isEmpty() && dataMax > dataMin)
    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(fieldName, dataMin, dataMax);
        auto* rangeGroup = new QGroupBox(QStringLiteral("显示范围"), &dialog);
        auto* rangeForm = new QFormLayout(rangeGroup);
        automaticRange = new QCheckBox(QStringLiteral("自动使用数据范围"), rangeGroup);
        automaticRange->setChecked(rangeStyle.automatic);
        rangeMinSpin = new QDoubleSpinBox(rangeGroup);
        rangeMaxSpin = new QDoubleSpinBox(rangeGroup);
        rangeMinSpin->setRange(-1.0e100, 1.0e100);
        rangeMaxSpin->setRange(-1.0e100, 1.0e100);
        rangeMinSpin->setDecimals(8);
        rangeMaxSpin->setDecimals(8);
        rangeMinSpin->setKeyboardTracking(false);
        rangeMaxSpin->setKeyboardTracking(false);
        rangeMinSpin->setValue(rangeStyle.initialized ? rangeStyle.minimum : dataMin);
        rangeMaxSpin->setValue(rangeStyle.initialized ? rangeStyle.maximum : dataMax);
        rangeMinSpin->setEnabled(!rangeStyle.automatic);
        rangeMaxSpin->setEnabled(!rangeStyle.automatic);
        QObject::connect(automaticRange, &QCheckBox::toggled, rangeMinSpin, &QWidget::setDisabled);
        QObject::connect(automaticRange, &QCheckBox::toggled, rangeMaxSpin, &QWidget::setDisabled);
        rangeForm->addRow(automaticRange);
        rangeForm->addRow(QStringLiteral("范围下限"), rangeMinSpin);
        rangeForm->addRow(QStringLiteral("范围上限"), rangeMaxSpin);
        rootLayout->addWidget(rangeGroup);
    }

    auto* scalarBarGroup = new QGroupBox(QStringLiteral("色标"), &dialog);
    auto* scalarBarForm = new QFormLayout(scalarBarGroup);
    PostScalarBarStyle& bar = postScalarBarStyle();

    auto makeSpin = [&dialog](int min, int max, int value) {
        auto* spin = new QSpinBox(&dialog);
        spin->setRange(min, max);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        return spin;
    };

    QSpinBox* titleFont = makeSpin(8, 36, bar.titleFont);
    QSpinBox* labelFont = makeSpin(8, 32, bar.labelFont);
    QSpinBox* precision = makeSpin(1, 10, bar.precision);
    QSpinBox* ticks = makeSpin(2, 64, bar.tickCount);
    QSpinBox* width = makeSpin(5, 25, bar.widthPercent);
    QSpinBox* height = makeSpin(25, 90, bar.heightPercent);
    width->setSuffix(QStringLiteral(" %"));
    height->setSuffix(QStringLiteral(" %"));

    scalarBarForm->addRow(QStringLiteral("标题字号"), titleFont);
    scalarBarForm->addRow(QStringLiteral("刻度字号"), labelFont);
    scalarBarForm->addRow(QStringLiteral("数值位数"), precision);
    scalarBarForm->addRow(QStringLiteral("刻度数量"), ticks);
    scalarBarForm->addRow(QStringLiteral("色标宽度"), width);
    scalarBarForm->addRow(QStringLiteral("色标高度"), height);
    rootLayout->addWidget(scalarBarGroup);

    QCheckBox* contourEnabled = nullptr;
    QSpinBox* contourLevels = nullptr;
    QCheckBox* contourLabels = nullptr;
    QSpinBox* contourLabelFont = nullptr;
    if (supportsContours)
    {
        auto* contourGroup = new QGroupBox(QStringLiteral("等值线"), &dialog);
        auto* contourForm = new QFormLayout(contourGroup);
        PostContourStyle& contour = postContourStyle();
        contourEnabled = new QCheckBox(QStringLiteral("显示等值线"), contourGroup);
        contourEnabled->setChecked(contour.enabled);
        contourLevels = makeSpin(2, 30, contour.levels);
        contourForm->addRow(contourEnabled);
        contourForm->addRow(QStringLiteral("等值线数量"), contourLevels);
        if (supportsContourLabels)
        {
            contourLabels = new QCheckBox(QStringLiteral("显示数值"), contourGroup);
            contourLabels->setChecked(contour.labels);
            contourLabelFont = makeSpin(8, 28, contour.labelFont);
            contourForm->addRow(contourLabels, contourLabelFont);
        }
        rootLayout->addWidget(contourGroup);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (automaticRange && rangeMinSpin && rangeMaxSpin)
    {
        PostFieldRangeStyle& rangeStyle = postFieldRangeStyle(fieldName, dataMin, dataMax);
        rangeStyle.automatic = automaticRange->isChecked();
        rangeStyle.initialized = true;
        if (rangeMaxSpin->value() > rangeMinSpin->value())
        {
            rangeStyle.minimum = rangeMinSpin->value();
            rangeStyle.maximum = rangeMaxSpin->value();
        }
        else
        {
            rangeStyle.minimum = dataMin;
            rangeStyle.maximum = dataMax;
            rangeStyle.automatic = true;
        }
    }

    bar.titleFont = titleFont->value();
    bar.labelFont = labelFont->value();
    bar.precision = precision->value();
    bar.tickCount = ticks->value();
    bar.widthPercent = width->value();
    bar.heightPercent = height->value();

    if (supportsContours)
    {
        PostContourStyle& contour = postContourStyle();
        contour.enabled = contourEnabled && contourEnabled->isChecked();
        contour.levels = contourLevels ? contourLevels->value() : contour.levels;
        if (supportsContourLabels)
        {
            contour.labels = contourLabels && contourLabels->isChecked();
            contour.labelFont = contourLabelFont ? contourLabelFont->value() : contour.labelFont;
        }
    }

    if (service && service->scalarBarActor())
    {
        applyStandardPostScalarBar(service->scalarBarActor());
        service->showScalarBar(bar.visible, false);
        service->render();
    }
    return true;
}


inline vtkSmartPointer<vtkActor> createPostContourActor(vtkAlgorithmOutput* inputPort,
                                                        const QString& scalarName,
                                                        double minV, double maxV)
{
    if (!inputPort || scalarName.isEmpty() || !postContourStyle().enabled ||
        !(maxV > minV))
        return nullptr;

    const QByteArray name = scalarName.toUtf8();
    auto contour = vtkSmartPointer<vtkContourFilter>::New();
    contour->SetInputConnection(inputPort);
    contour->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    name.constData());
    const int levels = qMax(2, postContourStyle().levels);
    contour->SetNumberOfContours(levels);
    for (int i = 0; i < levels; ++i)
    {
        const double t = double(i + 1) / double(levels + 1);
        contour->SetValue(i, minV + (maxV - minV) * t);
    }

    auto stripper = vtkSmartPointer<vtkStripper>::New();
    stripper->SetInputConnection(contour->GetOutputPort());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(stripper->GetOutputPort());
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -1.0);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->PickableOff();
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetColor(0.10, 0.10, 0.10);
    actor->GetProperty()->SetOpacity(0.82);
    actor->GetProperty()->SetLineWidth(1.0);
    actor->GetProperty()->SetRenderLinesAsTubes(true);
    return actor;
}

inline QString postActorId(vtkActor* actor)
{
    return QString("post_actor_%1").arg(reinterpret_cast<quintptr>(actor), 0, 16);
}

inline QString postActorId(const QString& ownerUuid, const QString& actorRole)
{
    const QString owner = ownerUuid.trimmed();
    const QString role = actorRole.trimmed();
    if (owner.isEmpty() || role.isEmpty())
        return QString();
    return QString("post.%1.%2").arg(owner, role);
}

inline bool addActorByService(VisualWidgetService* service, const vtkSmartPointer<vtkActor>& actor,
                              bool renderNow = true)
{
    if (!service || !actor)
        return false;
    const QString id = postActorId(actor.GetPointer());
    const bool ok = service->addProp(id, actor, "post", QString(), false, false);
    const bool attached = ok || service->getProp(id) == actor.GetPointer();
    if (renderNow)
        service->render();
    return attached;
}

inline bool addActorByService(VisualWidgetService* service, const QString& id,
                              const vtkSmartPointer<vtkActor>& actor, bool renderNow = true)
{
    if (!service || id.isEmpty() || !actor)
        return false;
    const bool ok = service->addProp(id, actor, "post", QString(), false, false);
    const bool attached = ok || service->getProp(id) == actor.GetPointer();
    if (renderNow)
        service->render();
    return attached;
}

inline bool removeActorByService(VisualWidgetService* service,
                                 const vtkSmartPointer<vtkActor>& actor, bool renderNow = true)
{
    if (!service || !actor)
        return false;
    const bool ok = service->removeProp(postActorId(actor.GetPointer()), false);
    if (renderNow)
        service->render();
    return ok;
}

inline bool removeActorByService(VisualWidgetService* service, const QString& id,
                                 bool renderNow = true)
{
    if (!service || id.isEmpty())
        return false;
    const bool ok = service->removeProp(id, false);
    if (renderNow)
        service->render();
    return ok;
}

inline void showPostProcessingOnly(VisualWidgetService* service, bool renderNow = true)
{
    if (!service)
        return;
    service->setGroupVisible("post", true, false);
    service->setGroupVisible("Geometry", false, false);
    service->setGroupVisible("网格", false, false);
    if (renderNow)
        service->render();
}
