#include "DisplacementConstraintModel.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QObject>
#include <QtMath>

DisplacementConstraintModel::DisplacementConstraintModel()
{
}

void DisplacementConstraintModel::clear()
{
    m_constraints.clear();
    m_madof = 3;
}

bool DisplacementConstraintModel::isInitialized() const
{
    return !m_constraints.isEmpty();
}

bool DisplacementConstraintModel::resetFromModel(const IiinGridModel &model, int madof)
{
    if (model.nodes().isEmpty()) {
        return false;
    }
    m_madof = qMax(1, madof);
    m_constraints.clear();
    m_constraints.resize(model.nodes().size());

    const QSet<int> activeNodeIds = collectActiveNodeIds(model);

    for (int i = 0; i < model.nodes().size(); ++i) {
        const int nodeId = model.nodes()[i].id;
        QVector<int> dofs(m_madof, 1);
        if (!activeNodeIds.contains(nodeId)) {
            dofs.fill(0);
        }
        m_constraints[i] = dofs;
    }
    return true;
}

bool DisplacementConstraintModel::loadFromFile(const QString &fileName,
                                               const IiinGridModel &model,
                                               QString *errMsg)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法打开约束文件: %1").arg(fileName);
        return false;
    }

    QTextStream in(&file);
    int numnp = 0;
    int madof = 0;
    in >> numnp >> madof;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = QObject::tr("约束文件首行读取失败");
        return false;
    }

    int numnp2 = 0;
    int madof2 = 0;
    in >> numnp2 >> madof2;
    if (in.status() != QTextStream::Ok) {
        if (errMsg) *errMsg = QObject::tr("约束文件第二行读取失败");
        return false;
    }

    if (numnp <= 0 || madof <= 0) {
        if (errMsg) *errMsg = QObject::tr("约束文件头部信息无效");
        return false;
    }

    if (model.nodes().size() != numnp) {
        if (errMsg) *errMsg = QObject::tr("约束文件节点数与网格不匹配");
        return false;
    }

    m_madof = madof;
    m_constraints.clear();
    m_constraints.resize(numnp);

    int index = 0;
    while (!in.atEnd() && index < numnp) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QStringList parts = line.simplified().split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 3 + m_madof) {
            continue;
        }
        QVector<int> dofs;
        dofs.reserve(m_madof);
        bool ok = true;
        for (int i = 0; i < m_madof; ++i) {
            const int value = parts[3 + i].toInt(&ok);
            if (!ok) {
                break;
            }
            dofs.push_back(value);
        }
        if (!ok || dofs.size() != m_madof) {
            continue;
        }
        m_constraints[index] = dofs;
        ++index;
    }

    if (index != numnp) {
        if (errMsg) *errMsg = QObject::tr("约束文件读取不完整");
        return false;
    }

    return true;
}

bool DisplacementConstraintModel::saveToFile(const QString &fileName,
                                             const IiinGridModel &model,
                                             QString *errMsg) const
{
    if (model.nodes().isEmpty()) {
        if (errMsg) *errMsg = QObject::tr("网格为空，无法保存约束文件");
        return false;
    }
    if (m_constraints.size() != model.nodes().size()) {
        if (errMsg) *errMsg = QObject::tr("约束数据与网格节点数不一致");
        return false;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errMsg) *errMsg = QObject::tr("无法写入约束文件: %1").arg(fileName);
        return false;
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    out << model.nodes().size() << " " << m_madof << "\n";
    out << model.nodes().size() << " " << m_madof << "\n";

    for (int i = 0; i < model.nodes().size(); ++i) {
        const auto &node = model.nodes()[i];
        const auto &dofs = m_constraints[i];
        out << node.x << " " << node.y << " " << node.z;
        for (int j = 0; j < m_madof; ++j) {
            const int value = (j < dofs.size()) ? dofs[j] : 1;
            out << " " << value;
        }
        out << "\n";
    }

    return true;
}

bool DisplacementConstraintModel::setNodeConstraint(const IiinGridModel &model,
                                                    int nodeId,
                                                    const QVector<int> &dofs,
                                                    QString *errMsg)
{
    if (!isInitialized()) {
        if (errMsg) *errMsg = QObject::tr("约束尚未初始化");
        return false;
    }
    const int index = model.nodeIndexById(nodeId);
    if (index < 0 || index >= m_constraints.size()) {
        if (errMsg) *errMsg = QObject::tr("节点 %1 不存在").arg(nodeId);
        return false;
    }
    m_constraints[index] = normalizedDofs(dofs);
    return true;
}

bool DisplacementConstraintModel::setConstraintsForMatype(const IiinGridModel &model,
                                                          int matype,
                                                          const QVector<int> &dofs)
{
    if (!isInitialized()) {
        return false;
    }
    const QVector<int> normalized = normalizedDofs(dofs);
    QSet<int> nodeIds;
    for (const auto &elem : model.elems()) {
        if (elem.matype != matype) {
            continue;
        }
        for (int nodeId : elem.nodeIds) {
            nodeIds.insert(nodeId);
        }
    }
    for (int nodeId : std::as_const(nodeIds)) {
        const int index = model.nodeIndexById(nodeId);
        if (index >= 0 && index < m_constraints.size()) {
            m_constraints[index] = normalized;
        }
    }
    return true;
}

bool DisplacementConstraintModel::setConstraintsForPlane(const IiinGridModel &model,
                                                         double a,
                                                         double b,
                                                         double c,
                                                         double d,
                                                         double tolerance,
                                                         const QVector<int> &dofs)
{
    if (!isInitialized()) {
        return false;
    }
    const QVector<int> normalized = normalizedDofs(dofs);
    const double tol = qAbs(tolerance);
    for (int i = 0; i < model.nodes().size(); ++i) {
        const auto &node = model.nodes()[i];
        const double value = a * node.x + b * node.y + c * node.z + d;
        if (qAbs(value) <= tol) {
            m_constraints[i] = normalized;
        }
    }
    return true;
}

QVector<int> DisplacementConstraintModel::constrainedNodeIndices(int dofIndex) const
{
    QVector<int> indices;
    if (!isInitialized() || dofIndex < 0 || dofIndex >= m_madof) {
        return indices;
    }
    indices.reserve(m_constraints.size());
    for (int i = 0; i < m_constraints.size(); ++i) {
        const auto &dofs = m_constraints[i];
        if (dofs.size() > dofIndex && dofs[dofIndex] != 1) {
            indices.push_back(i);
        }
    }
    return indices;
}

QVector<int> DisplacementConstraintModel::normalizedDofs(const QVector<int> &dofs) const
{
    QVector<int> normalized(m_madof, 1);
    for (int i = 0; i < m_madof && i < dofs.size(); ++i) {
        normalized[i] = dofs[i];
    }
    return normalized;
}

QSet<int> DisplacementConstraintModel::collectActiveNodeIds(const IiinGridModel &model) const
{
    QSet<int> ids;
    for (const auto &elem : model.elems()) {
        for (int nodeId : elem.nodeIds) {
            ids.insert(nodeId);
        }
    }
    return ids;
}
