#include "IiinGridModel.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDebug>

IiinGridModel::IiinGridModel()
{
}

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

    qDebug() << "[IiinGridModel] header:"
             << "numnp=" << m_numnp
             << "madim=" << m_madim
             << "nelem=" << m_nelem
             << "manod=" << m_manod
             << "am="    << m_am
             << "iobject=" << m_iobject
             << "ivertical=" << m_ivertical;

    if (m_numnp <= 0) {
        if (errMsg) *errMsg = "节点数 numnp 非法";
        return false;
    }

    // ---------- 读节点 ----------
    m_nodes.reserve(m_numnp);
    while (!in.atEnd() && m_nodes.size() < m_numnp) {
        QString raw = in.readLine();
        if (raw.isNull())
            break;
        QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        QString simplified = line.simplified();
        QStringList parts  = simplified.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            qWarning() << "[IiinGridModel] bad node line:" << line;
            continue;
        }

        bool ok = false;
        Node n;

        if (parts.size() >= 4) {
            // x y z id
            n.x  = parts[0].toDouble(&ok); if (!ok) continue;
            n.y  = parts[1].toDouble(&ok); if (!ok) continue;
            n.z  = parts[2].toDouble(&ok); if (!ok) continue;
            n.id = parts[3].toInt(&ok);    if (!ok) continue;
        } else {
            // 3 列: 2D 情况: x y id
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

        // 应用坐标放大倍数 AM
        n.x *= m_am;
        n.y *= m_am;
        n.z *= m_am;

        m_nodes.push_back(n);
    }

    if (m_nodes.isEmpty()) {
        if (errMsg) *errMsg = "未读取到任何节点";
        return false;
    }

    qDebug() << "[IiinGridModel] read nodes =" << m_nodes.size()
             << " first node id=" << m_nodes[0].id
             << " coord=(" << m_nodes[0].x << m_nodes[0].y << m_nodes[0].z << ")";

    // ---------- 读单元 ----------
    m_elems.reserve(m_nelem > 0 ? m_nelem : 1024);
    int countElemLines = 0;

    while (!in.atEnd()) {
        QString raw = in.readLine();
        if (raw.isNull())
            break;
        QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        QString simplified = line.simplified();
        QStringList parts  = simplified.split(' ', Qt::SkipEmptyParts);

        int minNeeded = 1 + m_manod + 3;
        if (parts.size() < minNeeded) {
            qWarning() << "[IiinGridModel] too few columns for element line, got"
                       << parts.size() << "need >=" << minNeeded
                       << ", raw line:" << line;
            continue;
        }

        bool ok = false;
        Elem e;
        e.nodeIds.resize(m_manod);

        e.id = parts[0].toInt(&ok);
        if (!ok) {
            qWarning() << "[IiinGridModel] bad elem id line:" << line;
            continue;
        }

        for (int j = 0; j < m_manod; ++j) {
            e.nodeIds[j] = parts[1 + j].toInt(&ok);
            if (!ok) break;
        }
        if (!ok) {
            qWarning() << "[IiinGridModel] bad node id in elem:" << e.id;
            continue;
        }

        e.ketype = parts[1 + m_manod].toInt(&ok);
        if (!ok) { qWarning() << "[IiinGridModel] bad ketype in elem:" << e.id; continue; }

        e.matype = parts[2 + m_manod].toInt(&ok);
        if (!ok) { qWarning() << "[IiinGridModel] bad matype in elem:" << e.id; continue; }

        e.layer  = parts[3 + m_manod].toInt(&ok);
        if (!ok) { qWarning() << "[IiinGridModel] bad layer in elem:"  << e.id; continue; }

        m_elems.push_back(e);
        ++countElemLines;

        m_allKetypes.insert(e.ketype);
        m_allMatypes.insert(e.matype);
        m_allLayers.insert(e.layer);
    }

    m_nelem = m_elems.size();

    qDebug() << "[IiinGridModel] read elems =" << m_elems.size();

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

    // 首行: 简化写回: 第 5 个写 0, 第 6 个写 AM, 后面三个 0
    out << m_nodes.size() << " "
        << m_madim        << " "
        << m_elems.size() << " "
        << m_manod        << " "
        << 0              << " "
        << m_am           << " "
        << 0.0            << " "
        << 0.0            << " "
        << 0.0            << "\n";

    // 第二行: 对象信息
    out << m_iobject  << " " << m_ivertical << " "
        << m_xmin     << " " << m_xmax     << " "
        << m_ymin     << " " << m_ymax     << " "
        << m_zmin     << " " << m_zmax     << "\n";

    // 节点
    for (const Node &n : m_nodes) {
        double ox = n.x / m_am;
        double oy = n.y / m_am;
        double oz = n.z / m_am;
        out << ox << " " << oy << " " << oz << " " << n.id << "\n";
    }

    // 单元
    for (const Elem &e : m_elems) {
        out << e.id;
        for (int j = 0; j < m_manod; ++j) {
            int nid = (j < e.nodeIds.size()) ? e.nodeIds[j] : 0;
            out << " " << nid;
        }
        out << " " << e.ketype << " " << e.matype << " " << e.layer << " 0 0\n";
    }

    out.flush();
    f.close();

    qDebug() << "[IiinGridModel] saved IIIN.DAT:" << fileName;
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
    if (idx < 0)
        return false;

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
