#pragma once
#ifndef POLYGONDIALOG_H
#define POLYGONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"


class PolygonDialog : public BasePage
{
	Q_OBJECT

public:
	explicit PolygonDialog(QWidget* parent = nullptr);
	double getRadius() const; // 获取半径
	int getSides() const; // 获取边数
	gp_Pnt2d getCenter() const; // 获取中心点


	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, double radius, int sides, const gp_Pnt2d &position);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* radiusEdit; // 半径输入框
	QLineEdit* sidesEdit; // 边数输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double radius; // 存储半径
	int sides; // 存储边数
	gp_Pnt2d center; // 存储中心点
};

#endif // POLYGONDIALOG_H

