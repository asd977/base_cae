#ifndef PLUGININTERFACE_H
#define PLUGININTERFACE_H

#include <QString>
#include <QtPlugin>

class PluginInterface {
 public:
  virtual QString pluginName() const = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
};

#define PluginInterface_iid "org.simu.PluginInterface"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)

#endif  // PLUGININTERFACE_H
