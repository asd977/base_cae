#include "parametriccurvedialog.h"
#include <QDoubleValidator>
#include <QIntValidator>
#include <QMessageBox>
#include <cmath>
#include <QString>
#include <QDebug>
#include <PublicVariables.h>
#include <PublicConstants.h>

// 初始化静态成员
double ParametricCurveDialog::linearFactor = 0.0;
double ParametricCurveDialog::powerFactor = 0.0;

ParametricCurveDialog::ParametricCurveDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_PARAM)
{
	QVBoxLayout* layout = new QVBoxLayout(this);

	// X坐标函数输入
	QLabel* xFuncLabel = new QLabel("X坐标函数（例如 sin(t), t*t, cos(t)）：", this);
	xFuncEdit = new QLineEdit(this);
	layout->addWidget(xFuncLabel);
	layout->addWidget(xFuncEdit);

	// Y坐标函数输入
	QLabel* yFuncLabel = new QLabel("Y坐标函数（例如 cos(t), t, sin(t)）：", this);
	yFuncEdit = new QLineEdit(this);
	layout->addWidget(yFuncLabel);
	layout->addWidget(yFuncEdit);

	// Z坐标函数输入
	QLabel* zFuncLabel = new QLabel("Z坐标函数（例如 t, 0, t*t）：", this);
	zFuncEdit = new QLineEdit(this);
	layout->addWidget(zFuncLabel);
	layout->addWidget(zFuncEdit);

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
	numPointsEdit->setValidator(new QIntValidator(1, 1000, this));
	layout->addWidget(numPointsLabel);
	layout->addWidget(numPointsEdit);
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
	connect(okButton, &QPushButton::clicked, this, &ParametricCurveDialog::validateInput);

	// 设置默认值
	xFuncEdit->setText("cos(t)");
	yFuncEdit->setText("sin(t)");
	zFuncEdit->setText("t");
	tMinEdit->setText("0.0");
	tMaxEdit->setText("6.28");
	numPointsEdit->setText("100");
}

double(*ParametricCurveDialog::getXFunc())(double)
{
	return xFunc;
}

double(*ParametricCurveDialog::getYFunc())(double)
{
	return yFunc;
}

double(*ParametricCurveDialog::getZFunc())(double)
{
	return zFunc;
}

double ParametricCurveDialog::getTMin() const
{
	return tMin;
}

double ParametricCurveDialog::getTMax() const
{
	return tMax;
}

int ParametricCurveDialog::getNumPoints() const
{
	return numPoints;
}

double(*ParametricCurveDialog::parseFunction(const QString& expr))(double)
{
	QString trimmed = expr.trimmed().toLower();
	if (trimmed.isEmpty()) {
		return zeroFunc;
	}

	// 支持的函数映射
	if (trimmed == "t") {
		return tFunc;
	}
	else if (trimmed == "sin(t)") {
		return sinFunc;
	}
	else if (trimmed == "cos(t)") {
		return cosFunc;
	}
	else if (trimmed == "tan(t)") {
		return tanFunc;
	}
	else if (trimmed == "exp(t)") {
		return expFunc;
	}
	else if (trimmed == "sqrt(t)") {
		return sqrtFunc;
	}
	else if (trimmed.startsWith("t*")) {
		bool ok;
		linearFactor = trimmed.mid(2).toDouble(&ok);
		if (ok) {
			return linearFunc;
		}
	}
	else if (trimmed.startsWith("t^")) {
		bool ok;
		powerFactor = trimmed.mid(2).toDouble(&ok);
		if (ok) {
			return powerFunc;
		}
	}
	else if (trimmed == "0") {
		return zeroFunc;
	}

	qDebug() << "无法解析函数表达式：" << expr;
	return nullptr;
}

