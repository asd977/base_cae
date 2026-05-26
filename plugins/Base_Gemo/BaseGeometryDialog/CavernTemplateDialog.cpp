#include "CavernTemplateDialog.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
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
#include <TopoDS_Compound.hxx>
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

bool addProfileSolid(TopoDS_Compound& compound, BRep_Builder& builder, const QVector<QPointF>& profile, double length)
{
    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;
    builder.Add(compound, solid);
    return true;
}

bool addBandSolid(TopoDS_Compound& compound,
                  BRep_Builder& builder,
                  double left0,
                  double right0,
                  double left1,
                  double right1,
                  double z0,
                  double z1,
                  double length)
{
    if (z1 - z0 <= kEps)
        return false;
    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    return addProfileSolid(compound, builder, profile, length);
}

// 洞口采用简化城门洞断面：直墙 + 折线拱顶。
// z=0 为洞底，z=wallH 为直墙顶，z=wallH+archH 为拱顶最高点。
double cavernLeftAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return 0.0;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span * 0.25 * t;
}

double cavernRightAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return span;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span - span * 0.25 * t;
}

QVector<QPointF> buildCavernProfile(double span, double wallH, double archH)
{
    QVector<QPointF> profile;
    profile << QPointF(0.0, 0.0)
            << QPointF(span, 0.0)
            << QPointF(span, wallH)
            << QPointF(span * 0.75, wallH + archH)
            << QPointF(span * 0.25, wallH + archH)
            << QPointF(0.0, wallH);
    return profile;
}

} // namespace

CavernTemplateDialog::CavernTemplateDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessCavernTemplate)
{
    Q_UNUSED(parent);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* positiveValidator = new QDoubleValidator(0.01, 1.0e9, 3, this);

    spanEdit = new QLineEdit("18", this);
    wallHEdit = new QLineEdit("12", this);
    archHEdit = new QLineEdit("4", this);
    lenEdit = new QLineEdit("80", this);
    liningEdit = new QLineEdit("4", this); // 复用原成员：这里表示洞口竖向开挖级数 N。

    auto* sideRockEdit = new QLineEdit("18", this);
    sideRockEdit->setObjectName("sideRockEdit");
    auto* topRockEdit = new QLineEdit("16", this);
    topRockEdit->setObjectName("topRockEdit");
    auto* bottomRockEdit = new QLineEdit("8", this);
    bottomRockEdit->setObjectName("bottomRockEdit");

    for (auto* edit : {spanEdit, wallHEdit, archHEdit, lenEdit, sideRockEdit, topRockEdit, bottomRockEdit})
        edit->setValidator(positiveValidator);
    liningEdit->setValidator(new QIntValidator(1, 50, this));

    form->addRow("洞口宽度 B", spanEdit);
    form->addRow("直墙高度 Hw", wallHEdit);
    form->addRow("拱顶高度 Ha", archHEdit);
    form->addRow("洞室进深 L", lenEdit);
    form->addRow("洞口开挖级数 N", liningEdit);
    form->addRow("左右围岩余量 Wr", sideRockEdit);
    form->addRow("顶部围岩厚度 Htop", topRockEdit);
    form->addRow("底部围岩厚度 Hbot", bottomRockEdit);
    mainLayout->addLayout(form);

    diagramLabel = new QLabel(this);
    diagramLabel->setMinimumHeight(250);
    diagramLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(diagramLabel);

    auto* row = new QHBoxLayout();
    auto* previewButton = new QPushButton("更新说明图", this);
    auto* generateButton = new QPushButton("生成洞室", this);
    row->addWidget(previewButton);
    row->addWidget(generateButton);
    mainLayout->addLayout(row);

    connect(previewButton, &QPushButton::clicked, this, &CavernTemplateDialog::onPreviewClicked);
    connect(generateButton, &QPushButton::clicked, this, &CavernTemplateDialog::onGenerateClicked);

    updateDiagram();
}

QJsonObject CavernTemplateDialog::saveJson()
{
    QJsonObject object;
    object["span"] = spanEdit->text().toDouble();
    object["wh"] = wallHEdit->text().toDouble();
    object["ah"] = archHEdit->text().toDouble();
    object["len"] = lenEdit->text().toDouble();
    object["stage"] = liningEdit->text().toInt();
    object["sideRock"] = readEdit(this, "sideRockEdit", 18.0);
    object["topRock"] = readEdit(this, "topRockEdit", 16.0);
    object["bottomRock"] = readEdit(this, "bottomRockEdit", 8.0);
    return object;
}

void CavernTemplateDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("span"))
        spanEdit->setText(QString::number(object["span"].toDouble()));
    if (object.contains("wh"))
        wallHEdit->setText(QString::number(object["wh"].toDouble()));
    if (object.contains("ah"))
        archHEdit->setText(QString::number(object["ah"].toDouble()));
    if (object.contains("len"))
        lenEdit->setText(QString::number(object["len"].toDouble()));
    if (object.contains("stage"))
        liningEdit->setText(QString::number(object["stage"].toInt()));
    else if (object.contains("lining"))
        liningEdit->setText(QString::number(qMax(1, qRound(object["lining"].toDouble()))));
    if (object.contains("sideRock"))
        setEditIfExists(this, "sideRockEdit", object["sideRock"].toDouble());
    if (object.contains("topRock"))
        setEditIfExists(this, "topRockEdit", object["topRock"].toDouble());
    if (object.contains("bottomRock"))
        setEditIfExists(this, "bottomRockEdit", object["bottomRock"].toDouble());

    updateDiagram();
}

void CavernTemplateDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/cavern_parameter_diagram.svg");
    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
    {
        diagramLabel->setText("参数示意图加载失败：resources/cavern_parameter_diagram.svg");
        return;
    }

    const QSize targetSize = diagramLabel->size().expandedTo(QSize(760, 320));
    QPixmap pixmap(targetSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    renderer.render(&painter);
    painter.end();

    diagramLabel->setPixmap(pixmap);
}




void CavernTemplateDialog::onPreviewClicked()
{
    updateDiagram();
}

void CavernTemplateDialog::buildLayeredCavern(double span, double wallH, double archH, double length, double stageValue)
{
    const int stageCount = qMax(1, qRound(stageValue));
    const double sideRock = readEdit(this, "sideRockEdit", 18.0);
    const double topRock = readEdit(this, "topRockEdit", 16.0);
    const double bottomRock = readEdit(this, "bottomRockEdit", 8.0);

    if (span <= kEps || wallH <= kEps || archH <= kEps || length <= kEps || stageCount <= 0 ||
        sideRock <= kEps || topRock <= kEps || bottomRock <= kEps)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：洞口尺寸、进深、开挖级数和围岩余量必须为正值。");
        return;
    }

    const double cavernH = wallH + archH;
    const double minX = -sideRock;
    const double maxX = span + sideRock;
    const double minZ = -bottomRock;
    const double maxZ = cavernH + topRock;

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    // 底部围岩：完整铺底。
    addBandSolid(compound, builder, minX, maxX, minX, maxX, minZ, 0.0, length);

    // 洞口高度范围内，按每个开挖级横向拆成：左围岩 + 开挖块 + 右围岩。
    // 左右围岩和开挖块使用同一组 left/right 函数，边界完全一致，所以不会出现空隙。
    for (int i = 0; i < stageCount; ++i)
    {
        const double z0 = cavernH * i / static_cast<double>(stageCount);
        const double z1 = cavernH * (i + 1) / static_cast<double>(stageCount);
        const double left0 = cavernLeftAtZ(span, wallH, archH, z0);
        const double right0 = cavernRightAtZ(span, wallH, archH, z0);
        const double left1 = cavernLeftAtZ(span, wallH, archH, z1);
        const double right1 = cavernRightAtZ(span, wallH, archH, z1);

        addBandSolid(compound, builder, minX, left0, minX, left1, z0, z1, length);       // 左围岩
        addBandSolid(compound, builder, left0, right0, left1, right1, z0, z1, length);  // 开挖级
        addBandSolid(compound, builder, right0, maxX, right1, maxX, z0, z1, length);    // 右围岩
    }

    // 顶部围岩：从拱顶最高点往上完整覆盖。
    addBandSolid(compound, builder, minX, maxX, minX, maxX, cavernH, maxZ, length);

    auto* object = new GeomShapeObject(m_uuid, new TopoDS_Shape(compound), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}

void CavernTemplateDialog::onGenerateClicked()
{
    updateDiagram();
    GeometryCleanup::removeExistingGeometry(m_uuid);
    buildLayeredCavern(spanEdit->text().toDouble(),
                       wallHEdit->text().toDouble(),
                       archHEdit->text().toDouble(),
                       lenEdit->text().toDouble(),
                       liningEdit->text().toDouble());
}
