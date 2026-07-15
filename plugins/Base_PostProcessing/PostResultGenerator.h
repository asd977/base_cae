#pragma once

#include <QString>
#include <QStringList>

class PostResultGenerator
{
  public:
    struct Options
    {
        QString projectDir;
        QString resultDir;
        QString outputDir;
        int objectType = 0;
        bool overwrite = true;
        bool writeLegacyAuxiliaryFiles = true;
    };

    struct Result
    {
        bool ok = false;
        QString error;
        QStringList generatedFiles;
        QStringList skippedFiles;
        QStringList warnings;
    };

    static Result generate(const Options& options);
};
