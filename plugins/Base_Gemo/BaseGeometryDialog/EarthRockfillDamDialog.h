#pragma once
#ifndef EARTHROCKFILLDAMDIALOG_H
#define EARTHROCKFILLDAMDIALOG_H

#include "BasePage.h"
#include <QLabel>
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
    void onPreviewClicked();
    void onGenerateClicked();

  private:
    void initUi();
    void updateDiagram();
    void buildLayeredDam(double H, double Bt, double mu, double md, double L, int layers);

    QLineEdit *hEdit, *crestEdit, *upEdit, *downEdit, *lenEdit, *coreWidthEdit, *layerEdit;
    QLabel* diagramLabel = nullptr;
};

#endif
