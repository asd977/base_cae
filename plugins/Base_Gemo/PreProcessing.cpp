
#include "PreProcessing.h"
#include <QDebug>
#include <QString>
#include <QUuid>
#include <services/PhysicalFieldService.h>
#include <services/VisualWidgetService.h>
#include <ServiceRegistry.h>
#include<iostream>
#include <fstream>
#include<string>
#include<sstream>
#include<vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include "vtkCamera.h"
#include "vtkActor.h"
#include "vtkRenderer.h"

#include <ServiceRegistry.h>
#include<vtkPoints.h>
#include<vtkPolyData.h>
#include<vtkCellData.h>
#include<vtkCellArray.h>
#include<vtkQuad.h>
#include<vtkRenderWindowInteractor.h>
#include<vtkHexahedron.h>
#include <vtkAutoInit.h>
#include<vtkUnstructuredGrid.h>
#include<vtkDataSetMapper.h>
#include <QToolButton>
#include <vtkCellPicker.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkSphereSource.h>
#include <set>
#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkDoubleArray.h>
#include <vtkMapper.h>
#include <vtkInformation.h>
#include <vtkInformationObjectBaseVectorKey.h>
#include <QFile>
#include <vtkQuad.h>
#include <vtkMath.h>
#include <vtkLine.h>
#include <vtkCellArray.h>
#include <vtkLODActor.h>
#include <vtkAppendPolyData.h>
#include <vtkGlyph3D.h>
#include <vtkPointData.h>
#include <vtkCleanPolyData.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkVectorText.h>
#include <vtkLightCollection.h>
#include <vtkCollectionIterator.h>
#include <vtkLight.h>
#include <vtkFollower.h>
#include <vtkCamera.h>
#include <vtkMatrix4x4.h>
#include <services/MaterialManagerService.h>
#include <services/PhysicalFieldService.h>
#include <services/UiFrameService.h>
#include "BaseGeometryDialog\Boxdialog.h"
#include "BaseGeometryDialog\ConeDialog.h"
#include "BaseGeometryDialog\SphereDialog.h"
#include "BaseGeometryDialog\TorusDialog.h"
#include "BaseGeometryDialog\CylinderDialog.h"
#include "BaseGeometryDialog\CircleDialog.h"
#include "BaseGeometryDialog\EllipseDialog.h"
#include "BaseGeometryDialog\RectangleDialog.h"
#include "BaseGeometryDialog\PolygonDialog.h"

#include <BRep_Builder.hxx>
#include <ContextMenuHub.h>
#include <EventManager.h>
#include <GeomManagerInstance.h>
#include "OccBooleanFuseWidget.h"
#include "OccBooleanUnionWidget.h"
#include "OccBooleanCutWidget.h"
#include "OccBooleanCommonWidget.h"
#include "GeometryImportWidget.h"
#include <QFileDialog>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <cmath>
#include <TopoDS_Shape.hxx>

#include <services/ProjectManagerService.h>

#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QIcon>
#include "GeometryCleanup.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PreProcessing::PreProcessing()
{
	ServiceRegistry::instance().registerService<PreProcessingService>(this);
	initPageFactory();
    initRibbonActionConn();
    initTreeMenu();

    m_uiFrameService  = ServiceRegistry::instance().getService<UiFrameService>();
}

