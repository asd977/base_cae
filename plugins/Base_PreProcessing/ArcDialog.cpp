#include "arcdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>

ArcDialog::ArcDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessArc)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 半径输入
	QLabel* radiusLabel = new QLabel("半径：", this);
	radiusEdit = new QLineEdit(this);
	radiusEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制半径为正数
	layout->addWidget(radiusLabel);
	layout->addWidget(radiusEdit);

	// 起始角度输入
	QLabel* startAngleLabel = new QLabel("起始角度（度）：", this);
	startAngleEdit = new QLineEdit(this);
	startAngleEdit->setValidator(new QDoubleValidator(-360.0, 360.0, 6, this)); // 角度范围[-360, 360]
	layout->addWidget(startAngleLabel);
	layout->addWidget(startAngleEdit);

	// 终止角度输入
	QLabel* endAngleLabel = new QLabel("终止角度（度）：", this);
	endAngleEdit = new QLineEdit(this);
	endAngleEdit->setValidator(new QDoubleValidator(-360.0, 360.0, 6, this)); // 角度范围[-360, 360]
	layout->addWidget(endAngleLabel);
	layout->addWidget(endAngleEdit);

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
	connect(okButton, &QPushButton::clicked, this, &ArcDialog::validateInput);
	// 设置默认值
	radiusEdit->setText("1.0");
	startAngleEdit->setText("0.0");
	endAngleEdit->setText("90.0");
	centerXEdit->setText("0.0");
	centerYEdit->setText("0.0");
}

double ArcDialog::getRadius() const
{
	return radius;
}

double ArcDialog::getStartAngle() const
{
	return startAngle;
}

double ArcDialog::getEndAngle() const
{
	return endAngle;
}

gp_Pnt2d ArcDialog::getCenter() const
{
	return center;
}

QJsonObject ArcDialog::saveJson()
{
	QJsonObject state;
	state["radius"] = radiusEdit->text().toDouble();
	state["startAngle"] = startAngleEdit->text().toDouble();
	state["endAngle"] = endAngleEdit->text().toDouble();
	state["X"] = centerXEdit->text().toDouble();
	state["Y"] = centerYEdit->text().toDouble();
	return state;
}

void ArcDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("radius"))
	{
		QString value = QString::number(state["radius"].toDouble());
		radiusEdit->setText(value);
	}
	if (state.contains("startAngle"))
	{
		QString value = QString::number(state["startAngle"].toDouble());
		startAngleEdit->setText(value);
	}
	if (state.contains("endAngle"))
	{
		QString value = QString::number(state["endAngle"].toDouble());
		endAngleEdit->setText(value);
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

void ArcDialog::validateInput()
{
	bool ok;
	double r = radiusEdit->text().toDouble(&ok);
	if (!ok || r <= 0.0) {
		QMessageBox::warning(this, "输入错误", "半径必须为正数。");
		return;
	}

	double sa = startAngleEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "起始角度必须为有效数字。");
		return;
	}

	double ea = endAngleEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "终止角度必须为有效数字。");
		return;
	}

	if (sa == ea) {
		QMessageBox::warning(this, "输入错误", "起始角度和终止角度不能相等。");
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
	startAngle = sa;
	endAngle = ea;
	center = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, radius, startAngle, endAngle,center);
}