#pragma once

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QString>
#include <QTextStream>
#include <QVector>

namespace CaveExcavationProtocol
{
// Match the last static task actually requested by ICAVE. CASE01 itself is
// never a static result directory in AUTOCAVE.
inline QString dynamicStateSubdirectory(bool lining, bool incrementalSupport,
    bool incrementalExcavation, bool wholeSupport, int levels, bool crane)
{
    Q_UNUSED(crane);
    // In AUTOCAVE, {E|J}{NN}PART is only the intermediate release step for the crane
    // installation stage; the completed state of stage NN is always {E|J}{NN}.
    const QString code=QStringLiteral("%1").arg(qMax(1,levels),2,10,QLatin1Char('0'));
    if(lining)return QStringLiteral("CASE01LINE");
    if(incrementalSupport)return QStringLiteral("CASE01JACA/J%1").arg(code);
    if(incrementalExcavation)return QStringLiteral("CASE01EXCA/E%1").arg(code);
    return wholeSupport?QStringLiteral("CASE01JALL"):QStringLiteral("CASE01EALL");
}
// EXCAIIWL interprets the second association as ILAY, not the adjacent KE.
// Keep this contract local to excavation; other IIJX consumers use KE2.
inline void writeFace(QTextStream& out, int ordinal, int element, int layer,
                      const QVector<int>& nodes, int reinforcement)
{
    out << QString("%1%2%3").arg(ordinal, 9).arg(element, 9).arg(layer, 9);
    for (int node : nodes)
        out << QString("%1").arg(node, 9);
    out << QString("%1\n").arg(reinforcement, 10);
}

// The deployed legacy CPU solver appends .DAT twice in the EALL branch, then
// reads the canonical name. Supply both names in the analysis workspace only.
// NEWDIR also requires FLAG_DIR.DAT when a directory already exists.
inline bool prepareElementInputs(const QDir& input, const QDir& workspace,
                                 int levelCount, QString* error)
{
    auto fail = [&](const QString& path) {
        if (error)
            *error = QStringLiteral("无法准备开挖单元计算文件：%1").arg(path);
        return false;
    };
    auto write = [&](const QString& path, const QByteArray& bytes) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
               && file.commit();
    };
    if (levelCount < 1)
        return fail(input.absolutePath());
    const QString name = QStringLiteral("IIKE_T01-%1.DAT")
                             .arg(levelCount, 2, 10, QLatin1Char('0'));
    QFile source(input.filePath(name));
    if (!source.open(QIODevice::ReadOnly))
        return fail(source.fileName());
    const QByteArray bytes = source.readAll();
    if (bytes.isEmpty())
        return fail(source.fileName());
    const QString alias = input.filePath(name + QStringLiteral(".DAT"));
    if (!write(alias, bytes))
        return fail(alias);

    // The UI currently exports a single case (ICASE=1). These are input-only
    // directories until the solver creates results; refresh them on every run.
    for (const QString& task : {QStringLiteral("CASE01EALL"), QStringLiteral("CASE01JALL")})
    {
        const QDir target(workspace.filePath(task));
        if (!QDir().mkpath(target.absolutePath()))
            return fail(target.absolutePath());
        const QString flag = target.filePath(QStringLiteral("FLAG_DIR.DAT"));
        if (!write(flag, QByteArrayLiteral("   1 IFLAG\n")) ||
            !write(target.filePath(name), bytes))
            return fail(target.absolutePath());
    }
    return true;
}
}