void PreProcessing::initPageFactory() 
{
	m_pageFactory.clear();
	// 网格
    // m_pageFactory[PublicConstants::kPagePreprocessMesh2d] = [this](QString title) { return m_preProcessingWidget->newTetrahedronGridWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessMesh3d] = [this](QString title) { return m_preProcessingWidget->newHexahedronGridWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessImportGrid] = [this](QString title) { return m_preProcessingWidget->newGridImportWidget(title); };
    // // 几何
    // m_pageFactory[PublicConstants::kPagePreprocessImportGeometry] = [this](QString title) { return m_preProcessingWidget->newGeometryImportWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessArc] = [this](QString title){ return m_preProcessingWidget->newGeometryArcWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBezier2d] = [this](QString title) { return m_preProcessingWidget->newGeometryTwoBsrWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBezier3d] = [this](QString title) { return m_preProcessingWidget->newGeometryThreeBsrWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCone] = [this](QString title) { return m_preProcessingWidget->newGeometryConeWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCylinder] = [this](QString title) { return m_preProcessingWidget->newGeometryCylinderWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessEllipse] = [this](QString title) { return m_preProcessingWidget->newGeometryEllipseWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessHelix] = [this](QString title) { return m_preProcessingWidget->newGeometryHelixWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessCircle] = [this](QString title) { return m_preProcessingWidget->newGeometryCircleWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessLine] = [this](QString title) { return m_preProcessingWidget->newGeometryLineWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessTorus] = [this](QString title) { return m_preProcessingWidget->newGeometryTorusWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessSquare] = [this](QString title) { return m_preProcessingWidget->newGeometrySquareWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessSphere] = [this](QString title) { return m_preProcessingWidget->newGeometrySphereWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessRectangle] = [this](QString title) { return m_preProcessingWidget->newGeometryRectangleWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessPolygon] = [this](QString title) { return m_preProcessingWidget->newGeometryPolygonWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessInterCurve] = [this](QString title) { return m_preProcessingWidget->newGeometryInterCurveWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessPoint] = [this](QString title) { return m_preProcessingWidget->newGeometryPointWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessParametricCurve] = [this](QString title) { return m_preProcessingWidget->newGeometryParamCurveWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessBox] = [this](QString title) { return m_preProcessingWidget->newGeometryBoxWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessHyperbola] = [this](QString title) { return m_preProcessingWidget->newGeometryHyperbolaWidget(title); };
    // m_pageFactory[PublicConstants::kPagePreprocessParabola] = [this](QString title) { return m_preProcessingWidget->newGeometryParabolaEdgeWidget(title); };

}

void PreProcessing::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node",
        this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();

            ContextMenuHub::addSeparator(menu);

            ContextMenuHub::addAction(menu, this, "隐藏/显示", [this, uuid]{
                this->toggleVisible(uuid);
            });

            ContextMenuHub::addAction(menu, this, "删除", [this, uuid]{
                this->removeNode(uuid);
            });
            ContextMenuHub::addAction(menu, this, "重命名", [this, uuid]{
                this->renameNode(uuid);
            });
        },
        100,
        [](const QVariantMap& ctx)->bool{
            return ctx.value("module").toString() == "Geometry";
        }
        );
}

void PreProcessing::toggleVisible(const QString& uuid)
{
    if (uuid.isEmpty()) return;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr) return;

    GeomShapeObject* object = mgr->getGeomShapeObject(uuid);
    if (!object) return;

    const bool currentVisible =
        (object->getSurfActor() && object->getSurfActor()->GetVisibility() != 0) ||
        (object->getEdgeActor() && object->getEdgeActor()->GetVisibility() != 0);
    const bool targetVisible = !currentVisible;

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>()) {
        vis->setVisible(uuid + "_face", targetVisible, false);
        vis->setVisible(uuid + "_edge", targetVisible, true);
    } else {
        if (object->getSurfActor()) object->getSurfActor()->SetVisibility(targetVisible);
        if (object->getEdgeActor()) object->getEdgeActor()->SetVisibility(targetVisible);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>()) {
        projectManagerService->setNodeDimmed(uuid, !targetVisible);
    }
}

void PreProcessing::removeNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode) return;

    GeometryCleanup::removeExistingGeometry(uuid);

    for (int i = 0; i < m_geomNode->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!node) continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty()) nodeUuid = node->getUuid();
        if (nodeUuid != uuid) continue;

        if (BasePage* page = node->getPage()) {
            page->deleteLater();
            node->setPage(nullptr);
        }

        m_geomNode->removeChild(node);
        delete node;
        break;
    }
}

void PreProcessing::renameNode(const QString& uuid)
{
    if (uuid.isEmpty() || !m_geomNode) return;

    for (int i = 0; i < m_geomNode->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_geomNode->child(i));
        if (!node) continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty()) nodeUuid = node->getUuid();
        if (nodeUuid != uuid) continue;

        const QString currentName = node->text(0);
        bool ok = false;
        const QString newName = QInputDialog::getText(
            nullptr,
            tr("重命名几何"),
            tr("请输入新名称："),
            QLineEdit::Normal,
            currentName,
            &ok);

        if (!ok) return;
        const QString trimmedName = newName.trimmed();
        if (trimmedName.isEmpty() || trimmedName == currentName) return;

        node->setText(0, trimmedName);
        node->setName(trimmedName);
        if (BasePage* page = node->getPage()) {
            page->setDisplayName(trimmedName);
        }
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance()) {
            if (GeomShapeObject* object = mgr->getGeomShapeObject(uuid)) {
                object->setName(trimmedName);
            }
        }
        return;
    }
}

