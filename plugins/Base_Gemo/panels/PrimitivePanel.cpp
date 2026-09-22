#include "PrimitivePanel.h"
#include "ui_BoxDialog.h"
#include "ui_SphereDialog.h"
#include "ui_ConeDialog.h"
#include "ui_CylinderDialog.h"
#include "ui_TorusDialog.h"
#include "ui_CircleDialog.h"
#include "ui_EllipseDialog.h"
#include "ui_RectangleDialog.h"
#include "ui_PolygonDialog.h"
#include "GeometryPanelUi.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <cmath>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <gp_Ax2.hxx>
#include <gp_Elips.hxx>
#include <gp_Pnt.hxx>

#include <GeometryServiceAccess.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <ServiceRegistry.h>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

namespace
{

QJsonValue coordinateJsonValue(const QJsonObject& state, QChar axis)
{
    const QString canonicalKey(1, axis.toLower());
    if (state.contains(canonicalKey))
        return state.value(canonicalKey);

    // 兼容旧工程里使用大写 X/Y/Z 保存的坐标。
    return state.value(QString(1, axis.toUpper()));
}

void loadCoordinateField(const QJsonObject& state, QChar axis, QLineEdit* edit)
{
    if (!edit)
        return;

    const QJsonValue value = coordinateJsonValue(state, axis);
    if (!value.isUndefined())
        edit->setText(QString::number(value.toDouble()));
}

} // namespace




BoxDialog::BoxDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessBox), ui(new Ui::BoxDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->lengthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    ui->widthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    ui->heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    for (auto* edit : {ui->xEdit, ui->yEdit, ui->zEdit})
        edit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));
    connect(ui->generateButton, &QPushButton::clicked, this, &BoxDialog::onOkClicked);
    GeometryPanelUi::apply(this);
}

BoxDialog::~BoxDialog()
{
    delete ui;
}

QJsonObject BoxDialog::saveJson()
{
    QJsonObject state;
    state["length"] = ui->lengthEdit->text().toDouble();
    state["width"] = ui->widthEdit->text().toDouble();
    state["height"] = ui->heightEdit->text().toDouble();
    state["x"] = ui->xEdit->text().toDouble();
    state["y"] = ui->yEdit->text().toDouble();
    state["z"] = ui->zEdit->text().toDouble();
    return state;
}

void BoxDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("length"))
        ui->lengthEdit->setText(QString::number(state["length"].toDouble()));
    if (state.contains("width"))
        ui->widthEdit->setText(QString::number(state["width"].toDouble()));
    if (state.contains("height"))
        ui->heightEdit->setText(QString::number(state["height"].toDouble()));
    loadCoordinateField(state, QLatin1Char('x'), ui->xEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->yEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->zEdit);
}

gp_Pnt BoxDialog::getPosition() const
{
    return gp_Pnt(ui->xEdit->text().toDouble(), ui->yEdit->text().toDouble(), ui->zEdit->text().toDouble());
}

void BoxDialog::onOkClicked()
{
    bool ok;
    double length = ui->lengthEdit->text().toDouble(&ok);
    if (!ok || length <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("长度必须大于 0。"));
        return;
    }
    double width = ui->widthEdit->text().toDouble(&ok);
    if (!ok || width <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("宽度必须大于 0。"));
        return;
    }
    double height = ui->heightEdit->text().toDouble(&ok);
    if (!ok || height <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("高度必须大于 0。"));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    gp_Pnt position = getPosition();
    service->recreateBox(
        m_uuid, getDisplayName(), length, width, height,
        position.X(), position.Y(), position.Z());
}




SphereDialog::SphereDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessSphere), ui(new Ui::SphereDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    for (auto* edit : {ui->xEdit, ui->yEdit, ui->zEdit})
        edit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));
    connect(ui->generateButton, &QPushButton::clicked, this, &SphereDialog::onOkClicked);
    GeometryPanelUi::apply(this);
}

SphereDialog::~SphereDialog()
{
    delete ui;
}

