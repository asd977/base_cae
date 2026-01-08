#include "LoadSettingWidget.h"
#include "ui_LoadSettingWidget.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QColor>

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkGlyph3D.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>

#include "PhysicalFieldMeshDisplay.h"

namespace {
class LoadPickingCallback : public vtkCommand
{
public:
    static LoadPickingCallback *New() { return new LoadPickingCallback; }

    LoadSettingWidget *Widget = nullptr;
    vtkPointPicker *PointPicker = nullptr;
    vtkRenderer *Renderer = nullptr;

    void Execute(vtkObject *caller, unsigned long eventId, void *callData) override
    {
        Q_UNUSED(callData)
        if (eventId != vtkCommand::LeftButtonPressEvent || !Widget || !PointPicker || !Renderer)
            return;

        if (!Widget->isVisible())
            return;

        auto *interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
        if (!interactor)
            return;

        int *pos = interactor->GetEventPosition();
        if (PointPicker->Pick(pos[0], pos[1], 0, Renderer)) {
            vtkIdType pointId = PointPicker->GetPointId();
            Widget->onPickedPoint(pointId);
        }
    }
};

vtkSmartPointer<vtkActor> createLoadActor(const QVector<Node> &nodes,
                                          const QVector<int> &nodeIds,
                                          const QColor &color)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    for (int nodeId : nodeIds) {
        for (const Node &n : nodes) {
            if (n.id != nodeId)
                continue;
            points->InsertNextPoint(n.x, n.y, n.z);
            break;
        }
    }

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);

    auto sphere = vtkSmartPointer<vtkSphereSource>::New();
    sphere->SetRadius(0.25);
    sphere->SetThetaResolution(12);
    sphere->SetPhiResolution(12);

    auto glyph = vtkSmartPointer<vtkGlyph3D>::New();
    glyph->SetSourceConnection(sphere->GetOutputPort());
    glyph->SetInputData(poly);
    glyph->ScalingOff();
    glyph->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(glyph->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color.redF(), color.greenF(), color.blueF());
    actor->GetProperty()->SetOpacity(0.9);
    return actor;
}
} // namespace

LoadSettingWidget::LoadSettingWidget(QWidget *parent)
    : ui(new Ui::LoadSettingWidget)
    , BasePage("LoadSettingWidget")
{
    ui->setupUi(this);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    m_meshModuleService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (m_visualWidgetService)
        m_renderer = m_visualWidgetService->renderer();

    connect(ui->buttonLoadMesh, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonLoadMesh_clicked);
    connect(ui->buttonUseCurrentMesh, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonUseCurrentMesh_clicked);
    connect(ui->buttonLoadLoad, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonLoadLoad_clicked);
    connect(ui->buttonSaveLoad, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonSaveLoad_clicked);
    connect(ui->checkShowBody, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkShowBody_toggled);
    connect(ui->checkShowSurface, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkShowSurface_toggled);
    connect(ui->checkShowPoint, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkShowPoint_toggled);
    connect(ui->checkShowTemperature, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkShowTemperature_toggled);
    connect(ui->checkShowSeepage, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkShowSeepage_toggled);
    connect(ui->checkEnablePicking, &QCheckBox::toggled, this, &LoadSettingWidget::on_checkEnablePicking_toggled);
    connect(ui->buttonApplyBody, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonApplyBody_clicked);
    connect(ui->buttonApplySurface, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonApplySurface_clicked);
    connect(ui->buttonApplyPoint, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonApplyPoint_clicked);
    connect(ui->buttonApplyTemperature, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonApplyTemperature_clicked);
    connect(ui->buttonApplySeepage, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonApplySeepage_clicked);
    connect(ui->buttonClearPicked, &QPushButton::clicked, this, &LoadSettingWidget::on_buttonClearPicked_clicked);
}

LoadSettingWidget::~LoadSettingWidget()
{
    teardownInteractor();
    delete ui;
}

void LoadSettingWidget::beforeLoad()
{
    setupInteractor();
}

