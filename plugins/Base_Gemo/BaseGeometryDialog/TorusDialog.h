#pragma once
#ifndef TORUSDIALOG_H
#define TORUSDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class TorusDialog : public BasePage {
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
  void parametersSubmitted(QString uuid, double majorRadius, double minorRadius,
                           const gp_Pnt &position);
 private slots:
  void onOkClicked();

 private:
  QLineEdit *majorRadiusEdit;
  QLineEdit *minorRadiusEdit;
  QLineEdit *xEdit;
  QLineEdit *yEdit;
  QLineEdit *zEdit;
  void initUI();
};

#endif  // TORUSDIALOG_H
