#pragma once
#include <QMap>
#include <QVector>
#include <QString>
#include <QDebug>
#include <gmsh.h>
#include <set>
#include <QCoreApplication>
#include <QDir>

/**
 * @file MeshProcessor.h
 * @brief 网格数据加工者类(维护几何和网格的映射关系以及网格节点信息)
 * @author tongle
 * @version 1.0.0
 * @note 结构化网格通常包含四边形（2D，elementType=3）或六面体（3D，elementType=5）。
 * @note 非结构化网格通常包含三角形（2D，elementType=2）或四面体（3D，elementType=4）。
 * @date 2025-07-19
 */

class MeshProcessor
{
public:
	/**
	 * @brief 构造函数
	 * @return
	 */
	MeshProcessor();
	/**
	 * @brief 析构函数
	 * @return
	 */
	~MeshProcessor();
	/**
	 * @brief 加载occ几何文件
	 * @param filePath 几何文件路径 (iges,igs,step,stp,brep)
	 * @return
	 */
	void loadOCCFile(const QString& filePath);
	/**
	 * @brief 获取点的网格节点
	 * @param pointTag 几何点
	 * @return 网格节点集合
	 */
	const std::vector<int> & getPointNodes(int pointTag) const;
	/**
	 * @brief 获取网格点的网格节点索引
	 * @param pointTag 网格点
	 * @return 网格节点索引
	 */
	const std::vector<int> & getPointGridNodeIndex(int pointTag) const;
	/**
	 * @brief 获取线的网格单元
	 * @param lineTag 几何线
	 * @return 网格单元集合
	 */
	const std::vector<int>& getLineElements(int lineTag) const;
	/**
	 * @brief 获取网格线单元的网格节点索引
	 * @param lineTag 网格线
	 * @return 网格节点索引
	 */
	const std::vector<int>& getLineGridNodeIndex(int lineTag) const;
	/**
	 * @brief 获取面的网格单元
	 * @param surfaceTag 几何面
	 * @return 网格单元集合
	 */
	const std::vector<int>& getSurfaceElements(int surfaceTag) const;
	/**
	 * @brief 获取网格面单元的网格节点索引
	 * @param surfaceTag 网格面
	 * @return 网格节点索引
	 */
	const std::vector<int>& getSurfaceGridNodeIndex(int surfaceTag) const;
	/**
	 * @brief 获取体的网格单元
	 * @param volumeTag 几何体
	 * @return 网格单元集合
	 */
	const std::vector<int>& getVolumeElements(int volumeTag) const;
	/**
	 * @brief 获取网格体单元的网格节点索引
	 * @param volumeTag 网格体
	 * @return 网格节点索引
	 */
	const std::vector<int>& getVolumeGridNodeIndex(int volumeTag) const;
	/**
	 * @brief 获取网格节点坐标
	 * @param nodeTag 网格节点索引
	 * @return 节点的三维坐标
	 */
	const std::vector<double>& getNodeCoordinates(int nodeTag) const;
	/**
	 * @brief 获取所有点的tag列表
	 * @return 所有点的tag列表
	 */
	const std::vector<std::pair<int, int> >& getPoints() const;
	/**
	 * @brief 获取所有线的tag列表
	 * @return 所有线的tag列表
	 */
	const std::vector<std::pair<int, int> >& getLines() const;
	/**
	 * @brief 获取所有面的tag列表
	 * @return 所有面的tag列表
	 */
	const std::vector<std::pair<int, int> >& getSurfaces() const;
	/**
	 * @brief 获取所有体的tag列表
	 * @return 所有体的tag列表
	 */
	const std::vector<std::pair<int, int> >& getVolumes() const;
	/**
	 * @brief 清空数据
	 */
	void clear();
	/**
	 * @brief 导出网格
	 */
	void writeGmshFile();
	/**
	 * @brief 导出网格
	 */
	bool writeGridFile(const QString& path);
	/**
	 * @brief 移除文件夹
	 */
	bool removeDir(QString dirPath);
	/**
	 * @brief 设置网格剖分参数--最大长度
	 */
	void setGmshLengthMax(int data) { m_gridLengthMax = data; };
	/**
	 * @brief 设置网格剖分参数--最小长度
	 */
	void setGmshLengthMin(int data) { m_gridLengthMin = data; };
	/**
	 * @brief 设置网格剖分参数--维度
	 */
	void setGmshDimension(int data) { m_dimension = data; };
	/**
	 * @brief 设置网格剖分参数--尺寸因子
	 */
	void setGmshSizeFactor(int data) { m_sizeFactor = data; };
	/**
	 * @brief 设置网格剖分参数--光滑迭代次数
	 */
	void setGmshSmoothing(int data) { m_smoothing = data; };
	/**
	 * @brief 设置网格剖分参数--阶数
	 */
	void setElementOrder(int data) { m_elementOrder = data; };
	/**
	 * @brief 设置网格剖分参数--网格类型
	 */
	void setGirdType(int data) { m_gridType = data; };
	/**
	 * @brief 设置网格剖分参数--算法
	 */
	void setAlgorithm(int data) { m_aigorithm = data; };
	/**
	 * @brief 设置网格剖分参数--是否启用修复几何
	 */
	void setFixShape(bool data) { m_fixShape = data; };
	/**
	 * @brief nas路径
	 */
	QString getNasPath()const {return m_nasPath;}
	/**
	 * @brief vtk路径
	 */
	QString getVtkPath()const { return m_vtkPath; }
	/**
	 * @brief msh路径
	 */
	QString getMshPath()const { return m_mshPath; }
	/**
	 * @brief 获取网格信息
	 */
	QList<int> getGridInfo();
	/**
	 * @brief 加载导入的网格
	 */
	bool loadImportGridFile(const QString & filePath);
	/**
	 * @brief 移除模型
	 */
	void removeExistingModel(const std::string& modelName);

