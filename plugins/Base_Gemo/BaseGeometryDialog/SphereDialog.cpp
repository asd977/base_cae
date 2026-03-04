#include "SphereDialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepPrimAPI_MakeSphere.hxx>
#include <QDoubleValidator>
#include <QMessageBox>

#include "spheredialog.h"

SphereDialog::SphereDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessSphere)
{
    initUI();
}

SphereDialog::~SphereDialog()
{
}

void SphereDialog::initUI()
{
    // 创建输入框和标签
    QLabel* radiusLabel = new QLabel("半径:", this);
    radiusEdit = new QLineEdit(this);
    radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* xLabel = new QLabel("中心X:", this);
    xEdit = new QLineEdit(this);
    xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* yLabel = new QLabel("中心Y:", this);
    yEdit = new QLineEdit(this);
    yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* zLabel = new QLabel("中心Z:", this);
    zEdit = new QLineEdit(this);
    zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    // 设置默认值
    radiusEdit->setText("40.0");
    xEdit->setText("0");
    yEdit->setText("0");
    zEdit->setText("0");

    // 创建按钮
    QPushButton* okButton = new QPushButton("生成几何体", this);
    // 布局
    QVBoxLayout* mainLayout = new QVBoxLayout;
    auto* pageNameLabel = new QLabel(QString("几何模块"), this);
    pageNameLabel->setObjectName("pageNameLabel");
    auto* sectionTitleButton = new QPushButton(QString("参数设置"), this);
    sectionTitleButton->setObjectName("sectionTitleButton");
    sectionTitleButton->setEnabled(false);
    mainLayout->addWidget(pageNameLabel);
    mainLayout->addWidget(sectionTitleButton);
    QHBoxLayout* radiusLayout = new QHBoxLayout;
    QHBoxLayout* xLayout = new QHBoxLayout;
    QHBoxLayout* yLayout = new QHBoxLayout;
    QHBoxLayout* zLayout = new QHBoxLayout;
    QHBoxLayout* buttonLayout = new QHBoxLayout;

    radiusLayout->addWidget(radiusLabel);
    radiusLayout->addWidget(radiusEdit);
    xLayout->addWidget(xLabel);
    xLayout->addWidget(xEdit);
    yLayout->addWidget(yLabel);
    yLayout->addWidget(yEdit);
    zLayout->addWidget(zLabel);
    zLayout->addWidget(zEdit);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);

    mainLayout->addLayout(radiusLayout);
    mainLayout->addLayout(xLayout);
    mainLayout->addLayout(yLayout);
    mainLayout->addLayout(zLayout);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    setLayout(mainLayout);
    connect(okButton, &QPushButton::clicked, this, &SphereDialog::onOkClicked);
}
QJsonObject SphereDialog::saveJson()
{
    QJsonObject state;
    state["radius"] = radiusEdit->text().toDouble();
    state["x"] = xEdit->text().toDouble();
    state["y"] = yEdit->text().toDouble();
    state["z"] = zEdit->text().toDouble();
    return state;
}

void SphereDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("radius"))
    {
        QString value = QString::number(state["radius"].toDouble());
        radiusEdit->setText(value);
    }
    if (state.contains("x"))
    {
        QString value = QString::number(state["x"].toDouble());
        xEdit->setText(value);
    }
    if (state.contains("y"))
    {
        QString value = QString::number(state["y"].toDouble());
        yEdit->setText(value);
    }
    if (state.contains("z"))
    {
        QString value = QString::number(state["z"].toDouble());
        zEdit->setText(value);
    }
}

void SphereDialog::onOkClicked()
{
    bool ok;
    double radius = radiusEdit->text().toDouble(&ok);
    if (!ok || radius <= 0)
    {
        QMessageBox::warning(this, "无效输入", "半径必须为正数!");
        return;
    }
    GeometryCleanup::removeExistingGeometry(m_uuid);
    gp_Pnt pos =
        gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
    BRepPrimAPI_MakeSphere sphereMaker(pos, radius);
    TopoDS_Shape* shape = new TopoDS_Shape(sphereMaker.Shape());

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
