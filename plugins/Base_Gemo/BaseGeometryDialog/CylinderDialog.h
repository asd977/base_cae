#pragma once
#ifndef CYLINDERDIALOG_H
#define CYLINDERDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class CylinderDialog : public BasePage
{
    Q_OBJECT

  public:
    CylinderDialog(QWidget* parent = nullptr);
    ~CylinderDialog();

    void beforeLoad() {};
    void beforeUnload() {};
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    // 获取用户输入的参数
    double getRadius() const;
    double getHeight() const;
    gp_Pnt getBaseCenter() const;

  signals:
    void parametersSubmitted(QString uuid, double radius, double height, const gp_Pnt& position);
  private slots:
    void onOkClicked();
    void onCancelClicked();

  private:
    QLineEdit* radiusEdit;
    QLineEdit* heightEdit;
    QLineEdit* xEdit;
    QLineEdit* yEdit;
    QLineEdit* zEdit;
};

#endif // CYLINDERDIALOG_H
