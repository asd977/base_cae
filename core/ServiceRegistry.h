#ifndef SERVICEREGISTRY_H
#define SERVICEREGISTRY_H

#include <core_global.h>

#include <QDebug>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

class CORE_EXPORT ServiceRegistry {
 public:
  // 获取全局唯一实例
  static ServiceRegistry& instance();

  // 注册服务
  template <typename T>
  void registerService(T* service) {
    std::type_index index(typeid(T));
    services[index] = static_cast<void*>(service);
    qDebug() << "Registered service:" << index.name() << service;
  }

  // 获取服务
  template <typename T>
  T* getService() {
    std::type_index index(typeid(T));
    auto it = services.find(index);
    if (it != services.end()) {
      return static_cast<T*>(it->second);
    }
    return nullptr;
  }

 private:
  ServiceRegistry() {}
  ~ServiceRegistry() {}

  std::unordered_map<std::type_index, void*> services;
};

#endif  // SERVICEREGISTRY_H
