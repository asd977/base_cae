#pragma once
#ifndef LINEDIALOG_H
#define LINEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class LineDialog : public BasePage
{
	Q_OBJECT

public:
	explicit LineDialog(QWidget* parent = nullptr);
	gp_Pnt2d getStartPoint() const; // 获取起点
	gp_Pnt2d getEndPoint() const; // 获取终点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;
signals:
	void parametersSubmitted(QString uuid, gp_Pnt2d startPoint, gp_Pnt2d endPoint);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* startXEdit; // 起点X坐标输入框
	QLineEdit* startYEdit; // 起点Y坐标输入框
	QLineEdit* endXEdit; // 终点X坐标输入框
	QLineEdit* endYEdit; // 终点Y坐标输入框
	QPushButton* okButton; // 确定按钮
	gp_Pnt2d startPoint; // 存储起点
	gp_Pnt2d endPoint; // 存储终点
};

#endif // LINEDIALOG_H

