#pragma once
#ifndef GRAVITYDAMDIALOG_H
#define GRAVITYDAMDIALOG_H

#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
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

  private:
    void resizeEvent(QResizeEvent* event) override;
    enum EngineeringTemplate
    {
        GravityDamTemplate = 0,
        EarthRockfillDamTemplate = 1,
        SlopeTemplate = 2,
        CavernTemplate = 3,
        DamFoundationLayerTemplate = 4,
        CurtainGroutingTemplate = 5,
        SupportStructureTemplate = 6
    };
    void initUI();
    bool readParameters(double& damHeight, double& crestWidth, double& upSlope, double& downSlope,
                        double& damLength, int& layerCount, double& x, double& y, double& z,
                        EngineeringTemplate& tpl) const;
    void applyPreviewText(double damHeight, double crestWidth, double upSlope, double downSlope,
                          int layerCount, EngineeringTemplate tpl) const;
    void buildDamBody(double damHeight, double crestWidth, double upSlope, double downSlope,
                      double damLength, int layerCount, double x, double y, double z,
                      EngineeringTemplate tpl);
    QVector<QPointF> buildSectionPoints(double damHeight, double crestWidth, double upSlope,
                                        double downSlope, EngineeringTemplate tpl) const;
    void updateIllustration(double damHeight, double crestWidth, double upSlope, double downSlope,
                            int layerCount, EngineeringTemplate tpl);

  private:
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
    QPixmap diagramPixmap;
};

#endif // GRAVITYDAMDIALOG_H
