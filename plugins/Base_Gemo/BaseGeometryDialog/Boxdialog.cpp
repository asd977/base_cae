#include "boxdialog.h"

#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

BoxDialog::BoxDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessBox)
{
    initUI();
}

BoxDialog::~BoxDialog()
{
}

QJsonObject BoxDialog::saveJson()
{
    QJsonObject state;
    state["length"] = lengthEdit->text().toDouble();
    state["width"] = widthEdit->text().toDouble();
    state["height"] = heightEdit->text().toDouble();
    state["x"] = xEdit->text().toDouble();
    state["y"] = yEdit->text().toDouble();
    state["z"] = zEdit->text().toDouble();
    return state;
}

void BoxDialog::initUI()
{
    // 创建输入框和标签
    QLabel* lengthLabel = new QLabel("长度:", this);
    lengthEdit = new QLineEdit(this);
    lengthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this)); // 限制正数

    QLabel* widthLabel = new QLabel("宽度:", this);
    widthEdit = new QLineEdit(this);
    widthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* heightLabel = new QLabel("高度:", this);
    heightEdit = new QLineEdit(this);
    heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

    QLabel* xLabel = new QLabel("位置X:", this);
    xEdit = new QLineEdit(this);
    xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* yLabel = new QLabel("位置Y:", this);
    yEdit = new QLineEdit(this);
    yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    QLabel* zLabel = new QLabel("位置Z:", this);
    zEdit = new QLineEdit(this);
    zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

    // 设置默认值
    lengthEdit->setText("100.0");
    widthEdit->setText("50.0");
    heightEdit->setText("30.0");
    xEdit->setText("50.0");
    yEdit->setText("50.0");
    zEdit->setText("0.0");

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
    QHBoxLayout* lengthLayout = new QHBoxLayout;
    QHBoxLayout* widthLayout = new QHBoxLayout;
    QHBoxLayout* heightLayout = new QHBoxLayout;
    QHBoxLayout* xLayout = new QHBoxLayout;
    QHBoxLayout* yLayout = new QHBoxLayout;
    QHBoxLayout* zLayout = new QHBoxLayout;
    QHBoxLayout* buttonLayout = new QHBoxLayout;

    lengthLayout->addWidget(lengthLabel);
    lengthLayout->addWidget(lengthEdit);
    widthLayout->addWidget(widthLabel);
    widthLayout->addWidget(widthEdit);
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

    mainLayout->addLayout(lengthLayout);
    mainLayout->addLayout(widthLayout);
    mainLayout->addLayout(heightLayout);
    mainLayout->addLayout(xLayout);
    mainLayout->addLayout(yLayout);
    mainLayout->addLayout(zLayout);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
    setLayout(mainLayout);

    connect(okButton, &QPushButton::clicked, this, &BoxDialog::onOkClicked);
}

void BoxDialog::loadJson(const QJsonObject& state)
{
    if (state.contains("length"))
        lengthEdit->setText(QString::number(state["length"].toDouble()));
    if (state.contains("width"))
        widthEdit->setText(QString::number(state["width"].toDouble()));
    if (state.contains("height"))
        heightEdit->setText(QString::number(state["height"].toDouble()));
    if (state.contains("x"))
        xEdit->setText(QString::number(state["x"].toDouble()));
    if (state.contains("y"))
        yEdit->setText(QString::number(state["y"].toDouble()));
    if (state.contains("z"))
        zEdit->setText(QString::number(state["z"].toDouble()));
}

gp_Pnt BoxDialog::getPosition() const
{
    return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void BoxDialog::onOkClicked()
{
    bool ok;
    double length = lengthEdit->text().toDouble(&ok);
    if (!ok || length <= 0)
    {
        QMessageBox::warning(this, "无效输入", "长度必须为正数！");
        return;
    }
    double width = widthEdit->text().toDouble(&ok);
    if (!ok || width <= 0)
    {
        QMessageBox::warning(this, "无效输入", "宽度必须为正数！");
        return;
    }
    double height = heightEdit->text().toDouble(&ok);
    if (!ok || height <= 0)
    {
        QMessageBox::warning(this, "无效输入", "高度必须为正数！");
        return;
    }

    GeometryCleanup::removeExistingGeometry(m_uuid);

    gp_Pnt position = getPosition();
    BRepPrimAPI_MakeBox boxMaker(length, width, height);
    TopoDS_Shape boxShape = boxMaker.Shape();
    gp_Trsf transform;
    transform.SetTranslation(gp_Pnt(0, 0, 0), position);
    BRepBuilderAPI_Transform transformer(boxShape, transform);
    boxShape = transformer.Shape();

    TopoDS_Shape* shape = new TopoDS_Shape(boxShape);

    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(m_uuid, shape, getDisplayName());
    mgr->appendGeomShapeObject(object);
    VisualWidgetService* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
    vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(m_uuid, false);
    }
}
