#ifndef PHYSICALFIELDACTIVATOR_H
#define PHYSICALFIELDACTIVATOR_H

#include <PluginInterface.h>
#include <qobject.h>

#include <QDebug>

#include "PhySicalField.h"

class PhysicalFieldActivator : public QObject, public PluginInterface {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID PluginInterface_iid)
  Q_INTERFACES(PluginInterface)
 public:
  PhysicalFieldActivator();

  QString pluginName() const override { return "PhySicalField"; }
  void start() override {
    qDebug() << "start PhySicalField";
    PhysicalField *phySicalField = new PhysicalField;
  }
  void stop() override { qDebug() << "Stopping..."; }
};

#endif  // PHYSICALFIELDACTIVATOR_H