void LoadSettingWidget::beforeUnload()
{
    teardownInteractor();
}

void LoadSettingWidget::setupInteractor()
{
    if (!m_visualWidgetService || !m_renderer || m_pointPicker)
        return;

    auto *interactor = m_visualWidgetService->interactor();
    if (!interactor)
        return;

    m_pointPicker = vtkSmartPointer<vtkPointPicker>::New();

    auto cb = vtkSmartPointer<LoadPickingCallback>::New();
    cb->Widget = this;
    cb->PointPicker = m_pointPicker;
    cb->Renderer = m_renderer;

    interactor->AddObserver(vtkCommand::LeftButtonPressEvent, cb, 1.0);
}

void LoadSettingWidget::teardownInteractor()
{
    m_pointPicker = nullptr;
}

void LoadSettingWidget::loadMeshFromFile(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QString err;
    IiinGridModel model;
    if (!model.loadFromFile(fileName, &err)) {
        QMessageBox::warning(this, tr("加载失败"), err.isEmpty() ? tr("无法读取 IIIN.DAT") : err);
        return;
    }

    m_model = model;
    m_meshFileName = fileName;
    ui->lineEditMeshFile->setText(fileName);

    m_grid = PhysicalFieldMeshDisplay::buildGridFromModel(model);
    if (!m_grid || !m_visualWidgetService)
        return;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(m_grid);
    mapper->ScalarVisibilityOff();

    m_meshActor = vtkSmartPointer<vtkActor>::New();
    m_meshActor->SetMapper(mapper);
    m_meshActor->GetProperty()->EdgeVisibilityOn();

    static const QString kMeshPropId = QStringLiteral("physicalfield.mesh");
    m_visualWidgetService->removeProp(kMeshPropId, false);
    m_visualWidgetService->addProp(kMeshPropId,
                                   m_meshActor,
                                   QStringLiteral("物理场"),
                                   QStringLiteral("荷载网格"),
                                   true,
                                   true);

    if (m_pointPicker) {
        m_pointPicker->InitializePickList();
        m_pointPicker->AddPickList(m_meshActor);
        m_pointPicker->PickFromListOn();
    }

    updateLoadMarkers();
}

void LoadSettingWidget::on_buttonUseCurrentMesh_clicked()
{
    if (!m_meshModuleService)
        return;

    IiinGridModel model;
    if (!m_meshModuleService->currentModel(model))
        return;

    m_model = model;
    m_meshFileName = model.fileName();
    ui->lineEditMeshFile->setText(model.fileName());

    m_grid = PhysicalFieldMeshDisplay::buildGridFromModel(model);
    if (!m_grid || !m_visualWidgetService)
        return;

    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->SetInputData(m_grid);
    mapper->ScalarVisibilityOff();

    m_meshActor = vtkSmartPointer<vtkActor>::New();
    m_meshActor->SetMapper(mapper);
    m_meshActor->GetProperty()->EdgeVisibilityOn();

    static const QString kMeshPropId = QStringLiteral("physicalfield.mesh");
    m_visualWidgetService->removeProp(kMeshPropId, false);
    m_visualWidgetService->addProp(kMeshPropId,
                                   m_meshActor,
                                   QStringLiteral("物理场"),
                                   QStringLiteral("荷载网格"),
                                   true,
                                   true);

    if (m_pointPicker) {
        m_pointPicker->InitializePickList();
        m_pointPicker->AddPickList(m_meshActor);
        m_pointPicker->PickFromListOn();
    }

    updateLoadMarkers();
}

