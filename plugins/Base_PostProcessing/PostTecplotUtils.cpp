#include "PostTecplotUtils.h"

#include <vtkAppendFilter.h>
#include <vtkDataObjectTreeIterator.h>
#include <vtkDataSet.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <memory>

namespace
{
struct GridCacheEntry
{
    qint64 size = -1;
    qint64 modifiedMs = -1;
    quint64 accessSerial = 0;
    vtkSmartPointer<vtkUnstructuredGrid> grid;
};

QMutex& gridCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, GridCacheEntry>& gridCache()
{
    static QHash<QString, GridCacheEntry> cache;
    return cache;
}

quint64& gridCacheSerial()
{
    static quint64 serial = 0;
    return serial;
}

vtkSmartPointer<vtkUnstructuredGrid> shallowGridCopy(vtkUnstructuredGrid* source)
{
    if (!source)
        return nullptr;
    auto copy = vtkSmartPointer<vtkUnstructuredGrid>::New();
    copy->ShallowCopy(source);
    return copy;
}

void trimGridCacheLocked()
{
    constexpr int maxEntries = 8;
    auto& cache = gridCache();
    while (cache.size() > maxEntries)
    {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it)
        {
            if (oldest == cache.end() || it->accessSerial < oldest->accessSerial)
                oldest = it;
        }
        if (oldest == cache.end())
            break;
        cache.erase(oldest);
    }
}

bool isAsciiPath(const QString& path)
{
    for (const QChar ch : path)
    {
        if (ch.unicode() > 0x7f)
            return false;
    }
    return true;
}

QString shortPathName(const QString& path)
{
#ifdef Q_OS_WIN
    const DWORD length = GetShortPathNameW(
        reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, 0);
    if (length == 0)
        return {};

    std::wstring buffer(static_cast<size_t>(length), L'\0');
    const DWORD written = GetShortPathNameW(
        reinterpret_cast<LPCWSTR>(path.utf16()), buffer.data(), length);
    if (written == 0 || written >= length)
        return {};
    buffer.resize(written);
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(buffer.size()));
#else
    Q_UNUSED(path);
    return {};
#endif
}

QString temporaryDirectory()
{
    const QStringList candidates = {
        QDir::tempPath(),
        QCoreApplication::applicationDirPath()
    };
    for (const QString& candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (isAsciiPath(candidate) && info.isDir() && info.isWritable())
            return QDir(candidate).absolutePath();
    }
    return QDir::tempPath();
}

bool copyToTemporaryFile(const QString& sourcePath, QTemporaryFile& target,
                         QString* errorMessage)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取结果文件：%1").arg(sourcePath);
        return false;
    }
    if (!target.open())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建后处理临时文件。");
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(chunkSize);
        if (chunk.isEmpty() && source.error() != QFile::NoError)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("读取结果文件失败：%1").arg(sourcePath);
            return false;
        }
        if (target.write(chunk) != chunk.size())
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("写入后处理临时文件失败。");
            return false;
        }
    }
    target.flush();
    target.close();
    return true;
}
}

namespace PostTecplotUtils
{
vtkSmartPointer<vtkUnstructuredGrid> readGrid(const QString& fileName,
                                              QString* errorMessage)
{
    const QString sourcePath = QDir::fromNativeSeparators(fileName.trimmed());
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果文件不存在：%1").arg(fileName);
        return nullptr;
    }

    const QString cacheKey = sourceInfo.canonicalFilePath().isEmpty()
                                 ? sourceInfo.absoluteFilePath()
                                 : sourceInfo.canonicalFilePath();
    const qint64 fileSize = sourceInfo.size();
    const qint64 modifiedMs = sourceInfo.lastModified().toMSecsSinceEpoch();
    {
        QMutexLocker locker(&gridCacheMutex());
        auto it = gridCache().find(cacheKey);
        if (it != gridCache().end() && it->size == fileSize &&
            it->modifiedMs == modifiedMs && it->grid)
        {
            it->accessSerial = ++gridCacheSerial();
            if (errorMessage)
                errorMessage->clear();
            return shallowGridCopy(it->grid);
        }
    }

    QString vtkPath = sourcePath;
    std::unique_ptr<QTemporaryFile> temporaryFile;
    if (!isAsciiPath(vtkPath))
    {
        const QString sourceShortPath = shortPathName(sourcePath);
        if (!sourceShortPath.isEmpty() && isAsciiPath(sourceShortPath))
            vtkPath = sourceShortPath;
        else
        {
            temporaryFile = std::make_unique<QTemporaryFile>(
                QDir(temporaryDirectory()).filePath(QStringLiteral("cae_post_XXXXXX.tec")));
            temporaryFile->setAutoRemove(true);
            if (!copyToTemporaryFile(sourcePath, *temporaryFile, errorMessage))
                return nullptr;
            vtkPath = temporaryFile->fileName();
        }
        if (!isAsciiPath(vtkPath))
        {
            const QString shortPath = shortPathName(vtkPath);
            if (!shortPath.isEmpty())
                vtkPath = shortPath;
        }
    }

    if (!isAsciiPath(vtkPath))
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法为结果文件创建兼容的读取路径。");
        return nullptr;
    }

    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    const QByteArray encodedPath = vtkPath.toLatin1();
    reader->SetFileName(encodedPath.constData());
    reader->Update();

    vtkMultiBlockDataSet* output = reader->GetOutput();
    if (!output || output->GetNumberOfBlocks() == 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果文件中没有可读取的网格数据：%1").arg(fileName);
        return nullptr;
    }

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    auto iterator = vtkSmartPointer<vtkDataObjectTreeIterator>::Take(output->NewTreeIterator());
    iterator->VisitOnlyLeavesOn();
    iterator->SkipEmptyNodesOn();
    int dataSetCount = 0;
    for (iterator->InitTraversal(); !iterator->IsDoneWithTraversal(); iterator->GoToNextItem())
    {
        if (auto* dataSet = vtkDataSet::SafeDownCast(iterator->GetCurrentDataObject()))
        {
            appender->AddInputData(dataSet);
            ++dataSetCount;
        }
    }
    if (dataSetCount == 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果文件中没有可显示的网格分区：%1").arg(fileName);
        return nullptr;
    }

    appender->Update();
    vtkUnstructuredGrid* appended = appender->GetOutput();
    if (!appended || appended->GetNumberOfPoints() == 0 || appended->GetNumberOfCells() == 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("结果网格为空：%1").arg(fileName);
        return nullptr;
    }

    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->ShallowCopy(appended);

    {
        QMutexLocker locker(&gridCacheMutex());
        GridCacheEntry entry;
        entry.size = fileSize;
        entry.modifiedMs = modifiedMs;
        entry.accessSerial = ++gridCacheSerial();
        entry.grid = grid;
        gridCache().insert(cacheKey, entry);
        trimGridCacheLocked();
    }

    if (errorMessage)
        errorMessage->clear();
    return shallowGridCopy(grid);
}
}
