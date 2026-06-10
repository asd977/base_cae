#include "cylinderdialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <QDoubleValidator>
#include <QMessageBox>

CylinderDialog::CylinderDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessCylinder)
{
    // 创建输入框和标签
    QLabel* radiusLabel = new QLabel("半径：", this);
    radiusEdit = new QLineEdit(this);
    radiusEdit->setMinimumWidth(10);
    radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* heightLabel = new QLabel("高度：", this);
    heightEdit = new QLineEdit(this);
    heightEdit->setMinimumWidth(10);
    heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* xLabel = new QLabel("底面中心 X：", this);
    xEdit = new QLineEdit(this);
    xEdit->setMinimumWidth(10);
    xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* yLabel = new QLabel("底面中心 Y：", this);
    yEdit = new QLineEdit(this);
    yEdit->setMinimumWidth(10);
    yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* zLabel = new QLabel("底面中心 Z：", this);
    zEdit = new QLineEdit(this);
    zEdit->setMinimumWidth(10);
    zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    // 设置默认值
    radiusEdit->setText("20.0");
    heightEdit->setText("60.0");
    xEdit->setText("0");
    yEdit->setText("0");
    zEdit->setText("0");

    // 创建按钮
    QPushButton* okButton = new QPushButton("生成几何体", this);

    // 布局
    QVBoxLayout* mainLayout = new QVBoxLayout;
    auto* pageHeaderLabel = new QLabel(QString("参数设置"), this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");
    mainLayout->addWidget(pageHeaderLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);
    contentLayout->setSpacing(6);
    mainLayout->addLayout(contentLayout);
    QHBoxLayout* radiusLayout = new QHBoxLayout;
    QHBoxLayout* heightLayout = new QHBoxLayout;
    QHBoxLayout* xLayout = new QHBoxLayout;
    QHBoxLayout* yLayout = new QHBoxLayout;
    QHBoxLayout* zLayout = new QHBoxLayout;
    QHBoxLayout* buttonLayout = new QHBoxLayout;

    radiusLayout->addWidget(radiusLabel);
    radiusLayout->addWidget(radiusEdit);
    heightLayout->addWidget(heightLabel);
    heightLayout->addWidget(heightEdit);
    xLayout->addWidget(xLabel);
    xLayout->addWidget(xEdit);
    yLayout->addWidget(yLabel);
    yLayout->addWidget(yEdit);
    zLayout->addWidget(zLabel);
    zLayout->addWidget(zEdit);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);

    contentLayout->addLayout(radiusLayout);
    contentLayout->addLayout(heightLayout);
    contentLayout->addLayout(xLayout);
    contentLayout->addLayout(yLayout);
    contentLayout->addLayout(zLayout);
    contentLayout->addLayout(buttonLayout);
    contentLayout->addStretch();
    setLayout(mainLayout);

    // 连接按钮信号
    connect(okButton, &QPushButton::clicked, this, &CylinderDialog::onOkClicked);
}

CylinderDialog::~CylinderDialog()
{
}

QJsonObject CylinderDialog::saveJson()
{
    QJsonObject state;
    state["radius"] = getRadius();
    state["height"] = getHeight();
    state["X"] = xEdit->text().toDouble();
    state["Y"] = yEdit->text().toDouble();
    state["Z"] = zEdit->text().toDouble();
    return state;
}

void CylinderDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("radius"))
    {
        QString value = QString::number(state["radius"].toDouble());
        radiusEdit->setText(value);
    }
    if (state.contains("height"))
    {
        QString value = QString::number(state["height"].toDouble());
        heightEdit->setText(value);
    }
    if (state.contains("X"))
    {
        QString value = QString::number(state["X"].toDouble());
        xEdit->setText(value);
    }
    if (state.contains("Y"))
    {
        QString value = QString::number(state["Y"].toDouble());
        yEdit->setText(value);
    }
    if (state.contains("Z"))
    {
        QString value = QString::number(state["Z"].toDouble());
        zEdit->setText(value);
    }
}

double CylinderDialog::getRadius() const
{
    return radiusEdit->text().toDouble();
}

double CylinderDialog::getHeight() const
{
    return heightEdit->text().toDouble();
}

gp_Pnt CylinderDialog::getBaseCenter() const
{
    return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void CylinderDialog::onOkClicked()
{
    bool ok;
    double radius = radiusEdit->text().toDouble(&ok);
    if (!ok || radius <= 0)
    {
        QMessageBox::warning(this, "无效输入", "半径必须为正数！");
        return;
    }
    double height = heightEdit->text().toDouble(&ok);
    if (!ok || height <= 0)
    {
        QMessageBox::warning(this, "无效输入", "高度必须为正数！");
        return;
    }

    GeometryCleanup::removeExistingGeometry(m_uuid);

    gp_Pnt baseCenter = getBaseCenter();

    BRepPrimAPI_MakeCylinder cylinderMaker(radius, height);
    TopoDS_Shape cylinderShape = cylinderMaker.Shape();

    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), baseCenter);
    BRepBuilderAPI_Transform transformer(cylinderShape, transform);
    cylinderShape = transformer.Shape();

    TopoDS_Shape* shape = new TopoDS_Shape(cylinderShape);

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);

    VisualWidgetService* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face");
    vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face");

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(m_uuid, false);
    }
}

void CylinderDialog::onCancelClicked()
{
}
