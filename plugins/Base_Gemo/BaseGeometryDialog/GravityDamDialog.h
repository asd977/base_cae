#pragma once
#ifndef GRAVITYDAMDIALOG_H
#define GRAVITYDAMDIALOG_H

#include <QComboBox>
#include <QLineEdit>
#include <QPointF>

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
    void onDamTypeChanged(int index);

  private:
    enum DamType
    {
        TrapezoidDam = 0,
        UpstreamVerticalDam = 1,
        DownstreamStepDam = 2
    };
    void initUI();
    bool readParameters(double& damHeight, double& crestWidth, double& upSlope, double& downSlope,
                        double& damLength, int& layerCount, double& x, double& y, double& z,
                        DamType& damType) const;
    void applyPreviewText(double damHeight, double crestWidth, double upSlope, double downSlope,
                          int layerCount, DamType damType) const;
    void buildDamBody(double damHeight, double crestWidth, double upSlope, double downSlope,
                      double damLength, int layerCount, double x, double y, double z, DamType damType);
    QVector<QPointF> buildSectionPoints(double damHeight, double crestWidth, double upSlope,
                                        double downSlope, DamType damType) const;
    void updateIllustration(double damHeight, double crestWidth, double upSlope, double downSlope,
                            int layerCount, DamType damType);

  private:
    QComboBox* damTypeCombo = nullptr;
    QLineEdit* heightEdit = nullptr;
    QLineEdit* crestWidthEdit = nullptr;
    QLineEdit* upstreamSlopeEdit = nullptr;
    QLineEdit* downstreamSlopeEdit = nullptr;
    QLineEdit* lengthEdit = nullptr;
    QLineEdit* layerCountEdit = nullptr;
    QLineEdit* xEdit = nullptr;
    QLineEdit* yEdit = nullptr;
    QLineEdit* zEdit = nullptr;
    QLineEdit* preview2DEdit = nullptr;
    QLabel* diagramLabel = nullptr;
};

#endif // GRAVITYDAMDIALOG_H
