#ifndef UIFRAME_H
#define UIFRAME_H

#include <QMainWindow>
#include <QTreeWidget>
#include <DockManager.h>
#include <DockWidget.h>
#include <QVBoxLayout>
#include <services/UiFrameService.h>
#include <QHeaderView>
#include <QToolButton>
#include <QLabel>
#include <QStackedWidget>

// saribbon
#include "SARibbonMainWindow.h"
#include "SARibbonApplicationButton.h"
#include "SARibbonBar.h"
#include "SARibbonMenu.h"
#include "SARibbonComboBox.h"
#include "SARibbonCheckBox.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonSystemButtonBar.h"
#include "SARibbonQuickAccessBar.h"

class UiFrame: public SARibbonMainWindow, public UiFrameService
{
    Q_OBJECT
public:
	UiFrame(QWidget* par = nullptr,
		SARibbonMainWindowStyles style = { static_cast<int>(SARibbonMainWindowStyleFlag::UseNativeFrame)
										   | static_cast<int>(SARibbonMainWindowStyleFlag::UseRibbonMenuBar) });
    //析构时存储布局
    ~UiFrame();
	void closeEvent(QCloseEvent *event)override;

    //在Dock中添加窗口
    QAction *addWidget(QWidget *widget, QString name);
	/**
	 * @brief 添加停靠窗口(tabwidget样式)
	 */
	QAction * addWidgetForTabwidget(QWidget * widget, QString name)override;
    //根据tabName添加tool按钮。group作为二级
    void addMenuButton(QString tabName, QString groupName, QToolButton *button);
    //添加文件tool按钮
    void addFileButton(QPushButton *button);
    //恢复布局
    void restoreUiState();
    //保存布局
    void storeUiState();
	/**
	 * @brief 添加左上角文件下面的action
	 * @param action 按钮
	 */
	void addMenuAction(QAction* action);
	/**
	 * @brief 添加左上角文件下面的分隔栏
	 */
	void addMenuSeparator();
	/**
     * @brief 添加顶层的标签页
	 * @param catName 标签页名称
	 * @param objName object名称
	 * @note SARibbonBar的Category和Pannel，以及对应的Action都应该设置ObjectName，因为如果要自定义action，这些ObjectName是必不可少的
	 */
	void addRibbonCategory(const QString& catName, const QString& objName);
	/**
	 * @brief 根据名称获取页面指针
	 * @param objName 标签页objectname
	 */
	SARibbonCategory* getCategoryByObjName(const QString& objName);
	/**
	 * @brief 添加按钮面板
	 * @param cate SARibbonCategory*指针
	 * @param name 面板名称
	 */
    SARibbonPannel* addRibbonPanel(SARibbonCategory* cate,const QString& name);
	/**
	 * @brief 添加按钮面板的下拉框
     * @param panel SARibbonPannel*指针
	 * @param mainAction 下拉框顶端的action
	 * @param mainActionName 下拉框顶端的action名称
	 * @param actions 下拉框弹出的按钮集合
	 * @param objNames 下拉框弹出的按钮Object名称集合
	 * @param actionType mainAction的类型 1为小 2为中 3为大 默认大
	 * @note 注意names的size == actions的size
	 */
    void addRibbonPanelCombox(SARibbonPannel * panel, QAction* mainAction,const QString & mainActionName,
		QList<QAction*> actions, const QList<QString> objNames, int actionType = 3);
	/**
	 * @brief 添加按钮面板的复选框
     * @param panel SARibbonPannel*指针
	 * @param text 文本信息
	 * @param objName obj名称
	 * @param actionType mainAction的类型 1为小 2为中 3为大 默认小
	 */
    QCheckBox* addRibbonPanelCheckBox(SARibbonPannel * panel, const QString & text,const QString & objName, int actionType = 1);
	/**
	 * @brief 添加按钮面板的按钮窗口
     * @param panel SARibbonPannel*指针
	 * @param btnWidgetText 按钮组文本信息
	 * @param btnWidgetObjName 按钮组obj名称
	 * @param labelText label文本信息
	 * @param labelObjName label obj名称
	 * @param comboxObjName 下拉框obj名称
	 * @param comboxNames 下拉框名称集合
	 * @param actionType mainAction的类型 1为小 2为中 3为大 默认小
	 * @note 左边label右边下拉框
	 */
    QComboBox* addRibbonPanelBtnWidget(SARibbonPannel * panel, const QString & btnWidgetText, const QString & btnWidgetObjName,
		const QString & labelText, const QString & labelObjName, 
		const QString & comboxObjName, const QStringList& comboxNames ,int actionType = 1);
	/**
	 * @brief 添加按钮面板的按钮
     * @param panel SARibbonPannel*指针
	 * @param action 按钮
	 * @param actionType 按钮类型 1代表小按钮 2代表中按钮  3代表大按钮 
	 */
    bool addRibbonPanelAction(SARibbonPannel* panel, QAction* action, int actionType = 1);
	/**
	 * @brief 添加按钮面板的滑动条和label
     * @param panel SARibbonPannel*指针
	 * @param label1 左边解释的label
	 * @param label2 内容显示的label
	 * @param slider 滑动条
	 * @param actionType 按钮类型 1代表小按钮 2代表中按钮  3代表大按钮 
	 */
    void addRibbonPanelSlider(SARibbonPannel* panel, QLabel* label1, QLabel* label2, QSlider* slider, int actionType = 1);
	/**
	 * @brief 得到面板的所有action
     * @param panel SARibbonPannel*指针
	 */
    QList<QAction*> getPanelAllAcition(SARibbonPannel* panel);
	/**
	 * @brief 得到页面的所有面板
	 * @param cate SARibbonCategory*指针
	 */
    QList<SARibbonPannel*> getCategoryAllPanel(SARibbonCategory* cate);
	/**
	 * @brief 设置面板使能
     * @param panel SARibbonPannel*指针
	 * @param isEnable 是否使能
	 */
    void setRibbonPanelEnabled(SARibbonPannel * panel, bool isEnable);
	/**
	 * @brief 设置面板显示
     * @param panel SARibbonPannel*指针
	 * @param isVisible 是否显示
	 */
    void setRibbonPanelVisible(SARibbonPannel * panel, bool isVisible);
	/**
	 * @brief 设置页面使能
	 * @param cate SARibbonCategory*指针
	 * @param isEnable 是否使能
	 */
	void setRibbonCategoryEnabled(SARibbonCategory * cate, bool isEnable);
	/**
	 * @brief 设置页面显示
	 * @param cate SARibbonCategory*指针
	 * @param isVisible 是否显示
	 */
	void setRibbonCategoryVisible(SARibbonCategory * cate, bool isVisible);
private:
    QVBoxLayout *m_mainLayout;
    ads::CDockManager* m_dockManager;
        ads::CDockAreaWidget* m_centerDockArea = nullptr;
        QByteArray m_defaultDockState;
        /**
         * @brief ribbonbarWindow 指针
         */
	SARibbonMainWindow*  m_pSARibbonMainWindow;
	/**
	 * @brief ribbonbarWindow的ribbonbar指针
	 */
	SARibbonBar* m_pSARibbon;
	/**
	 * @brief ribbon的按钮的menu
	 */
	QMenu* m_applicationMenu = nullptr;
	/**
	 * @brief SARibbonCategory映射
	 * @brief 键:名称, 值:指针
	 * @note 注意键不要一致
	 */
	QMap<QString, SARibbonCategory*> m_mRibbonCategoryMap;
	/**
     * @brief SARibbonPannel映射
     * @brief 键:SARibbonCategory指针, 值:SARibbonPannel指针集合
	 * @note 注意键不要一致
	 */
    QMap<SARibbonCategory*,QList<SARibbonPannel*>> m_mRibbonPannelMap;
	/**
     * @brief SARibbonPannel映射所有的action
     * @brief 键:SARibbonPannel指针, 值:action集合
	 * @note 注意键不要一致
	 */
    QMap<SARibbonPannel*, QList<QAction*>> m_mRibbonPannelActions;
	/**
	 * @brief 文件下面的所有action
	 */
	QAction* m_newProAct;
	QAction* m_openProAct;
	QAction* m_saveProAct;
	QAction* m_exportCurDatabaseAct;
	QAction* m_exitAct;
	
