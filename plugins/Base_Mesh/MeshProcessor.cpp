#include "MeshProcessor.h"

#include "NetgenMeshGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{
constexpr int kElemTypeHex8FromTet = 4;
}

MeshProcessor::MeshProcessor() = default;

MeshProcessor::~MeshProcessor() = default;

void MeshProcessor::clear()
{
    m_sourcePath.clear();
    m_nasPath.clear();
    m_vtkPath.clear();
    m_volPath.clear();
    m_nodeSize = 0;
    m_surfaceElementSize = 0;
    m_volumeElementSize = 0;
    m_hasGeneratedMesh = false;
}

void MeshProcessor::loadOCCFile(const QString& filePath)
{
    clear();
    m_sourcePath = filePath;
    writeNetgenFile();
}

void MeshProcessor::writeNetgenFile()
{
    const QString tempPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../tempGrid"));
    removeDir(tempPath);
    QDir().mkpath(tempPath);

    m_nasPath = QDir(tempPath).filePath(QStringLiteral("mesh.nas"));
    m_vtkPath = QDir(tempPath).filePath(QStringLiteral("mesh.vtk"));
    m_volPath = QDir(tempPath).filePath(QStringLiteral("mesh.vol"));

    NetgenMeshingRequest request;
    request.inputGeometryPath = m_sourcePath;
    request.nasPath = m_nasPath;
    request.vtkPath = m_vtkPath;
    request.volPath = m_volPath;
    request.dimension = m_dimension;
    request.elemType = m_gridType;
    request.lenMin = m_gridLengthMin;
    request.lenMax = m_gridLengthMax;
    request.sizeFactor = m_sizeFactor;
    request.elementOrder = m_elementOrder;
    request.smoothing = m_smoothing;
    request.fixShape = m_fixShape;
    request.exportVol = m_gridType != kElemTypeHex8FromTet;
    request.exportVtk = true;
    request.exportNas = true;

    NetgenMeshingResult result;
    QString err;
    m_hasGeneratedMesh = NetgenMeshGenerator::generate(request, &result, &err);
    if (m_hasGeneratedMesh)
    {
        m_nodeSize = result.pointCount;
        m_surfaceElementSize = result.surfaceElementCount;
        m_volumeElementSize = result.volumeElementCount;
    }
}

bool MeshProcessor::writeGridFile(const QString& path)
{
    if (!m_hasGeneratedMesh || path.trimmed().isEmpty())
        return false;

    const QString suffix = QFileInfo(path).suffix().toLower();
    QString source;
    if (suffix == QStringLiteral("vtk"))
    {
        source = m_vtkPath;
    }
    else if (suffix == QStringLiteral("nas"))
    {
        source = m_nasPath;
    }
    else if (suffix == QStringLiteral("vol"))
    {
        if (m_gridType == kElemTypeHex8FromTet)
            return false;
        source = m_volPath;
    }
    else if (suffix.isEmpty())
    {
        source = m_gridType == kElemTypeHex8FromTet ? m_vtkPath : m_volPath;
    }
    else
    {
        return false;
    }

    if (!QFileInfo::exists(source))
        return false;

    if (QFileInfo::exists(path) && !QFile::remove(path))
        return false;
    return QFile::copy(source, path);
}

bool MeshProcessor::removeDir(QString dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return true;

    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    const QFileInfoList fileList = dir.entryInfoList();
    for (const QFileInfo& file : fileList)
    {
        if (file.isFile())
        {
            file.dir().remove(file.fileName());
        }
        else
        {
            removeDir(file.absoluteFilePath());
        }
    }
    return dir.rmpath(dir.absolutePath());
}

QList<int> MeshProcessor::getGridInfo() const
{
    return QList<int>{m_nodeSize, 0, 0, m_surfaceElementSize, m_volumeElementSize};
}

bool MeshProcessor::loadImportGridFile(const QString& filePath)
{
    clear();
    if (!QFileInfo::exists(filePath))
        return false;
    m_sourcePath = filePath;
    return true;
}

int MeshProcessor::getNetgenFaceIndex(int surfaceTag) const
{
    return surfaceTag;
}
