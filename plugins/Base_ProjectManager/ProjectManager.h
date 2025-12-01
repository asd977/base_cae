#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include "ProjectTreeWidget.h"
#include "SettingWidget.h"

#include <QObject>
#include <services/OutputService.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <ServiceRegistry.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <QDir>
#include <QCoreApplication>
// #include <JlCompress.h>
#include <QFileDialog>
#include "qsettings.h"
#include "ProjectSetupDialog.h"

class ProjectManagerWidget;
class ProjectManager: public QObject, public ProjectManagerService
{
    Q_OBJECT
public:
    ProjectManager();
	~ProjectManager();
	/**
	 * @brief 是否有工程
	 */
    bool isHaveProject();
    void addTreeItem(ProjectNode *node);
private slots:
	void newProject();
	void openProject();
	void saveProject();
	void exportCurDatabase();
	void exitProject();
	/**
	* @brief 导入几何单击信号
	*/
	void onImportGeomAct();
	/**
	* @brief 长方体单击信号
	*/
	void onCuboidGemoAct();
	/**
	* @brief 球体单击信号
	*/
	void onSphereGemoAct();
    /**
     * @brief 参数单击信号
     */
    void onParamAct();

	/**
	 * @brief 二维曲面网格单击信号
	 */
	void onCurvedSurfaceGridAct();
	/**
	 * @brief 三维实体网格单击信号
	 */
	void onEntityGridAct();
private:
	/**
	 * @brief 链接ribbon框的信号
	 */
	void initRibbonActionConn();
	/**
	 * @brief 移除文件夹
	 */
	bool removeDir(QString dirPath);
	/**
	 * @brief 创建工程的所有数据文件夹（新建工程）
	 */
	bool createProjectFilesFolder();
	/**
	 * @brief 保存项目所有模块的数据到.smproj文件
	 * @param fileName .smproj名称
	 * @note 所有模块的保存放在此函数进行
	 */
	bool saveAllProModelData(const QString& fileName);
	/**
	 * @brief 保存树节点信息
	 */
	void saveProjectTreeXml();
	/**
	 * @brief 加载项目树的数据
	 */
	void loadProTreeData();
	/**
	 * @brief ribbon框添加节点的工厂类
	 * @param rootNodeType父节点类型
	 * @param page 窗口类
	 * @param nodeName 节点名称
	 * @note 根据不同的数据类型 给不同节点添加数据
	 */
	void treeChildFactory(const QString& rootNodeType, BasePage* page, QString& nodeName);
	/**
	 * @brief 恢复树默认节点的指针(主要用于加载项目)
	 * @note 主要为了加载项目的时候从ribbon栏添加界面的时候可以分清类别
	 */
	void resetDefaultNodePointer();
	/**
	 * @brief 得到项目保存的头路径 可用于别的模块保存大文件
	 */
	QString getProjectTempFolder()const { return m_projectTempFolder; };
	/**
	 * @brief 判断节点的名称是否相同
	 * @param parentNode 父节点
	 * @param originName 原始名称
	 */
	void judgeTreeNodeNameIsSame(const ProjectNode* parentNode, QString& originName);
	/**
	 * @brief 删除页面的响应函数
	 * @param uuid uuid
	 */
	void onDeletePageSig(const QString & uuid);
private:
	ProjectTreeWidget* m_projectTreeWidget;
	SettingWidget* m_settingWidget;
	UiFrameService * m_uiFrameService;
	OutputService * m_outputService;
	QString m_projectName = ""; // 项目名称
	QString m_projectPath = ""; // 项目路径
	QString m_projectEnginePath = "";// 项目工程路径
	QString m_projectTempFolder; // 项目所有文件保存的路径

	ProjectNode* paramNode  = nullptr; // 参数定义
	ProjectNode* materNode = nullptr; // 材料
	ProjectNode* gemoNode = nullptr; // 几何
	ProjectNode* gridNode = nullptr; // 网格
	ProjectNode* boundNode = nullptr; // 边界条件
	ProjectNode* phyNode = nullptr; // 物理场
	ProjectNode* solveNode = nullptr; // 求解
	ProjectNode* resNode = nullptr; // 结果
	ProjectNode* nephogramNode = nullptr; // 云图
	ProjectNode* vectorNode = nullptr; // 矢量图


    ProjectNode *m_rootNode = nullptr;
};

#endif // PROJECTMANAGER_H
