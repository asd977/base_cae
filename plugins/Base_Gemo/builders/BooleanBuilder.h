#pragma once
#include <geometry_global.h>
#include <QString>
#include <QStringList>
#include <TopoDS_Shape.hxx>

class GEOMETRY_EXPORT BooleanBuilder
{
public:
    static bool buildFuseShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCutShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCommonShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCutShape(const TopoDS_Shape& target, const TopoDS_Shape& tool,
                              TopoDS_Shape& result);
    static bool excavateFromGeology(const QString& geologyUuid,
                                   const TopoDS_Shape& excavationTool,
                                   TopoDS_Shape& cutGeology,
                                   QVector<int>& newExcavationFaceIndices,
                                   QString& errorMsg);
    static bool fitDamToValley(const QString& geologyUuid,
                               const TopoDS_Shape& rawDam,
                               TopoDS_Shape& fittedDam,
                               QVector<int>& contactFaceIndices,
                               QString& errorMsg);
    static bool recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
};
