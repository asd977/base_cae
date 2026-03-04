#include "ellipsedialog.h"

#include <PublicConstants.h>

#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

// ===== OCC =====
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_Ellipse.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Elips.hxx>

EllipseDialog::EllipseDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessEllipse) {
  QVBoxLayout* layout = new QVBoxLayout(this);
  auto* pageNameLabel = new QLabel(QString("几何模块"), this);
  pageNameLabel->setObjectName("pageNameLabel");
  auto* sectionTitleButton = new QPushButton(QString("参数设置"), this);
  sectionTitleButton->setObjectName("sectionTitleButton");
  sectionTitleButton->setEnabled(false);
  layout->addWidget(pageNameLabel);
  layout->addWidget(sectionTitleButton);

  // ---- 基准面选择 ----
  planeLabel = new QLabel(QString("基准面："), this);
  planeCombo = new QComboBox(this);
  planeCombo->addItem("XY", static_cast<int>(PlaneType::XY));
  planeCombo->addItem("XZ", static_cast<int>(PlaneType::XZ));
  planeCombo->addItem("YZ", static_cast<int>(PlaneType::YZ));
  layout->addWidget(planeLabel);
  layout->addWidget(planeCombo);

  connect(planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &EllipseDialog::onPlaneChanged);

  // ---- 长轴半径 ----
  QLabel* majorRadiusLabel = new QLabel(QString("长轴半径："), this);
  majorRadiusEdit = new QLineEdit(this);
  majorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this));
  layout->addWidget(majorRadiusLabel);
  layout->addWidget(majorRadiusEdit);

  // ---- 短轴半径 ----
  QLabel* minorRadiusLabel = new QLabel(QString("短轴半径："), this);
  minorRadiusEdit = new QLineEdit(this);
  minorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this));
  layout->addWidget(minorRadiusLabel);
  layout->addWidget(minorRadiusEdit);

  // ---- 中心点（X/Y/Z，全保留；基准面决定哪个是偏移）----
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

  // ---- 创建按钮 ----
  QPushButton* okButton = new QPushButton(QString("生成几何体"), this);

  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);
  layout->addLayout(buttonLayout);
  layout->addStretch();

  connect(okButton, &QPushButton::clicked, this, &EllipseDialog::validateInput);

  // ---- 默认值 ----
  planeCombo->setCurrentIndex(0);  // XY
  majorRadiusEdit->setText("2.0");
  minorRadiusEdit->setText("1.0");
  centerXEdit->setText("0.0");
  centerYEdit->setText("0.0");
  centerZEdit->setText("0.0");

  updatePlaneUi();
}

double EllipseDialog::getMajorRadius() const { return majorRadius; }
double EllipseDialog::getMinorRadius() const { return minorRadius; }
gp_Pnt EllipseDialog::getCenter() const { return center; }

QJsonObject EllipseDialog::saveJson() {
  QJsonObject state;
  state["plane"] = planeCombo->currentData().toInt();
  state["majorRadius"] = majorRadiusEdit->text().toDouble();
  state["minorRadius"] = minorRadiusEdit->text().toDouble();
  state["X"] = centerXEdit->text().toDouble();
  state["Y"] = centerYEdit->text().toDouble();
  state["Z"] = centerZEdit->text().toDouble();
  return state;
}

void EllipseDialog::loadJson(const QJsonObject& state) {
  if (state.contains("plane")) {
    const int p = state["plane"].toInt();
    const int idx = planeCombo->findData(p);
    if (idx >= 0) planeCombo->setCurrentIndex(idx);
  }

  if (state.contains("majorRadius"))
    majorRadiusEdit->setText(QString::number(state["majorRadius"].toDouble()));

  if (state.contains("minorRadius"))
    minorRadiusEdit->setText(QString::number(state["minorRadius"].toDouble()));

  if (state.contains("X"))
    centerXEdit->setText(QString::number(state["X"].toDouble()));

  if (state.contains("Y"))
    centerYEdit->setText(QString::number(state["Y"].toDouble()));

  if (state.contains("Z"))
    centerZEdit->setText(QString::number(state["Z"].toDouble()));

  updatePlaneUi();
}

void EllipseDialog::onPlaneChanged(int) { updatePlaneUi(); }