void LoadSettingWidget::updateLoadMarkers()
{
    if (!m_visualWidgetService)
        return;

    QVector<int> bodyNodes;
    for (const BodyLoad &load : m_bodyLoads) {
        if (load.byMaterial) {
            for (const Elem &e : m_model.elems()) {
                if (e.matype == load.targetId) {
                    for (int nid : e.nodeIds)
                        bodyNodes.append(nid);
                }
            }
        } else {
            for (const Elem &e : m_model.elems()) {
                if (e.id == load.targetId) {
                    for (int nid : e.nodeIds)
                        bodyNodes.append(nid);
                    break;
                }
            }
        }
    }

    QVector<int> surfaceNodes;
    for (const SurfaceLoad &load : m_surfaceLoads)
        surfaceNodes += load.nodeIds;

    QVector<int> pointNodes;
    for (const PointLoad &load : m_pointLoads)
        pointNodes.append(load.nodeId);

    QVector<int> tempNodes;
    for (const TemperatureLoad &load : m_temperatureLoads)
        tempNodes.append(load.nodeId);

    QVector<int> seepageNodes;
    for (const SeepageLoad &load : m_seepageLoads) {
        if (load.byMaterial) {
            for (const Elem &e : m_model.elems()) {
                if (e.matype == load.targetId) {
                    for (int nid : e.nodeIds)
                        seepageNodes.append(nid);
                }
            }
        } else {
            for (const Elem &e : m_model.elems()) {
                if (e.id == load.targetId) {
                    for (int nid : e.nodeIds)
                        seepageNodes.append(nid);
                    break;
                }
            }
        }
    }

    m_bodyActor = createLoadActor(m_model.nodes(), bodyNodes, QColor(52, 152, 219));
    m_surfaceActor = createLoadActor(m_model.nodes(), surfaceNodes, QColor(155, 89, 182));
    m_pointActor = createLoadActor(m_model.nodes(), pointNodes, QColor(241, 196, 15));
    m_temperatureActor = createLoadActor(m_model.nodes(), tempNodes, QColor(231, 76, 60));
    m_seepageActor = createLoadActor(m_model.nodes(), seepageNodes, QColor(46, 204, 113));

    static const QString kBodyId = QStringLiteral("physicalfield.load.body");
    static const QString kSurfaceId = QStringLiteral("physicalfield.load.surface");
    static const QString kPointId = QStringLiteral("physicalfield.load.point");
    static const QString kTempId = QStringLiteral("physicalfield.load.temp");
    static const QString kSeepageId = QStringLiteral("physicalfield.load.seepage");

    m_visualWidgetService->removeProp(kBodyId, false);
    m_visualWidgetService->removeProp(kSurfaceId, false);
    m_visualWidgetService->removeProp(kPointId, false);
    m_visualWidgetService->removeProp(kTempId, false);
    m_visualWidgetService->removeProp(kSeepageId, false);

    m_visualWidgetService->addProp(kBodyId, m_bodyActor, QStringLiteral("荷载"), QStringLiteral("体力"), false, false);
    m_visualWidgetService->addProp(kSurfaceId, m_surfaceActor, QStringLiteral("荷载"), QStringLiteral("面力"), false, false);
    m_visualWidgetService->addProp(kPointId, m_pointActor, QStringLiteral("荷载"), QStringLiteral("集中力"), false, false);
    m_visualWidgetService->addProp(kTempId, m_temperatureActor, QStringLiteral("荷载"), QStringLiteral("变温"), false, false);
    m_visualWidgetService->addProp(kSeepageId, m_seepageActor, QStringLiteral("荷载"), QStringLiteral("渗透体力"), false, false);

    setLoadDisplayVisible();
    m_visualWidgetService->render();
}

void LoadSettingWidget::setLoadDisplayVisible()
{
    if (!m_visualWidgetService)
        return;

    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.load.body"), ui->checkShowBody->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.load.surface"), ui->checkShowSurface->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.load.point"), ui->checkShowPoint->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.load.temp"), ui->checkShowTemperature->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.load.seepage"), ui->checkShowSeepage->isChecked(), false);
}

void LoadSettingWidget::appendPickedNode(int nodeId)
{
    if (nodeId <= 0)
        return;
    if (m_pickedNodeIds.contains(nodeId))
        return;
    m_pickedNodeIds.append(nodeId);
    QStringList list;
    for (int id : m_pickedNodeIds)
        list << QString::number(id);
    const QString joined = list.join(",");
    ui->lineEditPickedNodes->setText(joined);
    ui->lineEditSurfaceNodes->setText(joined);
}

