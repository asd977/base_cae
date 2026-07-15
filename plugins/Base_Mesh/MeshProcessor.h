#pragma once

#include <QString>
#include <QList>

class MeshProcessor
{
  public:
    MeshProcessor();
    ~MeshProcessor();

    void clear();
    void loadOCCFile(const QString& filePath);
    void writeNetgenFile();
    bool writeGridFile(const QString& path);
    bool removeDir(QString dirPath);

    void setNetgenLengthMax(int data) { m_gridLengthMax = data; }
    void setNetgenLengthMin(int data) { m_gridLengthMin = data; }
    void setNetgenDimension(int data) { m_dimension = data; }
    void setNetgenSizeFactor(int data) { m_sizeFactor = data; }
    void setNetgenSmoothing(int data) { m_smoothing = data; }
    void setElementOrder(int data) { m_elementOrder = data; }
    void setGridType(int data) { m_gridType = data; }
    void setAlgorithm(int data) { m_algorithm = data; }
    void setFixShape(bool data) { m_fixShape = data; }

    QString getNasPath() const { return m_nasPath; }
    QString getVtkPath() const { return m_vtkPath; }
    QString getVolPath() const { return m_volPath; }
    QList<int> getGridInfo() const;
    bool loadImportGridFile(const QString& filePath);
    int getNetgenFaceIndex(int surfaceTag) const;

  private:
    QString m_sourcePath;
    QString m_nasPath;
    QString m_vtkPath;
    QString m_volPath;

    int m_gridLengthMax = 10;
    int m_gridLengthMin = 0;
    int m_dimension = 3;
    int m_sizeFactor = 1;
    int m_elementOrder = 1;
    int m_algorithm = 1;
    bool m_fixShape = true;
    int m_smoothing = 10;
    int m_gridType = 3;

    int m_nodeSize = 0;
    int m_surfaceElementSize = 0;
    int m_volumeElementSize = 0;
    bool m_hasGeneratedMesh = false;
};