	/**
	 * @brief 自定义参数下面的所有action
	 */
	QAction * m_paramAct;
	QAction * m_funcAct;
	/**
	 * @brief 几何下面的所有action
	 */
	QAction* m_importGemoAct;
	QAction* m_exportGemoAct;
	QAction * m_circleGemoAct;
	QAction * m_squareGemoAct;
	QAction * m_ellipseGemoAct;
	QAction * m_polygonGemoAct;
	QAction * m_rectangleGemoAct;
	QAction * m_pointGemoAct;
	QAction * m_arcGemoAct;
	QAction * m_threeBsrCurveAct;
	QAction * m_interCurveAct;
	QAction * m_lineCurveAct;
	QAction * m_paramCurveAct;
	QAction * m_twoBsrCurveAct;
	QAction * m_parabolaAct;
	QAction * m_hyperbolaAct;
	QAction * m_delGemoAct;
	QAction * m_cuboidGemoAct;
	QAction * m_sphereGemoAct;
	QAction * m_coneGemoAct;
	QAction * m_ringGemoAct;
	QAction * m_cylinderGemoAct;
	QAction * m_spiralGemoAct;
	QAction * m_solidViewAct;
	QAction * m_lineViewAct;
	QAction * m_transmissionViewAct;
	QAction * m_measureViewAct;
	QAction * m_measurePositionViewAct;
	QAction * m_measureCentroidViewAct;
	QAction * m_measureDistanceViewAct;
    QAction * m_fuseAct;
    QAction * m_cutAct;
    QAction * m_commonAct;
	QAction * m_showFaceViewAct;
	QAction * m_showLineVViewAct;
	QAction * m_showPointViewAct;
	QComboBox* m_chooseGeomCombox;
	/**
	 * @brief 网格下面的所有action
	 */
	QAction * m_importGridAct;
	QAction * m_exportGridAct;
	//QAction * m_triangleGridAct;
	//QAction * m_quadGridAct;
	//QAction * m_tetrahedronGridAct;
	//QAction * m_hexahedronGridAct;
	QAction * m_curvedSurfaceGridAct;
	QAction * m_entityGridAct;