BasePage* PreProcessing::createPageByType(const QString& type, const QString& title)
{
	auto it = m_pageFactory.find(type);
	return it == m_pageFactory.end() ? nullptr : it.value()(title);
}

void PreProcessing::newProject()
{
	m_geomNode = new ProjectNode("几何");
	ProjectManagerService *projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
	projectManagerService->addTreeItem(m_geomNode);
}

bool PreProcessing::saveProject()
{
	if (!m_geomNode || !m_gridNode) return true;
	QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
	const QString filePath = QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);

	QJsonObject root;
	root["module"] = "PreProcessing";
	root["root_title"] = "前处理";

	QJsonArray geomItems;
	const int n1 = m_geomNode->childCount();
	for (int i = 0; i < n1; ++i)
	{
		ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(i));
		if (!node) continue;
		BasePage* page = node->getPage();

		QJsonObject item;
		item["uuid"] = node->getUuid();
		item["title"] = node->text(0);
		item["type"] = page->getPageType();
		item["state"] = page->saveJson();
		geomItems.append(item);
	}
	root["geom_items"] = geomItems;


	QJsonArray gridItems;
	const int n2 = m_gridNode->childCount();
	for (int i = 0; i < n2; ++i)
	{
		ProjectNode* node = static_cast<ProjectNode*>(m_gridNode->child(i));
		if (!node) continue;
		BasePage* page = node->getPage();

		QJsonObject item;
		item["uuid"] = node->getUuid();
		item["title"] = node->text(0);
		item["type"] = page->getPageType();
		item["state"] = page->saveJson();
		gridItems.append(item);
	}
	root["grid_items"] = gridItems;
	return writeJsonFile(filePath, root);
}

bool PreProcessing::loadProject()
{
	bool success = false;
	ProjectManagerService *projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>();
	m_geomNode = new ProjectNode("几何");
	projectManagerService->addTreeItem(m_geomNode);
	success = loadChildProject(m_geomNode, "geom_items");

	return success;
}

bool PreProcessing::loadChildProject(ProjectNode * rootNode,const QString & name)
{
	QString projectDir = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
	const QString filePath = QDir(projectDir).filePath(PublicConstants::kPreProcessingProjectFileName);
	QJsonObject root;
	if (!readJsonFile(filePath, root))
	{
		return false;
	}

	const QJsonArray items = root.value(name).toArray();
	for (const auto& v : items)
	{
		const auto obj = v.toObject();
		const QString id = obj.value("uuid").toString(QUuid::createUuid().toString());
		const QString title = obj.value("title").toString("未命名");
		const QString type = obj.value("type").toString();
		const QJsonObject state = obj.value("state").toObject();

		BasePage* page = createPageByType(type, title);
		//auto* node = new ProjectNode(title, id, page);
		//rootNode->addChild(node);

		if (page) page->loadJson(state);
	}
	return true;
}

void PreProcessing::closeProject()
{
	if (!m_geomNode|| !m_gridNode) return;

	while (m_geomNode->childCount() > 0)
	{
		ProjectNode* node = static_cast<ProjectNode*>(m_geomNode->child(0));
		if (node)
		{
			if (auto* page = node->getPage())
			{
				page->deleteLater();

				node->setPage(nullptr);
			}
			delete m_geomNode->takeChild(0);
		}
	}

	while (m_gridNode->childCount() > 0)
	{
		ProjectNode* node = static_cast<ProjectNode*>(m_gridNode->child(0));
		if (node)
		{
			if (auto* page = node->getPage())
			{
				page->deleteLater();

				node->setPage(nullptr);
			}
			delete m_gridNode->takeChild(0);
		}
	}

	// 从工程树移除根节点
	if (auto *pm = ServiceRegistry::instance().getService<ProjectManagerService>())
	{


	}
	delete m_geomNode;
	m_geomNode = nullptr;

	delete m_gridNode;
    m_gridNode = nullptr;
}

