#ifndef GEOMETRYCREATOR_H
#define GEOMETRYCREATOR_H

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Geom_Curve.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <GC_MakeCircle.hxx> 
#include <vector>
#include <cmath>
#include <functional>


/**
* @brief 生成简单几何工具类
* @author tongle
* @date 2025.08.05
*/
class GeometryCreator
{
public:
	/**
	* @brief 获取单例
	*/
	static GeometryCreator* getInstance();
	/**
	* @brief 长方体
	*/
	TopoDS_Shape* createBox(double length, double width, double height, const gp_Pnt& position = gp_Pnt(0, 0, 0));
	/**
	* @brief 球体
	*/
	TopoDS_Shape* createSphere(double radius, const gp_Pnt& center = gp_Pnt(0, 0, 0));
	/**
	* @brief 圆锥体
	*/
	TopoDS_Shape* createCone(double bottomRadius, double topRadius, double height, const gp_Pnt& baseCenter = gp_Pnt(0, 0, 0));
	/**
	* @brief 圆环
	*/
	TopoDS_Shape* createTorus(double majorRadius, double minorRadius, const gp_Pnt& center = gp_Pnt(0, 0, 0));
	/**
	* @brief 圆柱体
	*/
	TopoDS_Shape* createCylinder(double radius, double height, const gp_Pnt& baseCenter = gp_Pnt(0, 0, 0));
	/**
	* @brief 螺旋
	*/
	TopoDS_Shape* createHelix(double radius, double height, double pitch, const gp_Pnt& baseCenter = gp_Pnt(0, 0, 0));
	/**
	* @brief 圆
	*/
	TopoDS_Shape* createCircle2d(double radius, const gp_Pnt2d& center);
	/**
	* @brief 圆
	*/
	TopoDS_Shape* createCircle(const gp_Ax2& axis, double radius);
	/**
    * @brief 圆弧
    * @param radius半径
    * @param startAngle/endAngle 起始/终止角度，弧度
    * @param center圆心
    */
	TopoDS_Shape* createArcEdge(double radius, double startAngle, double endAngle, const gp_Pnt& center);
	/**
	* @brief 双曲线
	* @param a 实半轴
	* @param c 焦距
	* @param tmin/tmax 参数范围
	* @param numPoints 点数
	*/
	TopoDS_Shape* createHyperbolaEdge(double a, double c, double tmin, double tmax, int numPoints);
	/**
	* @brief 抛物线
	* @param f 焦距
	* @param tmin/tmax 参数范围
	* @param numPoints 点数
	*/
	TopoDS_Shape* createParabolaEdge(double f, double tmin, double tmax, int numPoints);
	/**
	* @brief b样条曲线
	* @param tmin/tmax 参数范围
	* @param numPoints 点数
	* @param xFunc/yFunc/zFunc 参数化函数
	*/
	TopoDS_Shape* createBSplineEdge(double tmin, double tmax, int numPoints,
		std::function<double(double)> xFunc,
		std::function<double(double)> yFunc,
		std::function<double(double)> zFunc);
	/**
	* @brief 正方形(面)
	*/
	TopoDS_Shape* createSquare2d(double sideLength, const gp_Pnt2d& center);
	/**
	* @brief 正方形(线框)
	*/
	TopoDS_Shape* createSquareWire(double sideLength, const gp_Pnt2d& center);
	/**
	* @brief 椭圆
	*/
	TopoDS_Shape* createEllipse2d(double majorRadius, double minorRadius, const gp_Pnt2d& center);
	/**
	* @brief 点
	*/
	TopoDS_Shape* createPoint2d(const gp_Pnt2d& point);
	/**
	* @brief 矩形（面）
	*/
	TopoDS_Shape* createRectangle2d(double length, double width, const gp_Pnt2d& center);
	/**
	* @brief 矩形（线框）
	*/
	TopoDS_Shape* createRectangleWire(double length, double width, const gp_Pnt2d& center);
	/**
	* @brief 多边形（面）
	*/
	TopoDS_Shape* createPolygon2d(double radius, int sides, const gp_Pnt2d& center);
	/**
	* @brief 多边形（线框）
	*/
	TopoDS_Shape* createPolygonWire(double radius, int sides, const gp_Pnt2d& center);
	/**
	* @brief 线段
	*/
	TopoDS_Shape* createLine2d(const gp_Pnt2d& start, const gp_Pnt2d& end);
	/**
	* @brief 插值曲线
	*/
	TopoDS_Shape* createInterpolationCurve2d(const std::vector<gp_Pnt2d>& points);
	/**
	* @brief 参数化曲线
	* @param xFunc X坐标函数
	* @param yFunc Y坐标函数
	* @param zFunc Z坐标函数
	* @param tmin  参数起始值
	* @param tmax 参数结束值
	* @param numPoints 离散点数量
	*/
	TopoDS_Shape* createParametricCurve(double(*xFunc)(double), double(*yFunc)(double), double(*zFunc)(double),
		double tmin, double tmax,
		int numPoints = 100);
	/**
	* @brief 二次贝塞尔曲线
	*/
	TopoDS_Shape* createBezier2Curve2d(const std::vector<gp_Pnt2d>& controlPoints);
	/**
	* @brief 三次贝塞尔曲线
	*/
	TopoDS_Shape* createBezier3Curve2d(const std::vector<gp_Pnt2d>& controlPoints);
	/**
	* @brief 线性拉伸
	* @note 线性拉伸是将二维面沿指定方向拉伸一定距离，生成一个三维实体
	*/
	TopoDS_Shape* linearExtrusion(TopoDS_Shape* face, double height);
	/**
	* @brief 旋转拉伸
	* @note 旋转拉伸是将二维面绕指定轴旋转一定角度，生成一个旋转体
	*/
	TopoDS_Shape* rotationalExtrusion(TopoDS_Shape* face, const gp_Ax1& axis, double angle);
	/**
	* @brief 沿路径拉伸
	* @note 沿路径拉伸是将二维面沿指定路径（通常是一条曲线或折线）扫掠，生成一个三维形状（如管道或复杂曲面）
	*/
	TopoDS_Shape* pathExtrusion(TopoDS_Shape* section, const TopoDS_Wire& path);
	/**
	* @brief 修复几何
	*/
	TopoDS_Shape* fixShape(TopoDS_Shape* inputShape);
	/**
	* @brief 几何求交
	* @note 交：取两个形状的重叠部分（公共区域）。例如，两个重叠的立方体相交后，结果是它们共享的体积部分。
	*/
	TopoDS_Shape* booleanIntersection(TopoDS_Shape* shape1, TopoDS_Shape* shape2);
	/**
	* @brief 几何求差
	* @note 差：从一个形状（主形状）中减去另一个形状（工具形状）。例如，从一个大立方体中减去一个小球，得到一个带有球形空腔的立方体。
	*/
	TopoDS_Shape* booleanDifference(TopoDS_Shape* shape1, TopoDS_Shape* shape2);
	/**
	* @brief 几何求并
	* @note 并：将两个形状合并为一个整体，去除重叠部分的内部边界。例如，两个部分重叠的立方体合并后形成一个连通的形状。
	*/
	TopoDS_Shape* booleanUnion(TopoDS_Shape* shape1, TopoDS_Shape* shape2);
	/**
	* @brief 复制
	*/
	TopoDS_Shape* copyShape(TopoDS_Shape* inputShape);
	/**
	* @brief 镜像
	*/
	TopoDS_Shape* mirrorShape(TopoDS_Shape* inputShape, const gp_Ax2& mirrorPlane);
	/**
	* @brief 旋转
	*/
	TopoDS_Shape* rotateShape(TopoDS_Shape* inputShape, const gp_Ax1& axis, double angle);
	/**
	* @brief 平移
	*/
	TopoDS_Shape* translateShape(TopoDS_Shape* inputShape, const gp_Vec& translation);
	/**
	* @brief 删除(差集)
	*/
	TopoDS_Shape* deletePart(TopoDS_Shape* mainShape, TopoDS_Shape* partToRemove);
	/**
	* @brief 圆角
	*/
	TopoDS_Shape* addFillet(TopoDS_Shape* inputShape, double radius);
	/**
	* @brief 分割域
	*/
	TopoDS_Shape* splitDomain(TopoDS_Shape* inputShape, const gp_Pln& plane);
	/**
	* @brief 分割域(返回分割的域)
	*/
	TopoDS_Shape* splitDomainReturnAll(TopoDS_Shape* inputShape, const gp_Pln& plane);
private:
	GeometryCreator() {}
	GeometryCreator(const GeometryCreator&) = delete;
	GeometryCreator& operator=(const GeometryCreator&) = delete;
	static GeometryCreator* instance;
};

#endif // GEOMETRYCREATOR_H