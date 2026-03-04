#pragma once
class QAction;
class QWidget;
class QString;

struct UiFrameService {
  virtual QAction *addWidget(QWidget *widget, QString name) = 0;
  virtual QAction *addWidgetForTabwidget(QWidget *widget, QString name) = 0;
};
