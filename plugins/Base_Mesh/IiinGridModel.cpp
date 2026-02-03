#include "IiinGridModel.h"

#include <QFile>
#include <QObject>
#include <QTextStream>
#include <QStringList>
#include <QDebug>

#include <PublicVariables.h>
#include <PublicConstants.h>

static inline bool parseInt(const QString& s, int& v)
{
    bool ok = false;
    v = s.toInt(&ok);
    return ok;
}

IiinGridModel::IiinGridModel() {}

void IiinGridModel::clear()
{
    m_fileName.clear();

    m_numnp    = 0;
    m_madim    = 3;
    m_nelem    = 0;
    m_manod    = 0;
    m_am       = 1.0;

    m_iobject  = 0;
    m_ivertical = 2;

    m_xmin = m_xmax = 0.0;
    m_ymin = m_ymax = 0.0;
    m_zmin = m_zmax = 0.0;

    m_nodes.clear();
    m_elems.clear();
    m_nodeId2Index.clear();

    m_allKetypes.clear();
    m_allMatypes.clear();
    m_allLayers.clear();
}

bool IiinGridModel::loadFromFile(const QString &fileName, QString *errMsg)
{
    clear();

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法打开文件: %1").arg(fileName);
        return false;
    }

    QTextStream in(&f);

    // ---------- 头两行 ----------
    int extra   = 0;
    double d1   = 0.0, d2 = 0.0, d3 = 0.0;

    in >> m_numnp >> m_madim >> m_nelem >> m_manod >> extra >> m_am >> d1 >> d2 >> d3;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = "首行(控制信息)读取失败";
        return false;
    }

    in >> m_iobject >> m_ivertical
        >> m_xmin >> m_xmax
        >> m_ymin >> m_ymax
        >> m_zmin >> m_zmax;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = "第二行(对象信息)读取失败";
        return false;
    }

    // 丢弃第二行后面剩余内容
    in.readLine();

    PublicVariables::instance().set(PublicConstants::kMeshDimension, m_madim ,"mesh");

    if (m_numnp <= 0) {
        if (errMsg) *errMsg = "节点数 numnp 非法";
        return false;
    }
    if (m_manod <= 0) {
        // 容错：有些文件 manod 写 0，这里给一个保底
        m_manod = 8;
    }

    // ---------- 读节点 ----------
    m_nodes.reserve(m_numnp);
    while (!in.atEnd() && m_nodes.size() < m_numnp) {
        QString raw = in.readLine();
        if (raw.isNull()) break;
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        QString simplified = line.simplified();
        QStringList parts  = simplified.split(' ', QString::SkipEmptyParts);
        if (parts.size() < 3) continue;

        bool ok = false;
        Node n;

        if (parts.size() >= 4) {
            n.x  = parts[0].toDouble(&ok); if (!ok) continue;
            n.y  = parts[1].toDouble(&ok); if (!ok) continue;
            n.z  = parts[2].toDouble(&ok); if (!ok) continue;
            n.id = parts[3].toInt(&ok);    if (!ok) continue;
        } else {
            // 3 列：2D 情况 x y id；3D 情况 x y z（id 默认顺序）
            if (m_madim == 2) {
                n.x  = parts[0].toDouble(&ok); if (!ok) continue;
                n.y  = parts[1].toDouble(&ok); if (!ok) continue;
                n.z  = 0.0;
                n.id = parts[2].toInt(&ok);    if (!ok) continue;
            } else {
                n.x  = parts[0].toDouble(&ok); if (!ok) continue;
                n.y  = parts[1].toDouble(&ok); if (!ok) continue;
                n.z  = parts[2].toDouble(&ok); if (!ok) continue;
                n.id = m_nodes.size() + 1;
            }
        }

        // 应用 AM
        n.x *= m_am;
        n.y *= m_am;
        n.z *= m_am;

        m_nodes.push_back(n);
    }

    if (m_nodes.isEmpty()) {
        if (errMsg) *errMsg = "未读取到任何节点";
        return false;
    }

    // ---------- 读单元 ----------
    m_elems.reserve(m_nelem > 0 ? m_nelem : 1024);

    // 关键：有些文件 element 行是“变长”的：id + Nnode + ketype matype layer (+ tail1 tail2)
    // 我们用“从右往左”来解析，兼容两种：
    //  A) ... ketype matype layer
    //  B) ... ketype matype layer tail1 tail2
    while (!in.atEnd()) {
        if (m_nelem > 0 && m_elems.size() >= m_nelem) break; // 容错：后面可能还有其它块

        QString raw = in.readLine();
        if (raw.isNull()) break;
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;

        QString simplified = line.simplified();
        QStringList parts  = simplified.split(' ', QString::SkipEmptyParts);
        if (parts.size() < 4) continue; // 至少：id + ketype + matype + layer

        int elemId = 0;
        if (!parseInt(parts[0], elemId)) continue;

        auto tryParse = [&](bool withTail, Elem& e)->bool {
            const int n = parts.size();
            int ketype=0, matype=0, layer=0, tail1=0, tail2=0;
            int nodeCount = 0;

            if (withTail) {
                if (n < 6) return false; // id + ketype matype layer tail1 tail2
                if (!parseInt(parts[n-5], ketype)) return false;
                if (!parseInt(parts[n-4], matype)) return false;
                if (!parseInt(parts[n-3], layer))  return false;
                if (!parseInt(parts[n-2], tail1))  return false;
                if (!parseInt(parts[n-1], tail2))  return false;
                nodeCount = n - 1 - 5;
            } else {
                if (!parseInt(parts[n-3], ketype)) return false;
                if (!parseInt(parts[n-2], matype)) return false;
                if (!parseInt(parts[n-1], layer))  return false;
                tail1 = tail2 = 0;
                nodeCount = n - 1 - 3;
            }

            if (ketype <= 0 || ketype > 100) return false;
            if (nodeCount < 0 || nodeCount > m_manod) return false;

            e.id = elemId;
            e.ketype = ketype;
            e.matype = matype;
            e.layer  = layer;
            e.tail1  = tail1;
            e.tail2  = tail2;

            e.nodeIds.clear();
            e.nodeIds.resize(m_manod);

            for (int i = 0; i < nodeCount; ++i) {
                int nid = 0;
                if (!parseInt(parts[1 + i], nid)) return false;
                e.nodeIds[i] = nid; // 允许重复（退化节点）
            }
            return true;
        };

        Elem e;

        // 优先尝试“带尾巴”，失败再尝试“不带尾巴”
        bool ok = tryParse(true, e);
        if (!ok) ok = tryParse(false, e);
        if (!ok) {
            // 再给一次兼容：如果它确实是固定 manod 的老格式
            const int minNeeded = 1 + m_manod + 3;
            if (parts.size() >= minNeeded) {
                bool ok2 = false;
                e.id = elemId;
                e.nodeIds.resize(m_manod);
                for (int j = 0; j < m_manod; ++j) {
                    e.nodeIds[j] = parts[1 + j].toInt(&ok2);
                    if (!ok2) break;
                }
                if (!ok2) continue;

                e.ketype = parts[1 + m_manod].toInt(&ok2); if (!ok2) continue;
                e.matype = parts[2 + m_manod].toInt(&ok2); if (!ok2) continue;
                e.layer  = parts[3 + m_manod].toInt(&ok2); if (!ok2) continue;
                e.tail1 = e.tail2 = 0;
                ok = true;
            }
        }

        if (!ok) continue;

        m_elems.push_back(e);
        m_allKetypes.insert(e.ketype);
        m_allMatypes.insert(e.matype);
        m_allLayers.insert(e.layer);
    }

    m_nelem = m_elems.size();
    if (m_elems.isEmpty()) {
        if (errMsg) *errMsg = "未读取到任何单元";
        return false;
    }

    f.close();
    m_fileName = fileName;
    rebuildNodeIndexMap();
    return true;
}

