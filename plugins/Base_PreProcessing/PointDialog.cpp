#include "pointdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

PointDialog::PointDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessPoint)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// X坐标输入
	QLabel* xLabel = new QLabel("X坐标：", this);
	xEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(xLabel);
	layout->addWidget(xEdit);

	// Y坐标输入
	QLabel* yLabel = new QLabel("Y坐标：", this);
	yEdit = new QLineEdit(this);
	yEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(yLabel);
	layout->addWidget(yEdit);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);
	layout->addLayout(buttonLayout);
	layout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &PointDialog::validateInput);

	// 设置默认值
	xEdit->setText("0.0");
	yEdit->setText("0.0");
}

gp_Pnt2d PointDialog::getPoint() const
{
	return point;
}

QJsonObject PointDialog::saveJson()
{
	QJsonObject state;
	state["X"] = xEdit->text().toDouble();
	state["Y"] = yEdit->text().toDouble();
	return state;
}

void PointDialog::loadJson(const QJsonObject & state)
{
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
}

void PointDialog::validateInput()
{
	bool ok;
	double x = xEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "X坐标必须为有效数字。");
		return;
	}

	double y = yEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "Y坐标必须为有效数字。");
		return;
	}

	point = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, point);
}
