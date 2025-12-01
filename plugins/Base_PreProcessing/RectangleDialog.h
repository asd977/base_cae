#pragma once
#ifndef RECTANGLEDIALOG_H
#define RECTANGLEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"
#include <PublicVariables.h>
#include <PublicConstants.h>

class RectangleDialog : public BasePage 
{
	Q_OBJECT

public:
	explicit RectangleDialog(QWidget* parent = nullptr);
	double getLength() const; // 获取长度
	double getWidth() const; // 获取宽度
	gp_Pnt2d getCenter() const; // 获取中心点


	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;
signals:
	void parametersSubmitted(QString uuid, double length, double width, const gp_Pnt2d &position);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* lengthEdit; // 长度输入框
	QLineEdit* widthEdit; // 宽度输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double length; // 存储长度
	double width; // 存储宽度
	gp_Pnt2d center; // 存储中心点
};

#endif // RECTANGLEDIALOG_H
