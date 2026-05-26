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
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVector>
#include <TopoDS_Wire.hxx>
#include <gp_Vec.hxx>

GravityDamDialog::GravityDamDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessGravityDam)
{
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
    state["damType"] = damTypeCombo->currentIndex();
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
    if (state.contains("damType"))
        damTypeCombo->setCurrentIndex(state["damType"].toInt());
    if (state.contains("x"))
        xEdit->setText(QString::number(state["x"].toDouble()));
    if (state.contains("y"))
        yEdit->setText(QString::number(state["y"].toDouble()));
    if (state.contains("z"))
        zEdit->setText(QString::number(state["z"].toDouble()));
}

void GravityDamDialog::initUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* pageNameLabel = new QLabel(QString("几何模块"), this);
    pageNameLabel->setObjectName("pageNameLabel");
    auto* sectionTitleButton = new QPushButton(QString("重力坝参数模板"), this);
    sectionTitleButton->setObjectName("sectionTitleButton");
    sectionTitleButton->setEnabled(false);

    auto* inputGroup = new QGroupBox("参数输入", this);
    auto* form = new QFormLayout(inputGroup);
    auto* posValidator = new QDoubleValidator(0.001, 1e9, 3, this);
    auto* slopeValidator = new QDoubleValidator(0.0, 1000.0, 4, this);
    auto* coordValidator = new QDoubleValidator(-1e9, 1e9, 3, this);

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

    auto* previewGroup = new QGroupBox("2D断面预览（XZ平面）", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    preview2DEdit = new QLineEdit(this);
    preview2DEdit->setReadOnly(true);
    diagramLabel = new QLabel(this);
    diagramLabel->setMinimumHeight(220);
    diagramLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(preview2DEdit);
    previewLayout->addWidget(diagramLabel);

    auto* btnLayout = new QHBoxLayout();
    auto* previewBtn = new QPushButton("更新2D断面", this);
    auto* generateBtn = new QPushButton("生成3D坝体", this);
    btnLayout->addStretch();
    btnLayout->addWidget(previewBtn);
    btnLayout->addWidget(generateBtn);

    mainLayout->addWidget(pageNameLabel);
    mainLayout->addWidget(sectionTitleButton);
    mainLayout->addWidget(inputGroup);
    mainLayout->addWidget(previewGroup);
    mainLayout->addLayout(btnLayout);
    mainLayout->addStretch();

    connect(previewBtn, &QPushButton::clicked, this, &GravityDamDialog::onPreview2DClicked);
    connect(generateBtn, &QPushButton::clicked, this, &GravityDamDialog::onGenerate3DClicked);

    onPreview2DClicked();
}