void PreProcessing::generateStepFile()
{
    // 1. 初始化容器
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    // 2. 获取单例数据
    GeomManager::GeomManagerInstance* mgr = GeomManager::GeomManagerInstance::getInstance();
    QHash<QString, GeomShapeObject *> tmp = mgr->getShapMap();

    bool hasShape = false;

    // 3. 遍历并添加形状
    // 使用 QHash 的迭代器或 values() 列表
    for (GeomShapeObject* shapeObj : tmp.values())
    {
        if (shapeObj && !shapeObj->getShape()->IsNull()) // 假设 getShape() 返回 TopoDS_Shape
        {
            builder.Add(compound, *(shapeObj->getShape()));
            hasShape = true;
        }
    }

    if (!hasShape) {
        // 如果没有有效形状，直接返回避免生成空文件
        return;
    }

    // 4. 配置 STEP 写入器
    STEPControl_Writer stepWriter;

    // 设置模式（AsIs 保持原样，ManifoldSolidBrep 适合实体）
    IFSelect_ReturnStatus transferStatus = stepWriter.Transfer(compound, STEPControl_AsIs);

    if (transferStatus != IFSelect_RetDone)
    {
        return;
    }

    // 5. 文件路径处理
    // 假设 currentWorPath 是类成员，这里拼接完整路径
    QString fileName = "ExportedModel.step";
    QString currentWorPath = QCoreApplication::applicationDirPath();
    QString filePath = currentWorPath + "/data/"+fileName;

    // 6. 执行写入操作
    IFSelect_ReturnStatus writeStatus = stepWriter.Write(filePath.toLocal8Bit().constData());

    if (writeStatus != IFSelect_RetDone) {
        return;
    }
}

bool PreProcessing::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
	QFile f(filePath);
	if (!f.open(QIODevice::WriteOnly)) return false;
	QJsonDocument doc(obj);
	f.write(doc.toJson(QJsonDocument::Indented));
	return true;
}

bool PreProcessing::readJsonFile(const QString& filePath, QJsonObject& out)
{
	QFile f(filePath);
	if (!f.exists()) return false;
	if (!f.open(QIODevice::ReadOnly)) return false;
	QJsonParseError err{};
	auto doc = QJsonDocument::fromJson(f.readAll(), &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
	out = doc.object();
	return true;
}

void PreProcessing::initRibbonActionConn()
{
    if (!m_uiFrameService) return;

    auto &em = EventManager::instance();

    auto reg = [&](const char* actionId, std::function<void()> fn)
    {
        em.registerHandler(actionId,this, [this, fn](const QVariantMap &)
                           {
                               fn();
                           });
    };

    // --- Geometry ---
    reg(PublicConstants::kActionPreprocessImportGeometry,  [this]{ newGeometryImportWidget(); });
    reg(PublicConstants::kActionPreprocessExportGeometry,  [this]{ exportGeom(); });

    reg(PublicConstants::kActionPreprocessBox,             [this]{ newGeometryBoxWidget(); }); //矩形
    reg(PublicConstants::kActionPreprocessSphere,          [this]{ newGeometrySphereWidget(); }); //球
    reg(PublicConstants::kActionPreprocessCone,            [this]{ newGeometryConeWidget(); }); //圆锥体
    reg(PublicConstants::kActionPreprocessCylinder,        [this]{ newGeometryCylinderWidget(); }); //圆柱体
    reg(PublicConstants::kActionPreprocessTorus,           [this]{ newGeometryTorusWidget(); }); //圆环

    reg(PublicConstants::kActionGeomCreatePlanarCircle,          [this]{ newGeometryCircleWidget(); });//圆形
    reg(PublicConstants::kActionGeomCreateEllipse,         [this]{ newGeometryEllipseWidget(); }); // 椭圆形
    reg(PublicConstants::kActionGeomCreatePlanarPolygon,         [this]{ newGeometryPolygonWidget(); }); //多边形
    reg(PublicConstants::kActionGeomCreatePlanarRect,       [this]{ newGeometryRectangleWidget(); }); //矩形

    reg(PublicConstants::kActionGeomFuse,   [this]{ newOccBooleanFuseWidget();}); //fuse
    reg(PublicConstants::kActionGeomUnion,  [this]{ newOccBooleanUnionWidget();}); //union keep domains
    reg(PublicConstants::kActionGeomCut,    [this]{ newOccBooleanCutWidget();}); //cut
    reg(PublicConstants::kActionGeomCommon, [this]{ newOccBooleanCommonWidget();}); //common
}

BasePage* PreProcessing::newGeometryImportWidget(QString title)
{
     if (!m_geomNode) return nullptr;
     auto *w = new GeometryImportWidget();
     auto *node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)w);
     m_geomNode->addChild(node);

     return w;
    return nullptr;
}