bool IiinGridModel::saveToFile(const QString &fileName, QString *errMsg) const
{
    if (m_nodes.isEmpty() || m_elems.isEmpty()) {
        if (errMsg) *errMsg = "没有可保存的节点或单元";
        return false;
    }

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法写入文件: %1").arg(fileName);
        return false;
    }

    QTextStream out(&f);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    out << m_nodes.size() << " "
        << m_madim        << " "
        << m_elems.size() << " "
        << m_manod        << " "
        << 0              << " "
        << m_am           << " "
        << 0.0            << " "
        << 0.0            << " "
        << 0.0            << "\n";

    out << m_iobject  << " " << m_ivertical << " "
        << m_xmin     << " " << m_xmax     << " "
        << m_ymin     << " " << m_ymax     << " "
        << m_zmin     << " " << m_zmax     << "\n";

    for (const Node &n : m_nodes) {
        double ox = n.x / m_am;
        double oy = n.y / m_am;
        double oz = n.z / m_am;
        out << ox << " " << oy << " " << oz << " " << n.id << "\n";
    }

    for (const Elem &e : m_elems) {
        out << e.id;
        for (int j = 0; j < m_manod; ++j) {
            int nid = (j < e.nodeIds.size()) ? e.nodeIds[j] : 0;
            out << " " << nid;
        }
        // 写回尾巴：保持读到的 tail1/tail2（没读到就是 0）
        out << " " << e.ketype << " " << e.matype << " " << e.layer
            << " " << e.tail1  << " " << e.tail2 << "\n";
    }

    out.flush();
    f.close();
    return true;
}

