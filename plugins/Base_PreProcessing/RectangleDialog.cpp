#include "rectangledialog.h"
#include <QDoubleValidator>
#include <QMessageBox>

RectangleDialog::RectangleDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessRectangle)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 长度输入
	QLabel* lengthLabel = new QLabel("长度：", this);
	lengthEdit = new QLineEdit(this);
	lengthEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制长度为正数
	layout->addWidget(lengthLabel);
	layout->addWidget(lengthEdit);

	// 宽度输入
	QLabel* widthLabel = new QLabel("宽度：", this);
	widthEdit = new QLineEdit(this);
	widthEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制宽度为正数
	layout->addWidget(widthLabel);
	layout->addWidget(widthEdit);

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
	connect(okButton, &QPushButton::clicked, this, &RectangleDialog::validateInput);

	// 设置默认值
	lengthEdit->setText("2.0");
	widthEdit->setText("1.0");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double RectangleDialog::getLength() const
{
	return length;
}

double RectangleDialog::getWidth() const
{
	return width;
}

gp_Pnt2d RectangleDialog::getCenter() const
{
	return center;
}

QJsonObject RectangleDialog::saveJson()
{
	QJsonObject state;
	state["length"] = lengthEdit->text().toDouble();
	state["width"] = widthEdit->text().toDouble();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void RectangleDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("length"))
	{
		QString value = QString::number(state["length"].toDouble());
		lengthEdit->setText(value);
	}
	if (state.contains("width"))
	{
		QString value = QString::number(state["width"].toDouble());
		widthEdit->setText(value);
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

void RectangleDialog::validateInput()
{
	bool ok;
	double l = lengthEdit->text().toDouble(&ok);
	if (!ok || l <= 0.0) {
		QMessageBox::warning(this, "输入错误", "长度必须为正数。");
		return;
	}

	double w = widthEdit->text().toDouble(&ok);
	if (!ok || w <= 0.0) {
		QMessageBox::warning(this, "输入错误", "宽度必须为正数。");
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

	length = l;
	width = w;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, length, width, center);
}
