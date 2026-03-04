#ifndef PROJECTMANAGERACTIVATOR_H
#define PROJECTMANAGERACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>

#include "ProjectManager.h"

class ProjectManagerActivator : public QObject, public PluginInterface {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID PluginInterface_iid)
  Q_INTERFACES(PluginInterface)

 public:
  ProjectManagerActivator();

  QString pluginName() const override { return "ProjectManager"; }
  ProjectManager *projectManager;
  void start() override {
    qDebug() << "start ProjectManager";
    projectManager = new ProjectManager;
  }
  void stop() override {
    qDebug() << "Stopping...";
    delete projectManager;
  }
};

#endif  // UIFRAMEACTIVATOR_H
