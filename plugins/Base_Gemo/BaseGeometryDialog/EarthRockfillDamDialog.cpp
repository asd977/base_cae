#include "EarthRockfillDamDialog.h"

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
#include <QSize>
#include <QSizePolicy>
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

bool appendBand(TopoDS_Compound& compound,
                BRep_Builder& builder,
                double outerLeft0,
                double outerRight0,
                double outerLeft1,
                double outerRight1,
                double regionLeft0,
                double regionRight0,
                double regionLeft1,
                double regionRight1,
                double z0,
                double z1,
                double length)
{
    const double left0 = qMax(outerLeft0, regionLeft0);
    const double right0 = qMin(outerRight0, regionRight0);
    const double left1 = qMax(outerLeft1, regionLeft1);
    const double right1 = qMin(outerRight1, regionRight1);

    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;

    builder.Add(compound, solid);
    return true;
}

} // namespace

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam)
{
    Q_UNUSED(parent);
    initUi();
}

QJsonObject EarthRockfillDamDialog::saveJson()
{
    QJsonObject object;
    object["h"] = hEdit->text().toDouble();
    object["crest"] = crestEdit->text().toDouble();
    object["upW"] = upEdit->text().toDouble();
    object["downW"] = downEdit->text().toDouble();
    object["len"] = lenEdit->text().toDouble();
    object["coreTop"] = coreWidthEdit->text().toDouble();
    object["coreBottom"] = readEdit(this, "coreBottomEdit", 22.0);
    object["filter"] = readEdit(this, "filterWidthEdit", 4.0);
    object["coreOffset"] = readEdit(this, "coreOffsetEdit", 0.0);
    object["layers"] = layerEdit->text().toInt();
    return object;
}

void EarthRockfillDamDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("h"))
        hEdit->setText(QString::number(object["h"].toDouble()));
    if (object.contains("crest"))
        crestEdit->setText(QString::number(object["crest"].toDouble()));
    if (object.contains("upW"))
        upEdit->setText(QString::number(object["upW"].toDouble()));
    else if (object.contains("up"))
        upEdit->setText(QString::number(object["up"].toDouble()));
    if (object.contains("downW"))
        downEdit->setText(QString::number(object["downW"].toDouble()));
    else if (object.contains("down"))
        downEdit->setText(QString::number(object["down"].toDouble()));
    if (object.contains("len"))
        lenEdit->setText(QString::number(object["len"].toDouble()));
    if (object.contains("coreTop"))
        coreWidthEdit->setText(QString::number(object["coreTop"].toDouble()));
    else if (object.contains("core"))
        coreWidthEdit->setText(QString::number(object["core"].toDouble()));
    if (object.contains("coreBottom"))
        setEditIfExists(this, "coreBottomEdit", object["coreBottom"].toDouble());
    if (object.contains("filter"))
        setEditIfExists(this, "filterWidthEdit", object["filter"].toDouble());
    if (object.contains("coreOffset"))
        setEditIfExists(this, "coreOffsetEdit", object["coreOffset"].toDouble());
    if (object.contains("layers"))
        layerEdit->setText(QString::number(object["layers"].toInt()));

    updateDiagram();
}

void EarthRockfillDamDialog::initUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    auto* positiveValidator = new QDoubleValidator(0.01, 1.0e9, 3, this);
    auto* coordValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);

    hEdit = new QLineEdit("80", this);
    crestEdit = new QLineEdit("20", this);
    upEdit = new QLineEdit("160", this);       // 上游坝坡水平宽 Wu
    downEdit = new QLineEdit("140", this);     // 下游坝坡水平宽 Wd
    lenEdit = new QLineEdit("220", this);

    coreWidthEdit = new QLineEdit("6", this);  // 心墙顶宽 Ct
    layerEdit = new QLineEdit("8", this);

    auto* coreBottomEdit = new QLineEdit("28", this);
    coreBottomEdit->setObjectName("coreBottomEdit");

    auto* filterWidthEdit = new QLineEdit("3", this);
    filterWidthEdit->setObjectName("filterWidthEdit");

    auto* coreOffsetEdit = new QLineEdit("0", this);
    coreOffsetEdit->setObjectName("coreOffsetEdit");

    for (auto* edit : {hEdit, crestEdit, upEdit, downEdit, lenEdit, coreWidthEdit, coreBottomEdit, filterWidthEdit})
        edit->setValidator(positiveValidator);
    coreOffsetEdit->setValidator(coordValidator);
    layerEdit->setValidator(new QIntValidator(1, 200, this));

    form->addRow("坝高 H", hEdit);
    form->addRow("坝顶宽 Bt", crestEdit);
    form->addRow("上游坝坡水平宽 Wu", upEdit);
    form->addRow("下游坝坡水平宽 Wd", downEdit);
    form->addRow("坝轴线长度 L", lenEdit);
    form->addRow("心墙顶宽 Ct", coreWidthEdit);
    form->addRow("心墙底宽 Cb", coreBottomEdit);
    form->addRow("反滤层宽 F", filterWidthEdit);
    form->addRow("心墙中心偏移 Xc", coreOffsetEdit);
    form->addRow("填筑层数 N", layerEdit);
    mainLayout->addLayout(form);

    diagramLabel = new QLabel(this);
    diagramLabel->setAlignment(Qt::AlignCenter);
    diagramLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    diagramLabel->setMinimumSize(0, 0);
    mainLayout->addWidget(diagramLabel);

    auto* row = new QHBoxLayout();
    auto* generateButton = new QPushButton("生成土石坝", this);
    row->addWidget(generateButton);
    mainLayout->addLayout(row);
    connect(generateButton, &QPushButton::clicked, this, &EarthRockfillDamDialog::onGenerateClicked);

    updateDiagram();
}


