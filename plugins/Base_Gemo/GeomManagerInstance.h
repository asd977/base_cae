#pragma once
#include <QObject>
#include <QString>

#include "GeomShapeObject.h"
/**
 * @file GeomManagerInstance.h
 * @brief 几何数据管理单例类
 * @author tongle
 * @version 1.0.0
 * @date 2025-07-08
 */
namespace GeomManager
{
class GeomManagerInstance : public QObject
{
    Q_OBJECT
  private:
    /**
     * @brief 构造函数
     * @return
     */
    GeomManagerInstance() = default;
    /**
     * @brief 析构函数
     * @return
     */
    ~GeomManagerInstance();

  public:
    static GeomManagerInstance* getInstance();
    /**
     * @brief 添加几何形状
     * @param shape 当前形状
     * @return void
     */
    void appendGeomShapeObject(GeomShapeObject* shape);

    /**
     * @brief 移除几何形状
     * @param shape 当前形状
     * @return void
     */
    void removeGeomShapeObject(GeomShapeObject* shape);
    bool removeGeomShapeObject(const QString& uuid);

    GeomShapeObject* getGeomShapeObject(const QString& uuid) const;

    /**
     * @brief 判断几何数据是否为空
     * @return bool
     */
    bool isEmpty();

    QHash<QString, GeomShapeObject*> getShapMap()
    {
        return _geomShapeMap;
    };

  private:
    /**
     * @brief 单例指针
     * @return
     */
    static GeomManagerInstance* _instance;
    /**
     * @brief 几何形状列表
     * @param uuid和shape
     */
    QHash<QString, GeomShapeObject*> _geomShapeMap{};
};
} // namespace GeomManager
