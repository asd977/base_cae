#include "squaredialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

SquareDialog::SquareDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessSquare)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 边长输入
	QLabel* sideLengthLabel = new QLabel("边长：", this);
	sideLengthEdit = new QLineEdit(this);
	sideLengthEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制边长为正数
	layout->addWidget(sideLengthLabel);
	layout->addWidget(sideLengthEdit);

	// 中心X坐标输入
	QLabel* centerXLabel = new QLabel("中心X坐标：", this);
	centerXEdit = new QLineEdit(this);
	centerXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(centerXLabel);
	layout->addWidget(centerXEdit);

	// 中心Y坐标输入
	QLabel* centerYLabel = new QLabel("中心Y坐标：", this);
	centerYEdit = new QLineEdit(this);
	centerYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(centerYLabel);
	layout->addWidget(centerYEdit);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);
	layout->addLayout(buttonLayout);
	layout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &SquareDialog::validateInput);

	// 设置默认值
	sideLengthEdit->setText("1.0");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double SquareDialog::getSideLength() const
{
	bool ok;
	double sl = sideLengthEdit->text().toDouble(&ok);
	return sl;
}

gp_Pnt2d SquareDialog::getCenter() const
{
	return center;
}

QJsonObject SquareDialog::saveJson()
{
	QJsonObject state;
	state["length"] = getSideLength();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void SquareDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("length"))
	{
		QString value = QString::number(state["length"].toDouble());
		sideLengthEdit->setText(value);
	}
	if (state.contains("X"))
	{
		QString value = QString::number(state["X"].toDouble());
		centerXEdit->setText(value);
	}
	if (state.contains("Y"))
	{
		QString value = QString::number(state["Y"].toDouble());
		centerYEdit->setText(value);
	}
}

void SquareDialog::validateInput()
{
	bool ok;
	double sl = sideLengthEdit->text().toDouble(&ok);
	if (!ok || sl <= 0.0) {
		QMessageBox::warning(this, "输入错误", "边长必须为正数。");
		return;
	}

	double x = centerXEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "中心X坐标必须为有效数字。");
		return;
	}

	double y = centerYEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "中心Y坐标必须为有效数字。");
		return;
	}

	sideLength = sl;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, sideLength, center);
}