EllipseDialog::PlaneType EllipseDialog::currentPlane() const {
  return static_cast<PlaneType>(planeCombo->currentData().toInt());
}

gp_Dir EllipseDialog::planeNormal(PlaneType p) const {
  switch (p) {
    case PlaneType::XY:
      return gp_Dir(0, 0, 1);
    case PlaneType::XZ:
      return gp_Dir(0, 1, 0);
    case PlaneType::YZ:
      return gp_Dir(1, 0, 0);
  }
  return gp_Dir(0, 0, 1);
}

void EllipseDialog::updatePlaneUi() {
  // 和 Circle/Rectangle 一致：提示哪个坐标是“平面偏移”
  switch (currentPlane()) {
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

void EllipseDialog::validateInput() {
  bool ok = false;

  // ---- 长轴半径 ----
  const double majorR = majorRadiusEdit->text().toDouble(&ok);
  if (!ok || majorR <= 0.0) {
    QMessageBox::warning(this, "输入错误", "长轴半径必须为正数。");
    return;
  }

  // ---- 短轴半径 ----
  const double minorR = minorRadiusEdit->text().toDouble(&ok);
  if (!ok || minorR <= 0.0) {
    QMessageBox::warning(this, "输入错误", "短轴半径必须为正数。");
    return;
  }

  if (minorR > majorR) {
    QMessageBox::warning(this, "输入错误", "短轴半径不能大于长轴半径。");
    return;
  }

  // ---- 坐标 ----
  const double x = centerXEdit->text().toDouble(&ok);
  if (!ok) {
    QMessageBox::warning(this, "输入错误", "中心X坐标必须为有效数字。");
    return;
  }

  const double y = centerYEdit->text().toDouble(&ok);
  if (!ok) {
    QMessageBox::warning(this, "输入错误", "中心Y坐标必须为有效数字。");
    return;
  }

  const double z = centerZEdit->text().toDouble(&ok);
  if (!ok) {
    QMessageBox::warning(this, "输入错误", "中心Z坐标必须为有效数字。");
    return;
  }

  majorRadius = majorR;
  minorRadius = minorR;
  center = gp_Pnt(x, y, z);

  // ---- 基于基准面构造椭圆：gp_Ax2(中心点, 法向) ----
  const PlaneType p = currentPlane();
  gp_Ax2 axis(center, planeNormal(p));

  // ---- OCC：椭圆 -> Edge -> Wire -> Face ----
  TopoDS_Shape ellipseShape;
  try {
    gp_Elips el(axis, majorRadius, minorRadius);
    Handle(Geom_Ellipse) ge = new Geom_Ellipse(el);

    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(ge);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);

    // 生成一个椭圆面（若只要线框：ellipseShape = wire）
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
    ellipseShape = face.IsNull() ? TopoDS_Shape(wire) : TopoDS_Shape(face);
  } catch (...) {
    QMessageBox::warning(this, QString("生成失败"),
                         QString("创建椭圆几何失败（OCC 异常）。"));
    return;
  }

  if (ellipseShape.IsNull()) {
    QMessageBox::warning(this, QString("生成失败"),
                         QString("创建椭圆几何失败（shape 为空）。"));
    return;
  }

  // ---- 按你 Circle 的方式写入几何管理器 ----
  TopoDS_Shape* shape = new TopoDS_Shape(ellipseShape);

  GeomManager::GeomManagerInstance* mgr =
      GeomManager::GeomManagerInstance::getInstance();
  if (!mgr) {
    QMessageBox::warning(this, QString("错误"),
                         QString("几何管理器未初始化。"));
    delete shape;
    return;
  }

  GeometryCleanup::removeExistingGeometry(m_uuid);

  GeomShapeObject* object =
      new GeomShapeObject(m_uuid, shape, getDisplayName());
  mgr->appendGeomShapeObject(object);

  // ---- 显示注册（面/边分开加）----
  auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
  if (vis) {
    if (object->getSurfActor()) {
      vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry",
                   m_uuid + "_face");
    }
    if (object->getEdgeActor()) {
      vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry",
                   m_uuid + "_edge");

      if (auto* projectManagerService =
              ServiceRegistry::instance().getService<ProjectManagerService>()) {
        projectManagerService->setNodeDimmed(m_uuid, false);
      }
    }
  }
}
