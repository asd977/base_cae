#include "GravityDamDialog.h"

#include "../builders/BooleanBuilder.h"
#include "../builders/GeometryBuildUtils.h"
#include "../core/CadDocument.h"
#include "../core/CadBody.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <GeometryServiceAccess.h>
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
#include <services/PreProcessingService.h>
#include <GeometryWorkbench.h>
#include <GeometryDisplayManager.h>

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
    state["linkGeology"] = m_linkGeology ? m_linkGeology->isChecked() : false;
    state["targetGeology"] = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
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

    if (m_linkGeology && state.contains("linkGeology"))
    {
        m_linkGeology->setChecked(state["linkGeology"].toBool());
        onLinkGeologyToggled(m_linkGeology->isChecked());
    }
    if (m_geologyCombo && state.contains("targetGeology"))
    {
        const QString target = state["targetGeology"].toString();
        int idx = m_geologyCombo->findData(target);
        if (idx >= 0) m_geologyCombo->setCurrentIndex(idx);
    }

    onPreview2DClicked();
}

void GravityDamDialog::initUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    auto* pageHeaderLabel = new QLabel(QString("重力坝参数模板"), this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");
    mainLayout->addWidget(pageHeaderLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);
    contentLayout->setSpacing(6);
    mainLayout->addLayout(contentLayout);


    auto* inputGroup = new QGroupBox("参数输入", this);
    auto* form = new QFormLayout(inputGroup);

    auto* posValidator = new QDoubleValidator(0.001, 1.0e9, 3, this);
    auto* slopeValidator = new QDoubleValidator(0.0, 1000.0, 4, this);
    auto* coordValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);

    heightEdit = new QLineEdit("80", this);
    heightEdit->setMinimumWidth(10);
    crestWidthEdit = new QLineEdit("10", this);
    crestWidthEdit->setMinimumWidth(10);
    upstreamSlopeEdit = new QLineEdit("0.2", this);
    upstreamSlopeEdit->setMinimumWidth(10);
    downstreamSlopeEdit = new QLineEdit("0.75", this);
    downstreamSlopeEdit->setMinimumWidth(10);
    lengthEdit = new QLineEdit("180", this);
    lengthEdit->setMinimumWidth(10);
    layerCountEdit = new QLineEdit("6", this);
    layerCountEdit->setMinimumWidth(10);
    xEdit = new QLineEdit("0", this);
    xEdit->setMinimumWidth(10);
    yEdit = new QLineEdit("0", this);
    yEdit->setMinimumWidth(10);
    zEdit = new QLineEdit("0", this);
    zEdit->setMinimumWidth(10);

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

    auto* geoGroup = new QGroupBox("河谷地形贴合联动", this);
    auto* geoForm = new QFormLayout(geoGroup);
    m_linkGeology = new QCheckBox("关联目标地质体进行河谷坝底贴合", this);
    m_geologyCombo = new QComboBox(this);
    m_geologyCombo->setEnabled(false);
    geoForm->addRow(m_linkGeology);
    geoForm->addRow("目标地质体", m_geologyCombo);

    connect(m_linkGeology, &QCheckBox::toggled, this, &GravityDamDialog::onLinkGeologyToggled);
    connect(m_geologyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyTargetGeologyDefaults();
    });

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

    contentLayout->addWidget(inputGroup);
    contentLayout->addWidget(geoGroup);
    contentLayout->addLayout(btnLayout);
    contentLayout->addWidget(diagramLabel, 2);
    contentLayout->addStretch(1);

    connect(generateBtn, &QPushButton::clicked, this, &GravityDamDialog::onGenerate3DClicked);

    onPreview2DClicked();
}

void GravityDamDialog::applyTargetGeologyDefaults()
{
    if (!m_geologyCombo) return;
    const QString uuid = m_geologyCombo->currentData().toString();
    if (uuid.isEmpty()) return;
    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc) return;
    CadBody* b = doc->getGeomShapeObject(uuid);
    if (!b || !b->getShape() || b->getShape()->IsNull()) return;
    Bnd_Box box;
    BRepBndLib::Add(*(b->getShape()), box);
    if (box.IsVoid()) return;
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    const double cx = (xmin + xmax) * 0.5;
    const double cy = (ymin + ymax) * 0.5;
    const double cz = zmin + 10.0;
    if (xEdit) xEdit->setText(QString::number(cx, 'f', 1));
    if (yEdit) yEdit->setText(QString::number(cy, 'f', 1));
    if (zEdit) zEdit->setText(QString::number(cz, 'f', 1));

    const double targetH = (zmax - cz) * 1.05;
    if (heightEdit && heightEdit->text().toDouble() < targetH * 0.5)
        heightEdit->setText(QString::number(targetH, 'f', 1));
    const double targetL = (lengthEdit && lengthEdit->text().toDouble() < (ymax - ymin) * 0.3)
        ? (ymax - ymin) * 0.6 : (lengthEdit ? lengthEdit->text().toDouble() : 400.0);
    if (lengthEdit) lengthEdit->setText(QString::number(targetL, 'f', 1));
}

