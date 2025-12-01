#include "polygondialog.h"
#include <QDoubleValidator>
#include <QIntValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

PolygonDialog::PolygonDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_POLYGON)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 半径输入
	QLabel* radiusLabel = new QLabel("半径：", this);
	radiusEdit = new QLineEdit(this);
	radiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制半径为正数
	layout->addWidget(radiusLabel);
	layout->addWidget(radiusEdit);

	// 边数输入
	QLabel* sidesLabel = new QLabel("边数：", this);
	sidesEdit = new QLineEdit(this);
	sidesEdit->setValidator(new QIntValidator(3, 100, this)); // 限制边数为3到100
	layout->addWidget(sidesLabel);
	layout->addWidget(sidesEdit);

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
	connect(okButton, &QPushButton::clicked, this, &PolygonDialog::validateInput);

	// 设置默认值
	radiusEdit->setText("1.0");
	sidesEdit->setText("5");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double PolygonDialog::getRadius() const
{
	return radius;
}

int PolygonDialog::getSides() const
{
	return sides;
}

gp_Pnt2d PolygonDialog::getCenter() const
{
	return center;
}

QJsonObject PolygonDialog::saveJson()
{
	QJsonObject state;
	state["radius"] = radiusEdit->text().toDouble();
	state["sides"] = sidesEdit->text().toInt();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void PolygonDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("radius"))
	{
		QString value = QString::number(state["radius"].toDouble());
		radiusEdit->setText(value);
	}
	if (state.contains("sides"))
	{
		QString value = QString::number(state["sides"].toInt());
		sidesEdit->setText(value);
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

void PolygonDialog::validateInput()
{
	bool ok;
	double r = radiusEdit->text().toDouble(&ok);
	if (!ok || r <= 0.0) {
		QMessageBox::warning(this, "输入错误", "半径必须为正数。");
		return;
	}

	int s = sidesEdit->text().toInt(&ok);
	if (!ok || s < 3) {
		QMessageBox::warning(this, "输入错误", "边数必须大于或等于3。");
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

	radius = r;
	sides = s;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, radius, sides, center);
}
