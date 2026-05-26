#pragma once
#ifndef CAVERNTEMPLATEDIALOG_H
#define CAVERNTEMPLATEDIALOG_H
#include "BasePage.h"
#include <QLabel>
#include <QLineEdit>

class CavernTemplateDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit CavernTemplateDialog(QWidget* parent = nullptr);
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void onPreviewClicked();
    void onGenerateClicked();
  private:
    void updateDiagram();
    void buildLayeredCavern(double span,double wallH,double archH,double len,double lining);
    QLineEdit *spanEdit,*wallHEdit,*archHEdit,*lenEdit,*liningEdit;
    QLabel* diagramLabel = nullptr;
};
#endif
