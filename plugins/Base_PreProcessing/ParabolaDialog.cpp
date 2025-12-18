#include "Paraboladialog.h"
#include <QDoubleValidator>
#include <QIntValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

ParabolaDialog::ParabolaDialog(QWidget* parent) : BasePage(PublicConstants::kPagePreprocessParabola)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// 焦距输入
	QLabel* focalLabel = new QLabel("焦距：", this);
	focalEdit = new QLineEdit(this);
	focalEdit->setValidator(new QDoubleValidator(0.0, 1e6, 6, this)); // 限制焦距为正数
	layout->addWidget(focalLabel);
	layout->addWidget(focalEdit);

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

	// 顶点X坐标输入
	QLabel* vertexXLabel = new QLabel("顶点X坐标：", this);
	vertexXEdit = new QLineEdit(this);
	vertexXEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(vertexXLabel);
	layout->addWidget(vertexXEdit);
	vertexXLabel->setVisible(false);
	vertexXEdit->setVisible(false);

	// 顶点Y坐标输入
	QLabel* vertexYLabel = new QLabel("顶点Y坐标：", this);
	vertexYEdit = new QLineEdit(this);
	vertexYEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	layout->addWidget(vertexYLabel);
	layout->addWidget(vertexYEdit);
	vertexYLabel->setVisible(false);
	vertexYEdit->setVisible(false);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);
	layout->addLayout(buttonLayout);
	layout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &ParabolaDialog::validateInput);

	// 设置默认值
	focalEdit->setText("1.0");
	tMinEdit->setText("-10.0");
	tMaxEdit->setText("10.0");
	numPointsEdit->setText("100");
	vertexXEdit->setText("0.0");
	vertexYEdit->setText("0.0");
}

double ParabolaDialog::getFocal() const
{
	return focal;
}

double ParabolaDialog::getTMin() const
{
	return tMin;
}

double ParabolaDialog::getTMax() const
{
	return tMax;
}

int ParabolaDialog::getNumPoints() const
{
	return numPoints;
}

gp_Pnt2d ParabolaDialog::getVertex() const
{
	return vertex;
}

QJsonObject ParabolaDialog::saveJson()
{
	QJsonObject state;
	state["focal"] = focalEdit->text().toDouble();
	state["tMin"] = tMinEdit->text().toDouble();
	state["tMax"] = tMaxEdit->text().toDouble();
	state["points"] = numPointsEdit->text().toInt();
	state["X"] = vertexXEdit->text().toDouble();
	state["Y"] = vertexYEdit->text().toDouble();
	return state;
}

void ParabolaDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("focal"))
	{
		QString value = QString::number(state["focal"].toDouble());
		focalEdit->setText(value);
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
		vertexXEdit->setText(value);
	}
	if (state.contains("Y"))
	{
		QString value = QString::number(state["Y"].toDouble());
		vertexYEdit->setText(value);
	}
}

void ParabolaDialog::validateInput()
{
	bool ok;
	double f = focalEdit->text().toDouble(&ok);
	if (!ok || f <= 0.0) {
		QMessageBox::warning(this, "输入错误", "焦距必须为正数。");
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

	double x = vertexXEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "顶点X坐标必须为有效数字。");
		return;
	}

	double y = vertexYEdit->text().toDouble(&ok);
	if (!ok) {
		QMessageBox::warning(this, "输入错误", "顶点Y坐标必须为有效数字。");
		return;
	}

	focal = f;
	tMin = tmin;
	tMax = tmax;
	numPoints = np;
	vertex = gp_Pnt2d(x, y);
	emit parametersSubmitted(m_uuid, focal, tMin, tMax, numPoints);
}