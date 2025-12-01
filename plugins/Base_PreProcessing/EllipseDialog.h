#pragma once
#ifndef ELLIPSEDIALOG_H
#define ELLIPSEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class EllipseDialog : public BasePage
{
	Q_OBJECT

public:
	explicit EllipseDialog(QWidget* parent = nullptr);
	double getMajorRadius() const; // 获取长轴半径
	double getMinorRadius() const; // 获取短轴半径
	gp_Pnt2d getCenter() const; // 获取中心点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, double majorRadius, double minorRadius,const gp_Pnt2d &position);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* majorRadiusEdit; // 长轴半径输入框
	QLineEdit* minorRadiusEdit; // 短轴半径输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double majorRadius; // 存储长轴半径
	double minorRadius; // 存储短轴半径
	gp_Pnt2d center; // 存储中心点
};

#endif // ELLIPSEDIALOG_H
