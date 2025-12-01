#include "bezier2dialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

Bezier2Dialog::Bezier2Dialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_BEZIER2D)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 控制点0输入
	QLabel* point0Label = new QLabel("控制点0 (X, Y)：", this);
	point0XEdit = new QLineEdit(this);
	point0YEdit = new QLineEdit(this);
	point0XEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	point0YEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	QHBoxLayout* point0Layout = new QHBoxLayout();
	point0Layout->addWidget(point0XEdit);
	point0Layout->addWidget(point0YEdit);
	layout->addWidget(point0Label);
	layout->addLayout(point0Layout);

	// 控制点1输入
	QLabel* point1Label = new QLabel("控制点1 (X, Y)：", this);
	point1XEdit = new QLineEdit(this);
	point1YEdit = new QLineEdit(this);
	point1XEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	point1YEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	QHBoxLayout* point1Layout = new QHBoxLayout();
	point1Layout->addWidget(point1XEdit);
	point1Layout->addWidget(point1YEdit);
	layout->addWidget(point1Label);
	layout->addLayout(point1Layout);

	// 控制点2输入
	QLabel* point2Label = new QLabel("控制点2 (X, Y)：", this);
	point2XEdit = new QLineEdit(this);
	point2YEdit = new QLineEdit(this);
	point2XEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	point2YEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	QHBoxLayout* point2Layout = new QHBoxLayout();
	point2Layout->addWidget(point2XEdit);
	point2Layout->addWidget(point2YEdit);
	layout->addWidget(point2Label);
	layout->addLayout(point2Layout);

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
	connect(okButton, &QPushButton::clicked, this, &Bezier2Dialog::validateInput);

	// 设置默认值
	point0XEdit->setText("0.0");
	point0YEdit->setText("0.0");
	point1XEdit->setText("1.0");
	point1YEdit->setText("1.0");
	point2XEdit->setText("2.0");
	point2YEdit->setText("0.0");
}

std::vector<gp_Pnt2d> Bezier2Dialog::getControlPoints() const
{
	return controlPoints;
}

QJsonObject Bezier2Dialog::saveJson()
{
	QJsonObject state;
	state["X0"] = point0XEdit->text().toDouble();
	state["Y0"] = point0YEdit->text().toDouble();
	state["X1"] = point1XEdit->text().toDouble();
	state["Y1"] = point1YEdit->text().toDouble();
	state["X2"] = point2XEdit->text().toDouble();
	state["Y2"] = point2YEdit->text().toDouble();
	return state;
}

void Bezier2Dialog::loadJson(const QJsonObject & state)
{
	if (state.contains("X0"))
	{
		QString value = QString::number(state["X0"].toDouble());
		point0XEdit->setText(value);
	}
	if (state.contains("Y0"))
	{
		QString value = QString::number(state["Y0"].toDouble());
		point0YEdit->setText(value);
	}
	if (state.contains("X1"))
	{
		QString value = QString::number(state["X1"].toDouble());
		point1XEdit->setText(value);
	}
	if (state.contains("Y1"))
	{
		QString value = QString::number(state["Y1"].toDouble());
		point1YEdit->setText(value);
	}
	if (state.contains("X2"))
	{
		QString value = QString::number(state["X2"].toDouble());
		point2XEdit->setText(value);
	}
	if (state.contains("Y2"))
	{
		QString value = QString::number(state["Y2"].toDouble());
		point2YEdit->setText(value);
	}
}

void Bezier2Dialog::validateInput()
{
	bool ok;
	double p0x = point0XEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点0的X坐标必须为有效数字。");
		return;
	}
	double p0y = point0YEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点0的Y坐标必须为有效数字。");
		return;
	}

	double p1x = point1XEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点1的X坐标必须为有效数字。");
		return;
	}
	double p1y = point1YEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点1的Y坐标必须为有效数字。");
		return;
	}

	double p2x = point2XEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点2的X坐标必须为有效数字。");
		return;
	}
	double p2y = point2YEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "控制点2的Y坐标必须为有效数字。");
		return;
	}

	controlPoints = { gp_Pnt2d(p0x, p0y), gp_Pnt2d(p1x, p1y), gp_Pnt2d(p2x, p2y) };
	emit parametersSubmitted(m_uuid, controlPoints);
}

