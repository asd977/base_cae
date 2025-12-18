#include "torusdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

TorusDialog::TorusDialog(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessTorus)
{
	// 创建输入框和标签
	QLabel *majorRadiusLabel = new QLabel("大半径:", this);
	majorRadiusEdit = new QLineEdit(this);
	majorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *minorRadiusLabel = new QLabel("小半径:", this);
	minorRadiusEdit = new QLineEdit(this);
	minorRadiusEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

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
	majorRadiusEdit->setText("50.0");
	minorRadiusEdit->setText("10.0");
	xEdit->setText("100.0");
	yEdit->setText("0.0");
	zEdit->setText("0.0");

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标


	// 布局
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *majorRadiusLayout = new QHBoxLayout;
	QHBoxLayout *minorRadiusLayout = new QHBoxLayout;
	QHBoxLayout *xLayout = new QHBoxLayout;
	QHBoxLayout *yLayout = new QHBoxLayout;
	QHBoxLayout *zLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

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

void TorusDialog::loadJson(const QJsonObject & state)
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
	if (!ok || majorRadius <= 0) {
		QMessageBox::warning(this, "无效输入", "大半径必须为正数！");
		return;
	}
	double minorRadius = minorRadiusEdit->text().toDouble(&ok);
	if (!ok || minorRadius <= 0 || minorRadius >= majorRadius) {
		QMessageBox::warning(this, "无效输入", "小半径必须为正数且小于大半径！");
		return;
	}
	emit parametersSubmitted(m_uuid, majorRadius, minorRadius, getCenter());;
}

void TorusDialog::onCancelClicked()
{
}
