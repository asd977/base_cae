#include "boxdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <PublicVariables.h>
#include <PublicConstants.h>

BoxDialog::BoxDialog(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessBox)
{
	// 创建输入框和标签
	QLabel *lengthLabel = new QLabel("长度:", this);
	lengthEdit = new QLineEdit(this);
	lengthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this)); // 限制正数

	QLabel *widthLabel = new QLabel("宽度:", this);
	widthEdit = new QLineEdit(this);
	widthEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *heightLabel = new QLabel("高度:", this);
	heightEdit = new QLineEdit(this);
	heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *xLabel = new QLabel("位置X:", this);
	xEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *yLabel = new QLabel("位置Y:", this);
	yEdit = new QLineEdit(this);
	yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *zLabel = new QLabel("位置Z:", this);
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
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标


	// 布局
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *lengthLayout = new QHBoxLayout;
	QHBoxLayout *widthLayout = new QHBoxLayout;
	QHBoxLayout *heightLayout = new QHBoxLayout;
	QHBoxLayout *xLayout = new QHBoxLayout;
	QHBoxLayout *yLayout = new QHBoxLayout;
	QHBoxLayout *zLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

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

BoxDialog::~BoxDialog()
{
}

QJsonObject BoxDialog::saveJson()
{
	QJsonObject state;
	state["Length"] = getLength();
	state["Width"] = getWidth();
	state["Height"] = getHeight();
	state["X"] = getX();
	state["Y"] = getY();
	state["Z"] = getZ();
	return state;
}

void BoxDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("Length"))
	{
		QString value = QString::number(state["Length"].toDouble());
		setLength(value);
	}
	if (state.contains("Width"))
	{
		QString value = QString::number(state["Width"].toDouble());
		setWidth(value);
	}
	if (state.contains("Height"))
	{
		QString value = QString::number(state["Height"].toDouble());
		setHeight(value);
	}
	if (state.contains("X"))
	{
		QString value = QString::number(state["X"].toDouble());
		setX(value);
	}
	if (state.contains("Y"))
	{
		QString value = QString::number(state["Y"].toDouble());
		setY(value);
	}
	if (state.contains("Z"))
	{
		QString value = QString::number(state["Z"].toDouble());
		setZ(value);
	}
}

double BoxDialog::getLength() const
{
	return lengthEdit->text().toDouble();
}

double BoxDialog::getWidth() const
{
	return widthEdit->text().toDouble();
}

double BoxDialog::getHeight() const
{
	return heightEdit->text().toDouble();
}

double BoxDialog::getX() const
{
	return xEdit->text().toDouble();
}

double BoxDialog::getY() const
{
	return yEdit->text().toDouble();
}

double BoxDialog::getZ() const
{
	return zEdit->text().toDouble();
}

void BoxDialog::setLength(QString value)
{
	lengthEdit->setText(value);
}

void BoxDialog::setWidth(QString value)
{
	widthEdit->setText(value);
}

void BoxDialog::setHeight(QString value)
{
	heightEdit->setText(value);
}

void BoxDialog::setX(QString value)
{
	xEdit->setText(value);
}

void BoxDialog::setY(QString value)
{
	yEdit->setText(value);
}

void BoxDialog::setZ(QString value)
{
	zEdit->setText(value);
}

gp_Pnt BoxDialog::getPosition() const
{
	return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void BoxDialog::onOkClicked()
{
	bool ok;
	double length = lengthEdit->text().toDouble(&ok);
	if (!ok || length <= 0) {
		QMessageBox::warning(this, "无效输入", "长度必须为正数！");
		return;
	}
	double width = widthEdit->text().toDouble(&ok);
	if (!ok || width <= 0) {
		QMessageBox::warning(this, "无效输入", "宽度必须为正数！");
		return;
	}
	double height = heightEdit->text().toDouble(&ok);
	if (!ok || height <= 0) {
		QMessageBox::warning(this, "无效输入", "高度必须为正数！");
		return;
	}
	// 发出信号，携带长方体参数
	emit parametersSubmitted(m_uuid,length, width, height, getPosition());
}