bool IiinGridModel::setFromData(const QVector<Node> &nodes,
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
                                QString *errMsg)
{
    if (nodes.isEmpty() || elems.isEmpty()) {
        if (errMsg) *errMsg = QObject::tr("节点或单元为空，无法生成 IIIN.DAT");
        return false;
    }

    clear();

    m_nodes = nodes;
    m_elems = elems;

    m_numnp = m_nodes.size();
    m_nelem = m_elems.size();
    m_madim = madim;
    m_manod = manod;
    m_am = am;
    m_iobject = iobject;
    m_ivertical = ivertical;
    m_xmin = xmin;
    m_xmax = xmax;
    m_ymin = ymin;
    m_ymax = ymax;
    m_zmin = zmin;
    m_zmax = zmax;
    m_fileName = fileName;

    m_allKetypes.clear();
    m_allMatypes.clear();
    m_allLayers.clear();
    for (const Elem &e : m_elems) {
        m_allKetypes.insert(e.ketype);
        m_allMatypes.insert(e.matype);
        m_allLayers.insert(e.layer);
    }

    rebuildNodeIndexMap();
    return true;
}

void IiinGridModel::rebuildNodeIndexMap()
{
    m_nodeId2Index.clear();
    m_nodeId2Index.reserve(m_nodes.size() * 2);
    for (int i = 0; i < m_nodes.size(); ++i) {
        m_nodeId2Index.insert(m_nodes[i].id, i);
    }
}

int IiinGridModel::nodeIndexById(int nodeId) const
{
    auto it = m_nodeId2Index.find(nodeId);
    if (it == m_nodeId2Index.end())
        return -1;
    return it.value();
}

bool IiinGridModel::moveNode(int nodeId,
                             const double *newX,
                             const double *newY,
                             const double *newZ)
{
    int idx = nodeIndexById(nodeId);
    if (idx < 0) return false;

    Node &n = m_nodes[idx];
    if (newX) n.x = *newX;
    if (newY) n.y = *newY;
    if (newZ) n.z = *newZ;
    return true;
}

bool IiinGridModel::moveNodeTo(int nodeId, double x, double y, double z)
{
    return moveNode(nodeId, &x, &y, &z);
}

bool IiinGridModel::deleteElemById(int elemId)
{
    for (int i = 0; i < m_elems.size(); ++i) {
        if (m_elems[i].id == elemId) {
            m_elems.remove(i);
            return true;
        }
    }
    return false;
}
