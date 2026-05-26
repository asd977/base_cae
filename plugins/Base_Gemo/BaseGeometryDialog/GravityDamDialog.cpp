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
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
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
    xEdit = new QLineEdit("0", this);
    yEdit = new QLineEdit("0", this);
    zEdit = new QLineEdit("0", this);

    heightEdit->setValidator(posValidator);
    crestWidthEdit->setValidator(posValidator);
    upstreamSlopeEdit->setValidator(slopeValidator);
    downstreamSlopeEdit->setValidator(slopeValidator);
    lengthEdit->setValidator(posValidator);
    xEdit->setValidator(coordValidator);
    yEdit->setValidator(coordValidator);
    zEdit->setValidator(coordValidator);

    form->addRow("坝高 H", heightEdit);
    form->addRow("坝顶宽 Bt", crestWidthEdit);
    form->addRow("上游坡比 mu", upstreamSlopeEdit);
    form->addRow("下游坡比 md", downstreamSlopeEdit);
    form->addRow("坝轴线长度 L", lengthEdit);
    form->addRow("基点 X", xEdit);
    form->addRow("基点 Y", yEdit);
    form->addRow("基点 Z", zEdit);

    auto* previewGroup = new QGroupBox("2D断面预览（XZ平面）", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    preview2DEdit = new QLineEdit(this);
    preview2DEdit->setReadOnly(true);
    previewLayout->addWidget(preview2DEdit);

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
                                      double& downSlope, double& damLength, double& x, double& y,
                                      double& z) const
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
    x = xEdit->text().toDouble(&ok);
    if (!ok)
        return false;
    y = yEdit->text().toDouble(&ok);
    if (!ok)
        return false;
    z = zEdit->text().toDouble(&ok);
    return ok;
}

void GravityDamDialog::applyPreviewText(double damHeight, double crestWidth, double upSlope,
                                        double downSlope) const
{
    const double baseWidth = crestWidth + damHeight * (upSlope + downSlope);
    const double x0 = 0.0;
    const double x1 = damHeight * upSlope;
    const double x2 = x1 + crestWidth;
    const double x3 = baseWidth;
    preview2DEdit->setText(QString("P1(0,0) P2(%1,%2) P3(%3,%2) P4(%4,0)")
                               .arg(x1, 0, 'f', 3)
                               .arg(damHeight, 0, 'f', 3)
                               .arg(x2, 0, 'f', 3)
                               .arg(x3, 0, 'f', 3));
    Q_UNUSED(x0);
}

void GravityDamDialog::onPreview2DClicked()
{
    double H = 0, Bt = 0, mu = 0, md = 0, L = 0, x = 0, y = 0, z = 0;
    if (!readParameters(H, Bt, mu, md, L, x, y, z))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L必须>0；坡比不能为负。");
        return;
    }
    applyPreviewText(H, Bt, mu, md);
}

void GravityDamDialog::onGenerate3DClicked()
{
    double H = 0, Bt = 0, mu = 0, md = 0, L = 0, x = 0, y = 0, z = 0;
    if (!readParameters(H, Bt, mu, md, L, x, y, z))
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：H、Bt、L必须>0；坡比不能为负。");
        return;
    }

    applyPreviewText(H, Bt, mu, md);
    buildDamBody(H, Bt, mu, md, L, x, y, z);
}

void GravityDamDialog::buildDamBody(double damHeight, double crestWidth, double upSlope,
                                    double downSlope, double damLength, double x, double y, double z)
{
    GeometryCleanup::removeExistingGeometry(m_uuid);

    const double x1 = x + damHeight * upSlope;
    const double x2 = x1 + crestWidth;
    const double x3 = x2 + damHeight * downSlope;
    const double zTop = z + damHeight;

    BRepBuilderAPI_MakePolygon profile;
    profile.Add(gp_Pnt(x, y, z));
    profile.Add(gp_Pnt(x1, y, zTop));
    profile.Add(gp_Pnt(x2, y, zTop));
    profile.Add(gp_Pnt(x3, y, z));
    profile.Close();

    TopoDS_Wire wire = profile.Wire();
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
    TopoDS_Shape damShape = BRepPrimAPI_MakePrism(face, gp_Vec(0, damLength, 0)).Shape();

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
