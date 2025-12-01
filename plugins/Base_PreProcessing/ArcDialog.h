#pragma once
#ifndef ARCDIALOG_H
#define ARCDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"
#include <PublicVariables.h>
#include <PublicConstants.h>

class ArcDialog : public BasePage
{
	Q_OBJECT

public:
	explicit ArcDialog(QWidget* parent = nullptr);
	double getRadius() const; // 获取半径
	double getStartAngle() const; // 获取起始角度
	double getEndAngle() const; // 获取终止角度
	gp_Pnt2d getCenter() const; // 获取中心点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;


signals:
	void parametersSubmitted(QString uuid, double radius, double startAngle, double endAngle, const gp_Pnt2d &position);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* radiusEdit; // 半径输入框
	QLineEdit* startAngleEdit; // 起始角度输入框
	QLineEdit* endAngleEdit; // 终止角度输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double radius; // 存储半径
	double startAngle; // 存储起始角度
	double endAngle; // 存储终止角度
	gp_Pnt2d center; // 存储中心点
};

#endif // ARCDIALOG_H

