#pragma once

#include <QVector>
#include <QSet>
#include <QString>

#include "IiinGridModel.h"

class DisplacementConstraintModel
{
public:
    DisplacementConstraintModel();

    void clear();
    bool isInitialized() const;

    bool resetFromModel(const IiinGridModel &model, int madof = 3);
    bool loadFromFile(const QString &fileName,
                      const IiinGridModel &model,
                      QString *errMsg = nullptr);
    bool saveToFile(const QString &fileName,
                    const IiinGridModel &model,
                    QString *errMsg = nullptr) const;

    int madof() const { return m_madof; }
    const QVector<QVector<int>> &constraints() const { return m_constraints; }

    bool setNodeConstraint(const IiinGridModel &model,
                           int nodeId,
                           const QVector<int> &dofs,
                           QString *errMsg = nullptr);
    bool setConstraintsForMatype(const IiinGridModel &model,
                                 int matype,
                                 const QVector<int> &dofs);
    bool setConstraintsForPlane(const IiinGridModel &model,
                                double a,
                                double b,
                                double c,
                                double d,
                                double tolerance,
                                const QVector<int> &dofs);

    QVector<int> constrainedNodeIndices(int dofIndex) const;

private:
    QVector<int> normalizedDofs(const QVector<int> &dofs) const;
    QSet<int> collectActiveNodeIds(const IiinGridModel &model) const;

private:
    int m_madof = 3;
    QVector<QVector<int>> m_constraints;
};
