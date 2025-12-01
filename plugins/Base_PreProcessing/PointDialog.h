#pragma once
#ifndef POINTDIALOG_H
#define POINTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"


class PointDialog : public BasePage
{
	Q_OBJECT

public:
	explicit PointDialog(QWidget* parent = nullptr);
	gp_Pnt2d getPoint() const; // 获取点坐标


	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;
signals:
    void parametersSubmitted(QString uuid, const gp_Pnt2d &position);
private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* xEdit; // X坐标输入框
	QLineEdit* yEdit; // Y坐标输入框
	QPushButton* okButton; // 确定按钮
	gp_Pnt2d point; // 存储点坐标
};

#endif // POINTDIALOG_H

