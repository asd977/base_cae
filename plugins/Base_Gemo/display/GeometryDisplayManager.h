#pragma once

#include <geometry_global.h>

#include <QString>
#include <QStringList>
#include <QJsonObject>

class CadBody;
using GeomShapeObject = CadBody;

class GEOMETRY_EXPORT GeometryDisplayManager
{
public:
    static QString facePropId(const QString& uuid);
    static QString edgePropId(const QString& uuid);

    static bool registerObject(const QString& uuid, GeomShapeObject* object,
                               bool resetCamera = false, bool renderNow = true);
    static bool removeObject(const QString& uuid, bool removeFromDocument = true,
                             bool renderNow = true);

    static bool setVisible(const QString& uuid, bool visible, bool renderNow = true);
    static bool hide(const QString& uuid, bool renderNow = true);
    static bool show(const QString& uuid, bool renderNow = true);
    static bool isVisible(const QString& uuid);
    static void setPresentation(const QStringList& uuids, int mode, double opacity = -1.0);
    static void isolateObjects(const QStringList& uuids);
    static void restorePresentation();
    static QJsonObject presentationJson();
    static void loadPresentationJson(const QJsonObject& object);

    // Terrain is presented as a surface, without exposing its internal facets.
    static void setTerrainPresentation(const QString& uuid, bool showColor = true, bool showContours = true);
    static void removeTerrainPreview(const QString& uuid);

    // 永久删除节点时清除单个对象状态；普通重建几何时不要调用。
    static void forgetVisibilityState(const QString& uuid);

    // 清空显示状态表。新建/关闭工程时调用；普通重建几何不清空。
    static void clearVisibilityStates();

    // 批量加载/重建工程时暂停即时渲染，避免每创建一个 actor 就 Render 一次。
    static void setRenderSuspended(bool suspended);
    static bool isRenderSuspended();

    static void hideSources(const QStringList& uuids, bool renderNow = true);
};