bool GravityDamDialog::readParameters(double& damHeight, double& crestWidth, double& upSlope,
                                      double& downSlope, double& damLength, int& layerCount,
                                      double& x, double& y, double& z, EngineeringTemplate& damType) const
{
    bool ok = false;
    damHeight = heightEdit->text().toDouble(&ok);
    if (!ok || damHeight <= 0)
        return false;
    crestWidth = crestWidthEdit->text().toDouble(&ok);
    if (!ok || crestWidth <= 0)
        return false;
    upSlope = upstreamSlopeEdit->text().toDouble(&ok);
    if (!ok || upSlope < 0)
        return false;
    downSlope = downstreamSlopeEdit->text().toDouble(&ok);
    if (!ok || downSlope < 0)
        return false;
    damLength = lengthEdit->text().toDouble(&ok);
    if (!ok || damLength <= 0)
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

void GravityDamDialog::applyPreviewText(double damHeight, double crestWidth, double upSlope,
                                        double downSlope, int layerCount, EngineeringTemplate damType) const
{
    const auto points = buildSectionPoints(damHeight, crestWidth, upSlope, downSlope, damType);
    if (points.size() < 4)
        return;
    const QString typeName = "重力坝模板";
    const double layerH = damHeight / layerCount;
    preview2DEdit->setText(QString("%1 | N=%2, ΔH=%3 | 顶宽=%4, 坝高=%5")
                               .arg(typeName)
                               .arg(layerCount)
                               .arg(layerH, 0, 'f', 3)
                               .arg(crestWidth, 0, 'f', 3)
                               .arg(damHeight, 0, 'f', 3));
}

QVector<QPointF> GravityDamDialog::buildSectionPoints(double damHeight, double crestWidth,
                                                      double upSlope, double downSlope,
                                                      EngineeringTemplate tpl) const
{
    QVector<QPointF> p;
    if (tpl == EarthRockfillDamTemplate)
    {
        // 土石坝：双坡更缓
        const double mu = qMax(upSlope, 1.2);
        const double md = qMax(downSlope, 1.5);
        const double x1 = damHeight * mu;
        const double x2 = x1 + crestWidth;
        const double x3 = x2 + damHeight * md;
        p << QPointF(0.0, 0.0) << QPointF(x1, damHeight) << QPointF(x2, damHeight) << QPointF(x3, 0.0);
    }
    else if (tpl == SlopeTemplate)
    {
        // 边坡：单坡台阶
        const double benchZ = damHeight * 0.55;
        const double benchW = qMax(crestWidth * 0.8, 3.0);
        const double xBenchLeft = (damHeight - benchZ) * qMax(downSlope, 0.8);
        const double xBaseRight = xBenchLeft + benchW + benchZ * qMax(downSlope, 0.8);
        p << QPointF(0.0, 0.0) << QPointF(xBenchLeft, benchZ) << QPointF(xBenchLeft + benchW, benchZ)
          << QPointF(xBaseRight, 0.0);
    }
    else if (tpl == CavernTemplate)
    {
        // 洞室：矩形+拱顶
        const double hWall = damHeight * 0.65;
        const double span = qMax(crestWidth * 2.0, 8.0);
        p << QPointF(0.0, 0.0) << QPointF(0.0, hWall) << QPointF(span * 0.25, damHeight)
          << QPointF(span * 0.75, damHeight) << QPointF(span, hWall) << QPointF(span, 0.0);
    }
    else if (tpl == DamFoundationLayerTemplate)
    {
        // 坝基分层：上窄下宽梯形基座
        const double baseH = damHeight * 0.7;
        const double toeL = baseH * qMax(upSlope, 0.3);
        const double toeR = baseH * qMax(downSlope, 0.6);
        p << QPointF(0.0, 0.0) << QPointF(toeL, baseH) << QPointF(toeL + crestWidth, baseH)
          << QPointF(toeL + crestWidth + toeR, 0.0);
    }
    else if (tpl == CurtainGroutingTemplate)
    {
        // 帷幕防渗：顶部平台+垂向帷幕墙
        const double span = qMax(crestWidth * 2.0, 10.0);
        const double cutX = span * 0.45;
        p << QPointF(0.0, 0.0) << QPointF(0.0, damHeight * 0.35) << QPointF(span, damHeight * 0.35)
          << QPointF(span, 0.0);
        p << QPointF(cutX, damHeight * 0.35) << QPointF(cutX + span * 0.06, damHeight * 0.35)
          << QPointF(cutX + span * 0.06, 0.0) << QPointF(cutX, 0.0);
    }
    else if (tpl == SupportStructureTemplate)
    {
        // 支护结构：锚喷+挡墙的组合轮廓
        const double wallH = damHeight * 0.6;
        const double wallW = qMax(crestWidth * 0.7, 2.0);
        p << QPointF(0.0, 0.0) << QPointF(0.0, wallH) << QPointF(wallW, wallH)
          << QPointF(wallW + wallH * 0.8, 0.0);
    }
    else
    {
        // 重力坝模板（默认）
        const double x1 = damHeight * upSlope;
        const double x2 = x1 + crestWidth;
        const double x3 = x2 + damHeight * downSlope;
        p << QPointF(0.0, 0.0) << QPointF(x1, damHeight) << QPointF(x2, damHeight) << QPointF(x3, 0.0);
    }
    return p;
}

void GravityDamDialog::updateIllustration(double damHeight, double crestWidth, double upSlope,
                                          double downSlope, int layerCount, EngineeringTemplate damType)
{
    const auto points = buildSectionPoints(damHeight, crestWidth, upSlope, downSlope, damType);
    if (points.size() < 4)
        return;

    QPixmap pix(520, 240);
    pix.fill(Qt::white);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double maxX = 0.0, maxZ = damHeight;
    for (const auto& pt : points)
    {
        maxX = qMax(maxX, pt.x());
        maxZ = qMax(maxZ, pt.y());
    }
    const double margin = 24.0;
    const double sx = (pix.width() - 2 * margin) / qMax(maxX, 1.0);
    const double sz = (pix.height() - 2 * margin) / qMax(maxZ, 1.0);
    const double scale = qMin(sx, sz);

    auto mapPt = [&](const QPointF& p)
    { return QPointF(margin + p.x() * scale, pix.height() - margin - p.y() * scale); };

    painter.setPen(QPen(QColor("#4C5A78"), 2));
    painter.drawLine(QPointF(margin, pix.height() - margin), QPointF(pix.width() - margin, pix.height() - margin));
    painter.drawLine(QPointF(margin, pix.height() - margin), QPointF(margin, margin));

    QPolygonF poly;
    for (const auto& pt : points)
        poly << mapPt(pt);

    painter.setPen(QPen(QColor("#1F4E79"), 2));
    painter.setBrush(QColor(193, 222, 255, 180));
    painter.drawPolygon(poly);

    painter.setPen(QPen(QColor("#7A7A7A"), 1, Qt::DashLine));
    for (int i = 1; i < layerCount; ++i)
    {
        const double zLayer = damHeight * i / layerCount;
        const QPointF p1 = mapPt(QPointF(0.0, zLayer));
        painter.drawLine(QPointF(margin, p1.y()), QPointF(pix.width() - margin, p1.y()));
    }

    painter.setPen(QPen(Qt::black, 1));
    painter.drawText(12, 18, QString("坝型: 重力坝模板"));
    painter.drawText(12, 36, QString("H=%1, Bt=%2, N=%3").arg(damHeight).arg(crestWidth).arg(layerCount));
    diagramLabel->setPixmap(pix);
}

void GravityDamDialog::onPreview2DClicked()
{
    double H = 0, Bt = 0, mu = 0, md = 0, L = 0, x = 0, y = 0, z = 0;
    int N = 0;
    EngineeringTemplate type = GravityDamTemplate;
    if (!readParameters(H, Bt, mu, md, L, N, x, y, z, type))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L、N必须>0；坡比不能为负。");
        return;
    }
    applyPreviewText(H, Bt, mu, md, N, type);
    updateIllustration(H, Bt, mu, md, N, type);
}

