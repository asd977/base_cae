#include "PostAnimationSequenceModel.h"

#include "PostResultAutoPrepare.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <utility>

namespace
{
QString normalizedAnalysisKey(const PostResultAutoPrepare::ResultGroup& group)
{
    if (!group.analysisKey.trimmed().isEmpty())
        return group.analysisKey.trimmed();
    return QDir(group.sourcePath).absolutePath().toCaseFolded();
}

QString normalizedAnalysisName(const PostResultAutoPrepare::ResultGroup& group)
{
    if (!group.analysisName.trimmed().isEmpty())
        return group.analysisName.trimmed();
    if (!group.displayName.trimmed().isEmpty())
        return group.displayName.trimmed();
    return QStringLiteral("当前计算");
}

QString normalizedStageName(const PostResultAutoPrepare::ResultGroup& group)
{
    return group.stageName.trimmed().isEmpty() ? group.displayName.trimmed()
                                               : group.stageName.trimmed();
}

QString normalizedResultKey(const PostResultAutoPrepare::ResultFile& file)
{
    const QString kind = file.kind.trimmed().toCaseFolded();
    const QString name = file.displayName.trimmed().toCaseFolded();
    return kind + QLatin1Char('|') + name;
}

QString resultName(const PostResultAutoPrepare::ResultFile& file)
{
    return file.displayName.trimmed().isEmpty() ? QStringLiteral("结果数据")
                                                 : file.displayName.trimmed();
}

QString stageCategory(const QString& stageName)
{
    if (stageName.contains(QStringLiteral("开挖")) ||
        stageName.contains(QStringLiteral("支护")) ||
        stageName.contains(QStringLiteral("衬砌")))
        return QStringLiteral("洞室施工过程");
    if (stageName.contains(QStringLiteral("填筑")))
        return QStringLiteral("分层填筑过程");
    if (stageName.contains(QStringLiteral("蓄水")))
        return QStringLiteral("分级蓄水过程");
    if (stageName.contains(QStringLiteral("流变")))
        return QStringLiteral("流变过程");
    if (stageName.contains(QStringLiteral("降水")))
        return QStringLiteral("降水过程");
    if (stageName.contains(QStringLiteral("湿化")))
        return QStringLiteral("湿化过程");
    if (stageName.contains(QStringLiteral("冻融")) ||
        stageName.contains(QStringLiteral("结冰")))
        return QStringLiteral("冻融过程");
    if (stageName.contains(QStringLiteral("动力")) ||
        stageName.contains(QStringLiteral("地震")))
        return QStringLiteral("动力响应过程");
    return QStringLiteral("计算阶段");
}

QString timeRangeName(const PostResultAutoPrepare::ResultGroup& group)
{
    QString name = normalizedStageName(group);
    if (name.isEmpty())
        name = QStringLiteral("时间过程");
    if (!name.contains(QStringLiteral("时间序列")))
        name += QStringLiteral("（时间序列）");
    return name;
}

void sortAndDeduplicateFrames(PostAnimationSequenceModel::Sequence& sequence)
{
    std::sort(sequence.frames.begin(), sequence.frames.end(),
              [](const PostAnimationSequenceModel::Frame& left,
                 const PostAnimationSequenceModel::Frame& right)
    {
        if (left.order != right.order)
            return left.order < right.order;
        if (left.value != right.value)
            return left.value < right.value;
        return left.label.localeAwareCompare(right.label) < 0;
    });

    QSet<QString> seenFrameKeys;
    QSet<QString> seenPaths;
    sequence.frames.erase(
        std::remove_if(sequence.frames.begin(), sequence.frames.end(),
                       [&](const PostAnimationSequenceModel::Frame& frame)
    {
        const QString path = QFileInfo(frame.filePath).absoluteFilePath().toCaseFolded();
        const QString frameKey = frame.frameKey.trimmed().toCaseFolded();
        if ((!frameKey.isEmpty() && seenFrameKeys.contains(frameKey)) ||
            seenPaths.contains(path))
            return true;
        if (!frameKey.isEmpty())
            seenFrameKeys.insert(frameKey);
        seenPaths.insert(path);
        return false;
    }), sequence.frames.end());
}

void appendUnique(QVector<PostAnimationSequenceModel::NamedKey>& values,
                  QSet<QString>& seen, const QString& key, const QString& name)
{
    if (key.isEmpty() || seen.contains(key))
        return;
    seen.insert(key);
    values.push_back({key, name});
}
} // namespace

