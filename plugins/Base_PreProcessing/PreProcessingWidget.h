#ifndef PREPROCESSINGWIDGET_H
#define PREPROCESSINGWIDGET_H

#include <QWidget>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <QToolBar>
#include <services/UiFrameService.h>
#include <services/OutputService.h>
#include <services/ProjectManagerService.h>
#include <ServiceRegistry.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <QFileDialog>
#include <BRepMesh_IncrementalMesh.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <CPnts_AbscissaPoint.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Pnt.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <QDebug>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <TopoDS.hxx>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include "vtkAxesActor.h"
#include "vtkOrientationMarkerWidget.h"
#include "vtkCamera.h"
#include "GeometryCreator.h"
#include "GemoRWUtilInstance.h"
#include "GeomManagerInstance.h"
#include "Boxdialog.h"
#include "ConeDialog.h"
#include "SphereDialog.h"
#include "TorusDialog.h"
#include "CylinderDialog.h"
#include "HelixDialog.h"
#include "MeshProcessor.h"
#include "vtkUnstructuredGridReader.h"
#include "SMThreading.h"
#include "GemoPickerInteractorStyle.h"
#include "qcombobox.h"
#include "CircleDialog.h"
#include "EllipseDialog.h"
#include "PointDialog.h"
#include "RectangleDialog.h"
#include "SquareDialog.h"
#include "PolygonDialog.h"
#include "ArcDialog.h"
#include "LineDialog.h"
#include "ParabolaDialog.h"
#include "HyperbolaDialog.h"
#include "Bezier2Dialog.h"
#include "Bezier3Dialog.h"
#include "InterpolationDialog.h"
#include "ParametricCurveDialog.h"
#include "PointPickingDialog.h"
#include "CentroidPickingDialog.h"
#include "DistancePickingDialog.h"
#include "MeshRefinementWidget.h"
#include "MeshRefinement3DWidget.h"
#include "GeometryImportWidget.h"
#include "GridImportWidget.h"
#include "ProjectNode.h"

namespace Ui {
class PreProcessingWidget;
}

enum class ViewType
{
	FitView = 0,
	XPlusView,
	XMinusView,
	YPlusView,
	YMinusView,
	ZPlusView,
	ZMinusView,
};

enum MeasureSelectionType
{
	SinglePoint = 0, // 位置测量一个点
	CentroidPoints, // 质心测量质心点集
	PointToPoint, // 距离测量的两点
	PointToLine, // 距离测量的点和线
	TwoPoints,   // 两个点
	Line,        // 一条线
	Face         // 一个面
};

class PreProcessingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreProcessingWidget(QWidget *parent = nullptr);
    ~PreProcessingWidget();
	/**
	 * @brief 设置几何树节点
	 */
	void setGeomNode(ProjectNode* node) { m_geomNode = node; };
	/**
	 * @brief 设置网格树节点
	 */
	void setGridNode(ProjectNode* node) { m_gridNode = node; };
	/**
	 * @brief 二维曲面网格窗口
	 */
	BasePage* newTetrahedronGridWidget(QString title = "二维曲面网格");
	/**
	 * @brief 三维实体网格窗口
	 */
	BasePage* newHexahedronGridWidget(QString title = "三维实体网格");
	/**
	 * @brief 导入几何窗口
	 */
	BasePage* newGeometryImportWidget(QString title = "几何导入");
	/**
	 * @brief 导入几何窗口
	 */
	BasePage* newGridImportWidget(QString title = "网格导入");
	/**
	 * @brief 长方体窗口
	 */
	BasePage* newGeometryBoxWidget(QString title = "长方体");
	/**
	 * @brief 球体窗口
	 */
	BasePage* newGeometrySphereWidget(QString title = "球体");
	/**
	 * @brief 圆锥窗口
	 */
	BasePage* newGeometryConeWidget(QString title = "圆锥");
	/**
	 * @brief 圆环窗口
	 */
	BasePage* newGeometryTorusWidget(QString title = "圆环");
	/**
	 * @brief 圆柱体窗口
	 */
	BasePage* newGeometryCylinderWidget(QString title = "圆柱体");
	/**
	 * @brief 螺旋窗口
	 */
	BasePage* newGeometryHelixWidget(QString title = "螺旋");
	/**
	 * @brief 圆窗口
	 */
	BasePage* newGeometryCircleWidget(QString title = "圆");
	/**
	 * @brief 正方形窗口
	 */
	BasePage* newGeometrySquareWidget(QString title = "正方形");
	/**
	 * @brief 椭圆窗口
	 */
	BasePage* newGeometryEllipseWidget(QString title = "椭圆");
	/**
	 * @brief 多边形窗口
	 */
	BasePage* newGeometryPolygonWidget(QString title = "多边形");
	/**
	 * @brief 矩形窗口
	 */
	BasePage* newGeometryRectangleWidget(QString title = "矩形");
	/**
	 * @brief 点窗口
	 */
	BasePage* newGeometryPointWidget(QString title = "点");
	/**
	 * @brief 圆弧窗口
	 */
	BasePage* newGeometryArcWidget(QString title = "圆弧");
	/**
     * @brief 三次贝塞尔窗口
     */
	BasePage* newGeometryThreeBsrWidget(QString title = "三次贝塞尔曲线");
	/**
	 * @brief 二次贝塞尔窗口
	 */
	BasePage* newGeometryTwoBsrWidget(QString title = "二次贝塞尔曲线");
	/**
	 * @brief 线段窗口
	 */
	BasePage* newGeometryLineWidget(QString title = "线段");
	/**
	 * @brief 插值曲线窗口
	 */
	BasePage* newGeometryInterCurveWidget(QString title = "插值曲线");
	/**
	 * @brief 参数化曲线窗口
	 */
	BasePage* newGeometryParamCurveWidget(QString title = "参数化曲线");
	/**
	 * @brief 抛物线
	 */
	BasePage* newGeometryParabolaEdgeWidget(QString title = "抛物线");
	/**
	 * @brief 双曲线
	 */
	BasePage* newGeometryHyperbolaWidget(QString title = "双曲线");
	/**
	 * @brief 响应模型树的页面删除
	 */
	void onTreeDeletePage(const QString & uuid);

	bool getGridIsDraw();

	/**
	 * @brief 设置点选类型
	 */
	void onSetSelectType(int type,bool isReset = true);

	/**
	 * @brief 实体显示
	 */
	void solidViewActor();

	int getNewSelGeomIndex(int type);

	std::vector<int> getGridDataByGridIndex(int type, int index);

    QList<QAction*> getViewActions() const;

    /**
     * @brief 移除所有actor
     */
    void removeAllActor();

    /**
     * @brief 移除actor
     * @param actor
     */
    void removeActor(vtkSmartPointer<vtkActor> actor);

    /**
     * @brief 创建简单几何体的后处理
     * @param shape 形状指针
     */
    void afterCreateSimpleGeom(TopoDS_Shape* shape,const QString &uuid);

    void addActor(vtkSmartPointer<vtkActor> actor);
public slots:
    void showEvent(QShowEvent *event);
private:
    Ui::PreProcessingWidget *ui;
	UiFrameService * m_uiFrameService;
	OutputService * m_outputService;
	ProjectManagerService * m_projectService;

    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderWindowInteractor> m_renderWindowInteractor;
	QList<vtkSmartPointer<vtkActor> > m_mainGeomActor;
	QList<vtkSmartPointer<vtkActor> > m_mainGridActor;
	vtkSmartPointer <GemoPickerInteractorStyle> m_picker;
	vtkSmartPointer<vtkLight> m_savedLight;

    QAction* m_fitView{ nullptr };
    QAction* m_XplusView{ nullptr };
    QAction* m_XminView{ nullptr };
    QAction* m_YplusView{ nullptr };
    QAction* m_YminView{ nullptr };
    QAction* m_ZplusView{ nullptr };
    QAction* m_ZminView{ nullptr };
    QAction* m_perspective{ nullptr };
    QAction* m_configView{nullptr};
