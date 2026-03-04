#pragma once
// 示例页面类，继承 BasePage
#include <QTextEdit>

#include "BasePage.h"
class ConcretePageA : public BasePage {
  Q_OBJECT
 private:
  QTextEdit* textEdit;

 public:
  ConcretePageA() : BasePage("ConcretePageA") {
    textEdit = new QTextEdit();
    textEdit->setText("123");
  }
  ~ConcretePageA() { delete textEdit; }
  void beforeLoad() override {};
  void beforeUnload() override {};
};