    /**
     * @brief 网格面id
     */
    int getGMeshFaceIndex(int surfaceTag) ;
private:
	/**
	 * @brief 根据维度生成网格
	 */
	bool createGridByDim();
	/**
	 * @brief 创建点的物理组
	 */
	std::map<int, int> createPointPhysicalGroup();
	/**
	 * @brief 创建线的物理组
	 */
	std::map<int, int> createLinePhysicalGroup();
	/**
	 * @brief 创建面的物理组
	 */
	std::map<int, int> createFacePhysicalGroup();
	/**
	 * @brief 创建体的物理组
	 */
	std::map<int, int> createSolidPhysicalGroup();
private:
	/**
	 * @brief 保存的nas文件路径
	 */
	QString m_nasPath = "";
	/**
	 * @brief 保存的vtk文件路径
	 */
	QString m_vtkPath = "";
	/**
	 * @brief 保存的msh文件路径
	 */
	QString m_mshPath = "";
	/**
	 * @brief 几何实体与网格的映射 点tag->网格点tag列表
	 */
	std::map<int, std::vector<int>> pointToNodes;
	/**
	 * @brief 网格点与网格节点的映射 网格点tag->网格节点index列表
	 */
	std::map<int, std::vector<int>> pointToNodesIndex;
	/**
	 * @brief 几何实体与网格的映射 线tag->单元tag列表
	 */
	std::map<int, std::vector<int>> lineToElements;
	/**
	 * @brief 网格线与网格节点的映射 网格线tag->网格节点index列表
	 */
	std::map<int, std::vector<int>> lineToElementsIndex;
	/**
	 * @brief 几何实体与网格的映射 面tag->单元tag列表
	 */
	std::map<int, std::vector<int>> surfaceToElements;
	/**
	 * @brief 网格面与网格节点的映射 网格面tag->网格节点index列表
	 */
	std::map<int, std::vector<int>> surfaceToElementsIndex;
	/**
	 * @brief 几何实体与网格的映射 体tag->单元tag列表
	 */
	std::map<int, std::vector<int>> volumeToElements;
	/**
	 * @brief 网格面与网格节点的映射 网格面tag->网格节点index列表
	 */
	std::map<int, std::vector<int>> volumeToElementsIndex;
	/**
	 * @brief 节点坐标存储 节点tag->[x, y, z]
	 */
	std::map<int, std::vector<double>> nodeCoordinates;
	/**
	 * @brief 几何实体信息 点:[dim, tag]
	 */
	std::vector<std::pair<int, int>> points;
	/**
	 * @brief 几何实体信息 线:[dim, tag]
	 */
	std::vector<std::pair<int, int>> edges;
	/**
	 * @brief 几何实体信息 面:[dim, tag]
	 */
	std::vector<std::pair<int, int>> faces;
	/**
	 * @brief 几何实体信息 体:[dim, tag]
	 */
	std::vector<std::pair<int, int>> volumes;

    /**
     * @brief 几何ID->gmeshID
     */
    std::map<int, int> m_geom_face_to_physical;
	/**
	 * @brief 网格剖分参数--最大长度
	 */
	int m_gridLengthMax = 10;
	/**
	 * @brief 网格剖分参数--最小长度
	 */
	int m_gridLengthMin = 0;
	/**
	 * @brief 网格剖分参数--维度
	 */
	int m_dimension = 3;
	/**
	 * @brief 网格剖分参数--尺寸因子
	 */
	int m_sizeFactor = 1;
	/**
	 * @brief 网格剖分参数--阶数
	 */
	int m_elementOrder = 1;
	/**
	 * @brief 网格剖分参数--算法
	 */
	int m_aigorithm = 2;
	/**
	 * @brief 网格剖分参数--是否启用几何修复
	 */
	bool m_fixShape = true;
	/**
	 * @brief 网格剖分参数--光滑迭代次数
	 */
	int m_smoothing = 10;
	/**
	 * @brief 网格剖分参数--网格类型(1三角形(非结构化) 2四边形(结构化) 3 四面体(非结构化) 4六面体(结构化))
	 */
	int m_gridType = 1;
	/**
	/**
	 * @brief 节点数量
	 */
	int m_nodeSize = 0;
	/**
	 * @brief 点单元数量
	 */
	int m_pointEleSize = 0;
	/**
	 * @brief 线单元数量
	 */
	int m_lineEleSize = 0;
	/**
	 * @brief 面单元数量
	 */
	int m_faceEleSize = 0;
	/**
	 * @brief 体单元数量
	 */
	int m_solidEleSize = 0;
	/**
	 * @brief 是否导入或者生成网格
	 */
	bool m_isImportOrGener = false;
};

