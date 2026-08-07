#pragma once

#include <QString>
#include <QVector>

class PostAnimationSequenceModel
{
  public:
    enum class Axis
    {
        Time,
        Stage
    };

    struct Frame
    {
        QString label;
        QString filePath;
        QString frameKey;
        double value = 0.0;
        QString unit;
        int order = -1;
    };

    struct Sequence
    {
        QString key;
        QString analysisKey;
        QString analysisName;
        QString rangeKey;
        QString rangeName;
        QString resultKey;
        QString resultName;
        QString resultKind;
        Axis axis = Axis::Stage;
        QVector<Frame> frames;
        bool supportsInterpolation = false;
    };

    struct NamedKey
    {
        QString key;
        QString name;
    };

    void rebuild(const QString& projectDir);
    void clear();

    const QVector<Sequence>& sequences() const;
    const Sequence* sequence(int index) const;
    int sequenceIndex(const QString& sequenceKey) const;

    QVector<NamedKey> analyses() const;
    QVector<NamedKey> ranges(const QString& analysisKey) const;
    QVector<NamedKey> results(const QString& analysisKey,
                              const QString& rangeKey) const;

  private:
    QVector<Sequence> m_sequences;
};