void LoadSettingWidget::clearPickedNodes()
{
    m_pickedNodeIds.clear();
    ui->lineEditPickedNodes->clear();
    ui->lineEditSurfaceNodes->clear();
}

LoadVector LoadSettingWidget::parseVector(const QString &text) const
{
    LoadVector vec;
    const QStringList parts = text.split(QRegExp("[,\\s]+"), Qt::SkipEmptyParts);
    if (parts.size() > 0) vec.fx = parts.at(0).toDouble();
    if (parts.size() > 1) vec.fy = parts.at(1).toDouble();
    if (parts.size() > 2) vec.fz = parts.at(2).toDouble();
    return vec;
}

void LoadSettingWidget::saveLoadFile(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);

    out << "0 0 0\n";
    out << "XP YP SGX SGY TXY II\n";

    out << "0\n";
    out << "FNAME IDIR HEIGHT GAMA PGROUT II\n";

    const int nfb = m_bodyLoads.size() + m_seepageLoads.size();
    out << nfb << "\n";
    out << "JMFB FBX FBY FBZ II\n";
    for (const BodyLoad &load : m_bodyLoads) {
        out << load.targetId << " " << load.value.fx << " " << load.value.fy << " " << load.value.fz << " 0\n";
    }
    for (const SeepageLoad &load : m_seepageLoads) {
        out << load.targetId << " " << load.value.fx << " " << load.value.fy << " " << load.value.fz << " 0\n";
    }

    out << m_pointLoads.size() << "\n";
    out << "JMFC X Y Z FCX FCY FCZ II\n";
    for (const PointLoad &load : m_pointLoads) {
        int idx = m_model.nodeIndexById(load.nodeId);
        if (idx < 0)
            continue;
        const Node &n = m_model.nodes().at(idx);
        out << load.nodeId << " " << n.x << " " << n.y << " " << n.z << " "
            << load.value.fx << " " << load.value.fy << " " << load.value.fz << " 0\n";
    }

    out << "0 0\n";

    out << m_surfaceLoads.size() << " 0\n";
    for (int i = 0; i < m_surfaceLoads.size(); ++i) {
        const SurfaceLoad &load = m_surfaceLoads.at(i);
        out << (i + 1) << " " << load.nodeIds.size() << "\n";
        for (int nodeId : load.nodeIds) {
            out << nodeId << " " << load.value.fx << " " << load.value.fy << " " << load.value.fz << "\n";
        }
    }

    out << m_temperatureLoads.size() << "\n";
    out << "JMFW ALFA DT II\n";
    for (const TemperatureLoad &load : m_temperatureLoads) {
        out << load.nodeId << " " << load.alfa << " " << load.dt << " 0\n";
    }

    out << "0\n";
    out << "JMFG XP YP ZP LDTYPE LOCATION LOAD ACTION_PLANE II\n";

    out << "0\n\n\n";
    out << "0\n\n\n";
    out << "0\n\n\n";
    out << "0 0 0 0 0\n";

    file.close();
    m_loadFileName = fileName;
    ui->lineEditLoadFile->setText(fileName);
}

void LoadSettingWidget::onPickedPoint(vtkIdType pointId)
{
    if (!m_enablePicking || pointId < 0)
        return;

    if (pointId >= m_model.nodes().size())
        return;

    int nodeId = m_model.nodes().at(static_cast<int>(pointId)).id;
    appendPickedNode(nodeId);
}

void LoadSettingWidget::on_buttonLoadMesh_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("导入 IIIN.DAT"), QString(), tr("IIIN Grid (*.dat)"));
    loadMeshFromFile(fileName);
}

void LoadSettingWidget::on_buttonLoadLoad_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("导入 IIWL.DAT"), QString(), tr("Load (*.dat)"));
    if (fileName.isEmpty())
        return;
    m_loadFileName = fileName;
    ui->lineEditLoadFile->setText(fileName);
}