void GravityDamDialog::onGenerate3DClicked()
{
    double H = 0, Bt = 0, mu = 0, md = 0, L = 0, x = 0, y = 0, z = 0;
    int N = 0;
    EngineeringTemplate type = GravityDamTemplate;
    if (!readParameters(H, Bt, mu, md, L, N, x, y, z, type))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L、N必须>0；坡比不能为负。");
        return;
    }

    applyPreviewText(H, Bt, mu, md, N, type);
    updateIllustration(H, Bt, mu, md, N, type);
    buildDamBody(H, Bt, mu, md, L, N, x, y, z, type);
}

void GravityDamDialog::onTemplateChanged(int index)
{
    Q_UNUSED(index);
    onPreview2DClicked();
}

void GravityDamDialog::buildDamBody(double damHeight, double crestWidth, double upSlope,
                                    double downSlope, double damLength, int layerCount, double x,
                                    double y, double z, EngineeringTemplate damType)
{
    GeometryCleanup::removeExistingGeometry(m_uuid);

    TopoDS_Compound layeredDam;
    BRep_Builder builder;
    builder.MakeCompound(layeredDam);
    const double layerH = damHeight / layerCount;
    const auto section = buildSectionPoints(damHeight, crestWidth, upSlope, downSlope, damType);
    if (section.size() < 4)
        return;
    auto intersectionRangeAtZ = [&](double zQuery, double& xMin, double& xMax) -> bool
    {
        QVector<double> xs;
        for (int i = 0; i < section.size(); ++i)
        {
            const QPointF a = section[i];
            const QPointF b = section[(i + 1) % section.size()];
            const double zA = a.y();
            const double zB = b.y();
            const double lo = qMin(zA, zB);
            const double hi = qMax(zA, zB);
            if (zQuery < lo - 1e-9 || zQuery > hi + 1e-9)
                continue;
            if (qFuzzyCompare(zA + 1.0, zB + 1.0))
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
        for (double vx : xs)
        {
            xMin = qMin(xMin, vx);
            xMax = qMax(xMax, vx);
        }
        return true;
    };

    for (int i = 0; i < layerCount; ++i)
    {
        const double z0 = z + layerH * i;
        const double z1 = z + layerH * (i + 1);
        double xL0 = 0, xR0 = 0, xL1 = 0, xR1 = 0;
        if (!intersectionRangeAtZ(z0 - z, xL0, xR0) || !intersectionRangeAtZ(z1 - z, xL1, xR1))
            continue;
        BRepBuilderAPI_MakePolygon layerProfile;
        layerProfile.Add(gp_Pnt(x + xL0, y, z0));
        layerProfile.Add(gp_Pnt(x + xL1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR1, y, z1));
        layerProfile.Add(gp_Pnt(x + xR0, y, z0));
        layerProfile.Close();

        TopoDS_Wire layerWire = layerProfile.Wire();
        TopoDS_Face layerFace = BRepBuilderAPI_MakeFace(layerWire);
        TopoDS_Shape layerSolid = BRepPrimAPI_MakePrism(layerFace, gp_Vec(0, damLength, 0)).Shape();
        builder.Add(layeredDam, layerSolid);
    }
    TopoDS_Shape damShape = layeredDam;

    TopoDS_Shape* shape = new TopoDS_Shape(damShape);
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}
