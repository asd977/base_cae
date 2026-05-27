#include "GravityDamDialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QSizePolicy>
#include <QPixmap>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <QVector>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

namespace
{
constexpr double kEps = 1.0e-7;

void setScaledDiagram(QLabel* label, const QPixmap& pixmap)
{
    if (!label || pixmap.isNull())
        return;

    QSize targetSize = label->size();
    if (targetSize.width() <= 1 || targetSize.height() <= 1)
        targetSize = QSize(640, 240);

    label->setPixmap(pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}

GravityDamDialog::GravityDamDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessGravityDam)
{
    Q_UNUSED(parent);
    initUI();
}

GravityDamDialog::~GravityDamDialog()
{
}

QJsonObject GravityDamDialog::saveJson()
{
    QJsonObject state;
    state["height"] = heightEdit->text().toDouble();
    state["crestWidth"] = crestWidthEdit->text().toDouble();
    state["upstreamSlope"] = upstreamSlopeEdit->text().toDouble();
    state["downstreamSlope"] = downstreamSlopeEdit->text().toDouble();
    state["length"] = lengthEdit->text().toDouble();
    state["layerCount"] = layerCountEdit->text().toInt();
    state["x"] = xEdit->text().toDouble();
    state["y"] = yEdit->text().toDouble();
    state["z"] = zEdit->text().toDouble();
    return state;
}

void GravityDamDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("height"))
        heightEdit->setText(QString::number(state["height"].toDouble()));
    if (state.contains("crestWidth"))
        crestWidthEdit->setText(QString::number(state["crestWidth"].toDouble()));
    if (state.contains("upstreamSlope"))
        upstreamSlopeEdit->setText(QString::number(state["upstreamSlope"].toDouble()));
    if (state.contains("downstreamSlope"))
        downstreamSlopeEdit->setText(QString::number(state["downstreamSlope"].toDouble()));
    if (state.contains("length"))
        lengthEdit->setText(QString::number(state["length"].toDouble()));
    if (state.contains("layerCount"))
        layerCountEdit->setText(QString::number(state["layerCount"].toInt()));
    if (state.contains("x"))
        xEdit->setText(QString::number(state["x"].toDouble()));
    if (state.contains("y"))
        yEdit->setText(QString::number(state["y"].toDouble()));
    if (state.contains("z"))
        zEdit->setText(QString::number(state["z"].toDouble()));

    onPreview2DClicked();
}

void GravityDamDialog::initUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    auto* pageNameLabel = new QLabel(QString("几何模块"), this);
    pageNameLabel->setObjectName("pageNameLabel");

    auto* sectionTitleButton = new QPushButton(QString("重力坝参数模板"), this);
    sectionTitleButton->setObjectName("sectionTitleButton");
    sectionTitleButton->setEnabled(false);

    auto* inputGroup = new QGroupBox("参数输入", this);
    auto* form = new QFormLayout(inputGroup);

    auto* posValidator = new QDoubleValidator(0.001, 1.0e9, 3, this);
    auto* slopeValidator = new QDoubleValidator(0.0, 1000.0, 4, this);
    auto* coordValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);

    heightEdit = new QLineEdit("80", this);
    crestWidthEdit = new QLineEdit("10", this);
    upstreamSlopeEdit = new QLineEdit("0.2", this);
    downstreamSlopeEdit = new QLineEdit("0.75", this);
    lengthEdit = new QLineEdit("180", this);
    layerCountEdit = new QLineEdit("6", this);
    xEdit = new QLineEdit("0", this);
    yEdit = new QLineEdit("0", this);
    zEdit = new QLineEdit("0", this);

    heightEdit->setValidator(posValidator);
    crestWidthEdit->setValidator(posValidator);
    upstreamSlopeEdit->setValidator(slopeValidator);
    downstreamSlopeEdit->setValidator(slopeValidator);
    lengthEdit->setValidator(posValidator);
    layerCountEdit->setValidator(new QIntValidator(1, 200, this));
    xEdit->setValidator(coordValidator);
    yEdit->setValidator(coordValidator);
    zEdit->setValidator(coordValidator);

    form->addRow("坝高 H", heightEdit);
    form->addRow("坝顶宽 Bt", crestWidthEdit);
    form->addRow("上游坡比 mu", upstreamSlopeEdit);
    form->addRow("下游坡比 md", downstreamSlopeEdit);
    form->addRow("坝轴线长度 L", lengthEdit);
    form->addRow("分层数 N", layerCountEdit);
    form->addRow("基点 X", xEdit);
    form->addRow("基点 Y", yEdit);
    form->addRow("基点 Z", zEdit);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);

    auto* generateBtn = new QPushButton("生成3D坝体", this);
    btnLayout->addStretch();
    btnLayout->addWidget(generateBtn);

    diagramLabel = new QLabel(this);
    diagramLabel->setAlignment(Qt::AlignCenter);
    diagramLabel->setMinimumSize(0, 0);
    diagramLabel->setScaledContents(false);
    diagramLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    mainLayout->addWidget(pageNameLabel);
    mainLayout->addWidget(sectionTitleButton);
    mainLayout->addWidget(inputGroup);
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(diagramLabel, 2);
    mainLayout->addStretch(1);

    connect(generateBtn, &QPushButton::clicked, this, &GravityDamDialog::onGenerate3DClicked);

    onPreview2DClicked();
}

