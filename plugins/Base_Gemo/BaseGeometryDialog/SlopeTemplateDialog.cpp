#include "SlopeTemplateDialog.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QPainter>
#include <QSize>
#include <QtSvg/QSvgRenderer>
#include <QVBoxLayout>
#include <QVector>
#include <ServiceRegistry.h>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

namespace
{
constexpr double kEps = 1.0e-7;


QLineEdit* findEdit(QWidget* owner, const char* objectName)
{
    return owner ? owner->findChild<QLineEdit*>(QString::fromLatin1(objectName)) : nullptr;
}

double readEdit(QWidget* owner, const char* objectName, double fallback)
{
    if (auto* edit = findEdit(owner, objectName))
    {
        bool ok = false;
        const double value = edit->text().toDouble(&ok);
        if (ok)
            return value;
    }
    return fallback;
}

void setEditIfExists(QWidget* owner, const char* objectName, double value)
{
    if (auto* edit = findEdit(owner, objectName))
        edit->setText(QString::number(value));
}

// 边坡断面：
// 1) 左侧是远场竖向边界；
// 2) 右侧是多级开挖坡面；
// 3) 坡脚外延保留，但必须有“底部厚度 Hbase”，否则外延只是一条零厚度面；
// 4) 模型厚度 L 由拉伸方向控制，平面断面不再额外设置“宽度”。
QVector<QPointF> buildSlopeProfile2D(double height,
                                     double topPlatformWidth,
                                     double middleBenchWidth,
                                     double stageHorizontalWidth,
                                     double toeExtensionLength,
                                     double baseHeight,
                                     int stageCount)
{
    QVector<QPointF> profile;
    if (height <= kEps || topPlatformWidth < 0.0 || middleBenchWidth < 0.0 ||
        stageHorizontalWidth <= kEps || toeExtensionLength < 0.0 || baseHeight <= kEps ||
        stageCount <= 0)
        return profile;

    profile << QPointF(0.0, -baseHeight);   // 左下远场底边
    profile << QPointF(0.0, height);        // 左上远场地表

    double x = 0.0;
    double z = height;
    const double dz = height / static_cast<double>(stageCount);

    if (topPlatformWidth > kEps)
    {
        x += topPlatformWidth;
        profile << QPointF(x, z);           // 坡顶平台
    }

    for (int i = 0; i < stageCount; ++i)
    {
        x += stageHorizontalWidth;
        z = qMax(0.0, height - (i + 1) * dz);
        profile << QPointF(x, z);           // 第 i 级坡面终点

        if (i != stageCount - 1 && middleBenchWidth > kEps)
        {
            x += middleBenchWidth;
            profile << QPointF(x, z);       // 中间平台
        }
    }

    if (toeExtensionLength > kEps)
    {
        x += toeExtensionLength;
        profile << QPointF(x, 0.0);         // 坡脚外延平台面
    }

    profile << QPointF(x, -baseHeight);     // 外延/坡脚底部，有厚度，不是零厚度面
    return profile;
}

TopoDS_Shape makePrismFromProfile(const QVector<QPointF>& profile, double length)
{
    if (profile.size() < 3 || length <= kEps)
        return TopoDS_Shape();

    BRepBuilderAPI_MakePolygon polygon;
    QPointF lastPoint;
    bool hasLast = false;
    int validCount = 0;

    for (const QPointF& point : profile)
    {
        if (hasLast && qAbs(point.x() - lastPoint.x()) < kEps && qAbs(point.y() - lastPoint.y()) < kEps)
            continue;
        polygon.Add(gp_Pnt(point.x(), 0.0, point.y()));
        lastPoint = point;
        hasLast = true;
        ++validCount;
    }

    if (validCount < 3)
        return TopoDS_Shape();

    polygon.Close();
    BRepBuilderAPI_MakeFace faceMaker(polygon.Wire());
    if (!faceMaker.IsDone())
        return TopoDS_Shape();

    return BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(0.0, length, 0.0)).Shape();
}

} // namespace

