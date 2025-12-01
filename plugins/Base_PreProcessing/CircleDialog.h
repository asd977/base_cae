#ifndef CIRCLEDIALOG_H
#define CIRCLEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt2d.hxx>
#include "BasePage.h"

class CircleDialog : public BasePage
{
	Q_OBJECT

public:
	explicit CircleDialog(QWidget* parent = nullptr);
	double getRadius() const; // 获取半径
	gp_Pnt getCenter() const; // 获取中心点

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

signals:
	void parametersSubmitted(QString uuid, double radius, const gp_Pnt &position);

private slots:
	void validateInput(); // 验证输入

private:
	QLineEdit* radiusEdit; // 半径输入框
	QLineEdit* centerXEdit; // 中心X坐标输入框
	QLineEdit* centerYEdit; // 中心Y坐标输入框
	QLineEdit* centerZEdit; // 中心Y坐标输入框
	QPushButton* okButton; // 确定按钮
	double radius; // 存储半径
	gp_Pnt center; // 存储中心点
};

#endif // CIRCLEDIALOG_H