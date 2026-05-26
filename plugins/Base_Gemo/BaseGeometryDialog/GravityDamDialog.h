#pragma once
#ifndef GRAVITYDAMDIALOG_H
#define GRAVITYDAMDIALOG_H

#include <QLineEdit>

#include "BasePage.h"

class GravityDamDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit GravityDamDialog(QWidget* parent = nullptr);
    ~GravityDamDialog() override;

    void beforeLoad() override
    {
    }
    void beforeUnload() override
    {
    }
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onPreview2DClicked();
    void onGenerate3DClicked();

  private:
    void initUI();
    bool readParameters(double& damHeight, double& crestWidth, double& upSlope, double& downSlope,
                        double& damLength, double& x, double& y, double& z) const;
    void applyPreviewText(double damHeight, double crestWidth, double upSlope, double downSlope) const;
    void buildDamBody(double damHeight, double crestWidth, double upSlope, double downSlope,
                      double damLength, double x, double y, double z);

  private:
    QLineEdit* heightEdit = nullptr;
    QLineEdit* crestWidthEdit = nullptr;
    QLineEdit* upstreamSlopeEdit = nullptr;
    QLineEdit* downstreamSlopeEdit = nullptr;
    QLineEdit* lengthEdit = nullptr;
    QLineEdit* xEdit = nullptr;
    QLineEdit* yEdit = nullptr;
    QLineEdit* zEdit = nullptr;
    QLineEdit* preview2DEdit = nullptr;
};

#endif // GRAVITYDAMDIALOG_H
