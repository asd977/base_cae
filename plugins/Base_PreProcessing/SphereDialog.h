#ifndef SPHEREDIALOG_H
#define SPHEREDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <gp_Pnt.hxx>
#include "BasePage.h"

class SphereDialog : public BasePage
{
	Q_OBJECT

public:
	SphereDialog(QWidget *parent = nullptr);
	~SphereDialog();

	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

	// 获取用户输入的参数
	double getRadius() const;
	gp_Pnt getCenter() const;

signals:
	void parametersSubmitted(QString uuid,double radius, const gp_Pnt &position);
private slots:
	void onOkClicked();
	void onCancelClicked();

private:
	QLineEdit *radiusEdit;
	QLineEdit *xEdit;
	QLineEdit *yEdit;
	QLineEdit *zEdit;
};

#endif // SPHEREDIALOG_H