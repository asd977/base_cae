#pragma once

#include <QString>

struct FillPrepParams
{
    double xmid = 0.0;
    double width = 0.0;

    double xa = 0.0;
    double ya = 0.0;

    double xb = 0.0;
    double yb = 0.0;

    double tol2 = 0.3;
};

class FillPrepIO
{
  public:
    static QString filePath(const QString& projectDir);

    static bool ensureExists(const QString& projectDir, QString* error = nullptr);
    static bool read(const QString& projectDir, FillPrepParams& params, QString* error = nullptr);
    static bool write(const QString& projectDir, const FillPrepParams& params, QString* error = nullptr);
};