private:
	/**
	 * @brief 初始化槽函数
	 */
	void initRibbonActionConn();
	/**
	 * @brief 导入几何
	 */
	bool importGeom(const QString & filePath, const QString & uuid);
	/**
	 * @brief 导出几何
	 */
	bool exportGeom();
	/**
	 * @brief 线图显示
	 */
	void lineViewActor();
	/**
	 * @brief 透射显示
	 * @note 50/100透明度进行切换
	 */
	void transmissionViewActor();
	/**
	 * @brief 添加几何actor
	 * @param actor
	 */
	void addGeomActor(const vtkSmartPointer<vtkActor> actor);
	/**
	 * @brief 添加几何actorList
	 * @param actors actor列表
	 */
	void addGeomActorList(const QList<vtkSmartPointer<vtkActor>> actors);
	/**
	 * @brief 添加网格actor
	 * @param actor
	 */
	void addGridActor(const vtkSmartPointer<vtkActor> actor);
	/**
	 * @brief 添加网格actorList
	 * @param actors actor列表
	 */
	void addGridActorList(const QList<vtkSmartPointer<vtkActor>> actors);

	/**
	 * @brief 移除所有几何actor
	 */
	void removeAllGeomActor();
	/**
	 * @brief 移除uuid的所有数据
	 */
	void removeUuidData(const QString & uuid);
	/**
	 * @brief 添加uuid的所有数据
	 */
	void addUuidData(const QString & uuid, TopoDS_Shape* shape,
		GeomShapeObject* object, QList<vtkActor*> actors);
	/**
	 * @brief 通过uuid移除几何actor
	 */
	void removeActorByUuid(const QString & uuid);
	/**
	 * @brief 通过uuid移除几何shape
	 */
	void removeGeomShapeByUuid(const QString & uuid);
	/**
	 * @brief 通过uuid移除几何object
	 */
	void removeGeomObjectByUuid(const QString & uuid);
	/**
	 * @brief 设置actor的可见性
	 * @param isShow  是否显示
	 * @param actor 
	 */
	void setActorVisibility(bool isShow, vtkSmartPointer<vtkActor> actor);
	/**
	 * @brief 设置所有几何actor的可见性
	 * @param isShow  是否显示
	 */
	void setAllGeomActorVisibility(bool isShow);
	/**
	 * @brief 设置所有网格actor的可见性
	 * @param isShow  是否显示
	 */
	void setAllGridActorVisibility(bool isShow);
	/**
	 * @brief 移除所有网格actor
	 */
	void removeAllGridActor();

	/**
	 * @brief 全显面
	 */
	void onShowFace();
	/**
	 * @brief 全显线
	 */
	void onShowLine();
	/**
	 * @brief 全显点
	 */
	void onShowPoint();
	/**
	 * @brief 测量按钮点击
	 */
	void onMeasureView();
	/**
	 * @brief 位置测量按钮点击
	 */
	void onMeasurePositionView();
	/**
	 * @brief 质心测量按钮点击
	 */
	void onMeasureCentroidView();
	/**
	 * @brief 距离测量按钮点击
	 */
	void onMeasureDistanceView();
	/**
	* @brief 删除创建的几何
	*/
	void deleteCreateGemo();
	/**
	 * @brief 创建临时几何文件
	 * @param tempPath 临时路径
	 */
	bool createTemporaryStepFile(const QString & tempPath);
	/**
	 * @brief 生成几何临时文件夹路径
	 */
	QString createTemporaryStepFilePath();

	/**
	 * @brief 刷新视图窗口
	 */
	void reFresh();
	/**
	 * @brief 创建三维坐标轴
	 */
	void createOrientationMarkerWidget();
	/**
	 * @brief 设置视图
	 */
	void setView(ViewType type);
	/**
	 * @brief 设置视图
	 */
	void setViewValue(double x1, double x2, double x3, double y1, double y2, double y3, double z1, double z2, double z3);
	/**
	 * @brief 调整视图到默认位置
	 */
	void fitView();
	/**
	 * @brief 返回当前几何路径
	 */
	QString getCurGeomPath()const {return m_curGeomPath;}
	/**
	 * @brief 计算点的位置坐标（位置测量）
	 */
	void calculateGeomPointPos(GeomShapeObject * object, int index);
	/**
	 * @brief 计算点集的质心坐标（质心测量）
	 */
	void calculateGeomPointsCentroid(GeomShapeObject * object, int index);
	/**
	 * @brief 计算距离测量的第一个点（距离测量）
	 */
	void calculateGeomFirstPointDistance(GeomShapeObject * object, int index);
	/**
	 * @brief 计算距离测量的第二个点或者线（距离测量）
	 */
	void calculateGeomSecondPointOrLineDistance(GeomShapeObject * object, int index);
	/**
	 * @brief 测量点线之间的距离（距离测量）
	 */
	QList<double> distancePointToEdge(const gp_Pnt& point, const TopoDS_Edge& edge);
	/**
	 * @brief 计算被选中的两点的长度（测量）
	 */
	void calculateGeomTwoPointLength(GeomShapeObject * object, int index);
	/**
	 * @brief 计算被选中的曲线的长度（测量）
	 */
	void calculateGeomCurveLength(GeomShapeObject * object, int index);
	/**
	 * @brief 计算被选中的面的面积（测量）
	 */
	void calculateGeomFaceArea(GeomShapeObject * object, int index);
	/**
	 * @brief 计算被选中的体的体积（测量）
	 */
	void calculateGeomSolidVolume(GeomShapeObject * object, int index);
	/**
	 * @brief 绘制网格
	 */
	bool drawGrid();
	/**
	 * @brief 设置网格输入的参数
	 */
	void setGridParams(int type);
	/**
	 * @brief 统计网格信息
	 */
	void countGridInfo();
	/**
	 * @brief 导入网格
	 */
	bool importGrid(const QString & filePath, const QString & uuid);
	/**
	 * @brief 导出网格
	 */
	bool exportGrid();
	/**
	 * @brief 加载生成的vtk文件
	 */
	void importGmshGrid(const QString & vtkPath);
	/**
	 * @brief 显示网格actor
	 */
	void gridViewActor();
	/**
	 * @brief 判断当前节点下面有没有特定的名称(用于标题栏给模型树添加唯一节点)
	 */
	bool hasDirectChildNamed(QTreeWidgetItem* parent, const QString& name);