QJsonObject SphereDialog::saveJson()
{
    QJsonObject state;
    state["radius"] = ui->radiusEdit->text().toDouble();
    state["x"] = ui->xEdit->text().toDouble();
    state["y"] = ui->yEdit->text().toDouble();
    state["z"] = ui->zEdit->text().toDouble();
    return state;
}

void SphereDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("radius"))
    {
        QString value = QString::number(state["radius"].toDouble());
        ui->radiusEdit->setText(value);
    }
    loadCoordinateField(state, QLatin1Char('x'), ui->xEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->yEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->zEdit);
}

void SphereDialog::onOkClicked()
{
    bool ok;
    double radius = ui->radiusEdit->text().toDouble(&ok);
    if (!ok || radius <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("半径必须大于 0。"));
        return;
    }
    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    gp_Pnt pos =
        gp_Pnt(ui->xEdit->text().toDouble(), ui->yEdit->text().toDouble(), ui->zEdit->text().toDouble());
    service->recreateSphere(m_uuid, getDisplayName(), radius, pos.X(), pos.Y(), pos.Z());
}



ConeDialog::ConeDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessCone), ui(new Ui::ConeDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->bottomRadiusEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this));
    ui->topRadiusEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this));
    ui->heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    for (auto* edit : {ui->xEdit, ui->yEdit, ui->zEdit})
        edit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));
    connect(ui->generateButton, &QPushButton::clicked, this, &ConeDialog::onOkClicked);
    GeometryPanelUi::apply(this);
}

ConeDialog::~ConeDialog()
{
    delete ui;
}

QJsonObject ConeDialog::saveJson()
{
    QJsonObject state;
    state["bottomRadius"] = getBottomRadius();
    state["topRadius"] = getTopRadius();
    state["height"] = getHeight();
    state["x"] = ui->xEdit->text().toDouble();
    state["y"] = ui->yEdit->text().toDouble();
    state["z"] = ui->zEdit->text().toDouble();
    return state;
}

void ConeDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("bottomRadius"))
    {
        QString value = QString::number(state["bottomRadius"].toDouble());
        ui->bottomRadiusEdit->setText(value);
    }
    if (state.contains("topRadius"))
    {
        QString value = QString::number(state["topRadius"].toDouble());
        ui->topRadiusEdit->setText(value);
    }
    if (state.contains("height"))
    {
        QString value = QString::number(state["height"].toDouble());
        ui->heightEdit->setText(value);
    }
    loadCoordinateField(state, QLatin1Char('x'), ui->xEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->yEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->zEdit);
}

double ConeDialog::getBottomRadius() const
{
    return ui->bottomRadiusEdit->text().toDouble();
}

double ConeDialog::getTopRadius() const
{
    return ui->topRadiusEdit->text().toDouble();
}

double ConeDialog::getHeight() const
{
    return ui->heightEdit->text().toDouble();
}

gp_Pnt ConeDialog::getBaseCenter() const
{
    return gp_Pnt(ui->xEdit->text().toDouble(), ui->yEdit->text().toDouble(), ui->zEdit->text().toDouble());
}

void ConeDialog::onOkClicked()
{
    bool ok;
    double bottomRadius = ui->bottomRadiusEdit->text().toDouble(&ok);
    if (!ok || bottomRadius < 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("底半径不能小于 0。"));
        return;
    }
    double topRadius = ui->topRadiusEdit->text().toDouble(&ok);
    if (!ok || topRadius < 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("顶半径不能小于 0。"));
        return;
    }
    if (bottomRadius <= 0 && topRadius <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("底半径和顶半径不能同时为 0。"));
        return;
    }
    double height = ui->heightEdit->text().toDouble(&ok);
    if (!ok || height <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("高度必须大于 0。"));
        return;
    }

    gp_Pnt baseCenter = getBaseCenter();
    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    service->recreateCone(m_uuid, getDisplayName(), bottomRadius, topRadius, height, baseCenter.X(), baseCenter.Y(), baseCenter.Z());
}

void ConeDialog::onCancelClicked()
{
}

