#include "rectangledialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDoubleValidator>
#include <QMessageBox>
#include <QIcon>

#include <PublicConstants.h>

// ===== OCC =====
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>

#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>

// ===== 你的项目类（按你 CircleDialog 用的）=====
#include <ServiceRegistry.h>
#include <GeomShapeObject.h>  // 你项目里 GeomShapeObject

#include <services/VisualWidgetService.h>
#include <services/ProjectManagerService.h>

RectangleDialog::RectangleDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessRectangle)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    // ---- 基准面选择 ----
    planeLabel = new QLabel(QStringLiteral("基准面："), this);
    planeCombo = new QComboBox(this);
    planeCombo->addItem("XY", static_cast<int>(PlaneType::XY));
    planeCombo->addItem("XZ", static_cast<int>(PlaneType::XZ));
    planeCombo->addItem("YZ", static_cast<int>(PlaneType::YZ));
    layout->addWidget(planeLabel);
    layout->addWidget(planeCombo);

    connect(planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RectangleDialog::onPlaneChanged);

    // ---- 长度输入 ----
    QLabel* lengthLabel = new QLabel(QStringLiteral("长度："), this);
    lengthEdit = new QLineEdit(this);
    lengthEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this));
    layout->addWidget(lengthLabel);
    layout->addWidget(lengthEdit);

    // ---- 宽度输入 ----
    QLabel* widthLabel = new QLabel(QStringLiteral("宽度："), this);
    widthEdit = new QLineEdit(this);
    widthEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this));
    layout->addWidget(widthLabel);
    layout->addWidget(widthEdit);

    // ---- 中心点输入（与 Circle 一致：X/Y/Z 都有，基准面决定哪个是偏移）----
    centerXLabel = new QLabel(QStringLiteral("中心X坐标："), this);
    centerXEdit  = new QLineEdit(this);
    centerXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerXLabel);
    layout->addWidget(centerXEdit);

    centerYLabel = new QLabel(QStringLiteral("中心Y坐标："), this);
    centerYEdit  = new QLineEdit(this);
    centerYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerYLabel);
    layout->addWidget(centerYEdit);

    centerZLabel = new QLabel(QStringLiteral("中心Z坐标："), this);
    centerZEdit  = new QLineEdit(this);
    centerZEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerZLabel);
    layout->addWidget(centerZEdit);

    // ---- 创建按钮 ----
    QPushButton* okButton = new QPushButton(QStringLiteral("生成几何体"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    layout->addLayout(buttonLayout);
    layout->addStretch();

    connect(okButton, &QPushButton::clicked, this, &RectangleDialog::validateInput);

    // ---- 默认值 ----
    planeCombo->setCurrentIndex(0); // XY
    lengthEdit->setText("2.0");
    widthEdit->setText("1.0");
    centerXEdit->setText("0.0");
    centerYEdit->setText("0.0");
    centerZEdit->setText("0.0");

    updatePlaneUi();
}

double RectangleDialog::getLength() const { return length; }
double RectangleDialog::getWidth()  const { return width; }
gp_Pnt  RectangleDialog::getCenter() const { return center; }

QJsonObject RectangleDialog::saveJson()
{
    QJsonObject state;
    state["plane"]  = planeCombo->currentData().toInt();
    state["length"] = lengthEdit->text().toDouble();
    state["width"]  = widthEdit->text().toDouble();
    state["X"]      = centerXEdit->text().toDouble();
    state["Y"]      = centerYEdit->text().toDouble();
    state["Z"]      = centerZEdit->text().toDouble();
    return state;
}

void RectangleDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane")) {
        const int p = state["plane"].toInt();
        const int idx = planeCombo->findData(p);
        if (idx >= 0) planeCombo->setCurrentIndex(idx);
    }

    if (state.contains("length"))
        lengthEdit->setText(QString::number(state["length"].toDouble()));

    if (state.contains("width"))
        widthEdit->setText(QString::number(state["width"].toDouble()));

    if (state.contains("X"))
        centerXEdit->setText(QString::number(state["X"].toDouble()));

    if (state.contains("Y"))
        centerYEdit->setText(QString::number(state["Y"].toDouble()));

    if (state.contains("Z"))
        centerZEdit->setText(QString::number(state["Z"].toDouble()));

    updatePlaneUi();
}

void RectangleDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

RectangleDialog::PlaneType RectangleDialog::currentPlane() const
{
    return static_cast<PlaneType>(planeCombo->currentData().toInt());
}

