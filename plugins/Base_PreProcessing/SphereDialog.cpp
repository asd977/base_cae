#include "SphereDialog.h"
#include "spheredialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

SphereDialog::SphereDialog(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessSphere)
{
	// 创建输入框和标签
	QLabel *radiusLabel = new QLabel("半径:", this);
	radiusEdit = new QLineEdit(this);
	radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *xLabel = new QLabel("中心X:", this);
	xEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *yLabel = new QLabel("中心Y:", this);
	yEdit = new QLineEdit(this);
	yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *zLabel = new QLabel("中心Z:", this);
	zEdit = new QLineEdit(this);
	zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	// 设置默认值
	radiusEdit->setText("40.0");
	xEdit->setText("0.0");
	yEdit->setText("0.0");
	zEdit->setText("50.0");

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标


	// 布局
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *radiusLayout = new QHBoxLayout;
	QHBoxLayout *xLayout = new QHBoxLayout;
	QHBoxLayout *yLayout = new QHBoxLayout;
	QHBoxLayout *zLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

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

	// 连接按钮信号
	connect(okButton, &QPushButton::clicked, this, &SphereDialog::onOkClicked);
}

SphereDialog::~SphereDialog()
{
}

QJsonObject SphereDialog::saveJson()
{
	QJsonObject state;
	state["radius"] = getRadius();
	state["x"] = xEdit->text().toDouble();
	state["y"] = yEdit->text().toDouble();
	state["z"] = zEdit->text().toDouble();
	return state;
}

void SphereDialog::loadJson(const QJsonObject & state)
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

double SphereDialog::getRadius() const
{
	return radiusEdit->text().toDouble();
}

gp_Pnt SphereDialog::getCenter() const
{
	return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void SphereDialog::onOkClicked()
{
	bool ok;
	double radius = radiusEdit->text().toDouble(&ok);
	if (!ok || radius <= 0) {
		QMessageBox::warning(this, "无效输入", "半径必须为正数!");
		return;
	}
	emit parametersSubmitted(m_uuid,radius, getCenter());
}

void SphereDialog::onCancelClicked()
{
}