void EarthRockfillDamDialog::resizeEvent(QResizeEvent* event)
{
    BasePage::resizeEvent(event);
    if (!diagramPixmap.isNull() && diagramLabel)
        diagramLabel->setPixmap(diagramPixmap.scaled(diagramLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void EarthRockfillDamDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/earth_rockfill_dam_parameter_diagram.png");
    diagramPixmap.load(resourcePath);
    if (diagramPixmap.isNull())
    {
        diagramLabel->setText("参数示意图加载失败：resources/earth_rockfill_dam_parameter_diagram.png");
        return;
    }

    diagramLabel->setPixmap(diagramPixmap.scaled(diagramLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}




void EarthRockfillDamDialog::onPreviewClicked()
{
    updateDiagram();
}

void EarthRockfillDamDialog::buildLayeredDam(double H, double Bt, double Wu, double Wd, double L, int layers)
{
    const double Ct = coreWidthEdit->text().toDouble();
    const double Cb = readEdit(this, "coreBottomEdit", 22.0);
    const double F = readEdit(this, "filterWidthEdit", 4.0);
    const double Xc = readEdit(this, "coreOffsetEdit", 0.0);

    if (H <= kEps || Bt <= kEps || Wu <= kEps || Wd <= kEps || L <= kEps || layers <= 0 ||
        Ct <= kEps || Cb <= kEps || F <= kEps)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：坝高、坝顶宽、上下游水平宽、长度、心墙宽、反滤层宽和层数必须为正值。");
        return;
    }

    const double totalWidth = Wu + Bt + Wd;
    const double center = Wu + Bt * 0.5 + Xc;

    const double topLeftWithFilter = center - Ct * 0.5 - F;
    const double topRightWithFilter = center + Ct * 0.5 + F;
    const double bottomLeftWithFilter = center - Cb * 0.5 - F;
    const double bottomRightWithFilter = center + Cb * 0.5 + F;

    if (topLeftWithFilter <= Wu + kEps || topRightWithFilter >= Wu + Bt - kEps)
    {
        QMessageBox::warning(this, "参数错误", "心墙顶宽 Ct + 两侧反滤层宽 2F 必须位于坝顶宽 Bt 内。请减小 Ct/F，或调整心墙中心偏移 Xc。");
        return;
    }
    if (bottomLeftWithFilter <= kEps || bottomRightWithFilter >= totalWidth - kEps)
    {
        QMessageBox::warning(this, "参数错误", "心墙底宽 Cb + 两侧反滤层宽 2F 必须位于坝体底宽内。请减小 Cb/F，或调整心墙中心偏移 Xc。");
        return;
    }

    const double layerH = H / static_cast<double>(layers);

    auto outerLeftAt = [&](double z)
    {
        return Wu * z / H;
    };
    auto outerRightAt = [&](double z)
    {
        return totalWidth - Wd * z / H;
    };
    auto coreHalfAt = [&](double z)
    {
        return Cb * 0.5 + (Ct - Cb) * 0.5 * z / H;
    };

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    for (int i = 0; i < layers; ++i)
    {
        const double z0 = i * layerH;
        const double z1 = (i + 1) * layerH;

        const double outerL0 = outerLeftAt(z0);
        const double outerR0 = outerRightAt(z0);
        const double outerL1 = outerLeftAt(z1);
        const double outerR1 = outerRightAt(z1);

        const double half0 = coreHalfAt(z0);
        const double half1 = coreHalfAt(z1);
        const double coreL0 = center - half0;
        const double coreR0 = center + half0;
        const double coreL1 = center - half1;
        const double coreR1 = center + half1;

        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   -1.0e12, coreL0 - F, -1.0e12, coreL1 - F, z0, z1, L); // 上游坝壳
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0 - F, coreL0, coreL1 - F, coreL1, z0, z1, L);    // 左反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0, coreR0, coreL1, coreR1, z0, z1, L);            // 心墙
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0, coreR0 + F, coreR1, coreR1 + F, z0, z1, L);    // 右反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0 + F, 1.0e12, coreR1 + F, 1.0e12, z0, z1, L);    // 下游坝壳
    }

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

void EarthRockfillDamDialog::onGenerateClicked()
{
    updateDiagram();
    GeometryCleanup::removeExistingGeometry(m_uuid);
    buildLayeredDam(hEdit->text().toDouble(),
                    crestEdit->text().toDouble(),
                    upEdit->text().toDouble(),
                    downEdit->text().toDouble(),
                    lenEdit->text().toDouble(),
                    layerEdit->text().toInt());
}
