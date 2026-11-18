#pragma once

#include <QFile>
#include <QSaveFile>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <vector>

namespace SolverDirectoryProtocol
{
inline bool validateRuntime(const QDir& runtime, QString* error)
{
    QStringList missing;
    for (const auto& name : {"C08_ErrShow/DEBUG/ErrShow.exe", "KEATT_CON.DAT", "KEATT_VAR.DAT", "KONG.DAT", "C02_TABLE/AT0128.DAT"})
    {
        const QFileInfo file(runtime.filePath(QString::fromLatin1(name)));
        if (!file.isFile() || file.size() <= 0) missing.append(file.absoluteFilePath());
    }
    if (missing.isEmpty()) return true;
    if (error) *error=QStringLiteral("当前程序的求解器运行文件缺失或为空：\n%1\n请从完整安装目录启动程序，或补齐上述文件；求解器尚未启动。")
        .arg(missing.join(QLatin1Char('\n')));
    return false;
}
// PCCAE reads the primary file; the legacy ErrShow helper reads the mirror.
// Stage both writes before committing so an unwritable mirror cannot leave a
// calculation pointing at a new directory with an old error-viewer directory.
inline bool writeMirrored(const QString& primary, const QString& mirror,
                          const QByteArray& content, QString* error = nullptr)
{
    const auto fail = [error](const QString& path,const QString& reason) {
        if(error)*error=QStringLiteral("无法读写求解器目录文件 %1：%2").arg(path,reason);
        return false;
    };
    struct Entry { QString path; bool existed; QByteArray original; };
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<QSaveFile>> staged;
    for (const QString& path : {primary, mirror})
    {
        QFile old(path);
        Entry entry{path, old.exists(), {}};
        if (entry.existed)
        {
            if (!old.open(QIODevice::ReadOnly)) return fail(path,old.errorString());
            entry.original = old.readAll();
            old.close();
        }
        auto file = std::make_unique<QSaveFile>(path);
        if (!file->open(QIODevice::WriteOnly) || file->write(content) != content.size())
            return fail(path,file->errorString());
        entries.push_back(entry);
        staged.push_back(std::move(file));
    }
    for (size_t i = 0; i < staged.size(); ++i)
    {
        if (staged[i]->commit()) continue;
        for (size_t j = 0; j < i; ++j)
        {
            if (!entries[j].existed) QFile::remove(entries[j].path);
            else
            {
                QSaveFile restore(entries[j].path);
                if (restore.open(QIODevice::WriteOnly))
                {
                    restore.write(entries[j].original);
                    restore.commit();
                }
            }
        }
        return fail(entries[i].path,staged[i]->errorString());
    }
    return true;
}
}
