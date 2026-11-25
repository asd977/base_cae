#pragma once
#ifndef SLOPETEMPLATEDIALOG_H
#define SLOPETEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QCheckBox>
#include <QComboBox>

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
    void onPreviewClicked();
    void onGenerateClicked();
    void onLinkGeologyToggled(bool checked);
  private:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void updateDiagram();
    void refreshGeologyCombo();
    void applyTargetGeologyDefaults();
    void buildLayeredSlope(double H, double benchW, double slope, double L, int benchCount);
    QLineEdit *hEdit,*benchWidthEdit,*slopeEdit,*lenEdit,*benchCountEdit;
    QCheckBox* m_linkGeology = nullptr;
    QComboBox* m_geologyCombo = nullptr;
    QLineEdit* m_posXEdit = nullptr;
    QLineEdit* m_posYEdit = nullptr;
    QLineEdit* m_posZEdit = nullptr;
    QLabel* diagramLabel = nullptr;
    QPixmap diagramPixmap;
};
#endif
