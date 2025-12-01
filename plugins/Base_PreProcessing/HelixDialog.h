#pragma once
#ifndef HELIXDIALOG_H
#define HELIXDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <gp_Pnt.hxx>
#include "BasePage.h"

class HelixDialog : public BasePage
{
	Q_OBJECT

public:
	HelixDialog(QWidget *parent = nullptr);
	~HelixDialog();

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

	// 获取用户输入的参数
	double getRadius() const;
	double getHeight() const;
	double getPitch() const;
	gp_Pnt getBaseCenter() const;

signals:
	void parametersSubmitted(QString uuid, double radius, double height, double pitch, const gp_Pnt& position);

private slots:
	void onOkClicked();
	void onCancelClicked();

private:
	QLineEdit *radiusEdit;
	QLineEdit *heightEdit;
	QLineEdit *pitchEdit;
	QLineEdit *xEdit;
	QLineEdit *yEdit;
	QLineEdit *zEdit;
};

#endif // HELIXDIALOG_H

