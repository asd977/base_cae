#include "geometrycreator.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2d_CartesianPoint.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Dir2d.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <TColStd_HArray1OfReal.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <QDebug>
#include <cmath>
#include <Geom_Plane.hxx>
#include <gp_Ax2d.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Pln.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <Geom_Parabola.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Hyperbola.hxx>
#include <Geom_Parabola.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax2.hxx>
#include <Standard_Handle.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <stdexcept>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Vec.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <ShapeFix_Shape.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BOPAlgo_Splitter.hxx>

GeometryCreator* GeometryCreator::instance = nullptr;

GeometryCreator* GeometryCreator::getInstance()
{
	if (!instance)
	{
		instance = new GeometryCreator();
	}
	return instance;
}

TopoDS_Shape* GeometryCreator::createBox(double length, double width, double height, const gp_Pnt& position)
{
	if (length <= 0 || width <= 0 || height <= 0) 
	{
		return nullptr;
	}
	BRepPrimAPI_MakeBox boxMaker(length, width, height);
	TopoDS_Shape shape = boxMaker.Shape();
	if (shape.IsNull()) 
	{
		return nullptr;
	}
	if (position.X() != 0.0 || position.Y() != 0.0 || position.Z() != 0.0)
	{
		gp_Trsf transform;
        transform.SetTranslation( gp_Pnt(0, 0, 0),position);
		BRepBuilderAPI_Transform transformer(shape, transform);
		shape = transformer.Shape();
	}
	TopAbs_ShapeEnum shapeType = shape.ShapeType();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::createSphere(double radius, const gp_Pnt& center)
{
	if (radius <= 0) 
	{
		return nullptr;
	}
	BRepPrimAPI_MakeSphere sphereMaker(center, radius);
	TopoDS_Shape shape = sphereMaker.Shape();
	if (shape.IsNull()) 
	{
		return nullptr;
	}
	TopAbs_ShapeEnum shapeType = shape.ShapeType();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::createCone(double bottomRadius, double topRadius, double height, const gp_Pnt& baseCenter)
{
	if (bottomRadius < 0 || topRadius < 0 || height <= 0) 
	{
		return nullptr;
	}
	if (bottomRadius <= topRadius)
	{
		return nullptr;
	}
	BRepPrimAPI_MakeCone coneMaker(bottomRadius, topRadius, height);
	TopoDS_Shape shape = coneMaker.Shape();
	if (shape.IsNull()) 
	{
		return nullptr;
	}
	if (baseCenter.X() != 0.0 || baseCenter.Y() != 0.0 || baseCenter.Z() != 0.0) {
		gp_Trsf transform;
        transform.SetTranslation( gp_Pnt(0, 0, 0),baseCenter);
		BRepBuilderAPI_Transform transformer(shape, transform);
		shape = transformer.Shape();
	}
	TopAbs_ShapeEnum shapeType = shape.ShapeType();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::createTorus(double majorRadius, double minorRadius, const gp_Pnt& center)
{
	if (majorRadius <= 0 || minorRadius <= 0 || minorRadius >= majorRadius)
	{
		return nullptr;
	}
	BRepPrimAPI_MakeTorus torusMaker(majorRadius, minorRadius);
	TopoDS_Shape shape = torusMaker.Shape();
	if (shape.IsNull()) 
	{
		return nullptr;
	}
	if (center.X() != 0.0 || center.Y() != 0.0 || center.Z() != 0.0) 
	{
		gp_Trsf transform;
        transform.SetTranslation( gp_Pnt(0, 0, 0),center);
		BRepBuilderAPI_Transform transformer(shape, transform);
		shape = transformer.Shape();
	}
	TopAbs_ShapeEnum shapeType = shape.ShapeType();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::createCylinder(double radius, double height, const gp_Pnt& baseCenter)
{
	if (radius <= 0 || height <= 0) 
	{
		return nullptr;
	}
	BRepPrimAPI_MakeCylinder cylinderMaker(radius, height);
	TopoDS_Shape shape = cylinderMaker.Shape();
	if (shape.IsNull()) 
	{
		return nullptr;
	}
	if (baseCenter.X() != 0.0 || baseCenter.Y() != 0.0 || baseCenter.Z() != 0.0) 
	{
		gp_Trsf transform;
        transform.SetTranslation(gp_Pnt(0, 0, 0), baseCenter);
		BRepBuilderAPI_Transform transformer(shape, transform);
		shape = transformer.Shape();
	}
	TopAbs_ShapeEnum shapeType = shape.ShapeType();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::createHelix(double radius, double height, double pitch, const gp_Pnt& baseCenter)
{
	// 输入验证
	if (radius <= 0 || height <= 0 || pitch <= 0) {
		return nullptr;
	}

	// 计算圈数并动态调整点数
	Standard_Real turns = height / pitch;
	Standard_Integer numPoints = std::max(100, static_cast<Standard_Integer>(100 * turns));

	// 创建螺旋路径
	TColgp_Array1OfPnt points(1, numPoints);
	Standard_Real step = height / (numPoints - 1);
	for (Standard_Integer i = 1; i <= numPoints; ++i) {
		Standard_Real t = (i - 1) * 2 * M_PI * turns / (numPoints - 1);
		Standard_Real z = (i - 1) * step;
		points(i) = gp_Pnt(radius * cos(t), radius * sin(t), z);
	}
	Handle(Geom_BSplineCurve) helixCurve = GeomAPI_PointsToBSpline(points).Curve();
	TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(helixCurve);
	TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);

	// 验证路径
	if (edge.IsNull() || wire.IsNull()) {
		return nullptr;
	}

	// 创建圆形截面
	Handle(Geom_Curve) sectionCircle = GC_MakeCircle(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), radius * 0.01).Value();
	TopoDS_Edge sectionEdge = BRepBuilderAPI_MakeEdge(sectionCircle);
	TopoDS_Wire sectionWire = BRepBuilderAPI_MakeWire(sectionEdge);
	TopoDS_Shape sectionShape = sectionWire;

	// 验证截面
	if (sectionEdge.IsNull() || sectionWire.IsNull()) {
		return nullptr;
	}

	// 沿螺旋路径扫掠
	try {
		BRepOffsetAPI_MakePipe pipeMaker(wire, sectionShape);
		pipeMaker.Build();
		if (!pipeMaker.IsDone()) {
			return nullptr;
		}
		TopoDS_Shape shape = pipeMaker.Shape();
		if (shape.IsNull()) {
			return nullptr;
		}

		// 应用平移变换
		if (baseCenter.X() != 0.0 || baseCenter.Y() != 0.0 || baseCenter.Z() != 0.0) {
			gp_Trsf transform;
			transform.SetTranslation(gp_Pnt(0, 0, 0), baseCenter);
			BRepBuilderAPI_Transform transformer(shape, transform);
			shape = transformer.Shape();
		}

		return new TopoDS_Shape(shape);
	}
	catch (const Standard_Failure& e) {
		return nullptr;
	}
}

TopoDS_Shape* GeometryCreator::createCircle2d(double radius, const gp_Pnt2d& center)
{
	// 创建 XY 平面
	Handle(Geom_Plane) plane = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));

	// 创建二维圆几何
	gp_Ax2d axis(center, gp_Dir2d(1.0, 0.0));
	Handle(Geom2d_Circle) circleGeom = new Geom2d_Circle(axis, radius);

	// 使用平面和二维几何创建边，显式指定参数范围 (0, 2*PI)
	BRepBuilderAPI_MakeEdge edgeMaker(circleGeom, plane, 0.0, 2.0 * M_PI);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for circle! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Edge edge = edgeMaker.Edge();

	// 创建线框
	BRepBuilderAPI_MakeWire wireMaker;
	wireMaker.Add(edge);
	if (!wireMaker.IsDone()) {
		qDebug() << "Error: Failed to create wire for circle! Error code:" << wireMaker.Error();
		return nullptr;
	}
	TopoDS_Wire wire = wireMaker.Wire();

	// 创建面，绑定到平面并检查线框方向
	BRepBuilderAPI_MakeFace faceMaker(plane, wire, true);
	if (!faceMaker.IsDone()) {
		qDebug() << "Error: Failed to create face for circle! Error code:" << faceMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(faceMaker.Shape());
	qDebug() << "Circle2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createCircle(const gp_Ax2 & axis, double radius)
{
	Handle(Geom_Circle) circle = new Geom_Circle(axis, radius);
	auto edge = BRepBuilderAPI_MakeEdge(circle).Edge();
	TopoDS_Shape* shape = new TopoDS_Shape(edge);
	return shape;
}

TopoDS_Shape* GeometryCreator::createArcEdge(double radius, double startAngle, double endAngle, const gp_Pnt& center)
{
	// 验证输入
	if (radius <= 0) 
	{
		qDebug() << "圆弧半径无效：radius > 0";
		return nullptr;
	}
	if (endAngle <= startAngle) 
	{
		qDebug() << "角度范围无效：endAngle 必须大于 startAngle";
		return nullptr;
	}
	// 将角度从度转换为弧度
	double startRad = startAngle * M_PI / 180.0;
	double endRad = endAngle * M_PI / 180.0;
	// 创建圆
	gp_Ax2 axis(center, gp::DZ()); // 圆心在 center，法向沿 Z 轴
	Handle(Geom_Circle) circle = new Geom_Circle(axis, radius);
	if (circle.IsNull()) 
	{
		qDebug() << "无法创建圆";
		return nullptr;
	}

	// 创建圆弧（修剪圆）
	Handle(Geom_TrimmedCurve) arc = new Geom_TrimmedCurve(circle, startRad, endRad);
	if (arc.IsNull()) 
	{
		qDebug() << "无法创建圆弧";
		return nullptr;
	}

	// 创建拓扑边
	BRepBuilderAPI_MakeEdge makeEdge(arc, startRad, endRad);
	if (!makeEdge.IsDone()) 
	{
		qDebug() << "无法从圆弧创建拓扑边";
		return nullptr;
	}
	TopoDS_Edge edge = makeEdge.Edge();

	return new TopoDS_Shape(edge);
}

TopoDS_Shape* GeometryCreator::createHyperbolaEdge(double a, double c, double tmin, double tmax, int numPoints) 
{
	// 验证输入
	if (a <= 0 || c <= a) 
	{
		qDebug() << "双曲线参数无效：a > 0 且 c > a";
		return nullptr;
	}
	if (tmax <= tmin) 
	{
		qDebug() << "参数范围无效：tmax 必须大于 tmin";
		return nullptr;
	}
	if (numPoints < 2)
	{
		qDebug() << "至少需要2个点";
		return nullptr;
	}

	// 使用参数方程生成点集：x = a * cosh(t), y = b * sinh(t), z = 0
	// b^2 = c^2 - a^2
	double b = std::sqrt(c * c - a * a);
	TColgp_Array1OfPnt points(1, numPoints);
	double step = (tmax - tmin) / (numPoints - 1);

	for (int i = 1; i <= numPoints; ++i) {
		double t = tmin + (i - 1) * step;
		double x = a * std::cosh(t);
		double y = b * std::sinh(t);
		double z = 0.0;
		if (!std::isfinite(x) || !std::isfinite(y))
		{
			qDebug() << "双曲线点坐标无效";
			return nullptr;
		}
		points.SetValue(i, gp_Pnt(x, y, z));
	}

	// 使用 GeomAPI_PointsToBSpline 拟合双曲线
	GeomAPI_PointsToBSpline curveBuilder(points, 3, 8, GeomAbs_C2, 1.0e-3);
	if (!curveBuilder.IsDone()) 
	{
		qDebug() << "无法创建双曲线B样条";
		return nullptr;
	}

	Handle(Geom_BSplineCurve) bsplineCurve = curveBuilder.Curve();
	if (bsplineCurve.IsNull()) 
	{
		qDebug() << "双曲线B样条曲线为空";
		return nullptr;
	}

	// 转换为 Handle(Geom_Curve)
	Handle(Geom_Curve) curve = bsplineCurve;
	if (curve.IsNull()) 
	{
		qDebug() << "无法将双曲线转换为通用曲线";
		return nullptr;
	}

	// 创建拓扑边
	Standard_Real uMin = bsplineCurve->FirstParameter();
	Standard_Real uMax = bsplineCurve->LastParameter();
	BRepBuilderAPI_MakeEdge makeEdge(curve, uMin, uMax);
	if (!makeEdge.IsDone()) 
	{
		qDebug() << "无法从双曲线创建拓扑边";
		return nullptr;
	}
	TopoDS_Edge edge = makeEdge.Edge();

	return new TopoDS_Shape(edge);
}

TopoDS_Shape* GeometryCreator::createParabolaEdge(double f, double tmin, double tmax, int numPoints) 
{
	// 验证输入
	if (f <= 0) 
	{
		qDebug() << "抛物线焦距无效：f > 0";
		return nullptr;
	}
	if (tmax <= tmin) 
	{
		qDebug() << "参数范围无效：tmax 必须大于 tmin";
		return nullptr;
	}
	if (numPoints < 2) 
	{
		qDebug() << "至少需要2个点";
		return nullptr;
	}

	// 使用参数方程生成点集：x = t, y = t^2 / (4f), z = 0
	TColgp_Array1OfPnt points(1, numPoints);
	double step = (tmax - tmin) / (numPoints - 1);

	for (int i = 1; i <= numPoints; ++i) {
		double t = tmin + (i - 1) * step;
		double x = t;
		double y = (t * t) / (4 * f);
		double z = 0.0;
		if (!std::isfinite(x) || !std::isfinite(y)) 
		{
			qDebug() << "抛物线点坐标无效";
			return nullptr;
		}
		points.SetValue(i, gp_Pnt(x, y, z));
	}

	// 使用 GeomAPI_PointsToBSpline 拟合抛物线
	GeomAPI_PointsToBSpline curveBuilder(points, 2, 2, GeomAbs_C1, 1.0e-3); // 抛物线可用2阶
	if (!curveBuilder.IsDone()) 
	{
		qDebug() << "无法创建抛物线B样条";
		return nullptr;
	}

	Handle(Geom_BSplineCurve) bsplineCurve = curveBuilder.Curve();
	if (bsplineCurve.IsNull()) 
	{
		qDebug() << "抛物线B样条曲线为空";
		return nullptr;
	}

	// 转换为 Handle(Geom_Curve)
	Handle(Geom_Curve) curve = bsplineCurve;
	if (curve.IsNull()) 
	{
		qDebug() << "无法将抛物线转换为通用曲线";
		return nullptr;
	}

	// 创建拓扑边
	Standard_Real uMin = bsplineCurve->FirstParameter();
	Standard_Real uMax = bsplineCurve->LastParameter();
	BRepBuilderAPI_MakeEdge makeEdge(curve, uMin, uMax);
	if (!makeEdge.IsDone()) 
	{
		qDebug() << "无法从抛物线创建拓扑边";
		return nullptr;
	}
	TopoDS_Edge edge = makeEdge.Edge();

	return new TopoDS_Shape(edge);
}

TopoDS_Shape* GeometryCreator::createBSplineEdge(double tmin, double tmax, int numPoints,
	std::function<double(double)> xFunc,std::function<double(double)> yFunc,std::function<double(double)> zFunc) {
	// 验证输入
	if (tmax <= tmin) 
	{
		qDebug() << "参数范围无效：tmax 必须大于 tmin";
		return nullptr;
	}
	if (numPoints < 2) 
	{
		qDebug() << "至少需要2个点";
		return nullptr;
	}

	// 创建点集
	TColgp_Array1OfPnt points(1, numPoints);
	double step = (tmax - tmin) / (numPoints - 1);

	for (int i = 1; i <= numPoints; ++i) 
	{
		double t = tmin + (i - 1) * step;
		double x = xFunc(t);
		double y = yFunc(t);
		double z = zFunc(t);
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) 
		{
			qDebug() << "B样条点坐标无效（NaN 或无穷大";
			return nullptr;
		}
		points.SetValue(i, gp_Pnt(x, y, z));
	}

	// 创建B样条曲线
	GeomAPI_PointsToBSpline curveBuilder(points, 3, 8, GeomAbs_C2, 1.0e-3);
	if (!curveBuilder.IsDone())
	{
		qDebug() << "无法创建B样条曲线";
		return nullptr;
	}

	Handle(Geom_BSplineCurve) bsplineCurve = curveBuilder.Curve();
	if (bsplineCurve.IsNull()) 
	{
		qDebug() << "B样条曲线为空";
		return nullptr;
	}

	// 转换为 Handle(Geom_Curve)
	Handle(Geom_Curve) curve = bsplineCurve;
	if (curve.IsNull())
	{
		qDebug() << "无法将B样条曲线转换为通用曲线";
		return nullptr;
	}

	// 创建拓扑边
	Standard_Real uMin = bsplineCurve->FirstParameter();
	Standard_Real uMax = bsplineCurve->LastParameter();
	BRepBuilderAPI_MakeEdge makeEdge(curve, uMin, uMax);
	if (!makeEdge.IsDone()) 
	{
		qDebug() << "无法从B样条曲线创建拓扑边";
		return nullptr;
	}
	TopoDS_Edge edge = makeEdge.Edge();

	return new TopoDS_Shape(edge);
}

TopoDS_Shape* GeometryCreator::createSquare2d(double sideLength, const gp_Pnt2d& center)
{
	double halfSide = sideLength / 2.0;
	// 创建三维点数组，Z=0
	TColgp_Array1OfPnt points(1, 4);
	points.SetValue(1, gp_Pnt(center.X() - halfSide, center.Y() - halfSide, 0.0));
	points.SetValue(2, gp_Pnt(center.X() + halfSide, center.Y() - halfSide, 0.0));
	points.SetValue(3, gp_Pnt(center.X() + halfSide, center.Y() + halfSide, 0.0));
	points.SetValue(4, gp_Pnt(center.X() - halfSide, center.Y() + halfSide, 0.0));

	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= 4; ++i) {
		polygonMaker.Add(points.Value(i)); // 使用 gp_Pnt
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	BRepBuilderAPI_MakeFace faceMaker(wire);
	TopoDS_Shape* shape = new TopoDS_Shape(wire);
	//TopoDS_Shape* shape = new TopoDS_Shape(faceMaker.Shape());
	qDebug() << "Square2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createSquareWire(double sideLength, const gp_Pnt2d& center)
{
	double halfSide = sideLength / 2.0;
	// 创建三维点数组，Z=0
	TColgp_Array1OfPnt points(1, 4);
	points.SetValue(1, gp_Pnt(center.X() - halfSide, center.Y() - halfSide, 0.0));
	points.SetValue(2, gp_Pnt(center.X() + halfSide, center.Y() - halfSide, 0.0));
	points.SetValue(3, gp_Pnt(center.X() + halfSide, center.Y() + halfSide, 0.0));
	points.SetValue(4, gp_Pnt(center.X() - halfSide, center.Y() + halfSide, 0.0));

	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= 4; ++i) {
		polygonMaker.Add(points.Value(i));
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	TopoDS_Shape* shape = new TopoDS_Shape(wire);
	qDebug() << "Square wire shape type:" << shape->ShapeType(); 
	return shape;
}

TopoDS_Shape* GeometryCreator::createEllipse2d(double majorRadius, double minorRadius, const gp_Pnt2d& center)
{
	// 创建 XY 平面
	Handle(Geom_Plane) plane = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));

	// 创建二维椭圆几何
	gp_Ax2d axis(center, gp_Dir2d(1.0, 0.0));
	Handle(Geom2d_Ellipse) ellipseGeom = new Geom2d_Ellipse(axis, majorRadius, minorRadius);

	// 使用平面和二维几何创建边，显式指定参数范围 (0, 2*PI)
	BRepBuilderAPI_MakeEdge edgeMaker(ellipseGeom, plane, 0.0, 2.0 * M_PI);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for ellipse! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Edge edge = edgeMaker.Edge();

	// 创建线框
	BRepBuilderAPI_MakeWire wireMaker;
	wireMaker.Add(edge);
	if (!wireMaker.IsDone()) {
		qDebug() << "Error: Failed to create wire for ellipse! Error code:" << wireMaker.Error();
		return nullptr;
	}
	TopoDS_Wire wire = wireMaker.Wire();

	// 创建面，绑定到平面并检查线框方向
	BRepBuilderAPI_MakeFace faceMaker(plane, wire, true);
	if (!faceMaker.IsDone()) {
		qDebug() << "Error: Failed to create face for ellipse! Error code:" << faceMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(faceMaker.Shape());
	qDebug() << "Ellipse2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createPoint2d(const gp_Pnt2d& point)
{
	BRepBuilderAPI_MakeVertex vertexMaker(gp_Pnt(point.X(), point.Y(), 0.0));
	if (!vertexMaker.IsDone()) {
		qDebug() << "Error: Failed to create vertex for point!";
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(vertexMaker.Shape());
	qDebug() << "Point2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createRectangle2d(double length, double width, const gp_Pnt2d& center)
{
	double halfLength = length / 2.0;
	double halfWidth = width / 2.0;
	TColgp_Array1OfPnt points(1, 4);
	points.SetValue(1, gp_Pnt(center.X() - halfLength, center.Y() - halfWidth, 0.0));
	points.SetValue(2, gp_Pnt(center.X() + halfLength, center.Y() - halfWidth, 0.0));
	points.SetValue(3, gp_Pnt(center.X() + halfLength, center.Y() + halfWidth, 0.0));
	points.SetValue(4, gp_Pnt(center.X() - halfLength, center.Y() + halfWidth, 0.0));
	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= 4; ++i) {
		polygonMaker.Add(points.Value(i));
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	BRepBuilderAPI_MakeFace faceMaker(wire);
	if (!faceMaker.IsDone()) {
		qDebug() << "Error: Failed to create face for rectangle! Error code:" << faceMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(faceMaker.Shape());
	qDebug() << "Rectangle2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createRectangleWire(double length, double width, const gp_Pnt2d& center)
{
	double halfLength = length / 2.0;
	double halfWidth = width / 2.0;
	TColgp_Array1OfPnt points(1, 4);
	points.SetValue(1, gp_Pnt(center.X() - halfLength, center.Y() - halfWidth, 0.0));
	points.SetValue(2, gp_Pnt(center.X() + halfLength, center.Y() - halfWidth, 0.0));
	points.SetValue(3, gp_Pnt(center.X() + halfLength, center.Y() + halfWidth, 0.0));
	points.SetValue(4, gp_Pnt(center.X() - halfLength, center.Y() + halfWidth, 0.0));
	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= 4; ++i) {
		polygonMaker.Add(points.Value(i));
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	TopoDS_Shape* shape = new TopoDS_Shape(wire);
	qDebug() << "Rectangle2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createPolygon2d(double radius, int sides, const gp_Pnt2d& center)
{
	if (sides < 3) {
		qDebug() << "Error: Polygon must have at least 3 sides!";
		return nullptr;
	}
	TColgp_Array1OfPnt points(1, sides);
	double angleStep = 2.0 * M_PI / sides;
	for (int i = 1; i <= sides; ++i) {
		double angle = (i - 1) * angleStep;
		double x = center.X() + radius * cos(angle);
		double y = center.Y() + radius * sin(angle);
		points.SetValue(i, gp_Pnt(x, y, 0.0));
	}
	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= sides; ++i) {
		BRepBuilderAPI_MakeVertex vertexMaker(points.Value(i));
		if (!vertexMaker.IsDone()) {
			qDebug() << "Error: Failed to create vertex for polygon at index" << i;
			return nullptr;
		}
		polygonMaker.Add(vertexMaker.Vertex());
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	BRepBuilderAPI_MakeFace faceMaker(wire);
	if (!faceMaker.IsDone()) {
		qDebug() << "Error: Failed to create face for polygon! Error code:" << faceMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(faceMaker.Shape());
	qDebug() << "Polygon2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createPolygonWire(double radius, int sides, const gp_Pnt2d& center)
{
	if (sides < 3) {
		qDebug() << "Error: Polygon must have at least 3 sides!";
		return nullptr;
	}
	TColgp_Array1OfPnt points(1, sides);
	double angleStep = 2.0 * M_PI / sides;
	for (int i = 1; i <= sides; ++i) {
		double angle = (i - 1) * angleStep;
		double x = center.X() + radius * cos(angle);
		double y = center.Y() + radius * sin(angle);
		points.SetValue(i, gp_Pnt(x, y, 0.0));
	}
	BRepBuilderAPI_MakePolygon polygonMaker;
	for (int i = 1; i <= sides; ++i) {
		BRepBuilderAPI_MakeVertex vertexMaker(points.Value(i));
		if (!vertexMaker.IsDone()) {
			qDebug() << "Error: Failed to create vertex for polygon at index" << i;
			return nullptr;
		}
		polygonMaker.Add(vertexMaker.Vertex());
	}
	polygonMaker.Close();
	TopoDS_Wire wire = polygonMaker.Wire();
	TopoDS_Shape* shape = new TopoDS_Shape(wire);
	qDebug() << "Polygon2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createLine2d(const gp_Pnt2d& start, const gp_Pnt2d& end)
{
	// 将 gp_Pnt2d 转换为 gp_Pnt，设置 Z=0
	gp_Pnt p1(start.X(), start.Y(), 0.0);
	gp_Pnt p2(end.X(), end.Y(), 0.0);
	BRepBuilderAPI_MakeEdge edgeMaker(p1, p2);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for line! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(edgeMaker.Shape());
	qDebug() << "Line2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createInterpolationCurve2d(const std::vector<gp_Pnt2d>& points)
{
	if (points.size() < 2) {
		qDebug() << "Error: Interpolation curve requires at least 2 points!";
		return nullptr;
	}
	Handle(Geom_Plane) plane = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
	// Create heap-allocated array for points
	Handle(TColgp_HArray1OfPnt2d) pointArray = new TColgp_HArray1OfPnt2d(1, points.size());
	for (size_t i = 0; i < points.size(); ++i) {
		pointArray->SetValue(i + 1, points[i]);
	}
	// Try simpler constructor
	Geom2dAPI_Interpolate interpolator(pointArray, false, 1e-6);
	interpolator.Perform();
	if (!interpolator.IsDone()) {
		qDebug() << "Error: Interpolation curve creation failed with simple constructor!";
		// Fallback to parameters-based constructor
		Handle(TColStd_HArray1OfReal) parameters = new TColStd_HArray1OfReal(1, points.size());
		Standard_Real totalLength = 0.0;
		parameters->SetValue(1, 0.0);
		for (size_t i = 1; i < points.size(); ++i) {
			Standard_Real dist = pointArray->Value(i).Distance(pointArray->Value(i + 1));
			totalLength += dist;
			parameters->SetValue(i + 1, totalLength);
		}
		Geom2dAPI_Interpolate interpolatorWithParams(pointArray, parameters, false, 1e-6);
		interpolatorWithParams.Perform();
		if (!interpolatorWithParams.IsDone()) {
			qDebug() << "Error: Interpolation curve creation failed with parameters!";
			return nullptr;
		}
		Handle(Geom2d_BSplineCurve) bsplineGeom = interpolatorWithParams.Curve();
		BRepBuilderAPI_MakeEdge edgeMaker(bsplineGeom, plane);
		if (!edgeMaker.IsDone()) {
			qDebug() << "Error: Failed to create edge for interpolation curve! Error code:" << edgeMaker.Error();
			return nullptr;
		}
		TopoDS_Shape* shape = new TopoDS_Shape(edgeMaker.Shape());
		qDebug() << "InterpolationCurve2d shape type:" << shape->ShapeType();
		return shape;
	}
	Handle(Geom2d_BSplineCurve) bsplineGeom = interpolator.Curve();
	BRepBuilderAPI_MakeEdge edgeMaker(bsplineGeom, plane);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for interpolation curve! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(edgeMaker.Shape());
	qDebug() << "InterpolationCurve2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createParametricCurve(double(*xFunc)(double),double(*yFunc)(double),double(*zFunc)(double),
	double tmin,double tmax,
	int numPoints)
{
	// 验证输入
	if (tmax <= tmin)
	{
		qDebug() << "参数范围无效：tmax 必须大于 tmin";
		return nullptr;
	}

	if (numPoints < 2)
	{
		qDebug() << "至少需要2个点";
		return nullptr;
	}
	// 创建点集
	TColgp_Array1OfPnt points(1, numPoints);
	double step = (tmax - tmin) / (numPoints - 1);

	for (int i = 1; i <= numPoints; ++i) 
	{
		double t = tmin + (i - 1) * step;
		points.SetValue(i, gp_Pnt(xFunc(t), yFunc(t), zFunc(t)));
	}

	// 创建B样条曲线
	GeomAPI_PointsToBSpline curveBuilder;
	curveBuilder.Init(points);
	Handle(Geom_BSplineCurve) bsplineCurve = curveBuilder.Curve();

	// 将BSpline曲线转换为通用的曲线句柄
	Handle(Geom_Curve) curve = bsplineCurve;

	// 正确创建拓扑边的方式 - 使用Geom_Curve句柄
	BRepBuilderAPI_MakeEdge makeEdge(curve);
	if (!makeEdge.IsDone()) 
	{
		qDebug() << "无法从B样条曲线创建拓扑边";
		return nullptr;
	}
	TopoDS_Edge edge = makeEdge.Edge();

	// 返回Shape指针
	return new TopoDS_Shape(edge);
}


TopoDS_Shape* GeometryCreator::createBezier2Curve2d(const std::vector<gp_Pnt2d>& controlPoints)
{
	if (controlPoints.size() != 3) {
		qDebug() << "Error: Bezier2 curve requires exactly 3 control points!";
		return nullptr;
	}
	Handle(Geom_Plane) plane = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
	TColgp_Array1OfPnt2d points(1, 3);
	for (int i = 0; i < 3; ++i) {
		points.SetValue(i + 1, controlPoints[i]);
	}
	Handle(Geom2d_BezierCurve) bezierGeom = new Geom2d_BezierCurve(points);
	BRepBuilderAPI_MakeEdge edgeMaker(bezierGeom, plane);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for Bezier2 curve! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(edgeMaker.Shape());
	qDebug() << "Bezier2Curve2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape* GeometryCreator::createBezier3Curve2d(const std::vector<gp_Pnt2d>& controlPoints)
{
	if (controlPoints.size() != 4) {
		qDebug() << "Error: Bezier3 curve requires exactly 4 control points!";
		return nullptr;
	}
	Handle(Geom_Plane) plane = new Geom_Plane(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
	TColgp_Array1OfPnt2d points(1, 4);
	for (int i = 0; i < 4; ++i) {
		points.SetValue(i + 1, controlPoints[i]);
	}
	Handle(Geom2d_BezierCurve) bezierGeom = new Geom2d_BezierCurve(points);
	BRepBuilderAPI_MakeEdge edgeMaker(bezierGeom, plane);
	if (!edgeMaker.IsDone()) {
		qDebug() << "Error: Failed to create edge for Bezier3 curve! Error code:" << edgeMaker.Error();
		return nullptr;
	}
	TopoDS_Shape* shape = new TopoDS_Shape(edgeMaker.Shape());
	qDebug() << "Bezier3Curve2d shape type:" << shape->ShapeType();
	return shape;
}

TopoDS_Shape * GeometryCreator::linearExtrusion(TopoDS_Shape* face, double height)
{
	// 定义拉伸方向和距离（沿 Z 轴拉伸）
	gp_Vec extrusionVector(0.0, 0.0, height);
	// 执行线性拉伸
	BRepPrimAPI_MakePrism prismMaker(*face, extrusionVector);
	auto shape = prismMaker.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape * GeometryCreator::rotationalExtrusion(TopoDS_Shape * face, const gp_Ax1& axis, double angle)
{
	//gp_Ax1 axis2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)); // 绕 Z 轴旋转
	//double angle = 2 * M_PI; // 旋转 360 度
	//shape = rotationalExtrusion(shape, axis2, angle);
	// 执行旋转拉伸（角度以弧度为单位）
	BRepPrimAPI_MakeRevol revolMaker(*face, axis, angle);
	auto shape = revolMaker.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape * GeometryCreator::pathExtrusion(TopoDS_Shape * section, const TopoDS_Wire & path)
{
	/*gp_Pnt p1(0, 0, 0);
	gp_Pnt p2(0, 0, 10);
	gp_Pnt p3(10, 0, 10);
	TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(p1, p2);//创建一个折线路径
	TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(p2, p3);
	auto wire2 = BRepBuilderAPI_MakeWire(e1, e2);
	shape = pathExtrusion(shape, wire2);*/
	BRepOffsetAPI_MakePipe pipeMaker(path, *section);
	auto shape = pipeMaker.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::fixShape(TopoDS_Shape* inputShape) 
{
	ShapeFix_Shape fixer(*inputShape);
	fixer.SetPrecision(1e-6);
	fixer.SetMaxTolerance(1e-3);
	fixer.Perform();
	TopoDS_Shape fixedShape = fixer.Shape();
	return new TopoDS_Shape(fixedShape);
}

TopoDS_Shape* GeometryCreator::booleanIntersection(TopoDS_Shape* shape1, TopoDS_Shape* shape2)
{
	/*TopoDS_Shape box1 = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10, 10, 10).Shape();
	TopoDS_Shape box2 = BRepPrimAPI_MakeBox(gp_Pnt(5, 5, 5), 10, 10, 10).Shape();
	TopoDS_Shape* intersection = booleanIntersection(&box1, &box1);*/
	BRepAlgoAPI_Common commonOp(*shape1, *shape2);
	commonOp.SetFuzzyValue(1e-6); // 设置容差，处理微小几何问题
	auto shape = commonOp.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::booleanDifference(TopoDS_Shape* shape1,TopoDS_Shape* shape2)
{
	BRepAlgoAPI_Cut cutOp(*shape1, *shape2);
	cutOp.SetFuzzyValue(1e-6);
	auto shape = cutOp.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::booleanUnion(TopoDS_Shape* shape1, TopoDS_Shape* shape2)
{
	BRepAlgoAPI_Fuse fuseOp(*shape1, *shape2);
	fuseOp.SetFuzzyValue(1e-6);
	auto shape = fuseOp.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::copyShape(TopoDS_Shape* inputShape)
{
	//BRepBuilderAPI_Copy 创建形状的深拷贝。
	BRepBuilderAPI_Copy copyMaker(*inputShape);
	auto shape = copyMaker.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::mirrorShape(TopoDS_Shape* inputShape, const gp_Ax2& mirrorPlane)
{
	gp_Trsf mirrorTrsf;
	mirrorTrsf.SetMirror(mirrorPlane); // 沿平面镜像
	BRepBuilderAPI_Transform transform(*inputShape, mirrorTrsf);
	auto shape = transform.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::rotateShape(TopoDS_Shape* inputShape, const gp_Ax1& axis, double angle)
{
	gp_Trsf rotationTrsf;
	rotationTrsf.SetRotation(axis, angle); // 角度以弧度为单位
	BRepBuilderAPI_Transform transform(*inputShape, rotationTrsf);
	auto shape = transform.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::translateShape(TopoDS_Shape* inputShape, const gp_Vec& translation)
{
	gp_Trsf translationTrsf;
	translationTrsf.SetTranslation(translation);
	BRepBuilderAPI_Transform transform(*inputShape, translationTrsf);
	auto shape = transform.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::deletePart(TopoDS_Shape* mainShape, TopoDS_Shape* partToRemove) 
{
	BRepAlgoAPI_Cut cutOp(*mainShape, *partToRemove);
	cutOp.SetFuzzyValue(1e-6);
	auto shape = cutOp.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::addFillet(TopoDS_Shape* inputShape, double radius)
{
	BRepFilletAPI_MakeFillet filletMaker(*inputShape);
	// 遍历所有边并添加圆角
	TopExp_Explorer edgeExplorer(*inputShape, TopAbs_EDGE);
	while (edgeExplorer.More()) 
	{
		TopoDS_Edge edge = TopoDS::Edge(edgeExplorer.Current());
		filletMaker.Add(radius, edge);
		edgeExplorer.Next();
	}
	auto shape = filletMaker.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::splitDomain(TopoDS_Shape* inputShape, const gp_Pln& plane) 
{
	// 创建分割平面
	TopoDS_Face planeFace = BRepBuilderAPI_MakeFace(plane, -100, 100, -100, 100);
	// 执行分割（计算交界面）
	BRepAlgoAPI_Section sectionOp(*inputShape, planeFace);
	sectionOp.SetFuzzyValue(1e-6);
	auto shape = sectionOp.Shape();
	return new TopoDS_Shape(shape);
}

TopoDS_Shape* GeometryCreator::splitDomainReturnAll(TopoDS_Shape* inputShape, const gp_Pln& plane)
{
	// 创建分割平面（足够大以覆盖输入形状）
	TopoDS_Face planeFace = BRepBuilderAPI_MakeFace(plane, -100, 100, -100, 100);
	// 初始化分割器
	BOPAlgo_Splitter splitter;
	splitter.AddArgument(*inputShape); // 添加要分割的实体
	splitter.AddTool(planeFace); // 添加分割工具（平面）
	splitter.SetFuzzyValue(1e-6); // 设置容差
	// 执行分割
	splitter.Perform();
	QVector<TopoDS_Solid> subDomains;
	TopExp_Explorer explorer(splitter.Shape(), TopAbs_SOLID);
	while (explorer.More()) 
	{
		TopoDS_Solid solid = TopoDS::Solid(explorer.Current());
		subDomains.push_back(solid);
		explorer.Next();
	}
	// 合并子域为一个 TopoDS_Compound
	TopoDS_Compound compound;
	BRep_Builder builder;
	builder.MakeCompound(compound);
	for (const auto& solid : subDomains)
	{
		builder.Add(compound, solid);
	}
	return new TopoDS_Shape(compound);
}