double(*ParametricCurveDialog::parseFunction2(const int & type))(double)
{
	// 支持的函数映射
	if (type == 1) {
		return tFunc;
	}
	else if (type == 2) {
		return sinFunc;
	}
	else if (type == 3) {
		return cosFunc;
	}
	else if (type == 4) {
		return tanFunc;
	}
	else if (type == 5) {
		return expFunc;
	}
	else if (type == 6) {
		return sqrtFunc;
	}
	else if (type == 7) {
			return linearFunc;
	}
	else if (type == 8) {
	     return powerFunc;
	}
	else if (type == 0) {
		return zeroFunc;
	}

	return nullptr;
}

QJsonObject ParametricCurveDialog::saveJson()
{
	QJsonObject state;
	state["xFunc"] = xFuncEdit->text();
	state["yFunc"] = yFuncEdit->text();
	state["zFunc"] = zFuncEdit->text();
	state["tMax"] = tMaxEdit->text().toDouble();
	state["tMin"] = tMinEdit->text().toDouble();
	state["points"] = numPointsEdit->text().toInt();
	return state;
}

void ParametricCurveDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("xFunc"))
	{
		QString value = state["xFunc"].toString();
		xFuncEdit->setText(value);
	}
	if (state.contains("yFunc"))
	{
		QString value = state["yFunc"].toString();
		yFuncEdit->setText(value);
	}
	if (state.contains("zFunc"))
	{
		QString value = state["zFunc"].toString();
		zFuncEdit->setText(value);
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
}

int ParametricCurveDialog::parseFunction1(const QString& expr)
{
	QString trimmed = expr.trimmed().toLower();
	if (trimmed.isEmpty()) {
		return -1;
	}

	// 支持的函数映射
	if (trimmed == "t") {
		return 1;
	}
	else if (trimmed == "sin(t)") {
		return 2;
	}
	else if (trimmed == "cos(t)") {
		return 3;
	}
	else if (trimmed == "tan(t)") {
		return 4;
	}
	else if (trimmed == "exp(t)") {
		return 5;
	}
	else if (trimmed == "sqrt(t)") {
		return 6;
	}
	else if (trimmed.startsWith("t*")) {
		bool ok;
		linearFactor = trimmed.mid(2).toDouble(&ok);
		if (ok) {
			return 7;
		}
	}
	else if (trimmed.startsWith("t^")) {
		bool ok;
		powerFactor = trimmed.mid(2).toDouble(&ok);
		if (ok) {
			return 8;
		}
	}
	else if (trimmed == "0") {
		return 0;
	}

	qDebug() << "无法解析函数表达式：" << expr;
	return -1;
}

void ParametricCurveDialog::validateInput()
{
	// 验证函数表达式
	int x = parseFunction1(xFuncEdit->text());
	if (x ==-1) {
		QMessageBox::warning(this, "输入错误", "X坐标函数表达式无效。支持：t, sin(t), cos(t), tan(t), exp(t), sqrt(t), t*n, t^n, 0");
		return;
	}
	int y = parseFunction1(yFuncEdit->text());
	if (y == -1) {
		QMessageBox::warning(this, "输入错误", "Y坐标函数表达式无效。支持：t, sin(t), cos(t), tan(t), exp(t), sqrt(t), t*n, t^n, 0");
		return;
	}
	int z = parseFunction1(zFuncEdit->text());
	if (z == -1) {
		QMessageBox::warning(this, "输入错误", "Z坐标函数表达式无效。支持：t, sin(t), cos(t), tan(t), exp(t), sqrt(t), t*n, t^n, 0");
		return;
	}

	// 验证参数范围
	bool ok;
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

	// 验证点数
	int np = numPointsEdit->text().toInt(&ok);
	if (!ok || np <= 0) {
		QMessageBox::warning(this, "输入错误", "点数必须为正整数。");
		return;
	}

	tMin = tmin;
	tMax = tmax;
	numPoints = np;
	emit parametersSubmitted(m_uuid, x, y, z, tMin, tMax, numPoints);
}