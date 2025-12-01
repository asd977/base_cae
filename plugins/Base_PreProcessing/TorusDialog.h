#pragma once
#ifndef TORUSDIALOG_H
#define TORUSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <gp_Pnt.hxx>
#include "BasePage.h"

class TorusDialog : public BasePage
{
	Q_OBJECT

public:
	TorusDialog(QWidget *parent = nullptr);
	~TorusDialog();

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

	// 获取用户输入的参数
	double getMajorRadius() const;
	double getMinorRadius() const;
	gp_Pnt getCenter() const;
signals:
	void parametersSubmitted(QString uuid, double majorRadius, double minorRadius, const gp_Pnt &position);
private slots:
	void onOkClicked();
	void onCancelClicked();

private:
	QLineEdit *majorRadiusEdit;
	QLineEdit *minorRadiusEdit;
	QLineEdit *xEdit;
	QLineEdit *yEdit;
	QLineEdit *zEdit;
};

#endif // TORUSDIALOG_H

