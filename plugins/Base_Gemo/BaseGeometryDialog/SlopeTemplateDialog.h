#pragma once
#ifndef SLOPETEMPLATEDIALOG_H
#define SLOPETEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLineEdit>
class SlopeTemplateDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit SlopeTemplateDialog(QWidget* parent = nullptr);
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void onGenerateClicked();
  private:
    QLineEdit *hEdit,*benchWidthEdit,*slopeEdit,*lenEdit;
};
#endif