CylinderDialog::CylinderDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessCylinder), ui(new Ui::CylinderDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    ui->heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    for (auto* edit : {ui->xEdit, ui->yEdit, ui->zEdit})
        edit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));
    connect(ui->generateButton, &QPushButton::clicked, this, &CylinderDialog::onOkClicked);
    GeometryPanelUi::apply(this);
}

CylinderDialog::~CylinderDialog()
{
    delete ui;
}

QJsonObject CylinderDialog::saveJson()
{
    QJsonObject state;
    state["radius"] = getRadius();
    state["height"] = getHeight();
    state["x"] = ui->xEdit->text().toDouble();
    state["y"] = ui->yEdit->text().toDouble();
    state["z"] = ui->zEdit->text().toDouble();
    return state;
}

void CylinderDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("radius"))
    {
        QString value = QString::number(state["radius"].toDouble());
        ui->radiusEdit->setText(value);
    }
    if (state.contains("height"))
    {
        QString value = QString::number(state["height"].toDouble());
        ui->heightEdit->setText(value);
    }
    loadCoordinateField(state, QLatin1Char('x'), ui->xEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->yEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->zEdit);
}

double CylinderDialog::getRadius() const
{
    return ui->radiusEdit->text().toDouble();
}

double CylinderDialog::getHeight() const
{
    return ui->heightEdit->text().toDouble();
}

gp_Pnt CylinderDialog::getBaseCenter() const
{
    return gp_Pnt(ui->xEdit->text().toDouble(), ui->yEdit->text().toDouble(), ui->zEdit->text().toDouble());
}

void CylinderDialog::onOkClicked()
{
    bool ok;
    double radius = ui->radiusEdit->text().toDouble(&ok);
    if (!ok || radius <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("半径必须大于 0。"));
        return;
    }
    double height = ui->heightEdit->text().toDouble(&ok);
    if (!ok || height <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("高度必须大于 0。"));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    gp_Pnt baseCenter = getBaseCenter();
    service->recreateCylinder(m_uuid, getDisplayName(), radius, height, baseCenter.X(), baseCenter.Y(), baseCenter.Z());
}

void CylinderDialog::onCancelClicked()
{
}



TorusDialog::TorusDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessTorus), ui(new Ui::TorusDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->majorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    ui->minorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));
    for (auto* edit : {ui->xEdit, ui->yEdit, ui->zEdit})
        edit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));
    connect(ui->generateButton, &QPushButton::clicked, this, &TorusDialog::onOkClicked);
    GeometryPanelUi::apply(this);
}

TorusDialog::~TorusDialog()
{
    delete ui;
}

QJsonObject TorusDialog::saveJson()
{
    QJsonObject state;
    state["majorRadius"] = getMajorRadius();
    state["minorRadius"] = getMinorRadius();
    state["x"] = ui->xEdit->text().toDouble();
    state["y"] = ui->yEdit->text().toDouble();
    state["z"] = ui->zEdit->text().toDouble();
    return state;
}

void TorusDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("majorRadius"))
    {
        QString value = QString::number(state["majorRadius"].toDouble());
        ui->majorRadiusEdit->setText(value);
    }
    if (state.contains("minorRadius"))
    {
        QString value = QString::number(state["minorRadius"].toDouble());
        ui->minorRadiusEdit->setText(value);
    }
    loadCoordinateField(state, QLatin1Char('x'), ui->xEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->yEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->zEdit);
}

double TorusDialog::getMajorRadius() const
{
    return ui->majorRadiusEdit->text().toDouble();
}

double TorusDialog::getMinorRadius() const
{
    return ui->minorRadiusEdit->text().toDouble();
}

gp_Pnt TorusDialog::getCenter() const
{
    return gp_Pnt(ui->xEdit->text().toDouble(), ui->yEdit->text().toDouble(), ui->zEdit->text().toDouble());
}

