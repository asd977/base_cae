#pragma once

#include <QString>
#include <QVector>
#include <TopoDS_Shape.hxx>

class ModifyBuilder
{
public:
    static bool buildFilletShape(const QString& sourceUuid, double radius,
                                 const QString& edgeSelection, TopoDS_Shape& result,
                                 QString* message = nullptr);
    static bool buildChamferShape(const QString& sourceUuid, double distance,
                                  const QString& edgeSelection, TopoDS_Shape& result,
                                  QString* message = nullptr);
    static bool buildSplitShape(const QString& sourceUuid, const QString& toolUuid,
                                TopoDS_Shape& result, QString* message = nullptr);
    static QVector<TopoDS_Shape> splitResultParts(const TopoDS_Shape& shape);

    static bool recreateFillet(const QString& uuid, const QString& name,
                               const QString& sourceUuid, double radius,
                               const QString& edgeSelection = QString());
    static bool recreateChamfer(const QString& uuid, const QString& name,
                                const QString& sourceUuid, double distance,
                                const QString& edgeSelection = QString());
    static bool recreateSplit(const QString& uuid, const QString& name,
                              const QString& sourceUuid, const QString& toolUuid);
    static bool recreateSplitPart(const QString& uuid, const QString& name,
                                  const QString& splitUuid, int partIndex);
};
