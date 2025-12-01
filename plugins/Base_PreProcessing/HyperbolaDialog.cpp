#include "hyperboladialog.h"
#include <QDoubleValidator>
#include <QIntValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

HyperbolaDialog::HyperbolaDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_HYPER)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 实半轴输入
	QLabel* majorRadiusLabel = new QLabel("实半轴：", this);
	majorRadiusEdit = new QLineEdit(this);
	majorRadiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制实半轴为正数
	layout->addWidget(majorRadiusLabel);
	layout->addWidget(majorRadiusEdit);

	// 焦距输入
	QLabel* focalDistanceLabel = new QLabel("焦距：", this);
	focalDistanceEdit = new QLineEdit(this);
	focalDistanceEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制焦距为正数
	layout->addWidget(focalDistanceLabel);
	layout->addWidget(focalDistanceEdit);

	// 参数下限输入
	QLabel* tMinLabel = new QLabel("参数下限：", this);
	tMinEdit = new QLineEdit(this);
	tMinEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(tMinLabel);
	layout->addWidget(tMinEdit);

	// 参数上限输入
	QLabel* tMaxLabel = new QLabel("参数上限：", this);
	tMaxEdit = new QLineEdit(this);
	tMaxEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(tMaxLabel);
	layout->addWidget(tMaxEdit);

	// 点数输入
	QLabel* numPointsLabel = new QLabel("点数：", this);
	numPointsEdit = new QLineEdit(this);
	numPointsEdit->setValidator(new QIntValidator(1, 1000, this)); // 限制点数为1到1000
	layout->addWidget(numPointsLabel);
	layout->addWidget(numPointsEdit);

	// 中心X坐标输入
	QLabel* centerXLabel = new QLabel("中心X坐标：", this);
	centerXEdit = new QLineEdit(this);
	centerXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(centerXLabel);
	layout->addWidget(centerXEdit);
	centerXLabel->setVisible(false);
	centerXEdit->setVisible(false);

	// 中心Y坐标输入
	QLabel* centerYLabel = new QLabel("中心Y坐标：", this);
	centerYEdit = new QLineEdit(this);
	centerYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(centerYLabel);
	layout->addWidget(centerYEdit);
	centerYLabel->setVisible(false);
	centerYEdit->setVisible(false);

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
	connect(okButton, &QPushButton::clicked, this, &HyperbolaDialog::validateInput);

	// 设置默认值
	majorRadiusEdit->setText("2.0");
	focalDistanceEdit->setText("2.5");
	tMinEdit->setText("-2.0");
	tMaxEdit->setText("2.0");
	numPointsEdit->setText("100");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double HyperbolaDialog::getMajorRadius() const
{
	return majorRadius;
}

double HyperbolaDialog::getFocalDistance() const
{
	return focalDistance;
}

double HyperbolaDialog::getTMin() const
{
	return tMin;
}

double HyperbolaDialog::getTMax() const
{
	return tMax;
}

int HyperbolaDialog::getNumPoints() const
{
	return numPoints;
}

gp_Pnt2d HyperbolaDialog::getCenter() const
{
	return center;
}

QJsonObject HyperbolaDialog::saveJson()
{
	QJsonObject state;
	state["majorRadius"] = majorRadiusEdit->text().toDouble();
	state["focal"] = focalDistanceEdit->text().toDouble();
	state["tMin"] = tMinEdit->text().toDouble();
	state["tMax"] = tMaxEdit->text().toDouble();
	state["points"] = numPointsEdit->text().toInt();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void HyperbolaDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("majorRadius"))
	{
		QString value = QString::number(state["majorRadius"].toDouble());
		majorRadiusEdit->setText(value);
	}
	if (state.contains("focal"))
	{
		QString value = QString::number(state["focal"].toDouble());
		focalDistanceEdit->setText(value);
	}
	if (state.contains("tMin"))
	{
		QString value = QString::number(state["tMin"].toDouble());
		tMinEdit->setText(value);
	}
	if (state.contains("tMax"))
	{
		QString value = QString::number(state["tMax"].toDouble());
		tMaxEdit->setText(value);
	}
	if (state.contains("points"))
	{
		QString value = QString::number(state["points"].toInt());
		numPointsEdit->setText(value);
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

void HyperbolaDialog::validateInput()
{
	bool ok;
	double a = majorRadiusEdit->text().toDouble(&ok);
	if (!ok || a <= 0.0) {
		QMessageBox::warning(this, "输入错误", "实半轴必须为正数。");
		return;
	}

	double c = focalDistanceEdit->text().toDouble(&ok);
	if (!ok || c <= 0.0) {
		QMessageBox::warning(this, "输入错误", "焦距必须为正数。");
		return;
	}

	if (c <= a) {
		QMessageBox::warning(this, "输入错误", "焦距必须大于实半轴。");
		return;
	}

	double tmin = tMinEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "参数下限必须为有效数字。");
		return;
	}

	double tmax = tMaxEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "参数上限必须为有效数字。");
		return;
	}

	if (tmin >= tmax) {
		QMessageBox::warning(this, "输入错误", "参数下限必须小于参数上限。");
		return;
	}

	int np = numPointsEdit->text().toInt(&ok);
	if (!ok || np <= 0) {
		QMessageBox::warning(this, "输入错误", "点数必须为正整数。");
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

	majorRadius = a;
	focalDistance = c;
	tMin = tmin;
	tMax = tmax;
	numPoints = np;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, majorRadius, focalDistance, tMin, tMax, numPoints);
}