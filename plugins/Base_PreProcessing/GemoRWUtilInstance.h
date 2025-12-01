#pragma once
#include <qstring.h>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>
#include <TopoDS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <IGESControl_Controller.hxx>
#include <Interface_Static.hxx>
#include <qvector.h>
#include <TopoDS_Compound.hxx>
/**
 * @file GemoRWUtilInstance.h
 * @brief 几何数据读取写入工具类
 * @author tongle
 * @version 1.0.0
 * @date 2025-07-17
 */


class GemoRWUtilInstance
{
private:
	/**
	 * @brief 构造函数
	 * @return
	 */
	GemoRWUtilInstance() = default;
	/**
	 * @brief 析构函数
	 * @return
	 */
	~GemoRWUtilInstance();
public:
	/**
	 * @brief 获取单例对象
	 * @return
	 */
	static GemoRWUtilInstance *getInstance();
	/**
	 * @brief 读取step文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功读取
	 */
	bool readOneShapeStepFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 读取iges文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功读取
	 */
	bool readOneShapeIgesFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 读取brep文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功读取
	 */
	bool readOneShapeBrepFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 写入step文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功写入
	 */
	bool writeOneShapeStepFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 写入iges文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功写入
	 */
	bool writeOneShapeIgesFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 写入iges复合文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功写入
	 */
	bool writeMultipleShapesIgesFile(const QString& filePath, const QVector<TopoDS_Shape*>& shapes);
	/**
	 * @brief 写入brep文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功写入
	 */
	bool writeOneShapeBrepFile(const QString& filePath, TopoDS_Shape* shape);
	/**
	 * @brief 写入brep复合文件
	 * @since 1.0.0
	 * @author tongle
	 * @return 是否成功写入
	 */
	bool writeMultipleShapesBrepFile(const QString& filePath, const QVector<TopoDS_Shape*>& shapes);
    bool writeMultipleShapesStepFile(const QString &filePath, const QVector<TopoDS_Shape *> &shapes);
private:
	/**
	 * @brief 单例指针
	 * @return
	 */
	static GemoRWUtilInstance *_instance;
};

