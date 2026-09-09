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

    // 旧接口仅为源码兼容保留。MATYPE 域已经写入 VTM Block，调用此函数
    // 不会再扫描结果单元或生成 domains.vtp。
    static bool ensureDomainSurfaceCache(const QString& resultFilePath,
                                         QString* error = nullptr);
};
