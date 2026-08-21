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

    // 域边界缓存按需生成：默认云图只需要最外壳 Surface，
    // 只有用户真正进行材料/分层筛选时才创建 domains.vtp。
    // 这样大网格首次进入后处理时不会提前扫描全部体单元构造内部域界面。
    static bool ensureDomainSurfaceCache(const QString& resultFilePath,
                                         QString* error = nullptr);
};
