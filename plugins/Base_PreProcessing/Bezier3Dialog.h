#pragma once
#ifndef BEZIER3DIALOG_H
#define BEZIER3DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include <vector>
#include "BasePage.h"


class Bezier3Dialog : public BasePage
{
	Q_OBJECT

public:
	explicit Bezier3Dialog(QWidget* parent = nullptr);
	std::vector<gp_Pnt2d> getControlPoints() const; // 获取控制点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, std::vector<gp_Pnt2d> controlPoints);

private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* point0XEdit; // 控制点0 X坐标
	QLineEdit* point0YEdit; // 控制点0 Y坐标
	QLineEdit* point1XEdit; // 控制点1 X坐标
	QLineEdit* point1YEdit; // 控制点1 Y坐标
	QLineEdit* point2XEdit; // 控制点2 X坐标
	QLineEdit* point2YEdit; // 控制点2 Y坐标
	QLineEdit* point3XEdit; // 控制点3 X坐标
	QLineEdit* point3YEdit; // 控制点3 Y坐标
	QPushButton* okButton; // 确定按钮
	std::vector<gp_Pnt2d> controlPoints; // 存储控制点
};

#endif // BEZIER3DIALOG_H