void TorusDialog::onOkClicked()
{
    bool ok;
    double majorRadius = ui->majorRadiusEdit->text().toDouble(&ok);
    if (!ok || majorRadius <= 0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("主半径必须大于 0。"));
        return;
    }
    double minorRadius = ui->minorRadiusEdit->text().toDouble(&ok);
    if (!ok || minorRadius <= 0 || minorRadius >= majorRadius)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("管半径必须大于 0，且小于主半径。"));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    gp_Pnt center = getCenter();
    service->recreateTorus(m_uuid, getDisplayName(), majorRadius, minorRadius, center.X(), center.Y(), center.Z());
}

// ===== OCC =====


//  �1�5     �9�8    �0�8      
// #include <GeomShapeManager.h>  //       �0�7�0�6  �0�5 �0�11 7?
// #include <GeomShapeObject.h>

CircleDialog::CircleDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessCircle), ui(new Ui::CircleDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->planeCombo->setItemData(0, static_cast<int>(PlaneType::XY));
    ui->planeCombo->setItemData(1, static_cast<int>(PlaneType::XZ));
    ui->planeCombo->setItemData(2, static_cast<int>(PlaneType::YZ));
    ui->radiusEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    for (auto* edit : {ui->centerXEdit, ui->centerYEdit, ui->centerZEdit})
        edit->setValidator(new QDoubleValidator(-1.0e6, 1.0e6, 6, this));
    connect(ui->planeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &CircleDialog::onPlaneChanged);
    connect(ui->generateButton, &QPushButton::clicked, this, &CircleDialog::validateInput);
    updatePlaneUi();
    GeometryPanelUi::apply(this);
}

CircleDialog::~CircleDialog()
{
    delete ui;
}

double CircleDialog::getRadius() const
{
    bool ok = false;
    const double r = ui->radiusEdit->text().toDouble(&ok);
    return ok ? r : 0.0;
}

gp_Pnt CircleDialog::getCenter() const
{
    return center;
}

QJsonObject CircleDialog::saveJson()
{
    QJsonObject state;
    state["plane"] = ui->planeCombo->currentData().toInt();
    state["radius"] = getRadius();
    state["x"] = ui->centerXEdit->text().toDouble();
    state["y"] = ui->centerYEdit->text().toDouble();
    state["z"] = ui->centerZEdit->text().toDouble();
    return state;
}

void CircleDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane"))
    {
        const int p = state["plane"].toInt();
        const int idx = ui->planeCombo->findData(p);
        if (idx >= 0)
            ui->planeCombo->setCurrentIndex(idx);
    }

    if (state.contains("radius"))
        ui->radiusEdit->setText(QString::number(state["radius"].toDouble()));

    loadCoordinateField(state, QLatin1Char('x'), ui->centerXEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->centerYEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->centerZEdit);

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
    return static_cast<PlaneType>(ui->planeCombo->currentData().toInt());
}

void CircleDialog::updatePlaneUi()
{
    //   �0�6 �΁0�4   �0�0   �0�9       �6�9 �0�0   �0�1 �0�3      �9�5  1 7?
    switch (currentPlane())
    {
    case PlaneType::XY:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::XZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::YZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    }
}

gp_Dir CircleDialog::planeNormal(PlaneType p) const
{
    // 2D �0�4    �0�9  �0�9   
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

    // ----  �1�1 ----
    const double r = ui->radiusEdit->text().toDouble(&ok);
    if (!ok || r <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("半径必须大于 0。"));
        return;
    }

    // ----      ----
    const double x = ui->centerXEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 X 无效。"));
        return;
    }

    const double y = ui->centerYEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Y 无效。"));
        return;
    }

    const double z = ui->centerZEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Z 无效。"));
        return;
    }

    radius = r;
    center = gp_Pnt(x, y, z);

    // ----    �1�7 �0�6 ��  �0�4  gp_Ax2(   �0�11 7?     ) ----
    const PlaneType p = currentPlane();
    gp_Ax2 axis(center, planeNormal(p));

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    service->recreateCircle(m_uuid, getDisplayName(), radius, center.X(), center.Y(), center.Z(), static_cast<int>(p));
}



// ===== OCC =====


