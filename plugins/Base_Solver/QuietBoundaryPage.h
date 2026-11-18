#pragma once
#include "QuietBoundarySelector.h"
#include <QCheckBox>
#include <QFormLayout>

// The parent calculation page owns the enable switch; this node owns the region.
class QuietBoundaryPage : public BasePage
{
 Q_OBJECT
public:
 QuietBoundaryPage(QCheckBox* enabled,QuietBoundarySelector* selection)
     :BasePage("QuietBoundary"),selector(selection)
 {
  auto detach=[](QWidget* widget){
   if(auto* form=qobject_cast<QFormLayout*>(widget->parentWidget()->layout())){
    auto row=form->takeRow(widget);
    if(row.labelItem){delete row.labelItem->widget();delete row.labelItem;}
    delete row.fieldItem;
   }
  };
  detach(selector);
  auto* layout=new QVBoxLayout(this);
  enabled->setText(QStringLiteral("启用粘弹性边界"));
  auto* hint=new QLabel(QStringLiteral("仅动力分析生效，用于吸收模型截断边界的波动。"),this);hint->setWordWrap(true);layout->addWidget(hint);
  layout->addWidget(selector);layout->addStretch();
 }
 void beforeLoad()override{selector->start();}
 void beforeUnload()override{selector->stop();}
 QJsonObject saveJson()override{return {{"selection",selector->save()}};}
 void loadJson(const QJsonObject& state)override{
  selector->load(state["selection"].toObject(),false);
 }
private:
 QuietBoundarySelector* selector;
};