bool GravityDamDialog::readParameters(double& damHeight, double& crestWidth, double& upSlope,
                                      double& downSlope, double& damLength, int& layerCount,
                                      double& x, double& y, double& z, EngineeringTemplate& damType) const
{
    bool ok = false;

    damHeight = heightEdit->text().toDouble(&ok);
    if (!ok || damHeight <= 0.0)
        return false;

    crestWidth = crestWidthEdit->text().toDouble(&ok);
    if (!ok || crestWidth <= 0.0)
        return false;

    upSlope = upstreamSlopeEdit->text().toDouble(&ok);
    if (!ok || upSlope < 0.0)
        return false;

    downSlope = downstreamSlopeEdit->text().toDouble(&ok);
    if (!ok || downSlope < 0.0)
        return false;

    damLength = lengthEdit->text().toDouble(&ok);
    if (!ok || damLength <= 0.0)
        return false;

    layerCount = layerCountEdit->text().toInt(&ok);
    if (!ok || layerCount <= 0)
        return false;

    x = xEdit->text().toDouble(&ok);
    if (!ok)
        return false;

    y = yEdit->text().toDouble(&ok);
    if (!ok)
        return false;

    z = zEdit->text().toDouble(&ok);
    if (!ok)
        return false;

    damType = GravityDamTemplate;
    return true;
}

QVector<QPointF> GravityDamDialog::buildSectionPoints(double damHeight, double crestWidth,
                                                      double upSlope, double downSlope,
                                                      EngineeringTemplate tpl) const
{
    Q_UNUSED(tpl);

    const double x1 = damHeight * upSlope;
    const double x2 = x1 + crestWidth;
    const double x3 = x2 + damHeight * downSlope;

    QVector<QPointF> points;
    points << QPointF(0.0, 0.0)
           << QPointF(x1, damHeight)
           << QPointF(x2, damHeight)
           << QPointF(x3, 0.0);
    return points;
}

void GravityDamDialog::resizeEvent(QResizeEvent* event)
{
    BasePage::resizeEvent(event);
    if (!diagramPixmap.isNull() && diagramLabel)
        setScaledDiagram(diagramLabel, diagramPixmap);
}

void GravityDamDialog::updateIllustration(double damHeight, double crestWidth, double upSlope,
                                          double downSlope, int layerCount, EngineeringTemplate damType)
{
    Q_UNUSED(damHeight);
    Q_UNUSED(crestWidth);
    Q_UNUSED(upSlope);
    Q_UNUSED(downSlope);
    Q_UNUSED(layerCount);
    Q_UNUSED(damType);

    const QString resourcePath = QStringLiteral(":/resources/gravity_dam_parameter_diagram.png");
    diagramPixmap.load(resourcePath);
    if (diagramPixmap.isNull())
    {
        diagramLabel->setText("参数示意图加载失败：resources/gravity_dam_parameter_diagram.png");
        return;
    }

    setScaledDiagram(diagramLabel, diagramPixmap);
}

