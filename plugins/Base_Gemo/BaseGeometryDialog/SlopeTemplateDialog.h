#pragma once
#ifndef SLOPETEMPLATEDIALOG_H
#define SLOPETEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>

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
  private:
    void resizeEvent(QResizeEvent* event) override;
    void updateDiagram();
    void buildLayeredSlope(double H, double benchW, double slope, double L, int benchCount);
    QLineEdit *hEdit,*benchWidthEdit,*slopeEdit,*lenEdit,*benchCountEdit;
    QLabel* diagramLabel = nullptr;
    QPixmap diagramPixmap;
};
#endif
