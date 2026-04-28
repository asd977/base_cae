#pragma once

#include "DamParametricTypes.h"

#include <QString>

class DamParametricCalculator
{
  public:
    static bool calculate(int iobject, MsgJzDamParams& p, QString* error = nullptr);

  private:
    static bool readDouble(const QString& text, double& value, const QString& name,
                           QString* error);
    static bool readInt(const QString& text, int& value, const QString& name, QString* error);

    static QString f4(double value);
};