gp_Dir RectangleDialog::planeNormal(PlaneType p) const
{
    switch (p)
    {
    case PlaneType::XY: return gp_Dir(0, 0, 1);
    case PlaneType::XZ: return gp_Dir(0, 1, 0);
    case PlaneType::YZ: return gp_Dir(1, 0, 0);
    }
    return gp_Dir(0, 0, 1);
}

void RectangleDialog::updatePlaneUi()
{
    // 和 Circle 同逻辑：让用户知道哪个是“平面偏移”
    switch (currentPlane())
    {
    case PlaneType::XY:
        centerXLabel->setText(QStringLiteral("中心X坐标："));
        centerYLabel->setText(QStringLiteral("中心Y坐标："));
        centerZLabel->setText(QStringLiteral("平面偏移(Z)："));
        break;
    case PlaneType::XZ:
        centerXLabel->setText(QStringLiteral("中心X坐标："));
        centerYLabel->setText(QStringLiteral("平面偏移(Y)："));
        centerZLabel->setText(QStringLiteral("中心Z坐标："));
        break;
    case PlaneType::YZ:
        centerXLabel->setText(QStringLiteral("平面偏移(X)："));
        centerYLabel->setText(QStringLiteral("中心Y坐标："));
        centerZLabel->setText(QStringLiteral("中心Z坐标："));
        break;
    }
}

void RectangleDialog::validateInput()
{
    bool ok = false;

    // ---- 长度 ----
    const double l = lengthEdit->text().toDouble(&ok);
    if (!ok || l <= 0.0) {
        QMessageBox::warning(this, QStringLiteral("输入错误"), QStringLiteral("长度必须为正数。"));
        return;
    }

    // ---- 宽度 ----
    const double w = widthEdit->text().toDouble(&ok);
    if (!ok || w <= 0.0) {
        QMessageBox::warning(this, QStringLiteral("输入错误"), QStringLiteral("宽度必须为正数。"));
        return;
    }

    // ---- 坐标 ----
    const double x = centerXEdit->text().toDouble(&ok);
    if (!ok) { QMessageBox::warning(this, "输入错误", "中心X坐标必须为有效数字。"); return; }

    const double y = centerYEdit->text().toDouble(&ok);
    if (!ok) { QMessageBox::warning(this, "输入错误", "中心Y坐标必须为有效数字。"); return; }

    const double z = centerZEdit->text().toDouble(&ok);
    if (!ok) { QMessageBox::warning(this, "输入错误", "中心Z坐标必须为有效数字。"); return; }

    length = l;
    width  = w;
    center = gp_Pnt(x, y, z);

    // ---- 基于基准面构造矩形：gp_Ax2(中心点, 法向) ----
    const PlaneType p = currentPlane();
    gp_Ax2 axis(center, planeNormal(p));

    // 平面内的两个方向：axis.XDirection / axis.YDirection
    const gp_Vec ux(axis.XDirection()); // “长度方向”
    const gp_Vec uy(axis.YDirection()); // “宽度方向”

    const double hl = 0.5 * length;
    const double hw = 0.5 * width;

    // ---- 四个角点（按中心点 ±ux*hl ±uy*hw）----
    const gp_Pnt p1 = center.Translated( ux * hl + uy * hw);
    const gp_Pnt p2 = center.Translated( ux * hl - uy * hw);
    const gp_Pnt p3 = center.Translated(-ux * hl - uy * hw);
    const gp_Pnt p4 = center.Translated(-ux * hl + uy * hw);

    // ---- OCC：4点 -> Wire -> Face ----
    TopoDS_Shape rectShape;
    try
    {
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(p1);
        poly.Add(p2);
        poly.Add(p3);
        poly.Add(p4);
        poly.Close();

        TopoDS_Wire wire = poly.Wire();

        // 生成矩形面（若只要线框：rectShape = wire）
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
        rectShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
    }
    catch (...)
    {
        QMessageBox::warning(this, QStringLiteral("生成失败"), QStringLiteral("创建矩形几何失败（OCC 异常）。"));
        return;
    }

    if (rectShape.IsNull()) {
        QMessageBox::warning(this, QStringLiteral("生成失败"), QStringLiteral("创建矩形几何失败（shape 为空）。"));
        return;
    }

    // ---- 按你 Circle 的方式：包装成 TopoDS_Shape*，塞进管理器 ----
    TopoDS_Shape* shape = new TopoDS_Shape(rectShape);

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("几何管理器未初始化。"));
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
        if (object->getSurfActor()) {
            vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face");
        }
        if (object->getEdgeActor()) {
            vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_edge");

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>()) {
        projectManagerService->setNodeDimmed(m_uuid, false);
    }
        }
    }
}
