#pragma once
#ifndef BOXDIALOG_H
#define BOXDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <gp_Pnt.hxx>
#include <qobject.h>
#include "BasePage.h"

// 声明 gp_Pnt 为 Qt 元类型
//Q_DECLARE_METATYPE(gp_Pnt)
class BoxDialog : public BasePage
{
	Q_OBJECT

public:
	BoxDialog(QWidget *parent = nullptr);
	~BoxDialog();

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

	double getLength() const;
	double getWidth() const;
	double getHeight() const;
	double getX() const;
	double getY() const;
	double getZ() const;

	void setLength(QString value);
	void setWidth(QString value);
	void setHeight(QString value);
	void setX(QString value);
	void setY(QString value);
	void setZ(QString value);
	gp_Pnt getPosition() const;
signals:
	void parametersSubmitted(QString uuid, double length, double width, double height, const gp_Pnt &position); // 信号，携带长方体参数
private slots:
	void onOkClicked();

private:
	QLineEdit *lengthEdit;
	QLineEdit *widthEdit;
	QLineEdit *heightEdit;
	QLineEdit *xEdit;
	QLineEdit *yEdit;
	QLineEdit *zEdit;
};

#endif // BOXDIALOG_H

