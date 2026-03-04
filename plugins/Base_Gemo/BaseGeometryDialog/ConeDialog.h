#pragma once
#ifndef CONEDIALOG_H
#define CONEDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class ConeDialog : public BasePage {
  Q_OBJECT

 public:
  ConeDialog(QWidget *parent = nullptr);
  ~ConeDialog();

  void beforeLoad() {};
  void beforeUnload() {};
  QJsonObject saveJson() override;
  void loadJson(const QJsonObject &obj) override;
  // 获取用户输入的参数
  double getBottomRadius() const;
  double getTopRadius() const;
  double getHeight() const;
  gp_Pnt getBaseCenter() const;

 private slots:
  void onOkClicked();
  void onCancelClicked();

 private:
  QLineEdit *bottomRadiusEdit;
  QLineEdit *topRadiusEdit;
  QLineEdit *heightEdit;
  QLineEdit *xEdit;
  QLineEdit *yEdit;
  QLineEdit *zEdit;
  void initUI();
};

#endif  // CONEDIALOG_H