void GravityDamDialog::onPreview2DClicked()
{
    double H = 0.0, Bt = 0.0, mu = 0.0, md = 0.0, L = 0.0, x = 0.0, y = 0.0, z = 0.0;
    int N = 0;
    EngineeringTemplate type = GravityDamTemplate;

    if (!readParameters(H, Bt, mu, md, L, N, x, y, z, type))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L、N必须>0；坡比不能为负。");
        return;
    }

    updateIllustration(H, Bt, mu, md, N, type);
}

void GravityDamDialog::onGenerate3DClicked()
{
    double H = 0.0, Bt = 0.0, mu = 0.0, md = 0.0, L = 0.0, x = 0.0, y = 0.0, z = 0.0;
    int N = 0;
    EngineeringTemplate type = GravityDamTemplate;

    if (!readParameters(H, Bt, mu, md, L, N, x, y, z, type))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L、N必须>0；坡比不能为负。");
        return;
    }

    updateIllustration(H, Bt, mu, md, N, type);
    buildDamBody(H, Bt, mu, md, L, N, x, y, z, type);
}

void GravityDamDialog::buildDamBody(double damHeight, double crestWidth, double upSlope,
                                    double downSlope, double damLength, int layerCount, double x,
                                    double y, double z, EngineeringTemplate damType)
{
    GeometryCleanup::removeExistingGeometry(m_uuid);

    const auto section = buildSectionPoints(damHeight, crestWidth, upSlope, downSlope, damType);
    if (section.size() < 4 || damLength <= kEps || layerCount <= 0)
        return;

    TopoDS_Compound layeredDam;
    BRep_Builder builder;
    builder.MakeCompound(layeredDam);

    const double layerH = damHeight / layerCount;

    auto intersectionRangeAtZ = [&](double zQuery, double& xMin, double& xMax) -> bool
    {
        QVector<double> xs;

        for (int i = 0; i < section.size(); ++i)
        {
            const QPointF a = section[i];
            const QPointF b = section[(i + 1) % section.size()];

            const double zA = a.y();
            const double zB = b.y();
            const double low = qMin(zA, zB);
            const double high = qMax(zA, zB);

            if (zQuery < low - kEps || zQuery > high + kEps)
                continue;

            if (qAbs(zA - zB) < kEps)
            {
                xs << a.x() << b.x();
                continue;
            }

            const double t = (zQuery - zA) / (zB - zA);
            xs << (a.x() + t * (b.x() - a.x()));
        }

        if (xs.isEmpty())
            return false;

        xMin = xs[0];
        xMax = xs[0];
        for (double value : xs)
        {
            xMin = qMin(xMin, value);
            xMax = qMax(xMax, value);
        }

        return xMax - xMin > kEps;
    };

    for (int i = 0; i < layerCount; ++i)
    {
        const double z0 = z + layerH * i;
        const double z1 = z + layerH * (i + 1);

        double xL0 = 0.0, xR0 = 0.0, xL1 = 0.0, xR1 = 0.0;
        if (!intersectionRangeAtZ(z0 - z, xL0, xR0) || !intersectionRangeAtZ(z1 - z, xL1, xR1))
            continue;

        BRepBuilderAPI_MakePolygon layerProfile;
        layerProfile.Add(gp_Pnt(x + xL0, y, z0));
        layerProfile.Add(gp_Pnt(x + xL1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR0, y, z0));
        layerProfile.Close();

        TopoDS_Wire layerWire = layerProfile.Wire();
        BRepBuilderAPI_MakeFace faceMaker(layerWire);
        if (!faceMaker.IsDone())
            continue;

        TopoDS_Shape layerSolid = BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(0.0, damLength, 0.0)).Shape();
        builder.Add(layeredDam, layerSolid);
    }

    auto* object = new GeomShapeObject(m_uuid, new TopoDS_Shape(layeredDam), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}