void PostAnimationSequenceModel::rebuild(const QString& projectDir)
{
    clear();
    if (projectDir.trimmed().isEmpty())
        return;

    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);
    QMap<QString, Sequence> timeSequences;
    QMap<QString, Sequence> stageSequences;

    for (const PostResultAutoPrepare::ResultGroup& group : groups)
    {
        const QString analysisKey = normalizedAnalysisKey(group);
        const QString analysisName = normalizedAnalysisName(group);
        const QString stageName = normalizedStageName(group);
        const QVector<PostResultAutoPrepare::ResultFile> files =
            PostResultAutoPrepare::resultFiles(projectDir, group.sourcePath, false);

        for (const PostResultAutoPrepare::ResultFile& file : files)
        {
            if (file.format.compare(QStringLiteral("tecplot"),
                                    Qt::CaseInsensitive) != 0 ||
                file.filePath.trimmed().isEmpty())
                continue;

            const QString fileResultKey = normalizedResultKey(file);
            Frame frame;
            frame.filePath = file.filePath;
            frame.frameKey = file.frameKey;
            frame.value = file.frameValue;
            frame.unit = file.frameUnit;
            frame.order = file.frameOrder;

            if (!file.frameKey.trimmed().isEmpty())
            {
                const QString rangeKey = QStringLiteral("time|%1|%2")
                    .arg(analysisKey,
                         QDir(group.sourcePath).absolutePath().toCaseFolded());
                const QString key = rangeKey + QLatin1Char('|') + fileResultKey;
                Sequence& sequence = timeSequences[key];
                sequence.key = key;
                sequence.analysisKey = analysisKey;
                sequence.analysisName = analysisName;
                sequence.rangeKey = rangeKey;
                sequence.rangeName = timeRangeName(group);
                sequence.resultKey = fileResultKey;
                sequence.resultName = resultName(file);
                sequence.resultKind = file.kind;
                sequence.axis = Axis::Time;
                sequence.supportsInterpolation = true;
                frame.label = file.frameLabel.trimmed().isEmpty()
                                  ? QStringLiteral("步 %1").arg(file.frameOrder)
                                  : file.frameLabel.trimmed();
                sequence.frames.push_back(frame);
            }
            else
            {
                const QString category = stageCategory(stageName);
                const QString rangeKey = QStringLiteral("stage|%1|%2")
                    .arg(analysisKey, category.toCaseFolded());
                const QString key = rangeKey + QLatin1Char('|') + fileResultKey;
                Sequence& sequence = stageSequences[key];
                sequence.key = key;
                sequence.analysisKey = analysisKey;
                sequence.analysisName = analysisName;
                sequence.rangeKey = rangeKey;
                sequence.rangeName = category + QStringLiteral("（阶段序列）");
                sequence.resultKey = fileResultKey;
                sequence.resultName = resultName(file);
                sequence.resultKind = file.kind;
                sequence.axis = Axis::Stage;
                sequence.supportsInterpolation = false;
                frame.label = stageName.isEmpty() ? group.displayName : stageName;
                frame.order = group.order;
                sequence.frames.push_back(frame);
            }
        }
    }

    auto append = [this](QMap<QString, Sequence>& source)
    {
        for (auto iterator = source.begin(); iterator != source.end(); ++iterator)
        {
            Sequence sequence = iterator.value();
            sortAndDeduplicateFrames(sequence);
            if (sequence.frames.size() >= 2)
                m_sequences.push_back(std::move(sequence));
        }
    };
    append(timeSequences);
    append(stageSequences);

    std::sort(m_sequences.begin(), m_sequences.end(),
              [](const Sequence& left, const Sequence& right)
    {
        int compare = left.analysisName.localeAwareCompare(right.analysisName);
        if (compare != 0)
            return compare < 0;
        compare = left.rangeName.localeAwareCompare(right.rangeName);
        if (compare != 0)
            return compare < 0;
        return left.resultName.localeAwareCompare(right.resultName) < 0;
    });
}

void PostAnimationSequenceModel::clear()
{
    m_sequences.clear();
}

const QVector<PostAnimationSequenceModel::Sequence>&
PostAnimationSequenceModel::sequences() const
{
    return m_sequences;
}

const PostAnimationSequenceModel::Sequence*
PostAnimationSequenceModel::sequence(int index) const
{
    return index >= 0 && index < m_sequences.size() ? &m_sequences.at(index)
                                                     : nullptr;
}

int PostAnimationSequenceModel::sequenceIndex(const QString& sequenceKey) const
{
    for (int index = 0; index < m_sequences.size(); ++index)
    {
        if (m_sequences.at(index).key == sequenceKey)
            return index;
    }
    return -1;
}

QVector<PostAnimationSequenceModel::NamedKey>
PostAnimationSequenceModel::analyses() const
{
    QVector<NamedKey> values;
    QSet<QString> seen;
    for (const Sequence& sequence : m_sequences)
        appendUnique(values, seen, sequence.analysisKey, sequence.analysisName);
    return values;
}

QVector<PostAnimationSequenceModel::NamedKey>
PostAnimationSequenceModel::ranges(const QString& analysisKey) const
{
    QVector<NamedKey> values;
    QSet<QString> seen;
    for (const Sequence& sequence : m_sequences)
    {
        if (sequence.analysisKey == analysisKey)
            appendUnique(values, seen, sequence.rangeKey, sequence.rangeName);
    }
    return values;
}

QVector<PostAnimationSequenceModel::NamedKey>
PostAnimationSequenceModel::results(const QString& analysisKey,
                                    const QString& rangeKey) const
{
    QVector<NamedKey> values;
    QSet<QString> seen;
    for (const Sequence& sequence : m_sequences)
    {
        if (sequence.analysisKey == analysisKey && sequence.rangeKey == rangeKey)
            appendUnique(values, seen, sequence.key, sequence.resultName);
    }
    return values;
}