	QAction * m_resetGridAct;
	QAction * m_drawGridAct;
	QAction * m_countInfoGridAct;
	QAction * m_resizeGemoAct;
	QAction * m_showGridAct;

	QAction * m_sizeGridAct;
	QAction * m_jxGridAct;
	QAction * m_cxGridAct;
	QAction * m_gxGridAct;
	QAction * m_xhGridAct;
	QAction * m_cgGridAct;
	QAction * m_chGridAct;
	QAction * m_gcGridAct;
	QAction * m_ccGridAct;
	QAction * m_jcGridAct;
	/**
	 * @brief 边界条件下面的所有action
	 */
	QAction * m_solidBoundAct;
	QAction * m_faceBoundAct;
	QAction * m_lineBoundAct;
	QAction * m_pointBoundAct;
	/**
	 * @brief 结果下面的所有action
	 */
	QAction * m_nephogramResultAct;
	QAction * m_vectorsResultAct;
	QAction * m_sectionResultAct;
	QAction * m_diceResultAct;
	QAction * m_isosurfacesResultAct;
	QAction * m_isosurfacesSegResultAct;
	QAction * m_xyPlotResultAct;
	QAction * m_animationResultAct;
	QAction * m_pickPhysicalResultAct;
	QAction * m_dataExpResultAct;
	QAction * m_derivativeResultAct;
	QAction * m_maxResultAct;
	QAction * m_minResultAct;
	QAction * m_averResultAct;
	/**
	 * @brief 显示下面的所有action
	 */
	QAction *  m_resetWidgetViewAct;
    QCheckBox* m_staffCheckBox;
	QCheckBox* m_lightCheckBox;
	QAction * m_x1ViewAct;
	QAction * m_x2ViewAct;
	QAction * m_y1ViewAct;
	QAction * m_y2ViewAct;
	QAction * m_z1ViewAct;
	QAction * m_z2ViewAct;
	QAction * m_resetCameraViewAct;
	QSlider* transparencySlider;
	QLabel* transparencyLabel;
    QComboBox* m_colorThemeCombox;
	/**
	 * @brief 代理模型下面的所有action
	 */
	QAction *  m_dataSamplingModelAct;
	QAction *  m_zjSamplingModelAct;
	QAction *  m_ldclfSamplingModelAct;
	QAction * m_dataEvalModelAct;
	QAction * m_dnnModelAct;
	QAction * m_podModelAct;
private:
	/**
	 * @brief 初始化dockmanager
	 */
    void initDockManager();
    //设置DockWidget样式
    void setDockWidgetStyle();
	/**
	 * @brief 基于名称推断停靠区域，保持常见CAE软件的布局习惯
	 */
	ads::DockWidgetArea preferDockArea(const QString& name) const;
        /**
         * @brief 捕获当前dock布局作为默认状态，用于重置
         */
        void captureDefaultDockState();
        void test();
	/**
	 * @brief 初始化ribbonbar
	 */
	void initSARibbon();
	/**
	 * @brief 生成文件下面的button
	 */
    void buildRibbonAppButton();
public:
	QAction* getNewProAct()const { return m_newProAct; };
	QAction* getOpenProAct()const { return m_openProAct; };
	QAction* getSaveProAct()const { return m_saveProAct; };
	QAction* getExportCurDatabaseAct()const { return m_exportCurDatabaseAct; };
	QAction* getExitAct()const { return m_exitAct; };