SlopeTemplateDialog::SlopeTemplateDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessSlopeTemplate)
{
    Q_UNUSED(parent);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* positiveValidator = new QDoubleValidator(0.01, 1.0e9, 3, this);
    auto* nonNegativeValidator = new QDoubleValidator(0.0, 1.0e9, 3, this);

    hEdit = new QLineEdit("65", this);
    benchWidthEdit = new QLineEdit("8", this);      // 中间平台宽 Bmid
    slopeEdit = new QLineEdit("18", this);          // 单级坡面水平宽 Ws
    lenEdit = new QLineEdit("120", this);           // 拉伸厚度 L
    benchCountEdit = new QLineEdit("3", this);      // 台阶级数 N

    auto* topPlatformEdit = new QLineEdit("20", this);
    topPlatformEdit->setObjectName("topPlatformEdit");
    auto* toeExtensionEdit = new QLineEdit("30", this);
    toeExtensionEdit->setObjectName("toeExtensionEdit");
    auto* baseHeightEdit = new QLineEdit("15", this);
    baseHeightEdit->setObjectName("baseHeightEdit");

    hEdit->setValidator(positiveValidator);
    benchWidthEdit->setValidator(nonNegativeValidator);
    slopeEdit->setValidator(positiveValidator);
    lenEdit->setValidator(positiveValidator);
    benchCountEdit->setValidator(new QIntValidator(1, 50, this));
    topPlatformEdit->setValidator(nonNegativeValidator);
    toeExtensionEdit->setValidator(nonNegativeValidator);
    baseHeightEdit->setValidator(positiveValidator);

    form->addRow("坡高 H", hEdit);
    form->addRow("坡顶平台宽 Btop", topPlatformEdit);
    form->addRow("中间平台宽 Bmid", benchWidthEdit);
    form->addRow("单级坡面水平宽 Ws", slopeEdit);
    form->addRow("坡脚外延长度 Etoe", toeExtensionEdit);
    form->addRow("底部厚度 Hbase", baseHeightEdit);
    form->addRow("模型厚度 L", lenEdit);
    form->addRow("台阶级数 N", benchCountEdit);
    mainLayout->addLayout(form);

    diagramLabel = new QLabel(this);
    diagramLabel->setMinimumHeight(260);
    diagramLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(diagramLabel);

    auto* row = new QHBoxLayout();
    auto* generateButton = new QPushButton("生成边坡", this);
    row->addWidget(generateButton);
    mainLayout->addLayout(row);
    connect(generateButton, &QPushButton::clicked, this, &SlopeTemplateDialog::onGenerateClicked);

    updateDiagram();
}

QJsonObject SlopeTemplateDialog::saveJson()
{
    QJsonObject object;
    object["h"] = hEdit->text().toDouble();
    object["top"] = readEdit(this, "topPlatformEdit", 20.0);
    object["bw"] = benchWidthEdit->text().toDouble();
    object["w"] = slopeEdit->text().toDouble();
    object["toe"] = readEdit(this, "toeExtensionEdit", 30.0);
    object["baseH"] = readEdit(this, "baseHeightEdit", 15.0);
    object["l"] = lenEdit->text().toDouble();
    object["bc"] = benchCountEdit->text().toInt();
    return object;
}

void SlopeTemplateDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("h"))
        hEdit->setText(QString::number(object["h"].toDouble()));
    if (object.contains("top"))
        setEditIfExists(this, "topPlatformEdit", object["top"].toDouble());
    if (object.contains("bw"))
        benchWidthEdit->setText(QString::number(object["bw"].toDouble()));
    if (object.contains("w"))
        slopeEdit->setText(QString::number(object["w"].toDouble()));
    else if (object.contains("s"))
        slopeEdit->setText(QString::number(object["s"].toDouble()));
    if (object.contains("toe"))
        setEditIfExists(this, "toeExtensionEdit", object["toe"].toDouble());
    if (object.contains("baseH"))
        setEditIfExists(this, "baseHeightEdit", object["baseH"].toDouble());
    if (object.contains("l"))
        lenEdit->setText(QString::number(object["l"].toDouble()));
    if (object.contains("bc"))
        benchCountEdit->setText(QString::number(object["bc"].toInt()));

    updateDiagram();
}

void SlopeTemplateDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/slope_parameter_diagram.svg");
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
    {
        diagramLabel->setText("参数示意图加载失败：resources/slope_parameter_diagram.svg");
        return;
    }

    const QSize targetSize = diagramLabel->size();
    QPixmap pixmap(targetSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();

    diagramLabel->setPixmap(pixmap.scaled(diagramLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}




void SlopeTemplateDialog::onPreviewClicked()
{
    updateDiagram();
}

void SlopeTemplateDialog::buildLayeredSlope(double H, double Bmid, double Ws, double L, int N)
{
    const double top = readEdit(this, "topPlatformEdit", 20.0);
    const double Etoe = readEdit(this, "toeExtensionEdit", 30.0);
    const double Hbase = readEdit(this, "baseHeightEdit", 15.0);

    if (H <= kEps || top < 0.0 || Bmid < 0.0 || Ws <= kEps || Etoe < 0.0 ||
        Hbase <= kEps || L <= kEps || N <= 0)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：坡高、单级坡面水平宽、底部厚度、模型厚度必须大于0；平台宽和坡脚外延长度可以为0；台阶级数必须大于0。");
        return;
    }

    const QVector<QPointF> profile = buildSlopeProfile2D(H, top, Bmid, Ws, Etoe, Hbase, N);
    TopoDS_Shape slopeSolid = makePrismFromProfile(profile, L);
    if (slopeSolid.IsNull())
    {
        QMessageBox::warning(this, "生成失败", "边坡断面无效，无法生成三维实体。");
        return;
    }

    auto* object = new GeomShapeObject(m_uuid, new TopoDS_Shape(slopeSolid), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}

void SlopeTemplateDialog::onGenerateClicked()
{
    updateDiagram();
    GeometryCleanup::removeExistingGeometry(m_uuid);
    buildLayeredSlope(hEdit->text().toDouble(),
                      benchWidthEdit->text().toDouble(),
                      slopeEdit->text().toDouble(),
                      lenEdit->text().toDouble(),
                      benchCountEdit->text().toInt());
}
