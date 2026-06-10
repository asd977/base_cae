#include "polygondialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>

#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

// ===== OCC =====
#include <GeomShapeObject.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>

PolygonDialog::PolygonDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessPolygon)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    auto* pageHeaderLabel = new QLabel(QString("参数设置"), this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");
    layout->addWidget(pageHeaderLabel);
    layout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);
    contentLayout->setSpacing(6);
    layout->addLayout(contentLayout);

    // ---- 基准面 ----
    planeLabel = new QLabel(QString("基准面："), this);
    planeCombo = new QComboBox(this);
    planeCombo->setMinimumWidth(10);
    planeCombo->addItem("XY", static_cast<int>(PlaneType::XY));
    planeCombo->addItem("XZ", static_cast<int>(PlaneType::XZ));
    planeCombo->addItem("YZ", static_cast<int>(PlaneType::YZ));
    contentLayout->addWidget(planeLabel);
    contentLayout->addWidget(planeCombo);

    connect(planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PolygonDialog::onPlaneChanged);

    // ---- 半径 ----
    QLabel* radiusLabel = new QLabel(QString("半径："), this);
    radiusEdit = new QLineEdit(this);
    radiusEdit->setMinimumWidth(10);
    radiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this));
    contentLayout->addWidget(radiusLabel);
    contentLayout->addWidget(radiusEdit);

    // ---- 边数 ----
    QLabel* sidesLabel = new QLabel(QString("边数："), this);
    sidesEdit = new QLineEdit(this);
    sidesEdit->setMinimumWidth(10);
    sidesEdit->setValidator(new QIntValidator(3, 100, this));
    contentLayout->addWidget(sidesLabel);
    contentLayout->addWidget(sidesEdit);

    // ---- 旋转角（建议加上）----
    QLabel* rotateLabel = new QLabel(QString("旋转角(°)："), this);
    rotateDegEdit = new QLineEdit(this);
    rotateDegEdit->setMinimumWidth(10);
    rotateDegEdit->setValidator(new QDoubleValidator(-360.0, 360.0, 6, this));
    contentLayout->addWidget(rotateLabel);
    contentLayout->addWidget(rotateDegEdit);

    // ---- 中心点 X/Y/Z（与其它 2D 图形一致）----
    centerXLabel = new QLabel(QString("中心X坐标："), this);
    centerXEdit = new QLineEdit(this);
    centerXEdit->setMinimumWidth(10);
    centerXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    contentLayout->addWidget(centerXLabel);
    contentLayout->addWidget(centerXEdit);

    centerYLabel = new QLabel(QString("中心Y坐标："), this);
    centerYEdit = new QLineEdit(this);
    centerYEdit->setMinimumWidth(10);
    centerYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    contentLayout->addWidget(centerYLabel);
    contentLayout->addWidget(centerYEdit);

    centerZLabel = new QLabel(QString("中心Z坐标："), this);
    centerZEdit = new QLineEdit(this);
    centerZEdit->setMinimumWidth(10);
    centerZEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    contentLayout->addWidget(centerZLabel);
    contentLayout->addWidget(centerZEdit);

    // ---- 按钮 ----
    QPushButton* okButton = new QPushButton(QString("生成几何体"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    contentLayout->addLayout(buttonLayout);
    contentLayout->addStretch();

    connect(okButton, &QPushButton::clicked, this, &PolygonDialog::validateInput);

    // ---- 默认值 ----
    planeCombo->setCurrentIndex(0); // XY
    radiusEdit->setText("1.0");
    sidesEdit->setText("5");
    rotateDegEdit->setText("0.0");
    centerXEdit->setText("0");
    centerYEdit->setText("0");
    centerZEdit->setText("0");

    updatePlaneUi();
}

double PolygonDialog::getRadius() const
{
    return radius;
}
int PolygonDialog::getSides() const
{
    return sides;
}
gp_Pnt PolygonDialog::getCenter() const
{
    return center;
}

QJsonObject PolygonDialog::saveJson()
{
    QJsonObject state;
    state["plane"] = planeCombo->currentData().toInt();
    state["radius"] = radiusEdit->text().toDouble();
    state["sides"] = sidesEdit->text().toInt();
    state["rotateDeg"] = rotateDegEdit->text().toDouble();
    state["X"] = centerXEdit->text().toDouble();
    state["Y"] = centerYEdit->text().toDouble();
    state["Z"] = centerZEdit->text().toDouble();
    return state;
}

void PolygonDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane"))
    {
        const int p = state["plane"].toInt();
        const int idx = planeCombo->findData(p);
        if (idx >= 0)
            planeCombo->setCurrentIndex(idx);
    }
    if (state.contains("radius"))
        radiusEdit->setText(QString::number(state["radius"].toDouble()));
    if (state.contains("sides"))
        sidesEdit->setText(QString::number(state["sides"].toInt()));
    if (state.contains("rotateDeg"))
        rotateDegEdit->setText(QString::number(state["rotateDeg"].toDouble()));
    if (state.contains("X"))
        centerXEdit->setText(QString::number(state["X"].toDouble()));
    if (state.contains("Y"))
        centerYEdit->setText(QString::number(state["Y"].toDouble()));
    if (state.contains("Z"))
        centerZEdit->setText(QString::number(state["Z"].toDouble()));

    updatePlaneUi();
}

void PolygonDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

PolygonDialog::PlaneType PolygonDialog::currentPlane() const
{
    return static_cast<PlaneType>(planeCombo->currentData().toInt());
}

gp_Dir PolygonDialog::planeNormal(PlaneType p) const
{
    switch (p)
    {
    case PlaneType::XY:
        return gp_Dir(0, 0, 1);
    case PlaneType::XZ:
        return gp_Dir(0, 1, 0);
    case PlaneType::YZ:
        return gp_Dir(1, 0, 0);
    }
    return gp_Dir(0, 0, 1);
}

void PolygonDialog::updatePlaneUi()
{
    // 提示哪个坐标是“平面偏移”
    switch (currentPlane())
    {
    case PlaneType::XY:
        centerXLabel->setText(QString("中心X坐标："));
        centerYLabel->setText(QString("中心Y坐标："));
        centerZLabel->setText(QString("平面偏移(Z)："));
        break;
    case PlaneType::XZ:
        centerXLabel->setText(QString("中心X坐标："));
        centerYLabel->setText(QString("平面偏移(Y)："));
        centerZLabel->setText(QString("中心Z坐标："));
        break;
    case PlaneType::YZ:
        centerXLabel->setText(QString("平面偏移(X)："));
        centerYLabel->setText(QString("中心Y坐标："));
        centerZLabel->setText(QString("中心Z坐标："));
        break;
    }
}

void PolygonDialog::validateInput()
{
    bool ok = false;

    const double r = radiusEdit->text().toDouble(&ok);
    if (!ok || r <= 0.0)
    {
        QMessageBox::warning(this, "输入错误", "半径必须为正数。");
        return;
    }

    const int s = sidesEdit->text().toInt(&ok);
    if (!ok || s < 3 || s > 100)
    {
        QMessageBox::warning(this, "输入错误", "边数必须在 3 到 100 之间。");
        return;
    }

    const double rotDeg = rotateDegEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "旋转角必须为有效数字。");
        return;
    }

    const double x = centerXEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "中心X坐标必须为有效数字。");
        return;
    }

    const double y = centerYEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "中心Y坐标必须为有效数字。");
        return;
    }

    const double z = centerZEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, "输入错误", "中心Z坐标必须为有效数字。");
        return;
    }

    radius = r;
    sides = s;
    center = gp_Pnt(x, y, z);

    // ---- 基于基准面构造局部坐标系 ----
    const PlaneType p = currentPlane();
    gp_Ax2 axis(center, planeNormal(p));

    // 平面内两个单位方向
    const gp_Vec ux(axis.XDirection());
    const gp_Vec uy(axis.YDirection());

    const double pi = 3.14159265358979323846;
    const double rotRad = rotDeg * pi / 180.0;

    // ---- 计算正多边形顶点（外接圆半径=radius）----
    TopoDS_Shape polyShape;
    try
    {
        BRepBuilderAPI_MakePolygon poly;

        for (int i = 0; i < sides; ++i)
        {
            const double theta = rotRad + (2.0 * pi * i) / static_cast<double>(sides);
            const double c = std::cos(theta);
            const double s2 = std::sin(theta);

            gp_Pnt pt = center.Translated(ux * (radius * c) + uy * (radius * s2));
            poly.Add(pt);
        }
        poly.Close();

        TopoDS_Wire wire = poly.Wire();

        // 面（需要面就用 Face；只要线框就改成 polyShape = wire）
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
        polyShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
    }
    catch (...)
    {
        QMessageBox::warning(this, "生成失败", "创建正多边形失败（OCC 异常）。");
        return;
    }

    if (polyShape.IsNull())
    {
        QMessageBox::warning(this, "生成失败", "创建正多边形失败（shape 为空）。");
        return;
    }

    // ---- 写入几何管理器：按你 Circle 的方式 ----
    TopoDS_Shape* shape = new TopoDS_Shape(polyShape);

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        QMessageBox::warning(this, "错误", "几何管理器未初始化。");
        delete shape;
        return;
    }

    GeometryCleanup::removeExistingGeometry(m_uuid);

    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);

    // ---- 显示注册（面/边分开加）----
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (vis)
    {
        if (object->getSurfActor())
        {
            vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face");
        }
        if (object->getEdgeActor())
        {
            vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_edge");

            if (auto* projectManagerService =
                    ServiceRegistry::instance().getService<ProjectManagerService>())
            {
                projectManagerService->setNodeDimmed(m_uuid, false);
            }
        }
    }
}
