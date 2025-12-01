#pragma once
#ifndef SQUAREDIALOG_H
#define SQUAREDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class SquareDialog : public BasePage
{
	Q_OBJECT

public:
	explicit SquareDialog(QWidget* parent = nullptr);
	double getSideLength() const; // 获取边长
	gp_Pnt2d getCenter() const; // 获取中心点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, double sideLength, const gp_Pnt2d &position);

private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* sideLengthEdit; // 边长输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double sideLength; // 存储边长
	gp_Pnt2d center; // 存储中心点
};

#endif // SQUAREDIALOG_H
