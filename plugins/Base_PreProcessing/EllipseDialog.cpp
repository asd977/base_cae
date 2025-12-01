#include "ellipsedialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

EllipseDialog::EllipseDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_ELLIPSE)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 长轴半径输入
	QLabel* majorRadiusLabel = new QLabel("长轴半径：", this);
	majorRadiusEdit = new QLineEdit(this);
	majorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制长轴半径为正数
	layout->addWidget(majorRadiusLabel);
	layout->addWidget(majorRadiusEdit);

	// 短轴半径输入
	QLabel* minorRadiusLabel = new QLabel("短轴半径：", this);
	minorRadiusEdit = new QLineEdit(this);
	minorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制短轴半径为正数
	layout->addWidget(minorRadiusLabel);
	layout->addWidget(minorRadiusEdit);

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
	connect(okButton, &QPushButton::clicked, this, &EllipseDialog::validateInput);
	// 设置默认值
	majorRadiusEdit->setText("2.0");
	minorRadiusEdit->setText("1.0");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double EllipseDialog::getMajorRadius() const
{
	return majorRadius;
}

double EllipseDialog::getMinorRadius() const
{
	return minorRadius;
}

gp_Pnt2d EllipseDialog::getCenter() const
{
	return center;
}

QJsonObject EllipseDialog::saveJson()
{
	QJsonObject state;
	state["majorRadius"] = majorRadiusEdit->text().toDouble();
	state["minorRadius"] = minorRadiusEdit->text().toDouble();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void EllipseDialog::loadJson(const QJsonObject & state)
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
		centerXEdit->setText(value);
	}
	if (state.contains("Y"))
	{
		QString value = QString::number(state["Y"].toDouble());
		centerYEdit->setText(value);
	}
}

void EllipseDialog::validateInput()
{
	bool ok;
	double majorR = majorRadiusEdit->text().toDouble(&ok);
	if (!ok || majorR <= 0.0) {
		QMessageBox::warning(this, "输入错误", "长轴半径必须为正数。");
		return;
	}

	double minorR = minorRadiusEdit->text().toDouble(&ok);
	if (!ok || minorR <= 0.0) {
		QMessageBox::warning(this, "输入错误", "短轴半径必须为正数。");
		return;
	}

	if (minorR > majorR) {
		QMessageBox::warning(this, "输入错误", "短轴半径不能大于长轴半径。");
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

	majorRadius = majorR;
	minorRadius = minorR;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, majorRadius, minorRadius, center);
}