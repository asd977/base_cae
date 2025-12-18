#include "helixdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

HelixDialog::HelixDialog(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessHelix)
{

	// 创建输入框和标签
	QLabel *radiusLabel = new QLabel("半径：", this);
	radiusEdit = new QLineEdit(this);
	radiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *heightLabel = new QLabel("高度：", this);
	heightEdit = new QLineEdit(this);
	heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *pitchLabel = new QLabel("螺距：", this);
	pitchEdit = new QLineEdit(this);
	pitchEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *xLabel = new QLabel("底面中心 X：", this);
	xEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *yLabel = new QLabel("底面中心 Y：", this);
	yEdit = new QLineEdit(this);
	yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *zLabel = new QLabel("底面中心 Z：", this);
	zEdit = new QLineEdit(this);
	zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	// 设置默认值
	radiusEdit->setText("20.0");
	heightEdit->setText("100.0");
	pitchEdit->setText("20.0");
	xEdit->setText("0.0");
	yEdit->setText("100.0");
	zEdit->setText("0.0");

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标


	// 布局
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *radiusLayout = new QHBoxLayout;
	QHBoxLayout *heightLayout = new QHBoxLayout;
	QHBoxLayout *pitchLayout = new QHBoxLayout;
	QHBoxLayout *xLayout = new QHBoxLayout;
	QHBoxLayout *yLayout = new QHBoxLayout;
	QHBoxLayout *zLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

	radiusLayout->addWidget(radiusLabel);
	radiusLayout->addWidget(radiusEdit);
	heightLayout->addWidget(heightLabel);
	heightLayout->addWidget(heightEdit);
	pitchLayout->addWidget(pitchLabel);
	pitchLayout->addWidget(pitchEdit);
	xLayout->addWidget(xLabel);
	xLayout->addWidget(xEdit);
	yLayout->addWidget(yLabel);
	yLayout->addWidget(yEdit);
	zLayout->addWidget(zLabel);
	zLayout->addWidget(zEdit);
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);

	mainLayout->addLayout(radiusLayout);
	mainLayout->addLayout(heightLayout);
	mainLayout->addLayout(pitchLayout);
	mainLayout->addLayout(xLayout);
	mainLayout->addLayout(yLayout);
	mainLayout->addLayout(zLayout);
	mainLayout->addLayout(buttonLayout);
	mainLayout->addStretch();
	setLayout(mainLayout);

	// 连接按钮信号
	connect(okButton, &QPushButton::clicked, this, &HelixDialog::onOkClicked);
}

HelixDialog::~HelixDialog()
{
}

QJsonObject HelixDialog::saveJson()
{
	QJsonObject state;
	state["radius"] = getRadius();
	state["height"] = getHeight();
	state["pitch"] = getPitch();
	state["X"] = xEdit->text().toDouble();
	state["Y"] = yEdit->text().toDouble();
	state["Z"] = zEdit->text().toDouble();
	return state;
}

void HelixDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("pitch"))
	{
		QString value = QString::number(state["pitch"].toDouble());
		pitchEdit->setText(value);
	}
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

double HelixDialog::getRadius() const
{
	return radiusEdit->text().toDouble();
}

double HelixDialog::getHeight() const
{
	return heightEdit->text().toDouble();
}

double HelixDialog::getPitch() const
{
	return pitchEdit->text().toDouble();
}

gp_Pnt HelixDialog::getBaseCenter() const
{
	return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void HelixDialog::onOkClicked()
{
	bool ok;
	double radius = radiusEdit->text().toDouble(&ok);
	if (!ok || radius <= 0) {
		QMessageBox::warning(this, "无效输入", "半径必须为正数！");
		return;
	}
	double height = heightEdit->text().toDouble(&ok);
	if (!ok || height <= 0) {
		QMessageBox::warning(this, "无效输入", "高度必须为正数！");
		return;
	}
	double pitch = pitchEdit->text().toDouble(&ok);
	if (!ok || pitch <= 0) {
		QMessageBox::warning(this, "无效输入", "螺距必须为正数！");
		return;
	}
	emit parametersSubmitted(m_uuid, radius, height, pitch, getBaseCenter());
}

void HelixDialog::onCancelClicked()
{

}
