#include "linedialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

LineDialog::LineDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessLine)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 起点X坐标输入
	QLabel* startXLabel = new QLabel("起点X坐标：", this);
	startXEdit = new QLineEdit(this);
	startXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(startXLabel);
	layout->addWidget(startXEdit);

	// 起点Y坐标输入
	QLabel* startYLabel = new QLabel("起点Y坐标：", this);
	startYEdit = new QLineEdit(this);
	startYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(startYLabel);
	layout->addWidget(startYEdit);

	// 终点X坐标输入
	QLabel* endXLabel = new QLabel("终点X坐标：", this);
	endXEdit = new QLineEdit(this);
	endXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(endXLabel);
	layout->addWidget(endXEdit);

	// 终点Y坐标输入
	QLabel* endYLabel = new QLabel("终点Y坐标：", this);
	endYEdit = new QLineEdit(this);
	endYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(endYLabel);
	layout->addWidget(endYEdit);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);
	layout->addLayout(buttonLayout);
	layout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &LineDialog::validateInput);

	// 设置默认值
	startXEdit->setText("0.0");
	startYEdit->setText("0.0");
	endXEdit->setText("1.0");
	endYEdit->setText("1.0");
}

gp_Pnt2d LineDialog::getStartPoint() const
{
	return startPoint;
}

gp_Pnt2d LineDialog::getEndPoint() const
{
	return endPoint;
}

QJsonObject LineDialog::saveJson()
{
	QJsonObject state;
	state["startX"] = startXEdit->text().toDouble();
	state["startY"] = startYEdit->text().toDouble();
	state["endX"] = endXEdit->text().toDouble();
	state["endY"] = endYEdit->text().toDouble();
	return state;
}

void LineDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("startX"))
	{
		QString value = QString::number(state["startX"].toDouble());
		startXEdit->setText(value);
	}
	if (state.contains("startY"))
	{
		QString value = QString::number(state["startY"].toDouble());
		startYEdit->setText(value);
	}
	if (state.contains("endX"))
	{
		QString value = QString::number(state["endX"].toDouble());
		endXEdit->setText(value);
	}
	if (state.contains("endY"))
	{
		QString value = QString::number(state["endY"].toDouble());
		endYEdit->setText(value);
	}
}

void LineDialog::validateInput()
{
	bool ok;
	double sx = startXEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "起点X坐标必须为有效数字。");
		return;
	}

	double sy = startYEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "起点Y坐标必须为有效数字。");
		return;
	}

	double ex = endXEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "终点X坐标必须为有效数字。");
		return;
	}

	double ey = endYEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "终点Y坐标必须为有效数字。");
		return;
	}

	if (sx == ex && sy == ey) {
		QMessageBox::warning(this, "输入错误", "起点和终点不能相同。");
		return;
	}

	startPoint = gp_Pnt2d(sx, sy);
	endPoint = gp_Pnt2d(ex, ey);
	emit parametersSubmitted(m_uuid, startPoint, endPoint);
}