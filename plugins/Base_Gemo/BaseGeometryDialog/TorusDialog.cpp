#include "torusdialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <QDoubleValidator>
#include <QMessageBox>

TorusDialog::TorusDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessTorus)
{
    initUI();
}

TorusDialog::~TorusDialog()
{
}

QJsonObject TorusDialog::saveJson()
{
    QJsonObject state;
    state["majorRadius"] = getMajorRadius();
    state["minorRadius"] = getMinorRadius();
    state["X"] = xEdit->text().toDouble();
    state["Y"] = yEdit->text().toDouble();
    state["Z"] = zEdit->text().toDouble();
    return state;
}

void TorusDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("majorRadius"))
    {
        QString value = QString::number(state["majorRadius"].toDouble());
        majorRadiusEdit->setText(value);
    }
    if (state.contains("minorRadius"))
    {
        QString value = QString::number(state["minorRadius"].toDouble());
        minorRadiusEdit->setText(value);
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

double TorusDialog::getMajorRadius() const
{
    return majorRadiusEdit->text().toDouble();
}

double TorusDialog::getMinorRadius() const
{
    return minorRadiusEdit->text().toDouble();
}

gp_Pnt TorusDialog::getCenter() const
{
    return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void TorusDialog::onOkClicked()
{
    bool ok;
    double majorRadius = majorRadiusEdit->text().toDouble(&ok);
    if (!ok || majorRadius <= 0)
    {
        QMessageBox::warning(this, "无效输入", "大半径必须为正数！");
        return;
    }
    double minorRadius = minorRadiusEdit->text().toDouble(&ok);
    if (!ok || minorRadius <= 0 || minorRadius >= majorRadius)
    {
        QMessageBox::warning(this, "无效输入", "小半径必须为正数且小于大半径！");
        return;
    }

    GeometryCleanup::removeExistingGeometry(m_uuid);

    gp_Pnt center = getCenter();
    BRepPrimAPI_MakeTorus torusMaker(majorRadius, minorRadius);
    TopoDS_Shape torusShape = torusMaker.Shape();

    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), center);
    BRepBuilderAPI_Transform transformer(torusShape, transform);
    torusShape = transformer.Shape();
    TopoDS_Shape* shape = new TopoDS_Shape(torusShape);

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

void TorusDialog::initUI()
{
    // 创建输入框和标签
    QLabel* majorRadiusLabel = new QLabel("大半径:", this);
    majorRadiusEdit = new QLineEdit(this);
    majorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* minorRadiusLabel = new QLabel("小半径:", this);
    minorRadiusEdit = new QLineEdit(this);
    minorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

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
    majorRadiusEdit->setText("50.0");
    minorRadiusEdit->setText("10.0");
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
    QHBoxLayout* majorRadiusLayout = new QHBoxLayout;
    QHBoxLayout* minorRadiusLayout = new QHBoxLayout;
    QHBoxLayout* xLayout = new QHBoxLayout;
    QHBoxLayout* yLayout = new QHBoxLayout;
    QHBoxLayout* zLayout = new QHBoxLayout;
    QHBoxLayout* buttonLayout = new QHBoxLayout;

    majorRadiusLayout->addWidget(majorRadiusLabel);
    majorRadiusLayout->addWidget(majorRadiusEdit);
    minorRadiusLayout->addWidget(minorRadiusLabel);
    minorRadiusLayout->addWidget(minorRadiusEdit);
    xLayout->addWidget(xLabel);
    xLayout->addWidget(xEdit);
    yLayout->addWidget(yLabel);
    yLayout->addWidget(yEdit);
    zLayout->addWidget(zLabel);
    zLayout->addWidget(zEdit);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);

    mainLayout->addLayout(majorRadiusLayout);
    mainLayout->addLayout(minorRadiusLayout);
    mainLayout->addLayout(xLayout);
    mainLayout->addLayout(yLayout);
    mainLayout->addLayout(zLayout);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    setLayout(mainLayout);

    // 连接按钮信号
    connect(okButton, &QPushButton::clicked, this, &TorusDialog::onOkClicked);
}
