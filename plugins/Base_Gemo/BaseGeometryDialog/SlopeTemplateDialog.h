#pragma once
#ifndef SLOPETEMPLATEDIALOG_H
#define SLOPETEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLabel>
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
    void onPreviewClicked();
    void onGenerateClicked();
  private:
    void updateDiagram();
    QLineEdit *hEdit,*benchWidthEdit,*slopeEdit,*lenEdit;
    QLabel* diagramLabel = nullptr;
};
#endif
