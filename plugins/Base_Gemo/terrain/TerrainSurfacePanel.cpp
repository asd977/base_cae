#include "TerrainSurfacePanel.h"
#include "../builders/GeometryBuildUtils.h"
#include <PublicConstants.h>
#include <GeometryDisplayManager.h>
#include <GeometryCleanup.h>
#include "../workbench/GeometryWorkbench.h"
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QSignalBlocker>
#include <QCheckBox>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyDataMapper.h>
#include <cmath>

using namespace GeometryTerrain;
TerrainSurfacePanel::TerrainSurfacePanel(QWidget* parent):BasePage(PublicConstants::kPageGeomTerrainSurface)
{
    Q_UNUSED(parent);
    auto* layout=new QVBoxLayout(this);
    auto* title=new QLabel(tr("等高线生成地形面"),this);title->setStyleSheet("font-weight:600;");layout->addWidget(title);
    auto* import=new QPushButton(tr("导入等高线 DXF"),this);import->setObjectName("importContoursButton");layout->addWidget(import);
    m_table=new QTableWidget(this);m_table->setObjectName("terrainSourceTable");m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("参与"),tr("图层 / 对象"),tr("用途"),tr("高程"),tr("检查")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(56);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(160);m_table->setMaximumHeight(320);m_table->verticalHeader()->hide();layout->addWidget(m_table);
    auto* form=new QFormLayout;
    m_domainMode=new QComboBox(this);m_domainMode->setObjectName("terrainDomainModeCombo");
    m_domainMode->addItems({tr("按等高线分域"),tr("整体地形面域")});form->addRow(tr("面域划分"),m_domainMode);
    m_spacing=new QDoubleSpinBox(this);m_spacing->setObjectName("terrainSpacingSpin");m_spacing->setDecimals(3);m_spacing->setRange(0.001,1e8);m_spacing->setValue(5);
    form->addRow(tr("预览采样间距"),m_spacing);layout->addLayout(form);
    auto* generateButton=new QPushButton(tr("构建地形面"),this);generateButton->setObjectName("buildTerrainButton");layout->addWidget(generateButton);
    auto* displayRow=new QHBoxLayout;
    m_showContours=new QCheckBox(tr("显示等高线"),this);m_showContours->setObjectName("showTerrainContoursCheck");m_showContours->setChecked(true);displayRow->addWidget(m_showContours);
    layout->addLayout(displayRow);
    m_status=new QLabel(tr("未导入等高线"),this);m_status->setObjectName("terrainStatusLabel");m_status->setWordWrap(true);layout->addWidget(m_status);layout->addStretch();
    connect(import,&QPushButton::clicked,this,[this]
    {
        const auto path=QFileDialog::getOpenFileName(this,tr("导入等高线"),{},tr("DXF 文件 (*.dxf)"));if(path.isEmpty())return;
        QString diagnostic;if(!importFile(path,diagnostic))QMessageBox::warning(this,tr("导入失败"),diagnostic);
    });
    connect(generateButton,&QPushButton::clicked,this,[this]{QString diagnostic;if(!generate(diagnostic))QMessageBox::warning(this,tr("无法构建地形面"),diagnostic);});
    connect(m_showContours,&QCheckBox::toggled,this,[this]{updatePreview();});
    connect(m_spacing,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{invalidate();});
    connect(m_domainMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{invalidate();});
    connect(m_table,&QTableWidget::cellChanged,this,[this](int row,int column)
    {
        if(m_refreshing||row<0||row>=m_sources.size())return;
        if(column==0)m_sources[row].enabled=m_table->item(row,0)->checkState()==Qt::Checked;
        else if(column==3)
        {
            bool ok=false;double z=m_table->item(row,3)->text().toDouble(&ok);
            if(!ok||!std::isfinite(z)) {m_status->setText(tr("高程必须是有效数字。"));refreshTable();return;}
            for(auto& p:m_sources[row].points)p.SetZ(z);
        }
        else return;
        invalidate();refreshTable();updatePreview();
    });
}
void TerrainSurfacePanel::invalidate()
{
    if(m_generated)
    {
        GeometryCleanup::removeExistingGeometry(getUuid(),false);
        if(auto* project=ServiceRegistry::instance().getService<ProjectManagerService>())project->setNodeDimmed(getUuid(),true);
        if(auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>())visual->render();
    }
    m_generated=false;m_result={};
    m_status->setText(tr("地形数据已更新"));
}
void TerrainSurfacePanel::setSources(const QVector<Source>& sources)
{invalidate();m_sources=sources;refreshTable();updatePreview(true);}
bool TerrainSurfacePanel::importFile(const QString& path,QString& diagnostic)
{
    QVector<Source> sources;
    if(!TerrainSurfaceBuilder::readDxf(path,sources,diagnostic))return false;
    setSources(sources);m_sourcePath=QFileInfo(path).fileName();m_status->setText(tr("已导入 %1 条线").arg(m_sources.size()));return true;
}
void TerrainSurfacePanel::refreshTable()
{
    m_refreshing=true;const QSignalBlocker guard(m_table);m_table->setRowCount(m_sources.size());
    for(int row=0;row<m_sources.size();++row)
    {
        const auto& s=m_sources[row];double z=0;const bool constant=s.constantElevation(&z);
        auto* check=new QTableWidgetItem;check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable);check->setCheckState(s.enabled?Qt::Checked:Qt::Unchecked);m_table->setItem(row,0,check);
        auto* name=new QTableWidgetItem(s.layer+" / "+s.name);name->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);m_table->setItem(row,1,name);
        auto* role=new QComboBox(m_table);role->addItems({tr("等高线"),tr("范围边界"),tr("控制线"),tr("高程点")});role->setCurrentIndex(int(s.role));m_table->setCellWidget(row,2,role);
        connect(role,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,row](int index){if(m_refreshing)return;m_sources[row].role=Role(index);invalidate();refreshTable();updatePreview();});
        auto* height=new QTableWidgetItem(constant?QString::number(z,'g',12):tr("保留逐点 Z"));m_table->setItem(row,3,height);
        QString status;
        if(s.role==Role::Contour&&!constant)status=tr("非等高，请修正");
        else if(s.role==Role::Boundary&&!s.closed)status=tr("边界未闭合");
        else status=tr("%1 点，%2").arg(s.points.size()).arg(s.closed?tr("闭合"):tr("开放"));
        auto* info=new QTableWidgetItem(status);info->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);m_table->setItem(row,4,info);
    }
    m_refreshing=false;
}
bool TerrainSurfacePanel::generate(QString& diagnostic)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Result result=TerrainSurfaceBuilder::build(m_sources,m_spacing->value(),DomainMode(m_domainMode->currentIndex()));
    QApplication::restoreOverrideCursor();
    if(!result.ok()) {invalidate();diagnostic=result.error;m_status->setText(diagnostic);return false;}
    m_result=result;m_generated=true;
    GeometryDisplayManager::setTerrainPresentation(getUuid());
    GeometryBuildUtils::registerShape(getUuid(),getDisplayName(),new TopoDS_Shape(result.shape));
    if(auto* workbench=dynamic_cast<GeometryWorkbench*>(ServiceRegistry::instance().getService<PreProcessingService>()))
        workbench->refreshTreePresentation(getUuid());
    diagnostic=result.summary();
    m_status->setText(tr("已生成 %1 个面域，高程 %2～%3")
                      .arg(result.domains.size()).arg(result.minElevation).arg(result.maxElevation));
    updatePreview();
    if(auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        double b[6];result.mesh->GetBounds(b);fitView(b);
        visual->render();
    }
    return true;
}
QJsonObject TerrainSurfacePanel::saveJson()
{auto state=TerrainSurfaceBuilder::state(m_sources,m_generated,m_spacing->value(),DomainMode(m_domainMode->currentIndex()));state["sourceFileName"]=m_sourcePath;state["showContours"]=m_showContours->isChecked();return state;}
void TerrainSurfacePanel::loadJson(const QJsonObject& state)
{
    m_sources=TerrainSurfaceBuilder::sourcesFromState(state);m_sourcePath=state["sourceFileName"].toString();
    {const QSignalBlocker guard(m_spacing);m_spacing->setValue(state["spacing"].toDouble(5));}
    {const QSignalBlocker guard(m_domainMode);m_domainMode->setCurrentIndex(int(TerrainSurfaceBuilder::domainMode(state)));}
    {const QSignalBlocker a(m_showContours);m_showContours->setChecked(state["showContours"].toBool(true));}
    GeometryDisplayManager::setTerrainPresentation(getUuid());
    m_generated=state["generated"].toBool();m_result={};refreshTable();
    m_status->setText(m_generated?tr("已恢复地形面"):tr("已恢复等高线"));
}
void TerrainSurfacePanel::beforeLoad()
{if(m_generated&&!m_result.ok()){QString diagnostic;generate(diagnostic);}else updatePreview(true);}
TerrainSurfacePanel::~TerrainSurfacePanel(){beforeUnload();}
void TerrainSurfacePanel::beforeUnload()
{
    if(auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>())
        if(visual->removeProp(getUuid()+"_terrain_sources",false))visual->render();
}
void TerrainSurfacePanel::fitView(const double b[6])
{
    auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* renderer=visual?visual->renderer():nullptr;if(!renderer)return;
    auto* camera=renderer->GetActiveCamera();
    const double x=(b[0]+b[1])/2,y=(b[2]+b[3])/2,z=(b[4]+b[5])/2;
    const double span=std::max({b[1]-b[0],b[3]-b[2],b[5]-b[4],1.0});
    camera->SetFocalPoint(x,y,z);camera->SetPosition(x+span,y-span,z+span);camera->SetViewUp(0,0,1);
    renderer->ResetCamera(b);renderer->ResetCameraClippingRange();
}
void TerrainSurfacePanel::updatePreview(bool fitCamera)
{
    auto* visual=ServiceRegistry::instance().getService<VisualWidgetService>();if(!visual)return;
    const QString id=getUuid()+"_terrain_sources";visual->removeProp(id,false);
    vtkNew<vtkPoints> points;points->SetDataTypeToDouble();vtkNew<vtkCellArray> lines;vtkNew<vtkCellArray> vertices;
    if(m_showContours->isChecked())for(const auto& source:m_sources)
    {
        if(!source.enabled||source.points.isEmpty())continue;
        const vtkIdType start=points->GetNumberOfPoints();
        for(const auto& p:source.points)points->InsertNextPoint(p.X(),p.Y(),p.Z());
        if(source.points.size()==1){vertices->InsertNextCell(1);vertices->InsertCellPoint(start);}
        else
        {
            lines->InsertNextCell(source.points.size()+(source.closed?1:0));
            for(vtkIdType i=0;i<source.points.size();++i)lines->InsertCellPoint(start+i);
            if(source.closed)lines->InsertCellPoint(start);
        }
    }
    if(points->GetNumberOfPoints())
    {
        vtkNew<vtkPolyData> data;data->SetPoints(points);data->SetLines(lines);data->SetVerts(vertices);
        vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(data);mapper->ScalarVisibilityOff();
        mapper->SetResolveCoincidentTopologyToPolygonOffset();mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0,-1);
        auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);actor->PickableOff();
        actor->GetProperty()->SetColor(.05,.32,.72);actor->GetProperty()->SetLineWidth(2);actor->GetProperty()->SetPointSize(8);actor->GetProperty()->LightingOff();
        visual->addProp(id,actor,"GeometryPreview",tr("地形等高线预览"),false,false);
        if(fitCamera){double bounds[6];data->GetBounds(bounds);fitView(bounds);}
    }
    visual->render();
}