void LoadSettingWidget::on_buttonSaveLoad_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("保存 IIWL.DAT"), m_loadFileName, tr("Load (*.dat)"));
    if (fileName.isEmpty())
        return;
    saveLoadFile(fileName);
}

void LoadSettingWidget::on_checkShowBody_toggled(bool checked)
{
    Q_UNUSED(checked)
    setLoadDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void LoadSettingWidget::on_checkShowSurface_toggled(bool checked)
{
    Q_UNUSED(checked)
    setLoadDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void LoadSettingWidget::on_checkShowPoint_toggled(bool checked)
{
    Q_UNUSED(checked)
    setLoadDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void LoadSettingWidget::on_checkShowTemperature_toggled(bool checked)
{
    Q_UNUSED(checked)
    setLoadDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void LoadSettingWidget::on_checkShowSeepage_toggled(bool checked)
{
    Q_UNUSED(checked)
    setLoadDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void LoadSettingWidget::on_checkEnablePicking_toggled(bool checked)
{
    m_enablePicking = checked;
}

void LoadSettingWidget::on_buttonApplyBody_clicked()
{
    BodyLoad load;
    load.byMaterial = (ui->comboBodyTargetType->currentIndex() == 0);
    load.targetId = ui->lineEditBodyTarget->text().trimmed().toInt();
    load.value = parseVector(ui->lineEditBodyValue->text());
    m_bodyLoads.append(load);
    updateLoadMarkers();
    if (!m_loadFileName.isEmpty())
        saveLoadFile(m_loadFileName);
}

void LoadSettingWidget::on_buttonApplySurface_clicked()
{
    if (!(m_pickedNodeIds.size() == 2 || m_pickedNodeIds.size() == 4))
        return;

    SurfaceLoad load;
    load.nodeIds = m_pickedNodeIds;
    load.value = parseVector(ui->lineEditSurfaceValue->text());
    m_surfaceLoads.append(load);
    clearPickedNodes();
    updateLoadMarkers();
    if (!m_loadFileName.isEmpty())
        saveLoadFile(m_loadFileName);
}

void LoadSettingWidget::on_buttonApplyPoint_clicked()
{
    bool ok = false;
    int nodeId = ui->lineEditPointNode->text().trimmed().toInt(&ok);
    if (!ok || nodeId <= 0)
        return;

    PointLoad load;
    load.nodeId = nodeId;
    load.value = parseVector(ui->lineEditPointValue->text());
    m_pointLoads.append(load);
    updateLoadMarkers();
    if (!m_loadFileName.isEmpty())
        saveLoadFile(m_loadFileName);
}

void LoadSettingWidget::on_buttonApplyTemperature_clicked()
{
    bool ok = false;
    int nodeId = ui->lineEditTemperatureNode->text().trimmed().toInt(&ok);
    if (!ok || nodeId <= 0)
        return;

    const QStringList parts = ui->lineEditTemperatureValue->text().split(QRegExp("[,\\s]+"), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return;

    TemperatureLoad load;
    load.nodeId = nodeId;
    load.alfa = parts.at(0).toDouble();
    load.dt = parts.at(1).toDouble();
    m_temperatureLoads.append(load);
    updateLoadMarkers();
    if (!m_loadFileName.isEmpty())
        saveLoadFile(m_loadFileName);
}

void LoadSettingWidget::on_buttonApplySeepage_clicked()
{
    SeepageLoad load;
    load.byMaterial = (ui->comboSeepageTargetType->currentIndex() == 0);
    load.targetId = ui->lineEditSeepageTarget->text().trimmed().toInt();
    load.value = parseVector(ui->lineEditSeepageValue->text());
    m_seepageLoads.append(load);
    updateLoadMarkers();
    if (!m_loadFileName.isEmpty())
        saveLoadFile(m_loadFileName);
}

void LoadSettingWidget::on_buttonClearPicked_clicked()
{
    clearPickedNodes();
}