    QAction* getParamAct() const {return m_paramAct;};

	QAction* getNephogramResultAct()const { return m_nephogramResultAct; };
	QAction* getVectorsResultAct()const { return m_vectorsResultAct; };
	QAction* getSectionResultAct()const { return m_sectionResultAct; };
	QAction* getDiceResultAct()const { return m_diceResultAct; };
	QAction* getIsosurfacesResultAct()const { return m_isosurfacesResultAct; };
	QAction* getIsosurfacesSegResultAct()const { return m_isosurfacesSegResultAct; };
	QAction* getXyPlotResultAct()const { return m_xyPlotResultAct; };
	QAction* getAnimationResultAct()const { return m_animationResultAct; };
	QAction* getPickPhysicalResultAct()const { return m_pickPhysicalResultAct; };
	QAction* getDataExpResultAct()const { return m_dataExpResultAct; };
	QAction* getDerivativeResultAct()const { return m_derivativeResultAct; };
	QAction* getMaxResultAct()const { return m_maxResultAct; };
	QAction* getMinResultAct()const { return m_minResultAct; };
	QAction* getAverResultAct()const { return m_averResultAct; };

	QAction* getImportGemoAct()const { return m_importGemoAct; };
	QAction* getExportGemoAc()const { return m_exportGemoAct; };
	QAction* getCircleGemoAct()const { return m_circleGemoAct; };
	QAction* getSquareGemoAct()const { return m_squareGemoAct; };
	QAction* getEllipseGemoAct()const { return m_ellipseGemoAct; };
	QAction* getPolygonGemoAct()const { return m_polygonGemoAct; };
	QAction* getRectangleGemoAct()const { return m_rectangleGemoAct; };
	QAction* getPointGemoAct()const { return m_pointGemoAct; };
	QAction* getInerCurveGemoAct()const { return m_interCurveAct; };
	QAction* getParamCurveGemoAct()const { return m_paramCurveAct; };
	QAction* getLineGemoAct()const { return m_lineCurveAct; };
	QAction* getArcGemoAct()const { return m_arcGemoAct; };
	QAction* getThreeBsrCurveAct()const { return m_threeBsrCurveAct; };
	QAction* getTwoBsrCurveActt()const { return m_twoBsrCurveAct; };
	QAction* getParabolaAct()const { return m_parabolaAct; };
	QAction* getHyperbolaAct()const { return m_hyperbolaAct; };
	QAction* getDelGemoAct()const { return m_delGemoAct; };
	QAction* getCuboidGemoAct()const { return m_cuboidGemoAct; };
	QAction* getSphereGemoAct()const { return m_sphereGemoAct; };
	QAction* getConeGemoAct()const { return m_coneGemoAct; };
	QAction* getRingGemoAct()const { return m_ringGemoAct; };
	QAction* getCylinderGemoAct()const { return m_cylinderGemoAct; };
	QAction* getSpiralGemoAct()const { return m_spiralGemoAct; };
	QAction* getSolidViewActt()const { return m_solidViewAct; };
	QAction* getLineViewAct()const { return m_lineViewAct; };
	QAction* getTransmissionViewAct()const { return m_transmissionViewAct; };
	QAction* getMeasureViewAct()const { return m_measureViewAct; };
	QAction* getMeasurePositionViewAct()const { return m_measurePositionViewAct; };
	QAction* getMeasureCentroidViewAct()const { return m_measureCentroidViewAct; };
	QAction* getMeasureDistanceViewAct()const { return m_measureDistanceViewAct; };
	QAction* getShowFaceViewAct()const { return m_showFaceViewAct; };
	QAction* getShowLineViewAct()const { return m_showLineVViewAct; };
	QAction* getShowPointViewAct()const { return m_showPointViewAct; };
	QComboBox* getChooseGeomCombox()const { return m_chooseGeomCombox; };

