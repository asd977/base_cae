#include "circledialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

CircleDialog::CircleDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_CIRCLE)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 半径输入
	QLabel* radiusLabel = new QLabel("半径：", this);
	radiusEdit = new QLineEdit(this);
	radiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制半径为正数
	layout->addWidget(radiusLabel);
	layout->addWidget(radiusEdit);

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

	// 中心Z坐标输入
	QLabel* centerZLabel = new QLabel("中心Z坐标：", this);
	centerZEdit = new QLineEdit(this);
	centerZEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(centerZLabel);
	layout->addWidget(centerZEdit);
	centerZLabel->setVisible(false);
	centerZEdit->setVisible(false);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	// 使用 QSS 去除按钮边框并优化视觉融合
	QString buttonStyle = R"(
        QPushButton {
            border: none; /* 去除边框 */
            background: transparent; /* 透明背景 */
            padding: 2px; /* 减少内边距，文字和图标更紧凑 */
            color: #333; /* 文字颜色，深灰色 */
        }
        QPushButton:hover {
            background: #e0e0e0; /* 鼠标悬停时浅灰背景 */
            border-radius: 4px; /* 圆角效果 */
        }
        QPushButton:pressed {
            background: #c0c0c0; /* 点击时更深的背景 */
        }
    )";
	okButton->setStyleSheet(buttonStyle);
	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);
	layout->addLayout(buttonLayout);
	layout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &CircleDialog::validateInput);
	

	// 设置默认值
	radiusEdit->setText("1.0");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
	centerZEdit->setText("0.0");
}

double CircleDialog::getRadius() const
{
	bool ok;
	double r = radiusEdit->text().toDouble(&ok);
	return r;
}

gp_Pnt CircleDialog::getCenter() const
{
	return center;
}

QJsonObject CircleDialog::saveJson()
{
	QJsonObject state;
	state["radius"] = getRadius();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	state["Z"] = centerZEdit->text().toDouble();
	return state;
}

void CircleDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("radius"))
	{
		QString value = QString::number(state["radius"].toDouble());
		radiusEdit->setText(value);
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
	if (state.contains("Z"))
	{
		QString value = QString::number(state["Z"].toDouble());
		centerZEdit->setText(value);
	}
}

void CircleDialog::validateInput()
{
	bool ok;
	double r = radiusEdit->text().toDouble(&ok);
	if (!ok || r <= 0.0) {
		QMessageBox::warning(this, "输入错误", "半径必须为正数。");
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

	double z = centerZEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "中心Z坐标必须为有效数字。");
		return;
	}

	radius = r;
	center = gp_Pnt(x, y, z);
	emit parametersSubmitted(m_uuid, radius, center);
}