private slots:
	/**
	 * @brief 响应网格线程结束
	 */
	bool onDrawGridTaskFinished();
	/**
	 * @brief 响应被选中
	 */
	void onGeomShapeSelectedSig(GeomShapeObject * object, int index);
	/**
	 * @brief 响应点选
	 */
	void onSelectGemoPoint(vtkActor* actor, int cellIndex, int pointIndex);
	/**
	 * @brief 响应线选
	 */
	void onSelectGemoLine(vtkActor* actor, int cellIndex, int pointIndex);
	/**
	 * @brief 响应面选
	 */
	void onSelectGemoFace(vtkActor* actor, int cellIndex, int pointIndex);
	/**
	 * @brief 响应体选
	 */
	void onSelectGemoSolid(vtkActor* actor, int cellIndex, int pointIndex);
	/**
	 * @brief 重置所有颜色
	 */
	void resetGeomColor();
	/**
	 * @brief 重置几何颜色
	 * @param type 1234 点线面体
	 * @param index 几何索引
	 */
	void resetGeomColorByIndex(int type,int index, const QString& uuid);
	/**
	 * @brief 响应测量距离按钮点击
	 */
	void onMeasurePosBtnToggled(bool isConnect);
	/**
     * @brief 响应测量质心按钮点击
     */
	void onMeasureCentroidBtnToggled(bool isConnect);
	/**
	 * @brief 响应测量距离第一个点按钮点击
	 */
	void onMeasureDistancePointBtnToggled(bool isConnect);
	/**
	 * @brief 响应测量距离第二个点或者线按钮点击
	 */
	void onMeasureDistanceLineBtnToggled(bool isConnect, const QString& type);
	/**
	 * @brief 响应对话框的计算质心按钮
	 */
	void onCalculateCentroidRequested();
	/**
	 * @brief 响应对话框的计算距离按钮
	 */
	void onCalculateDistanceRequested();
	/**
	 * @brief 响应长方体对话框确认按钮
	 */
	void onBoxParamSubmitted(const QString& uuid, double length, double width, double height, const gp_Pnt &position);
	/**
	 * @brief 响应球体对话框确认按钮
	 */
	void onSphereParamSubmitted(const QString& uuid, double radius, const gp_Pnt &position);
	/**
	 * @brief 响应圆锥对话框确认按钮
	 */
	void onConeParamSubmitted(const QString& uuid, double bottomRadiu, double topRadiu, double height, const gp_Pnt &position);
	/**
	 * @brief 响应圆环对话框确认按钮
	 */
	void onTorusParamSubmitted(const QString& uuid, double majorRadius, double minorRadius, const gp_Pnt& position);
	/**
	 * @brief 响应圆柱体对话框确认按钮
	 */
	void onCylinderParamSubmitted(const QString& uuid, double radius, double height, const gp_Pnt& position);
	/**
	 * @brief 响应螺旋对话框确认按钮
	 */
	void onHelixParamSubmitted(const QString& uuid, double radius, double height, double pitch, const gp_Pnt& position);
	/**
	 * @brief 响应圆对话框确认按钮
	 */
	void onCircleParamSubmitted(const QString& uuid, double radius, const gp_Ax2& position);
	/**
	 * @brief 响应正方形对话框确认按钮
	 */
	void onSquareParamSubmitted(const QString& uuid, double length, const gp_Pnt2d& position);
	/**
	 * @brief 响应椭圆对话框确认按钮
	 */
	void onEllipseParamSubmitted(const QString& uuid, double majorRadiu, double minorRadius, const gp_Pnt2d& position);
	/**
	 * @brief 响应多边形对话框确认按钮
	 */
	void onPolygonParamSubmitted(const QString& uuid, double radius, int sides, const gp_Pnt2d& position);
	/**
	 * @brief 响应矩形对话框确认按钮
	 */
	void onRectangleParamSubmitted(const QString& uuid, double length, double width, const gp_Pnt2d& position);
	/**
	 * @brief 响应点对话框确认按钮
	 */
	void onPointParamSubmitted(const QString& uuid, const gp_Pnt2d& position);
	/**
	 * @brief 响应圆弧对话框确认按钮
	 */
	void onArcParamSubmitted(const QString& uuid, double radius, double startAngle, double endAngle,
		const gp_Pnt& position);
	/**
	 * @brief 响应三次贝塞尔对话框确认按钮
	 */
	void onThreeBsrParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec);
	/**
	 * @brief 响应二次贝塞尔对话框确认按钮
	 */
	void onTwoBsrParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec);
	/**
	 * @brief 响应线段对话框确认按钮
	 */
	void onLineParamSubmitted(const QString& uuid, gp_Pnt2d startPoint, gp_Pnt2d endPoint);
	/**
	 * @brief 响应插值曲线对话框确认按钮
	 */
	void onInterCurveParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec);
	/**
	 * @brief 响应抛物线对话框确认按钮
	 */
	void onParabolaParamSubmitted(const QString& uuid, double focal, double min, double max, int points);
	/**
	 * @brief 响应双曲线对话框确认按钮
	 */
	void onHyperbolaParamSubmitted(const QString& uuid, double major, double focal, double min, double max, int points);
	/**
	 * @brief 响应光照复选框状态变化
	 */
	void onLightCheckBoxStateChanged(int state);
	/**
	 * @brief 响应透明度值变化
	 */
	void onTransValueChanged(int value);
	/**
	 * @brief 响应选中的几何的索引
	 */
	void onSelectedGeomShapeIndex(GeomShapeObject * shape, int type, int _index);
    void newOccBoolenToolWidget();
