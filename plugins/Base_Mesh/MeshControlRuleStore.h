#ifndef MESHCONTROLRULESTORE_H
#define MESHCONTROLRULESTORE_H

#include <QVector>
#include <QMutex>
#include <QMutexLocker>

struct MeshControlRule
{
    int physId = -1;
    double minSize = 0.0;
    double maxSize = 0.0;
};

class MeshControlRuleStore
{
public:
    static MeshControlRuleStore& instance()
    {
        static MeshControlRuleStore s;
        return s;
    }

    void setEdgeRules(const QVector<MeshControlRule>& rules)
    {
        QMutexLocker locker(&m_mutex);
        m_edgeRules = rules;
    }

    void setFaceRules(const QVector<MeshControlRule>& rules)
    {
        QMutexLocker locker(&m_mutex);
        m_faceRules = rules;
    }

    void setVolumeRules(const QVector<MeshControlRule>& rules)
    {
        QMutexLocker locker(&m_mutex);
        m_volumeRules = rules;
    }

    QVector<MeshControlRule> edgeRules() const
    {
        QMutexLocker locker(&m_mutex);
        return m_edgeRules;
    }

    QVector<MeshControlRule> faceRules() const
    {
        QMutexLocker locker(&m_mutex);
        return m_faceRules;
    }

    QVector<MeshControlRule> volumeRules() const
    {
        QMutexLocker locker(&m_mutex);
        return m_volumeRules;
    }

private:
    MeshControlRuleStore() = default;

private:
    mutable QMutex m_mutex;
    QVector<MeshControlRule> m_edgeRules;
    QVector<MeshControlRule> m_faceRules;
    QVector<MeshControlRule> m_volumeRules;
};

#endif // MESHCONTROLRULESTORE_H
