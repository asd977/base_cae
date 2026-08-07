#pragma once

#include "PostResultAutoPrepare.h"

#include <QComboBox>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QSignalBlocker>
#include <QString>

namespace PostResultSelectionUtils
{


inline QString absoluteSourcePath(const QString& projectDir, const QString& sourcePath)
{
    const QString path = QDir::fromNativeSeparators(sourcePath.trimmed());
    if (path.isEmpty())
        return QString();
    if (QDir::isAbsolutePath(path))
        return QDir(path).absolutePath();
    return QDir(projectDir).absoluteFilePath(path);
}

inline int sourceComboIndex(const QComboBox* combo, const QString& projectDir,
                            const QString& sourcePath)
{
    return combo ? combo->findData(absoluteSourcePath(projectDir, sourcePath)) : -1;
}

inline QString selectedAnalysisKey(const QComboBox* combo)
{
    return combo ? combo->currentData().toString() : QString();
}

inline void populateAnalysisCombo(
    QComboBox* combo,
    const QVector<PostResultAutoPrepare::ResultGroup>& groups,
    const QString& preferredKey)
{
    if (!combo)
        return;

    const QSignalBlocker blocker(combo);
    combo->clear();

    QSet<QString> seen;
    for (const auto& group : groups)
    {
        if (seen.contains(group.analysisKey))
            continue;
        seen.insert(group.analysisKey);
        const QString name = group.analysisName.trimmed().isEmpty()
                                 ? QStringLiteral("当前计算")
                                 : group.analysisName.trimmed();
        combo->addItem(name, group.analysisKey);
    }

    int index = preferredKey.isEmpty() ? -1 : combo->findData(preferredKey);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);
}

inline void populateAnalysisCombo(QComboBox* combo, const QString& projectDir,
                                  const QString& preferredKey)
{
    populateAnalysisCombo(combo, PostResultAutoPrepare::resultGroups(projectDir),
                          preferredKey);
}

inline void populateStageCombo(
    QComboBox* combo,
    const QVector<PostResultAutoPrepare::ResultGroup>& groups,
    const QString& analysisKey,
    const QString& preferredSourcePath)
{
    if (!combo)
        return;

    const QSignalBlocker blocker(combo);
    combo->clear();

    for (const auto& group : groups)
    {
        if (!analysisKey.isEmpty() && group.analysisKey != analysisKey)
            continue;
        const QString name = group.stageName.trimmed().isEmpty()
                                 ? group.displayName
                                 : group.stageName;
        combo->addItem(name, group.sourcePath);
    }

    int index = preferredSourcePath.isEmpty()
                    ? -1
                    : combo->findData(preferredSourcePath);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);
}

inline void populateStageCombo(QComboBox* combo, const QString& projectDir,
                               const QString& analysisKey,
                               const QString& preferredSourcePath)
{
    populateStageCombo(combo, PostResultAutoPrepare::resultGroups(projectDir),
                       analysisKey,
                       absoluteSourcePath(projectDir, preferredSourcePath));
}

inline QString analysisKeyForSource(
    const QVector<PostResultAutoPrepare::ResultGroup>& groups,
    const QString& sourcePath)
{
    for (const auto& group : groups)
    {
        if (QDir::cleanPath(group.sourcePath).compare(
                QDir::cleanPath(sourcePath), Qt::CaseInsensitive) == 0)
            return group.analysisKey;
    }
    return QString();
}

inline QString analysisKeyForSource(const QString& projectDir,
                                    const QString& sourcePath)
{
    return analysisKeyForSource(
        PostResultAutoPrepare::resultGroups(projectDir),
        absoluteSourcePath(projectDir, sourcePath));
}

inline QString selectedFrameKey(const QComboBox* combo)
{
    return combo ? combo->currentData().toString() : QString();
}

inline void populateTimeCombo(QComboBox* combo, const QString& projectDir,
                              const QString& sourceDir, const QString& preferredKey,
                              bool includeVtu)
{
    if (!combo)
        return;

    const QVector<PostResultAutoPrepare::ResultFrame> frames =
        PostResultAutoPrepare::resultFrames(projectDir, sourceDir, includeVtu);

    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const auto& frame : frames)
        combo->addItem(frame.displayName, frame.key);

    int index = preferredKey.isNull() ? -1 : combo->findData(preferredKey);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);

    const bool hasSequence =
        frames.size() > 1 || (!frames.isEmpty() && !frames.constFirst().key.isEmpty());
    combo->setEnabled(hasSequence);
    combo->setToolTip(hasSequence ? QStringLiteral("选择结果时刻")
                                  : QStringLiteral("当前结果没有时间序列"));
}

inline void populateResultCombo(QComboBox* combo, const QString& projectDir,
                                const QString& sourceDir, const QString& frameKey,
                                const QString& preferredFile, bool includeVtu)
{
    if (!combo)
        return;

    const QVector<PostResultAutoPrepare::ResultFile> files =
        PostResultAutoPrepare::resultFilesForFrame(projectDir, sourceDir,
                                                   frameKey, includeVtu);
    const QSignalBlocker blocker(combo);
    combo->clear();

    QHash<QString, int> names;
    for (const auto& file : files)
    {
        QString name = file.displayName.trimmed().isEmpty()
                           ? QStringLiteral("结果数据")
                           : file.displayName.trimmed();
        const int count = names.value(name) + 1;
        names.insert(name, count);
        if (count > 1)
            name += QStringLiteral(" (%1)").arg(count);
        combo->addItem(name, file.filePath);
    }

    int index = preferredFile.isEmpty() ? -1 : combo->findData(preferredFile);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);
}
} // namespace PostResultSelectionUtils
