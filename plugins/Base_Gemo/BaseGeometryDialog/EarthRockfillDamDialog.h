#pragma once
#ifndef EARTHROCKFILLDAMDIALOG_H
#define EARTHROCKFILLDAMDIALOG_H

#include "BasePage.h"
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QCheckBox>
#include <QComboBox>

class QResizeEvent;
class QShowEvent;

class EarthRockfillDamDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit EarthRockfillDamDialog(QWidget* parent = nullptr);
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void beforeLoad() override {}
    void beforeUnload() override {}

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private slots:
    void onPreviewClicked();
    void onGenerateClicked();
    void onLinkGeologyToggled(bool checked);

  private:
    void initUi();
    void updateDiagram();
    void refreshGeologyCombo();
    void applyTargetGeologyDefaults();
    void buildLayeredDam(double H, double Bt, double mu, double md, double L, int layers);

    QLineEdit *hEdit, *crestEdit, *upEdit, *downEdit, *lenEdit, *coreWidthEdit, *layerEdit;
    QCheckBox* m_linkGeology = nullptr;
    QComboBox* m_geologyCombo = nullptr;
    QLineEdit* m_posXEdit = nullptr;
    QLineEdit* m_posYEdit = nullptr;
    QLineEdit* m_posZEdit = nullptr;
    QLabel* diagramLabel = nullptr;
    QPixmap diagramPixmap;
};

#endif
