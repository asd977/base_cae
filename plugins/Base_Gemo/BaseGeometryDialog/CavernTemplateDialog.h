#pragma once
#ifndef CAVERNTEMPLATEDIALOG_H
#define CAVERNTEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLineEdit>
class CavernTemplateDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit CavernTemplateDialog(QWidget* parent = nullptr);
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void onGenerateClicked();
  private:
    QLineEdit *spanEdit,*wallH,*archH,*lenEdit;
};
#endif
