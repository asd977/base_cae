#include "ServiceRegistry.h"

ServiceRegistry& ServiceRegistry::instance() {
  static ServiceRegistry registry;
  return registry;
}
