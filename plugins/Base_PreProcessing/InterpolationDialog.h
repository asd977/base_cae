#pragma once
#ifndef INTERPOLATIONDIALOG_H
#define INTERPOLATIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QVector>
#include <gp_Pnt2d.hxx>
#include <vector>
#include "BasePage.h"

class InterpolationDialog : public BasePage
{
	Q_OBJECT

public:
	explicit InterpolationDialog(QWidget* parent = nullptr);
	std::vector<gp_Pnt2d> getPoints() const; // 获取点集

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, std::vector<gp_Pnt2d> points);

private slots:
	void addPoint(); // 添加点
	void removePoint(); // 删除点
	void validateInput(); // 验证输入

private:
	QVector<QLineEdit*> xEdits; // X坐标输入框列表
	QVector<QLineEdit*> yEdits; // Y坐标输入框列表
	QVBoxLayout* pointsLayout; // 点输入布局
	QPushButton* addButton; // 添加点按钮
	QPushButton* removeButton; // 删除点按钮
	QPushButton* okButton; // 确定按钮
	std::vector<gp_Pnt2d> points; // 存储点集
};

#endif // INTERPOLATIONDIALOG_H