EllipseDialog::EllipseDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessEllipse), ui(new Ui::EllipseDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->planeCombo->setItemData(0, static_cast<int>(PlaneType::XY));
    ui->planeCombo->setItemData(1, static_cast<int>(PlaneType::XZ));
    ui->planeCombo->setItemData(2, static_cast<int>(PlaneType::YZ));
    ui->majorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    ui->minorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    for (auto* edit : {ui->centerXEdit, ui->centerYEdit, ui->centerZEdit})
        edit->setValidator(new QDoubleValidator(-1.0e6, 1.0e6, 6, this));
    connect(ui->planeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &EllipseDialog::onPlaneChanged);
    connect(ui->generateButton, &QPushButton::clicked, this, &EllipseDialog::validateInput);
    updatePlaneUi();
    GeometryPanelUi::apply(this);
}

EllipseDialog::~EllipseDialog()
{
    delete ui;
}

double EllipseDialog::getMajorRadius() const
{
    return majorRadius;
}
double EllipseDialog::getMinorRadius() const
{
    return minorRadius;
}
gp_Pnt EllipseDialog::getCenter() const
{
    return center;
}

QJsonObject EllipseDialog::saveJson()
{
    QJsonObject state;
    state["plane"] = ui->planeCombo->currentData().toInt();
    state["majorRadius"] = ui->majorRadiusEdit->text().toDouble();
    state["minorRadius"] = ui->minorRadiusEdit->text().toDouble();
    state["x"] = ui->centerXEdit->text().toDouble();
    state["y"] = ui->centerYEdit->text().toDouble();
    state["z"] = ui->centerZEdit->text().toDouble();
    return state;
}

void EllipseDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane"))
    {
        const int p = state["plane"].toInt();
        const int idx = ui->planeCombo->findData(p);
        if (idx >= 0)
            ui->planeCombo->setCurrentIndex(idx);
    }

    if (state.contains("majorRadius"))
        ui->majorRadiusEdit->setText(QString::number(state["majorRadius"].toDouble()));

    if (state.contains("minorRadius"))
        ui->minorRadiusEdit->setText(QString::number(state["minorRadius"].toDouble()));

    loadCoordinateField(state, QLatin1Char('x'), ui->centerXEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->centerYEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->centerZEdit);

    updatePlaneUi();
}

void EllipseDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

EllipseDialog::PlaneType EllipseDialog::currentPlane() const
{
    return static_cast<PlaneType>(ui->planeCombo->currentData().toInt());
}

gp_Dir EllipseDialog::planeNormal(PlaneType p) const
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

void EllipseDialog::updatePlaneUi()
{
    //  1 7?Circle/Rectangle    1 7 �0�5   �0�5 �0�0      �0�61 7 1 7�0�9  �0�1 �0�21 7 1 7?
    switch (currentPlane())
    {
    case PlaneType::XY:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::XZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::YZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    }
}

void EllipseDialog::validateInput()
{
    bool ok = false;

    // ----     �1�1 ----
    const double majorR = ui->majorRadiusEdit->text().toDouble(&ok);
    if (!ok || majorR <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("主半径必须大于 0。"));
        return;
    }

    // ----     �1�1 ----
    const double minorR = ui->minorRadiusEdit->text().toDouble(&ok);
    if (!ok || minorR <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("管半径必须大于 0。"));
        return;
    }

    if (minorR > majorR)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("管半径不能大于主半径。"));
        return;
    }

    // ----      ----
    const double x = ui->centerXEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 X 无效。"));
        return;
    }

    const double y = ui->centerYEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Y 无效。"));
        return;
    }

    const double z = ui->centerZEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Z 无效。"));
        return;
    }

    majorRadius = majorR;
    minorRadius = minorR;
    center = gp_Pnt(x, y, z);

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    service->recreateEllipse(m_uuid, getDisplayName(), majorRadius, minorRadius, center.X(), center.Y(), center.Z(), static_cast<int>(currentPlane()));
}



// ===== OCC =====


// =====      �0�7 �1�4     CircleDialog  �0�1�0�11 7?====

