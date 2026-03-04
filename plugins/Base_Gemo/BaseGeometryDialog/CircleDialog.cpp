#include "circledialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/VisualWidgetService.h"

// ===== OCC =====
#include <GeomShapeObject.h>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_Circle.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>

// 你工程里这几个类应当已有
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
// #include <GeomShapeManager.h>  // 视你项目实际头文件
// #include <GeomShapeObject.h>

CircleDialog::CircleDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessCircle)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    auto* pageNameLabel = new QLabel(QString("几何模块"), this);
    pageNameLabel->setObjectName("pageNameLabel");
    auto* sectionTitleButton = new QPushButton(QString("参数设置"), this);
    sectionTitleButton->setObjectName("sectionTitleButton");
    sectionTitleButton->setEnabled(false);
    layout->addWidget(pageNameLabel);
    layout->addWidget(sectionTitleButton);

    // ---- 基准面选择（2D 圆必须有）----
    planeLabel = new QLabel(QString("基准面："), this);
    planeCombo = new QComboBox(this);
    planeCombo->addItem("XY", static_cast<int>(PlaneType::XY));
    planeCombo->addItem("XZ", static_cast<int>(PlaneType::XZ));
    planeCombo->addItem("YZ", static_cast<int>(PlaneType::YZ));
    layout->addWidget(planeLabel);
    layout->addWidget(planeCombo);

    connect(planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CircleDialog::onPlaneChanged);

    // ---- 半径输入 ----
    QLabel* radiusLabel = new QLabel(QString("半径："), this);
    radiusEdit = new QLineEdit(this);
    radiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 正数
    layout->addWidget(radiusLabel);
    layout->addWidget(radiusEdit);

    // ---- 中心点输入（X/Y/Z 全保留，基准面不同语义不同）----
    centerXLabel = new QLabel(QString("中心X坐标："), this);
    centerXEdit = new QLineEdit(this);
    centerXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerXLabel);
    layout->addWidget(centerXEdit);

    centerYLabel = new QLabel(QString("中心Y坐标："), this);
    centerYEdit = new QLineEdit(this);
    centerYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerYLabel);
    layout->addWidget(centerYEdit);

    centerZLabel = new QLabel(QString("中心Z坐标："), this);
    centerZEdit = new QLineEdit(this);
    centerZEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
    layout->addWidget(centerZLabel);
    layout->addWidget(centerZEdit);

    // ---- 按钮 ----
    QPushButton* okButton = new QPushButton(QString("生成几何体"), this);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    layout->addLayout(buttonLayout);
    layout->addStretch();

    connect(okButton, &QPushButton::clicked, this, &CircleDialog::validateInput);

    // ---- 默认值 ----
    planeCombo->setCurrentIndex(0); // XY
    radiusEdit->setText("1.0");
    centerXEdit->setText("0.0");
    centerYEdit->setText("0.0");
    centerZEdit->setText("0.0");
    updatePlaneUi();
}

double CircleDialog::getRadius() const
{
    bool ok = false;
    const double r = radiusEdit->text().toDouble(&ok);
    return ok ? r : 0.0;
}

gp_Pnt CircleDialog::getCenter() const
{
    return center;
}

QJsonObject CircleDialog::saveJson()
{
    QJsonObject state;
    state["plane"] = planeCombo->currentData().toInt();
    state["radius"] = getRadius();
    state["X"] = centerXEdit->text().toDouble();
    state["Y"] = centerYEdit->text().toDouble();
    state["Z"] = centerZEdit->text().toDouble();
    return state;
}

void CircleDialog::loadJson(const QJsonObject& state)
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

    if (state.contains("X"))
        centerXEdit->setText(QString::number(state["X"].toDouble()));

    if (state.contains("Y"))
        centerYEdit->setText(QString::number(state["Y"].toDouble()));

    if (state.contains("Z"))
        centerZEdit->setText(QString::number(state["Z"].toDouble()));

    updatePlaneUi();
}

void CircleDialog::beforeLoad()
{
}

void CircleDialog::beforeUnload()
{
}

void CircleDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

CircleDialog::PlaneType CircleDialog::currentPlane() const
{
    return static_cast<PlaneType>(planeCombo->currentData().toInt());
}

void CircleDialog::updatePlaneUi()
{
    // 基准面不同：哪个是平面内坐标、哪个是偏移（法向方向）会变化
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

gp_Dir CircleDialog::planeNormal(PlaneType p) const
{
    // 2D 圆所在平面的法向
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

void CircleDialog::validateInput()
{
    bool ok = false;

    // ---- 半径 ----
    const double r = radiusEdit->text().toDouble(&ok);
    if (!ok || r <= 0.0)
    {
        QMessageBox::warning(this, QString("输入错误"), QString("半径必须为正数。"));
        return;
    }

    // ---- 坐标 ----
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
    center = gp_Pnt(x, y, z);

    // ---- 基于基准面构造圆：gp_Ax2(中心点, 法向) ----
    const PlaneType p = currentPlane();
    gp_Ax2 axis(center, planeNormal(p));

    // ---- OCC：圆 -> Edge -> Wire -> Face ----
    TopoDS_Shape circleShape;

    GeometryCleanup::removeExistingGeometry(m_uuid);

    Handle(Geom_Circle) gc = new Geom_Circle(axis, radius);
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(gc);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);

    // 生成一个圆盘面（如果你只想要线框，可直接 shape = wire 或 edge）
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
    circleShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);

    TopoDS_Shape* shape = new TopoDS_Shape(circleShape);
    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
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