	QAction* getImportGridAct()const { return m_importGridAct; };
	QAction* getExportGridAct()const { return m_exportGridAct; };
	//QAction* getTriangleGridAct()const { return m_triangleGridAct; };
	//QAction* getQuadGridAct()const { return m_quadGridAct; };
	//QAction* getTetrahedronGridAct()const { return m_tetrahedronGridAct; };
	//QAction* getHexahedronGridAct()const { return m_hexahedronGridAct; };
	QAction* getCurvedSurfaceGridAct()const { return m_curvedSurfaceGridAct; };
	QAction* getEntityGridAct()const { return m_entityGridAct; };
	QAction* getResetGridAct()const { return m_resetGridAct; };
	QAction* getDrawGridAct()const { return m_drawGridAct; };
	QAction* getCountInfoGridAct()const { return m_countInfoGridAct; };
	QAction* getResizeGridAct()const { return m_resizeGemoAct; };
	QAction* getShowGridAct()const { return m_showGridAct; };


	QAction* getSizeGridAct()const { return m_sizeGridAct; };
	QAction* getJxGridAct()const { return m_jxGridAct; };
	QAction* getCxGridAct()const { return m_cxGridAct; };
	QAction* getGxGridAct()const { return m_gxGridAct; };
	QAction* getXhGridAct()const { return m_xhGridAct; };
	QAction* getCgGridAct()const { return m_cgGridAct; };
	QAction* getChGridAct()const { return m_chGridAct; };
	QAction* getGcGridAct()const { return m_gcGridAct; };
	QAction* getCcGridAct()const { return m_ccGridAct; };
	QAction* getJcGridAct()const { return m_jcGridAct; };

	QAction* getX1ViewAct()const { return m_x1ViewAct; };
	QAction* getX2ViewAct()const { return m_x2ViewAct; };
	QAction* getY1ViewAct()const { return m_y1ViewAct; };
	QAction* getY2ViewAct()const { return m_y2ViewAct; };
	QAction* getZ1ViewAct()const { return m_z1ViewAct; };
	QAction* getZ2ViewAct()const { return m_z2ViewAct; };
	QAction* getResetViewAct()const { return m_resetCameraViewAct; };
	QAction* getResetWidgetViewAct()const { return m_resetWidgetViewAct; };
	QCheckBox* getStaffCheckViewAct()const { return m_staffCheckBox; };
	QCheckBox* getLightCheckViewAct()const { return m_lightCheckBox; };
	QComboBox* getColorBoxAct()const { return m_colorThemeCombox; };
	QLabel* getTransLabelAct()const { return transparencyLabel; };
	QSlider* getTransSliderAct()const { return transparencySlider; };

	QAction* getSolidBoundAct()const { return m_solidBoundAct; };
	QAction* getFaceBoundAct()const { return m_faceBoundAct; };
	QAction* getLineBoundAct()const { return m_lineBoundAct; };
	QAction* getPointBoundAct()const { return m_pointBoundAct; };


private slots:
    void onNewProjectClicked();
    void onNephogramClicked();
    void onVectorsClicked();
    void onSectionClicked();
    void onDiceResultClicked();
    void onIsosurfacesResultClicked();
    void onIsosurfacesSegClicked();
    void onXYPlotResultClicked();
    void onAnimationResultClicked();
    void onPickPhysicalResultClicked();
    void onDerivativeResultClicked();
    void onDataExpResultClicked();
    void onZJSamplingModelClicked();
    void onLdclfSamplingModelClicked();
    void onDataEvalModelClicked();
    void onDNNModelClicked();
    void onPODModelClicked();
	void updateTransparencyLabel(int value);
	void onResetWidgetClicked();// 恢复默认布局槽函数
	void onColorThemeChanged(const QString &text);// 主题切换槽函数
    void onBoolenClicked();
};

#endif // UIFRAME_H
