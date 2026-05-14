#ifndef RESULTRULEMANAGER_H
#define RESULTRULEMANAGER_H

#include <QVector>
#include <QString>

struct ResultDirectoryRule
{
    QString dirName;      // 工程目录下的相对结果目录，例如 SEEPCASE01 或 CASE01JACA/J03
    QString displayName;  // UI 显示名称
    QString description;  // 说明
};

struct ResultFileRule
{
    QString fileName;     // 结果文件名，例如 TEC_HP.DAT
    QString displayName;  // UI 显示名称
    QString description;  // 说明
    QString format;       // tecplot / vtu
};

class ResultRuleManager
{
public:
    static ResultRuleManager& instance();

    void ensureLoaded();

    QVector<ResultDirectoryRule> directoryRules() const;
    QVector<ResultFileRule> fileRules() const;

    void setDirectoryRules(const QVector<ResultDirectoryRule>& rules);
    void setFileRules(const QVector<ResultFileRule>& rules);

    bool load();
    bool save() const;
    bool resetToDefault();

    QString configFilePath() const;

private:
    ResultRuleManager() = default;

    QVector<ResultDirectoryRule> defaultDirectoryRules() const;
    QVector<ResultFileRule> defaultFileRules() const;

private:
    bool m_loaded{false};
    QVector<ResultDirectoryRule> m_directoryRules;
    QVector<ResultFileRule> m_fileRules;
};

#endif // RESULTRULEMANAGER_H