void GravityDamDialog::refreshGeologyCombo()
{
    if (!m_geologyCombo) return;
    const QString currentUuid = m_geologyCombo->currentData().toString();
    const QSignalBlocker blocker(m_geologyCombo);
    m_geologyCombo->clear();
    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc) return;
    const auto map = doc->getShapMap();
    int defaultIdx = -1;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        if (it.key() == m_uuid) continue;
        CadBody* b = it.value();
        if (!b || !b->getShape() || b->getShape()->IsNull()) continue;
        m_geologyCombo->addItem(b->getName().isEmpty() ? it.key() : b->getName(), it.key());
        if (b->getName().contains("地形") || b->getName().contains("地质") || !b->faceGroups().isEmpty())
            defaultIdx = m_geologyCombo->count() - 1;
    }
    if (!currentUuid.isEmpty())
    {
        int idx = m_geologyCombo->findData(currentUuid);
        if (idx >= 0) m_geologyCombo->setCurrentIndex(idx);
    }
    else if (defaultIdx >= 0)
    {
        m_geologyCombo->setCurrentIndex(defaultIdx);
    }
}

void GravityDamDialog::onLinkGeologyToggled(bool checked)
{
    if (m_geologyCombo) m_geologyCombo->setEnabled(checked);
    if (checked)
    {
        refreshGeologyCombo();
        applyTargetGeologyDefaults();
    }
}

void GravityDamDialog::showEvent(QShowEvent* event)
{
    BasePage::showEvent(event);
    refreshGeologyCombo();
    if (m_linkGeology && m_linkGeology->isChecked())
        applyTargetGeologyDefaults();
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

    if (m_linkGeology && m_linkGeology->isChecked())
    {
        const QString targetUuid = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
        if (targetUuid.isEmpty())
        {
            QMessageBox::warning(this, tr("参数提示"), tr("请选择要关联的目标地质体。"));
            return;
        }

        auto* doc = GeomManager::CadDocument::getInstance();
        CadBody* targetBody = doc ? doc->getGeomShapeObject(targetUuid) : nullptr;
        if (targetBody && targetBody->getShape())
        {
            Bnd_Box geoBox;
            BRepBndLib::Add(*(targetBody->getShape()), geoBox);
            if (!geoBox.IsVoid())
            {
                double xmin, ymin, zmin, xmax, ymax, zmax;
                geoBox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                if (qAbs(x) < 1.0 && qAbs(y) < 1.0 && (xmin > 10.0 || xmax < -10.0))
                {
                    applyTargetGeologyDefaults();
                }
            }
        }

        const double actualX = xEdit ? xEdit->text().toDouble() : x;
        const double actualY = yEdit ? yEdit->text().toDouble() : y;
        const double actualZ = zEdit ? zEdit->text().toDouble() : z;
        const double actualH = heightEdit ? heightEdit->text().toDouble() : H;
        const double actualL = lengthEdit ? lengthEdit->text().toDouble() : L;

        const auto section = buildSectionPoints(actualH, Bt, mu, md, type);
        TopoDS_Shape rawDam = makePrismFromProfile(section, actualL);
        if (rawDam.IsNull())
        {
            QMessageBox::warning(this, tr("生成失败"), tr("无法构建重力坝基础实体。"));
            return;
        }

        const double actualBottomW = Bt + actualH * (mu + md);
        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(actualX - actualBottomW * 0.5, actualY - actualL * 0.5, actualZ));
        BRepBuilderAPI_Transform xform(rawDam, trsf);
        TopoDS_Shape placedDam = xform.Shape();

        TopoDS_Shape fittedDam;
        QVector<int> contactFaces;
        QString errorMsg;
        if (!BooleanBuilder::fitDamToValley(targetUuid, placedDam, fittedDam, contactFaces, errorMsg))
        {
            QMessageBox::warning(this, tr("贴合失败"), tr("重力坝与河谷贴合布尔裁剪失败：%1").arg(errorMsg));
            return;
        }

        GeometryBuildUtils::registerShape(m_uuid, getDisplayName(), new TopoDS_Shape(fittedDam));
        if (auto* body = doc ? doc->getGeomShapeObject(m_uuid) : nullptr)
        {
            body->addFaceGroup({QStringLiteral("重力坝基底接触面组"), QStringLiteral("Contact"), contactFaces});
        }

        // 同步树节点状态、勾选并提交历史
        if (auto* pre = ServiceRegistry::instance().getService<PreProcessingService>())
        {
            if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
            {
                workbench->refreshTreePresentation(m_uuid);
                workbench->refreshTreePresentation(targetUuid);
            }
        }

        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();

        QMessageBox::information(this, tr("贴合完成"),
            tr("重力坝已严丝合缝贴合起伏河谷地形，削掉地下掩埋与两岸插入部分，生成基底贴合面 %1 个，已自动归入【重力坝基底接触面组】。")
            .arg(contactFaces.size()));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;
    if (!service->recreateGravityDam(m_uuid, getDisplayName(), H, Bt, mu, md,
                                     L, N, x, y, z))
    {
        QMessageBox::warning(this, tr("生成失败"), tr("重力坝参数无效，无法生成三维实体。"));
    }
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
