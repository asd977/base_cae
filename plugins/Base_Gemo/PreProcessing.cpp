
#include "PreProcessing.h"
#include <QDebug>
#include <QColor>
#include <QString>
#include <QUuid>
#include <services/PhysicalFieldService.h>
#include <services/VisualWidgetService.h>
#include <services/MeshModuleControllerService.h>
#include <ServiceRegistry.h>
#include <IiinGridModel.h>
#include<iostream>
#include <fstream>
#include<string>
#include<sstream>
#include<vtkProperty.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkUnsignedCharArray.h>
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
#include <OccBooleanToolWidget.h>
#include <QFileDialog>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <cmath>

#include <services/ProjectManagerService.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
static const QString kImportedMeshPropId = QStringLiteral("mesh.imported");

int expectedNodeCountForKetype(int ketype)
{
    switch (ketype) {
    case 1:
    case 2:
    case 10:
    case 21:
        return 2;
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 11:
    case 12:
    case 29:
        return 4;
    case 31:
    case 32:
    case 33:
    case 34:
        return 3;
    case 6:
    case 9:
    case 13:
    case 17:
    case 36:
    case 37:
        return 8;
    case 35:
        return 4;
    case 38:
        return 6;
    case 39:
        return 5;
    default:
        return -1;
    }
}
} // namespace

PreProcessing::PreProcessing()
{
	ServiceRegistry::instance().registerService<PreProcessingService>(this);
	initPageFactory();
    initRibbonActionConn();

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

            ContextMenuHub::addAction(menu, this, "重建", [this, uuid]{
                // this->rebuildFeature(uuid);
            });

            ContextMenuHub::addAction(menu, this, "隐藏/显示", [this, uuid]{
                // this->toggleVisible(uuid);
            });

            if (kind.startsWith("Box"))
            {
                ContextMenuHub::addAction(menu, this, "删除", [this, uuid]{
                    // this->editBooleanParams(uuid);
                });
            }
        },
        100,
        [](const QVariantMap& ctx)->bool{
            return ctx.value("module").toString() == "Geometry";
        }
        );
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

QSet<int> PreProcessing::getAvailableMatypes() const
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        return {};
    }

    IiinGridModel model;
    if (!meshService->currentModel(model)) {
        return {};
    }

    return model.allMatypes();
}