private:
	ProjectNode* m_geomNode = nullptr;//几何树节点
	ProjectNode* m_gridNode = nullptr;//网格树节点
	SMThreadingTool::SMThreading* m_threadPool;// 线程池工具类
	MeshProcessor m_meshProcessor;// mesh工具类
	int m_selectGemoType = 0;// 点选类型
	MeasureSelectionType m_measureSelectionType;// 测量类型

	QPair<GeomShapeObject *, int> m_measurePointPair;// 位置测量的上一个点
	QList<QPair<GeomShapeObject *, int>> m_centroidPointsPair;// 质心点集
	QList<gp_Pnt> m_centroidPointsPos;// 质心点集坐标

	QPair<GeomShapeObject *, int> m_distanceFirstPointPair;// 距离测量的第一个点
	gp_Pnt m_distanceFirstPos{0,0,0};// 距离测量的第一个点坐标
	QPair<GeomShapeObject *, int> m_distanceSecondPointPair;// 距离测量的第二个点
	gp_Pnt m_distanceSecondPos{ 0,0,0 };// 距离测量的第二个点坐标
	QPair<GeomShapeObject *, int> m_distanceLine;// 距离测量的线
	TopoDS_Edge m_distanceEdge;// 距离测量的线的对象
	QString m_distancePickingType; // 距离测量的类型（点/线）

	QList<QPair<GeomShapeObject *, int>> m_pointSelectList;// 测量两点距离的列表
	double m_point[3];
	QPair<GeomShapeObject *, int> m_line{};// 线
	QPair<GeomShapeObject *, int> m_face{};// 面
	QPair<GeomShapeObject *, int> m_solid{};// 体

	MeshRefinementWidget* m_meshRefinementWidget = nullptr;// 二维曲面网格指针(为了绘制网格)
	MeshRefinement3DWidget* m_meshRefinement3DWidget = nullptr;// 三维实体网格指针(为了绘制网格)

	QString m_curGeomPath = "";// 当前几何路径
	QMap<QString, GeomShapeObject*> m_uuidGeomObject;//页面uuid和几何对象的映射
	QMap<QString, TopoDS_Shape*> m_uuidGeomShape;//页面uuid和几何形状的映射
	QMap<QString, QList<vtkActor*>> m_uuidActMap;//页面uuid和当前actor的映射

	bool m_bDrawGrid = false;// 绘制网格是否成功
	int m_iNewSelPointGeomIndex = -1;
	int m_iNewSelLineGeomIndex = -1;
	int m_iNewSelFaceGeomIndex = -1;
	int m_iNewSelSolidGeomIndex = -1;
    bool writeMultipleShapesStepFile(const QString &filePath);
};

#endif // PREPROCESSINGWIDGET_H
