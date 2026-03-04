#pragma once
#ifndef BOXDIALOG_H
#define BOXDIALOG_H

#include <qobject.h>

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt.hxx>

#include "BasePage.h"

// 声明 gp_Pnt 为 Qt 元类型
// Q_DECLARE_METATYPE(gp_Pnt)
class BoxDialog : public BasePage {
  Q_OBJECT

 public:
  BoxDialog(QWidget *parent = nullptr);
  ~BoxDialog();

  void beforeLoad() {};
  void beforeUnload() {};
  QJsonObject saveJson() override;
  void loadJson(const QJsonObject &obj) override;

  gp_Pnt getPosition() const;

 private slots:
  void onOkClicked();

 private:
  QLineEdit *lengthEdit;
  QLineEdit *widthEdit;
  QLineEdit *heightEdit;
  QLineEdit *xEdit;
  QLineEdit *yEdit;
  QLineEdit *zEdit;
  void initUI();
};

#endif  // BOXDIALOG_H