void PreProcessing::highlightMatypes(const QSet<int> &matypes,
                                     int selectedMatype,
                                     int previewMatype,
                                     int preDeleteMatype)
{
    auto *visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visualService) {
        return;
    }

    auto *actor = vtkActor::SafeDownCast(visualService->getProp(kImportedMeshPropId));
    if (!actor) {
        return;
    }

    auto *mapper = vtkDataSetMapper::SafeDownCast(actor->GetMapper());
    if (!mapper) {
        return;
    }

    auto *grid = mapper->GetInput();
    if (!grid || grid->GetNumberOfCells() == 0) {
        return;
    }

    if (matypes.isEmpty() && selectedMatype < 0 && previewMatype < 0 && preDeleteMatype < 0) {
        grid->GetCellData()->SetScalars(nullptr);
        mapper->ScalarVisibilityOff();
        visualService->render();
        return;
    }

    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService) {
        return;
    }

    IiinGridModel model;
    if (!meshService->currentModel(model)) {
        return;
    }

    const QColor baseColor(220, 220, 220);
    const QColor appliedColor(110, 176, 255);
    const QColor selectedColor(255, 221, 102);
    const QColor previewColor(200, 230, 255);
    const QColor preDeleteColor(255, 160, 160);

    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("MatypeColors");
    colors->SetNumberOfComponents(3);
    colors->SetNumberOfTuples(grid->GetNumberOfCells());

    vtkIdType cellIndex = 0;
    const auto &nodes = model.nodes();
    const auto &elems = model.elems();

    for (const Elem &e : elems) {
        const int expectedNodes = expectedNodeCountForKetype(e.ketype);
        if (expectedNodes < 0 || e.nodeIds.size() < expectedNodes) {
            continue;
        }

        bool valid = true;
        for (int i = 0; i < expectedNodes; ++i) {
            const int nid = e.nodeIds[i];
            if (nid <= 0 || nid > nodes.size()) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        if (cellIndex >= grid->GetNumberOfCells()) {
            break;
        }

        QColor color = baseColor;
        if (previewMatype >= 0 && e.matype == previewMatype) {
            color = previewColor;
        } else if (preDeleteMatype >= 0 && e.matype == preDeleteMatype) {
            color = preDeleteColor;
        } else if (selectedMatype >= 0 && e.matype == selectedMatype) {
            color = selectedColor;
        } else if (matypes.contains(e.matype)) {
            color = appliedColor;
        }

        unsigned char rgb[3] = {
            static_cast<unsigned char>(color.red()),
            static_cast<unsigned char>(color.green()),
            static_cast<unsigned char>(color.blue())
        };
        colors->SetTypedTuple(cellIndex, rgb);
        ++cellIndex;
    }

    while (cellIndex < grid->GetNumberOfCells()) {
        unsigned char rgb[3] = {
            static_cast<unsigned char>(baseColor.red()),
            static_cast<unsigned char>(baseColor.green()),
            static_cast<unsigned char>(baseColor.blue())
        };
        colors->SetTypedTuple(cellIndex, rgb);
        ++cellIndex;
    }

    grid->GetCellData()->SetScalars(colors);
    mapper->SetScalarModeToUseCellData();
    mapper->SetColorModeToDirectScalars();
    mapper->ScalarVisibilityOn();
    visualService->render();
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
        em.registerHandler(actionId, [this, fn](const QVariantMap &)
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

    reg(PublicConstants::kActionGeomFuse,   [this]{ newOccBoolenToolWidget();}); //fuse
    reg(PublicConstants::kActionGeomCut,    [this]{ newOccBoolenToolWidget();}); //cut
    reg(PublicConstants::kActionGeomCommon, [this]{ newOccBoolenToolWidget();}); //common
}

BasePage* PreProcessing::newGeometryImportWidget(QString title)
{
    // if (!m_geomNode) return nullptr;
    // auto *w = new GeometryImportWidget();
    // auto *node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)w);
    // m_geomNode->addChild(node);

    // return w;
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
    QString uuid = QUuid::createUuid().toString();
    ProjectNode *node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);
    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Box");
}

void PreProcessing::newGeometrySphereWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new SphereDialog();
    QString uuid = QUuid::createUuid().toString();
    ProjectNode *node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Sphere");
}

void PreProcessing::newGeometryConeWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new ConeDialog();
    QString uuid = QUuid::createUuid().toString();
    ProjectNode *node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Cone");
}

void PreProcessing::newGeometryTorusWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new TorusDialog();
    QString uuid = QUuid::createUuid().toString();
    ProjectNode *node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Torus");
}

void PreProcessing::newGeometryCylinderWidget(QString title)
{
    if (!m_geomNode) return;
    auto *w = new CylinderDialog();
    QString uuid = QUuid::createUuid().toString();
    ProjectNode *node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Sphere");
}

BasePage* PreProcessing::newGeometryCircleWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new CircleDialog();
    QString uuid = QUuid::createUuid().toString();

    ProjectNode* node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Circle");

    return w;
}

BasePage* PreProcessing::newGeometryEllipseWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new EllipseDialog();
    QString uuid = QUuid::createUuid().toString();

    ProjectNode* node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Ellipse");

    return w;
}

BasePage* PreProcessing::newGeometryPolygonWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new PolygonDialog();
    QString uuid = QUuid::createUuid().toString();

    ProjectNode* node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Polygon");

    return w;
}

BasePage* PreProcessing::newGeometryRectangleWidget(QString title)
{
    if (!m_geomNode) return nullptr;

    auto* w = new RectangleDialog();
    QString uuid = QUuid::createUuid().toString();

    ProjectNode* node = new ProjectNode(title, uuid, (BasePage*)w);
    m_geomNode->addChild(node);

    node->setData(0, ProjectNode::RoleUuid,   uuid);
    node->setData(0, ProjectNode::RoleModule, "Geometry");
    node->setData(0, ProjectNode::RoleKind,   "Rectangle");

    return w;
}

void PreProcessing::newOccBoolenToolWidget()
{
    if (!m_geomNode) return;
    OccBooleanToolWidget* w = new OccBooleanToolWidget();
    m_geomNode->addChild(new ProjectNode("boolen", QUuid::createUuid().toString(), (BasePage*)w));
}
