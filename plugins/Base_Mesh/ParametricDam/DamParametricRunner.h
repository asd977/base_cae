#pragma once

#include "DamParametricTypes.h"

#include <QString>

class DamParametricRunner
{
  public:
    static bool run(const DamParametricContext& ctx, QString* error = nullptr);

  private:
    static bool writeTaskFile(const QString& projectDir, QString* error);
    static QString executablePath(const DamParametricContext& ctx);
};
