#pragma once

#include "PostResultAutoPrepare.h"

#include <QComboBox>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
#include <QString>

namespace PostResultSelectionUtils
{



struct SharedResultSelection
{
    QString analysisKey;
    QString resultDir;
    QString timeKey;
    QString resultFile;
    QString fieldName;
    int fieldAssociation{-1};
};

inline SharedResultSelection& sharedResultSelection()
{
    static SharedResultSelection selection;
    return selection;
}

inline void clearSharedResultSelection()
{
    sharedResultSelection() = SharedResultSelection{};
}

inline void rememberBasicSelection(const QComboBox* analysisCombo,
                                   const QComboBox* stageCombo,
                                   const QComboBox* timeCombo,
                                   const QComboBox* resultCombo)
{
    SharedResultSelection& selection = sharedResultSelection();
    if (analysisCombo && analysisCombo->currentIndex() >= 0)
        selection.analysisKey = analysisCombo->currentData().toString();
    if (stageCombo && stageCombo->currentIndex() >= 0)
        selection.resultDir = stageCombo->currentData().toString();
    if (timeCombo && timeCombo->currentIndex() >= 0)
        selection.timeKey = timeCombo->currentData().toString();
    if (resultCombo && resultCombo->currentIndex() >= 0)
        selection.resultFile = resultCombo->currentData().toString();
}

inline void rememberFieldSelection(const QComboBox* combo,
                                   int nameRole = Qt::UserRole,
                                   int associationRole = Qt::UserRole + 1)
{
    if (!combo || combo->currentIndex() < 0)
        return;
    SharedResultSelection& selection = sharedResultSelection();
    selection.fieldName = combo->currentData(nameRole).toString();
    if (selection.fieldName.isEmpty())
        selection.fieldName = combo->currentText();
    const QVariant association = combo->currentData(associationRole);
    selection.fieldAssociation = association.isValid() ? association.toInt() : -1;
}

inline int findSharedField(const QComboBox* combo,
                           int nameRole = Qt::UserRole,
                           int associationRole = Qt::UserRole + 1)
{
    if (!combo)
        return -1;
    const SharedResultSelection& selection = sharedResultSelection();
    if (selection.fieldName.isEmpty())
        return -1;
    for (int i = 0; i < combo->count(); ++i)
    {
        QString name = combo->itemData(i, nameRole).toString();
        if (name.isEmpty())
            name = combo->itemText(i);
        if (name != selection.fieldName)
            continue;
        if (selection.fieldAssociation < 0)
            return i;
        const QVariant association = combo->itemData(i, associationRole);
        if (!association.isValid() || association.toInt() == selection.fieldAssociation)
            return i;
    }
    return -1;
}

inline QJsonObject saveSharedResultSelection()
{
    const SharedResultSelection& selection = sharedResultSelection();
    QJsonObject obj;
    obj[QStringLiteral("analysisKey")] = selection.analysisKey;
    obj[QStringLiteral("resultDir")] = selection.resultDir;
    obj[QStringLiteral("timeKey")] = selection.timeKey;
    obj[QStringLiteral("resultFile")] = selection.resultFile;
    obj[QStringLiteral("fieldName")] = selection.fieldName;
    obj[QStringLiteral("fieldAssociation")] = selection.fieldAssociation;
    return obj;
}

inline void loadSharedResultSelection(const QJsonObject& obj)
{
    SharedResultSelection& selection = sharedResultSelection();
    selection.analysisKey = obj.value(QStringLiteral("analysisKey")).toString();
    selection.resultDir = obj.value(QStringLiteral("resultDir")).toString();
    selection.timeKey = obj.value(QStringLiteral("timeKey")).toString();
    selection.resultFile = obj.value(QStringLiteral("resultFile")).toString();
    selection.fieldName = obj.value(QStringLiteral("fieldName")).toString();
    selection.fieldAssociation = obj.value(QStringLiteral("fieldAssociation")).toInt(-1);
}

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

    const QString resolvedPreferred = !sharedResultSelection().analysisKey.isEmpty()
                                          ? sharedResultSelection().analysisKey
                                          : preferredKey;
    int index = resolvedPreferred.isEmpty() ? -1 : combo->findData(resolvedPreferred);
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

    const QString resolvedPreferred = !sharedResultSelection().resultDir.isEmpty()
                                          ? sharedResultSelection().resultDir
                                          : preferredSourcePath;
    int index = resolvedPreferred.isEmpty() ? -1 : combo->findData(resolvedPreferred);
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

    const QString resolvedPreferred = !sharedResultSelection().timeKey.isEmpty()
                                          ? sharedResultSelection().timeKey
                                          : preferredKey;
    int index = resolvedPreferred.isEmpty() ? -1 : combo->findData(resolvedPreferred);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);

    const bool hasSequence =
        frames.size() > 1 || (!frames.isEmpty() && !frames.constFirst().key.isEmpty());
    combo->setEnabled(hasSequence);

    bool strengthReductionSelector = false;
    for (const auto& frame : frames)
    {
        if (frame.key.startsWith(QStringLiteral("condition:strength:")))
        {
            strengthReductionSelector = true;
            break;
        }
    }

    // 复用现有第三层选择器，不额外增加一排控件：
    // 普通瞬态结果仍叫“结果时刻”，强度折减结果动态显示为“折减系数”。
    if (QWidget* parent = combo->parentWidget())
    {
        if (QLabel* label = parent->findChild<QLabel*>(QStringLiteral("resultTimeLabel")))
            label->setText(strengthReductionSelector
                               ? QStringLiteral("折减系数")
                               : QStringLiteral("结果时刻"));
    }
    combo->setToolTip(
        strengthReductionSelector
            ? QStringLiteral("选择强度折减系数")
            : (hasSequence ? QString() : QStringLiteral("当前结果仅包含一个时刻")));
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

    const QString resolvedPreferred = !sharedResultSelection().resultFile.isEmpty()
                                          ? sharedResultSelection().resultFile
                                          : preferredFile;
    int index = resolvedPreferred.isEmpty() ? -1 : combo->findData(resolvedPreferred);
    if (index < 0 && combo->count() > 0)
        index = 0;
    combo->setCurrentIndex(index);
}
} // namespace PostResultSelectionUtils