RectangleDialog::RectangleDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessRectangle), ui(new Ui::RectangleDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->planeCombo->setItemData(0, static_cast<int>(PlaneType::XY));
    ui->planeCombo->setItemData(1, static_cast<int>(PlaneType::XZ));
    ui->planeCombo->setItemData(2, static_cast<int>(PlaneType::YZ));
    ui->lengthEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    ui->widthEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    for (auto* edit : {ui->centerXEdit, ui->centerYEdit, ui->centerZEdit})
        edit->setValidator(new QDoubleValidator(-1.0e6, 1.0e6, 6, this));
    connect(ui->planeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RectangleDialog::onPlaneChanged);
    connect(ui->generateButton, &QPushButton::clicked, this, &RectangleDialog::validateInput);
    updatePlaneUi();
    GeometryPanelUi::apply(this);
}

RectangleDialog::~RectangleDialog()
{
    delete ui;
}

double RectangleDialog::getLength() const
{
    return length;
}
double RectangleDialog::getWidth() const
{
    return width;
}
gp_Pnt RectangleDialog::getCenter() const
{
    return center;
}

QJsonObject RectangleDialog::saveJson()
{
    QJsonObject state;
    state["plane"] = ui->planeCombo->currentData().toInt();
    state["length"] = ui->lengthEdit->text().toDouble();
    state["width"] = ui->widthEdit->text().toDouble();
    state["x"] = ui->centerXEdit->text().toDouble();
    state["y"] = ui->centerYEdit->text().toDouble();
    state["z"] = ui->centerZEdit->text().toDouble();
    return state;
}

void RectangleDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane"))
    {
        const int p = state["plane"].toInt();
        const int idx = ui->planeCombo->findData(p);
        if (idx >= 0)
            ui->planeCombo->setCurrentIndex(idx);
    }

    if (state.contains("length"))
        ui->lengthEdit->setText(QString::number(state["length"].toDouble()));

    if (state.contains("width"))
        ui->widthEdit->setText(QString::number(state["width"].toDouble()));

    loadCoordinateField(state, QLatin1Char('x'), ui->centerXEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->centerYEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->centerZEdit);

    updatePlaneUi();
}

void RectangleDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

RectangleDialog::PlaneType RectangleDialog::currentPlane() const
{
    return static_cast<PlaneType>(ui->planeCombo->currentData().toInt());
}

gp_Dir RectangleDialog::planeNormal(PlaneType p) const
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

void RectangleDialog::updatePlaneUi()
{
    //  1 7?Circle �0�4 1 7 1 7       �0�4 �0�4   �0�0  �0�61 7 1 7�0�9  �0�1 �0�21 7 1 7?
    switch (currentPlane())
    {
    case PlaneType::XY:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::XZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::YZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    }
}

void RectangleDialog::validateInput()
{
    bool ok = false;

    // ----      ----
    const double l = ui->lengthEdit->text().toDouble(&ok);
    if (!ok || l <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("长度必须大于 0。"));
        return;
    }

    // ----      ----
    const double w = ui->widthEdit->text().toDouble(&ok);
    if (!ok || w <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("宽度必须大于 0。"));
        return;
    }

    // ----      ----
    const double x = ui->centerXEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 X 无效。"));
        return;
    }

    const double y = ui->centerYEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Y 无效。"));
        return;
    }

    const double z = ui->centerZEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Z 无效。"));
        return;
    }

    length = l;
    width = w;
    center = gp_Pnt(x, y, z);

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    service->recreateRectangle(m_uuid, getDisplayName(), length, width, center.X(), center.Y(), center.Z(), static_cast<int>(currentPlane()));
}



// ===== OCC =====


