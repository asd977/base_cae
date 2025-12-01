#ifndef PARABOLADIALOG_H
#define PARABOLADIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class ParabolaDialog : public BasePage
{
	Q_OBJECT

public:
	explicit ParabolaDialog(QWidget* parent = nullptr);
	double getFocal() const; // 获取焦距
	double getTMin() const; // 获取参数下限
	double getTMax() const; // 获取参数上限
	int getNumPoints() const; // 获取点数
	gp_Pnt2d getVertex() const; // 获取顶点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, double focal, double tMin, double tMax, int numPoints);

private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* focalEdit; // 焦距输入框
	QLineEdit* tMinEdit; // 参数下限输入框
	QLineEdit* tMaxEdit; // 参数上限输入框
	QLineEdit* numPointsEdit; // 点数输入框
	QLineEdit* vertexXEdit; // 顶点X坐标输入框
	QLineEdit* vertexYEdit; // 顶点Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double focal; // 存储焦距
	double tMin; // 存储参数下限
	double tMax; // 存储参数上限
	int numPoints; // 存储点数
	gp_Pnt2d vertex; // 存储顶点
};

#endif // PARABOLADIALOG_H