#pragma once

#include <QStringList>



class QString;
class QWidget;
class QPushButton;
class QToolButton;
class QLabel;
class QAction;
class SARibbonCategory;
class SARibbonPannel;
class QCheckBox;
class QComboBox;
class QSlider;
class SARibbonPannel;
template <typename T> class QList;

struct UiFrameService
{
    //在Dock中添加窗口
    virtual QAction *addWidget(QWidget *widget, QString name) = 0;\
    //根据tabName添加tool按钮。group作为二级
    virtual void addMenuButton(QString tabName, QString groupName, QToolButton *button) = 0;
    //添加文件tool按钮
    virtual void addFileButton(QPushButton *button) = 0;
	virtual QAction * addWidgetForTabwidget(QWidget * widget, QString name) = 0;

	virtual void addMenuAction(QAction* action) = 0;
	virtual void addMenuSeparator() = 0;
	virtual void addRibbonCategory(const QString& catName, const QString& objName) = 0;
	virtual SARibbonCategory* getCategoryByObjName(const QString& objName) = 0;
    virtual SARibbonPannel* addRibbonPanel(SARibbonCategory* cate, const QString& name) = 0;
    virtual bool addRibbonPanelAction(SARibbonPannel* panel, QAction* action, int actionType = 1) = 0;
    virtual QList<QAction*> getPanelAllAcition(SARibbonPannel* panel) = 0;
    virtual QList<SARibbonPannel*> getCategoryAllPanel(SARibbonCategory* cate) = 0;
    virtual void addRibbonPanelCombox(SARibbonPannel * panel, QAction* mainAction, const QString & mainActionName,
		QList<QAction*> actions, const QList<QString> objNames, int actionType = 3) = 0;
    virtual QCheckBox* addRibbonPanelCheckBox(SARibbonPannel * panel, const QString & text,
		const QString & objName, int actionType = 1) = 0;
    virtual QComboBox* addRibbonPanelBtnWidget(SARibbonPannel * panel, const QString & btnWidgetText, const QString & btnWidgetObjName,
		const QString & labelText, const QString & labelObjName, 
		const QString & comboxObjName, const QStringList& comboxNames,int actionType = 1) = 0;
    virtual void setRibbonPanelEnabled(SARibbonPannel * panel, bool isEnable) = 0;
    virtual void setRibbonPanelVisible(SARibbonPannel * panel, bool isVisible) = 0;
	virtual void setRibbonCategoryEnabled(SARibbonCategory * cate, bool isEnable) = 0;
	virtual void setRibbonCategoryVisible(SARibbonCategory * cate, bool isVisible) = 0;
    virtual void addRibbonPanelSlider(SARibbonPannel* panel, QLabel* label1,
		QLabel* label2, QSlider* slider, int actionType = 1) = 0;
	// 文件
	virtual QAction* getNewProAct()const = 0;
	virtual QAction* getOpenProAct()const = 0;
	virtual QAction* getSaveProAct()const = 0;
	virtual QAction* getExportCurDatabaseAct()const = 0;
	virtual QAction* getExitAct()const = 0;
    // 参数
    virtual QAction* getParamAct()const = 0;
	// 结果
	virtual QAction* getNephogramResultAct()const = 0;
	virtual QAction* getVectorsResultAct()const = 0;
	virtual QAction* getSectionResultAct()const = 0;
	virtual QAction* getDiceResultAct()const = 0;
	virtual QAction* getIsosurfacesResultAct()const = 0;
	virtual QAction* getIsosurfacesSegResultAct()const = 0;
	virtual QAction* getXyPlotResultAct()const = 0;
	virtual QAction* getAnimationResultAct()const = 0;
	virtual QAction* getPickPhysicalResultAct()const = 0;
	virtual QAction* getDataExpResultAct()const = 0;
	virtual QAction* getDerivativeResultAct()const = 0;
	virtual QAction* getMaxResultAct()const = 0;
	virtual QAction* getMinResultAct()const = 0;
	virtual QAction* getAverResultAct()const = 0;
	// 几何
	virtual QAction* getImportGemoAct()const = 0;
	virtual QAction* getExportGemoAc()const = 0;
	virtual QAction* getCircleGemoAct()const = 0;
	virtual QAction* getSquareGemoAct()const = 0;
	virtual QAction* getEllipseGemoAct()const = 0;
	virtual QAction* getPolygonGemoAct()const = 0;
	virtual QAction* getRectangleGemoAct()const = 0;
	virtual QAction* getPointGemoAct()const = 0;
	virtual QAction* getLineGemoAct()const = 0;
	virtual QAction* getInerCurveGemoAct()const = 0;
	virtual QAction* getParamCurveGemoAct()const = 0;
	virtual QAction* getArcGemoAct()const = 0;
	virtual QAction* getThreeBsrCurveAct()const = 0;
	virtual QAction* getTwoBsrCurveActt()const = 0;
	virtual QAction* getParabolaAct()const = 0;
	virtual QAction* getHyperbolaAct()const = 0;
	virtual QAction* getDelGemoAct()const = 0;
	virtual QAction* getCuboidGemoAct()const = 0;
	virtual QAction* getSphereGemoAct()const = 0;
	virtual QAction* getConeGemoAct()const = 0;
	virtual QAction* getRingGemoAct()const = 0;
	virtual QAction* getCylinderGemoAct()const = 0;
	virtual QAction* getSpiralGemoAct()const = 0;
	virtual QAction* getSolidViewActt()const = 0;
	virtual QAction* getLineViewAct()const = 0;
	virtual QAction* getTransmissionViewAct()const = 0;
	virtual QAction* getMeasureViewAct()const = 0;
	virtual QAction* getMeasurePositionViewAct()const = 0;
	virtual QAction* getMeasureCentroidViewAct()const = 0;
	virtual QAction* getMeasureDistanceViewAct()const = 0;
	virtual QAction* getShowFaceViewAct()const = 0;
	virtual QAction* getShowLineViewAct()const = 0;
	virtual QAction* getShowPointViewAct()const = 0;
	virtual QComboBox* getChooseGeomCombox()const = 0;
	// 网格
	virtual QAction* getImportGridAct()const = 0;
	virtual QAction* getExportGridAct()const = 0;
	//virtual QAction* getTriangleGridAct()const = 0;
	//virtual QAction* getQuadGridAct()const = 0;
	//virtual QAction* getTetrahedronGridAct()const = 0;
	//virtual QAction* getHexahedronGridAct()const = 0;
	virtual QAction* getCurvedSurfaceGridAct()const = 0;
	virtual QAction* getEntityGridAct()const = 0;
	virtual QAction* getResetGridAct()const = 0;
	virtual QAction* getDrawGridAct()const = 0;
	virtual QAction* getCountInfoGridAct()const = 0;
	virtual QAction* getResizeGridAct()const = 0;
	virtual QAction* getShowGridAct()const = 0; 
	virtual QAction* getSizeGridAct()const = 0;
	virtual QAction* getJxGridAct()const = 0;
	virtual QAction* getCxGridAct()const = 0;
	virtual QAction* getGxGridAct()const = 0;
	virtual QAction* getXhGridAct()const = 0;
	virtual QAction* getCgGridAct()const = 0;
	virtual QAction* getChGridAct()const = 0;
	virtual QAction* getGcGridAct()const = 0;
	virtual QAction* getCcGridAct()const = 0;
	virtual QAction* getJcGridAct()const = 0;
	// 显示
	virtual QAction* getX1ViewAct()const = 0;
	virtual QAction* getX2ViewAct()const = 0;
	virtual QAction* getY1ViewAct()const = 0;
	virtual QAction* getY2ViewAct()const = 0;
	virtual QAction* getZ1ViewAct()const = 0;
	virtual QAction* getZ2ViewAct()const = 0;
	virtual QAction* getResetViewAct()const = 0;
	virtual QAction* getResetWidgetViewAct()const = 0;
	virtual QCheckBox* getStaffCheckViewAct()const = 0;
	virtual QCheckBox* getLightCheckViewAct()const = 0;
	virtual QComboBox* getColorBoxAct()const = 0;
	virtual QLabel* getTransLabelAct()const = 0;
	virtual QSlider* getTransSliderAct()const = 0;
	// 边界条件
	virtual QAction* getSolidBoundAct()const = 0;
	virtual QAction* getFaceBoundAct()const = 0;
	virtual QAction* getLineBoundAct()const = 0;
	virtual QAction* getPointBoundAct()const = 0;

};
