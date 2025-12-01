#ifndef PARAMETRICCURVEDIALOG_H
#define PARAMETRICCURVEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
#include "BasePage.h"


class ParametricCurveDialog : public BasePage
{
	Q_OBJECT

public:
	explicit ParametricCurveDialog(QWidget* parent = nullptr);
	double(*getXFunc())(double); // 获取X坐标函数指针
	double(*getYFunc())(double); // 获取Y坐标函数指针
	double(*getZFunc())(double); // 获取Z坐标函数指针
	double getTMin() const; // 获取参数下限
	double getTMax() const; // 获取参数上限
	int getNumPoints() const; // 获取点数

	double(*parseFunction2(const int& type))(double);


	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;
signals:
	void parametersSubmitted(QString uuid, int x,
		int y,
		int z,
		double tmin, double tmax, int numPoints);

private slots:
	void validateInput(); // 验证输入

private:
	// 静态函数定义
	static double tFunc(double t) { return t; }
	static double sinFunc(double t) { return std::sin(t); }
	static double cosFunc(double t) { return std::cos(t); }
	static double tanFunc(double t) { return std::tan(t); }
	static double expFunc(double t) { return std::exp(t); }
	static double sqrtFunc(double t) { return t >= 0 ? std::sqrt(t) : 0.0; }
	static double linearFunc(double t) { return linearFactor * t; }
	static double powerFunc(double t) { return std::pow(t, powerFactor); }
	static double zeroFunc(double t) { return 0.0; }

	// 静态参数存储
	static double linearFactor; // 用于t*n
	static double powerFactor;  // 用于t^n

	double(*parseFunction(const QString& expr))(double); // 解析函数表达式

	int parseFunction1(const QString& expr);
	QLineEdit* xFuncEdit; // X坐标函数输入框
	QLineEdit* yFuncEdit; // Y坐标函数输入框
	QLineEdit* zFuncEdit; // Z坐标函数输入框
	QLineEdit* tMinEdit; // 参数下限输入框
	QLineEdit* tMaxEdit; // 参数上限输入框
	QLineEdit* numPointsEdit; // 点数输入框
	QPushButton* okButton; // 确定按钮
	double(*xFunc)(double); // 存储X坐标函数指针
	double(*yFunc)(double); // 存储Y坐标函数指针
	double(*zFunc)(double); // 存储Z坐标函数指针
	double tMin; // 存储参数下限
	double tMax; // 存储参数上限
	int numPoints; // 存储点数
};

#endif // PARAMETRICCURVEDIALOG_H