
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

#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PreProcessing::PreProcessing()
{
	ServiceRegistry::instance().registerService<PreProcessingService>(this);
    UiFrameService *uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    m_preProcessingWidget = new PreProcessingWidget;
    uiFrameService->addWidget(m_preProcessingWidget,"几何");
	initPageFactory();
}

void PreProcessing::initPageFactory() 
{
	m_pageFactory.clear();
	// 网格
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_MESH2D] = [this](QString title) { return m_preProcessingWidget->newTetrahedronGridWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_MESH3D] = [this](QString title) { return m_preProcessingWidget->newHexahedronGridWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_IMPORT_GRID] = [this](QString title) { return m_preProcessingWidget->newGridImportWidget(title); };
	// 几何
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_IMPORTGEOM] = [this](QString title) { return m_preProcessingWidget->newGeometryImportWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_ARC] = [this](QString title){ return m_preProcessingWidget->newGeometryArcWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_BEZIER2D] = [this](QString title) { return m_preProcessingWidget->newGeometryTwoBsrWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_BEZIER3D] = [this](QString title) { return m_preProcessingWidget->newGeometryThreeBsrWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_CONE] = [this](QString title) { return m_preProcessingWidget->newGeometryConeWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_CYLINDER] = [this](QString title) { return m_preProcessingWidget->newGeometryCylinderWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_ELLIPSE] = [this](QString title) { return m_preProcessingWidget->newGeometryEllipseWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_HELIX] = [this](QString title) { return m_preProcessingWidget->newGeometryHelixWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_CIRCLE] = [this](QString title) { return m_preProcessingWidget->newGeometryCircleWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_LINE] = [this](QString title) { return m_preProcessingWidget->newGeometryLineWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_TORUS] = [this](QString title) { return m_preProcessingWidget->newGeometryTorusWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_SQUARE] = [this](QString title) { return m_preProcessingWidget->newGeometrySquareWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_SPHERE] = [this](QString title) { return m_preProcessingWidget->newGeometrySphereWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_RECTANGLE] = [this](QString title) { return m_preProcessingWidget->newGeometryRectangleWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_POLYGON] = [this](QString title) { return m_preProcessingWidget->newGeometryPolygonWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_INTER] = [this](QString title) { return m_preProcessingWidget->newGeometryInterCurveWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_POINT] = [this](QString title) { return m_preProcessingWidget->newGeometryPointWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_PARAM] = [this](QString title) { return m_preProcessingWidget->newGeometryParamCurveWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_BOX] = [this](QString title) { return m_preProcessingWidget->newGeometryBoxWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_HYPER] = [this](QString title) { return m_preProcessingWidget->newGeometryHyperbolaWidget(title); };
	m_pageFactory[PublicConstants::PAGE_PREPROCESS_PARABOLA] = [this](QString title) { return m_preProcessingWidget->newGeometryParabolaEdgeWidget(title); };

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
	m_preProcessingWidget->setGeomNode(m_geomNode);
	m_gridNode = new ProjectNode("网格");
	projectManagerService->addTreeItem(m_gridNode);
	m_preProcessingWidget->setGridNode(m_gridNode);
}

bool PreProcessing::saveProject()
{
	if (!m_geomNode || !m_gridNode) return true;
	QString projectDir = PublicVariables::instance().get(PublicConstants::VALUE_PROJECT_DIR).toString();
	const QString filePath = QDir(projectDir).filePath(PublicConstants::PROJECT_FILE_PREPROCESSING);

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
	m_preProcessingWidget->setGeomNode(m_geomNode);
	success = loadChildProject(m_geomNode, "geom_items");
	if (!success) return false;
	m_gridNode = new ProjectNode("网格");
	projectManagerService->addTreeItem(m_gridNode);
	m_preProcessingWidget->setGridNode(m_gridNode);
	success = loadChildProject(m_gridNode, "grid_items");
	if (!success) return false;
	return success;
}

bool PreProcessing::loadChildProject(ProjectNode * rootNode,const QString & name)
{
	QString projectDir = PublicVariables::instance().get(PublicConstants::VALUE_PROJECT_DIR).toString();
	const QString filePath = QDir(projectDir).filePath(PublicConstants::PROJECT_FILE_PREPROCESSING);
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

bool PreProcessing::getGridIsDraw()
{
	return m_preProcessingWidget->getGridIsDraw();
}

void PreProcessing::render()
{
    m_preProcessingWidget->render();
}


