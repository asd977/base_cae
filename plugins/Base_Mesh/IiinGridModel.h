#pragma once

#include <QVector>
#include <QSet>
#include <QHash>
#include <QString>

struct Node
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int    id = 0;
};

struct Elem
{
    int id = 0;
    QVector<int> nodeIds; // 长度一般 = manod，短的会补 0；退化节点允许重复
    int ketype = 0;
    int matype = 0;
    int layer  = 0;

    // 可选尾巴字段（很多文件会有 0 0）
    int tail1 = 0;
    int tail2 = 0;
};

class IiinGridModel
{
public:
    IiinGridModel();

    void clear();

    bool loadFromFile(const QString &fileName, QString *errMsg = nullptr);
    bool saveToFile(const QString &fileName, QString *errMsg = nullptr) const;

    bool setFromData(const QVector<Node> &nodes,
                     const QVector<Elem> &elems,
                     int madim,
                     int manod,
                     double am,
                     int iobject,
                     int ivertical,
                     double xmin,
                     double xmax,
                     double ymin,
                     double ymax,
                     double zmin,
                     double zmax,
                     const QString &fileName,
                     QString *errMsg = nullptr);

    const QVector<Node>& nodes() const { return m_nodes; }
    const QVector<Elem>& elems() const { return m_elems; }
    QVector<Node>& nodes() { return m_nodes; }
    QVector<Elem>& elems() { return m_elems; }

    int numnp() const { return m_numnp; }
    int madim() const { return m_madim; }
    int nelem() const { return m_nelem; }
    int manod() const { return m_manod; }
    double am() const { return m_am; }

    int iobject() const { return m_iobject; }
    int ivertical() const { return m_ivertical; }

    double xmin() const { return m_xmin; }
    double xmax() const { return m_xmax; }
    double ymin() const { return m_ymin; }
    double ymax() const { return m_ymax; }
    double zmin() const { return m_zmin; }
    double zmax() const { return m_zmax; }

    QString fileName() const { return m_fileName; }

    QSet<int> allKetypes() const { return m_allKetypes; }
    QSet<int> allMatypes() const { return m_allMatypes; }
    QSet<int> allLayers() const { return m_allLayers; }

    void rebuildNodeIndexMap();
    int  nodeIndexById(int nodeId) const;

    bool moveNode(int nodeId, const double *newX, const double *newY, const double *newZ);
    bool moveNodeTo(int nodeId, double x, double y, double z);

    bool deleteElemById(int elemId);

private:
    QString m_fileName;

    int m_numnp = 0;
    int m_madim = 3;
    int m_nelem = 0;
    int m_manod = 0;
    double m_am = 1.0;

    int m_iobject = 0;
    int m_ivertical = 2;

    double m_xmin = 0.0, m_xmax = 0.0;
    double m_ymin = 0.0, m_ymax = 0.0;
    double m_zmin = 0.0, m_zmax = 0.0;

    QVector<Node> m_nodes;
    QVector<Elem> m_elems;
    QHash<int,int> m_nodeId2Index;

    QSet<int> m_allKetypes;
    QSet<int> m_allMatypes;
    QSet<int> m_allLayers;
};
