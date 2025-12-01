#ifndef HYPERBOLADIALOG_H
#define HYPERBOLADIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class HyperbolaDialog : public BasePage
{
	Q_OBJECT

public:
	explicit HyperbolaDialog(QWidget* parent = nullptr);
	double getMajorRadius() const; // 获取实半轴
	double getFocalDistance() const; // 获取焦距
	double getTMin() const; // 获取参数下限
	double getTMax() const; // 获取参数上限
	int getNumPoints() const; // 获取点数
	gp_Pnt2d getCenter() const; // 获取中心点
	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

private slots:
	void validateInput(); // 验证输入
signals:
	void parametersSubmitted(QString uuid, double major, double focal, double min, double max, int points);

private:
	QLineEdit* majorRadiusEdit; // 实半轴输入框
	QLineEdit* focalDistanceEdit; // 焦距输入框
	QLineEdit* tMinEdit; // 参数下限输入框
	QLineEdit* tMaxEdit; // 参数上限输入框
	QLineEdit* numPointsEdit; // 点数输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double majorRadius; // 存储实半轴
	double focalDistance; // 存储焦距
	double tMin; // 存储参数下限
	double tMax; // 存储参数上限
	int numPoints; // 存储点数
	gp_Pnt2d center; // 存储中心点
};

#endif // HYPERBOLADIALOG_H