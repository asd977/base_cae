#pragma once
#ifndef EARTHROCKFILLDAMDIALOG_H
#define EARTHROCKFILLDAMDIALOG_H

#include "BasePage.h"
#include <QLineEdit>

class EarthRockfillDamDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit EarthRockfillDamDialog(QWidget* parent = nullptr);
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void beforeLoad() override {}
    void beforeUnload() override {}

  private slots:
    void onGenerateClicked();

  private:
    void initUi();
    QLineEdit *hEdit, *crestEdit, *upEdit, *downEdit, *lenEdit, *coreWidthEdit;
};

#endif