bool PreProcessing::exportGeom()
{
    // 更稳：直接从 GeomManager 导出（避免你本地 map 和 manager 生命周期不一致）
    QString filePath = QFileDialog::getSaveFileName(
        nullptr, tr("导出几何"), QDir::homePath(),
        tr("Geometry Files (*.step *.stp *.iges *.igs *.brep)")
        );
    if (filePath.isEmpty()) return false;

    QStringList validExtensions = { ".step", ".stp", ".iges", ".igs", ".brep" };
    QString extension;
    for (const QString& ext : validExtensions)
    {
        if (filePath.endsWith(ext, Qt::CaseInsensitive))
        {
            extension = ext.toLower();
            break;
        }
    }
    if (extension.isEmpty())
    {
        filePath = filePath.section('.', 0, 0) + ".step";
        extension = ".step";
    }

    bool success = false;

    // 你当前工程里 step 多形状导出已经写了这个函数（更可靠）
    if (extension == ".step" || extension == ".stp")
    {
        success = writeMultipleShapesStepFile(filePath);
    }
    else
    {
        // 其他格式你原来走 GemoRWUtilInstance，这里先保留“失败提示”，你后续再补全
        success = false;
    }

    // if (m_outputService)
    // {
    //     m_outputService->outputLog(success ? QString("几何导出成功!【%1】").arg(filePath)
    //                                        : QString("几何导出失败!【%1】").arg(filePath));
    // }
    return success;
}

bool PreProcessing::writeMultipleShapesStepFile(const QString& filePath)
{
    GeomManager::GeomManagerInstance *mgr = GeomManager::GeomManagerInstance::getInstance();

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    QHash<QString, GeomShapeObject *> shapes = mgr->getShapMap();
    for (auto it = shapes.begin(); it != shapes.end(); ++it)
    {
        GeomShapeObject* obj = it.value();
        qDebug()<<it.key();
        if (!obj || !obj->getShape() || obj->getShape()->IsNull()) continue;
        builder.Add(compound, *obj->getShape());
    }

    STEPControl_Writer writer;
    IFSelect_ReturnStatus status = writer.Transfer(compound, STEPControl_AsIs);
    if (status != IFSelect_RetDone) return false;

    qDebug()<<filePath;
    status = writer.Write(filePath.toUtf8().constData());
    return (status == IFSelect_RetDone);
}

void PreProcessing::newGeometryBoxWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new BoxDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Box");
    ProjectNode *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Box");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometrySphereWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new SphereDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Sphere");
    ProjectNode *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Sphere");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryConeWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new ConeDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Cone");
    ProjectNode *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Cone");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryTorusWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new TorusDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Torus");
    ProjectNode *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Torus");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newGeometryCylinderWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new CylinderDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Cylinder");
    ProjectNode *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Cylinder");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

BasePage* PreProcessing::newGeometryCircleWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new CircleDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Circle");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Circle");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryEllipseWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new EllipseDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Ellipse");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Ellipse");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryPolygonWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new PolygonDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Polygon");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Polygon");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

BasePage* PreProcessing::newGeometryRectangleWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new RectangleDialog();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "Rectangle");

    ProjectNode* node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Rectangle");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));

    return w;
}

void PreProcessing::newOccBooleanFuseWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new OccBooleanFuseWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanFuse");
    auto *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "BooleanFuse");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newOccBooleanCutWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new OccBooleanCutWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanCut");
    auto *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "BooleanCut");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newOccBooleanUnionWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new OccBooleanUnionWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanUnion");
    auto *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "BooleanUnion");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

void PreProcessing::newOccBooleanCommonWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new OccBooleanCommonWidget();
    QString uuid = w->getUuid();
    QString displayName = makeUniqueName(title, "BooleanCommon");
    auto *node = new ProjectNode(displayName, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    w->setDisplayName(displayName);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "BooleanCommon");
    node->setIcon(0, QIcon(":/resources/icon/geom-unused.svg"));
}

QString PreProcessing::makeUniqueName(const QString &baseName, const QString &typeKey)
{
    QString key = typeKey.isEmpty() ? baseName : typeKey;
    int count = m_nameCounters.value(key, 0) + 1;
    m_nameCounters.insert(key, count);
    return QStringLiteral("%1(%2)").arg(baseName).arg(count);
}
