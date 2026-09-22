#pragma once
#include <QString>
#include <QStringList>
#include <TopoDS_Shape.hxx>
class BooleanBuilder
{
public:
    static bool buildFuseShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCutShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCommonShape(const QStringList& selectedKeys, TopoDS_Shape& result);
    static bool buildCutShape(const TopoDS_Shape& target, const TopoDS_Shape& tool,
                              TopoDS_Shape& result);
    static bool recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
};
