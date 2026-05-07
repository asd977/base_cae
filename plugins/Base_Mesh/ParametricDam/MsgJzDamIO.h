#pragma once

#include "DamParametricTypes.h"

#include <QString>

class MsgJzDamIO
{
  public:
    static QString filePath(const QString& projectDir);

    static bool ensureExists(const QString& projectDir, QString* error = nullptr);
    static bool read(const QString& projectDir, MsgJzDamParams& params, QString* error = nullptr);
    static bool write(const QString& projectDir, const MsgJzDamParams& params,
                      QString* error = nullptr);

  private:
    static QString cleanLine(QString line);
};