PolygonDialog::PolygonDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessPolygon), ui(new Ui::PolygonDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->planeCombo->setItemData(0, static_cast<int>(PlaneType::XY));
    ui->planeCombo->setItemData(1, static_cast<int>(PlaneType::XZ));
    ui->planeCombo->setItemData(2, static_cast<int>(PlaneType::YZ));
    ui->radiusEdit->setValidator(new QDoubleValidator(0.0, 1.0e6, 6, this));
    ui->sidesEdit->setValidator(new QIntValidator(3, 100, this));
    ui->rotateDegEdit->setValidator(new QDoubleValidator(-360.0, 360.0, 6, this));
    for (auto* edit : {ui->centerXEdit, ui->centerYEdit, ui->centerZEdit})
        edit->setValidator(new QDoubleValidator(-1.0e6, 1.0e6, 6, this));
    connect(ui->planeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PolygonDialog::onPlaneChanged);
    connect(ui->generateButton, &QPushButton::clicked, this, &PolygonDialog::validateInput);
    updatePlaneUi();
    GeometryPanelUi::apply(this);
}

PolygonDialog::~PolygonDialog()
{
    delete ui;
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
    state["plane"] = ui->planeCombo->currentData().toInt();
    state["radius"] = ui->radiusEdit->text().toDouble();
    state["sides"] = ui->sidesEdit->text().toInt();
    state["rotateDeg"] = ui->rotateDegEdit->text().toDouble();
    state["x"] = ui->centerXEdit->text().toDouble();
    state["y"] = ui->centerYEdit->text().toDouble();
    state["z"] = ui->centerZEdit->text().toDouble();
    return state;
}

void PolygonDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("plane"))
    {
        const int p = state["plane"].toInt();
        const int idx = ui->planeCombo->findData(p);
        if (idx >= 0)
            ui->planeCombo->setCurrentIndex(idx);
    }
    if (state.contains("radius"))
        ui->radiusEdit->setText(QString::number(state["radius"].toDouble()));
    if (state.contains("sides"))
        ui->sidesEdit->setText(QString::number(state["sides"].toInt()));
    if (state.contains("rotateDeg"))
        ui->rotateDegEdit->setText(QString::number(state["rotateDeg"].toDouble()));
    loadCoordinateField(state, QLatin1Char('x'), ui->centerXEdit);
    loadCoordinateField(state, QLatin1Char('y'), ui->centerYEdit);
    loadCoordinateField(state, QLatin1Char('z'), ui->centerZEdit);

    updatePlaneUi();
}

void PolygonDialog::onPlaneChanged(int)
{
    updatePlaneUi();
}

PolygonDialog::PlaneType PolygonDialog::currentPlane() const
{
    return static_cast<PlaneType>(ui->planeCombo->currentData().toInt());
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
    //   �0�5 �0�0      �0�61 7 1 7�0�9  �0�1 �0�21 7 1 7?
    switch (currentPlane())
    {
    case PlaneType::XY:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::XZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    case PlaneType::YZ:
        ui->centerXLabel->setText(tr("中心 X:"));
        ui->centerYLabel->setText(tr("中心 Y:"));
        ui->centerZLabel->setText(tr("中心 Z:"));
        break;
    }
}

void PolygonDialog::validateInput()
{
    bool ok = false;

    const double r = ui->radiusEdit->text().toDouble(&ok);
    if (!ok || r <= 0.0)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("外接圆半径必须大于 0。"));
        return;
    }

    const int s = ui->sidesEdit->text().toInt(&ok);
    if (!ok || s < 3 || s > 100)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("边数范围为 3 到 100。"));
        return;
    }

    const double rotDeg = ui->rotateDegEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("旋转角无效。"));
        return;
    }

    const double x = ui->centerXEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 X 无效。"));
        return;
    }

    const double y = ui->centerYEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Y 无效。"));
        return;
    }

    const double z = ui->centerZEdit->text().toDouble(&ok);
    if (!ok)
    {
        QMessageBox::warning(this, tr("输入错误"), tr("中心 Z 无效。"));
        return;
    }

    radius = r;
    sides = s;
    center = gp_Pnt(x, y, z);

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    service->recreatePolygon(m_uuid, getDisplayName(), radius, sides, rotDeg, center.X(), center.Y(), center.Z(), static_cast<int>(currentPlane()));
}
