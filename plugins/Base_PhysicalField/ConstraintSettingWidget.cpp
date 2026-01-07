#include "ConstraintSettingWidget.h"
#include "ui_ConstraintSettingWidget.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QColor>
#include <functional>

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkGlyph3D.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>

#include "PhysicalFieldMeshDisplay.h"

namespace {
class ConstraintPickingCallback : public vtkCommand
{
public:
    static ConstraintPickingCallback *New() { return new ConstraintPickingCallback; }

    ConstraintSettingWidget *Widget = nullptr;
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

vtkSmartPointer<vtkActor> createConstraintActor(const QVector<Node> &nodes,
                                                const QHash<int, ConstraintSettingWidget::ConstraintState> &constraints,
                                                std::function<bool(const ConstraintSettingWidget::ConstraintState&)> filter,
                                                const QColor &color)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    for (const Node &n : nodes) {
        if (!constraints.contains(n.id))
            continue;
        if (!filter(constraints.value(n.id)))
            continue;
        points->InsertNextPoint(n.x, n.y, n.z);
    }

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);

    auto sphere = vtkSmartPointer<vtkSphereSource>::New();
    sphere->SetRadius(0.2);
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

ConstraintSettingWidget::ConstraintSettingWidget(QWidget *parent)
    : ui(new Ui::ConstraintSettingWidget)
    , BasePage("ConstraintSettingWidget")
{
    ui->setupUi(this);
    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (m_visualWidgetService)
        m_renderer = m_visualWidgetService->renderer();

    connect(ui->buttonLoadMesh, &QPushButton::clicked, this, &ConstraintSettingWidget::on_buttonLoadMesh_clicked);
    connect(ui->buttonLoadConstraint, &QPushButton::clicked, this, &ConstraintSettingWidget::on_buttonLoadConstraint_clicked);
    connect(ui->buttonSaveConstraint, &QPushButton::clicked, this, &ConstraintSettingWidget::on_buttonSaveConstraint_clicked);
    connect(ui->buttonApplyNode, &QPushButton::clicked, this, &ConstraintSettingWidget::on_buttonApplyNode_clicked);
    connect(ui->buttonApplyMaterial, &QPushButton::clicked, this, &ConstraintSettingWidget::on_buttonApplyMaterial_clicked);
    connect(ui->checkShowUx, &QCheckBox::toggled, this, &ConstraintSettingWidget::on_checkShowUx_toggled);
    connect(ui->checkShowUy, &QCheckBox::toggled, this, &ConstraintSettingWidget::on_checkShowUy_toggled);
    connect(ui->checkShowUz, &QCheckBox::toggled, this, &ConstraintSettingWidget::on_checkShowUz_toggled);
    connect(ui->checkEnablePicking, &QCheckBox::toggled, this, &ConstraintSettingWidget::on_checkEnablePicking_toggled);
}

ConstraintSettingWidget::~ConstraintSettingWidget()
{
    teardownInteractor();
    delete ui;
}

void ConstraintSettingWidget::beforeLoad()
{
    setupInteractor();
}

void ConstraintSettingWidget::beforeUnload()
{
    teardownInteractor();
}

void ConstraintSettingWidget::setupInteractor()
{
    if (!m_visualWidgetService || !m_renderer || m_pointPicker)
        return;

    auto *interactor = m_visualWidgetService->interactor();
    if (!interactor)
        return;

    m_pointPicker = vtkSmartPointer<vtkPointPicker>::New();

    auto cb = vtkSmartPointer<ConstraintPickingCallback>::New();
    cb->Widget = this;
    cb->PointPicker = m_pointPicker;
    cb->Renderer = m_renderer;

    interactor->AddObserver(vtkCommand::LeftButtonPressEvent, cb, 1.0);
}

void ConstraintSettingWidget::teardownInteractor()
{
    m_pointPicker = nullptr;
}

void ConstraintSettingWidget::loadMeshFromFile(const QString &fileName)
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
                                   QStringLiteral("约束网格"),
                                   true,
                                   true);

    if (m_pointPicker) {
        m_pointPicker->InitializePickList();
        m_pointPicker->AddPickList(m_meshActor);
        m_pointPicker->PickFromListOn();
    }

    updateMaterialSetOptions();
    updateConstraintMarkers();
}

void ConstraintSettingWidget::updateMaterialSetOptions()
{
    ui->comboMaterialSet->clear();
    ui->comboMaterialSet->addItem(tr("请选择"), QVariant());
    const auto matypes = m_model.allMatypes();
    QList<int> list = matypes.values();
    std::sort(list.begin(), list.end());
    for (int matype : list) {
        ui->comboMaterialSet->addItem(QString::number(matype), matype);
    }
}

void ConstraintSettingWidget::updateConstraintMarkers()
{
    if (!m_visualWidgetService)
        return;

    m_constraintActorX = createConstraintActor(m_model.nodes(),
                                               m_constraints,
                                               [](const ConstraintState &s){ return s.ux != 1; },
                                               QColor(231, 76, 60));
    m_constraintActorY = createConstraintActor(m_model.nodes(),
                                               m_constraints,
                                               [](const ConstraintState &s){ return s.uy != 1; },
                                               QColor(46, 204, 113));
    m_constraintActorZ = createConstraintActor(m_model.nodes(),
                                               m_constraints,
                                               [](const ConstraintState &s){ return s.uz != 1; },
                                               QColor(52, 152, 219));

    static const QString kConstraintXId = QStringLiteral("physicalfield.constraint.x");
    static const QString kConstraintYId = QStringLiteral("physicalfield.constraint.y");
    static const QString kConstraintZId = QStringLiteral("physicalfield.constraint.z");

    m_visualWidgetService->removeProp(kConstraintXId, false);
    m_visualWidgetService->removeProp(kConstraintYId, false);
    m_visualWidgetService->removeProp(kConstraintZId, false);

    m_visualWidgetService->addProp(kConstraintXId, m_constraintActorX, QStringLiteral("约束"), QStringLiteral("约束X"), false, false);
    m_visualWidgetService->addProp(kConstraintYId, m_constraintActorY, QStringLiteral("约束"), QStringLiteral("约束Y"), false, false);
    m_visualWidgetService->addProp(kConstraintZId, m_constraintActorZ, QStringLiteral("约束"), QStringLiteral("约束Z"), false, false);

    setConstraintDisplayVisible();
    m_visualWidgetService->render();
}

void ConstraintSettingWidget::applyConstraintToNode(int nodeId, const ConstraintState &state)
{
    if (nodeId <= 0)
        return;
    m_constraints[nodeId] = state;
    updateConstraintMarkers();
}

void ConstraintSettingWidget::applyConstraintToMaterialSet(int matype, const ConstraintState &state)
{
    if (matype <= 0)
        return;

    QSet<int> nodeIds;
    for (const Elem &e : m_model.elems()) {
        if (e.matype != matype)
            continue;
        for (int nid : e.nodeIds)
            nodeIds.insert(nid);
    }

    for (int nid : nodeIds)
        m_constraints[nid] = state;

    updateConstraintMarkers();
}

void ConstraintSettingWidget::setConstraintDisplayVisible()
{
    if (!m_visualWidgetService)
        return;

    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.constraint.x"), ui->checkShowUx->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.constraint.y"), ui->checkShowUy->isChecked(), false);
    m_visualWidgetService->setVisible(QStringLiteral("physicalfield.constraint.z"), ui->checkShowUz->isChecked(), false);
}

ConstraintSettingWidget::ConstraintState ConstraintSettingWidget::constraintFromUi() const
{
    auto convert = [this](QComboBox *combo) -> int {
        bool ok = false;
        int value = combo->currentData().toInt(&ok);
        if (!ok)
            return 1;
        if (value != -1)
            return value;

        int index = ui->lineEditGivenIndex->text().trimmed().toInt(&ok);
        if (!ok || index <= 0 || index == 10000)
            return -1;
        return -index;
    };

    ConstraintState state;
    state.ux = convert(ui->comboUx);
    state.uy = convert(ui->comboUy);
    state.uz = convert(ui->comboUz);
    return state;
}

void ConstraintSettingWidget::saveConstraintFile(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    const int madof = 3;
    out << m_model.numnp() << " " << madof << "\n";

    for (const Node &n : m_model.nodes()) {
        const auto state = m_constraints.value(n.id, ConstraintState{});
        out << n.x << " " << n.y << " " << n.z << " "
            << state.ux << " " << state.uy << " " << state.uz << "\n";
    }

    file.close();
    m_constraintFileName = fileName;
    ui->lineEditConstraintFile->setText(fileName);
}

bool ConstraintSettingWidget::loadConstraintFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    QString header = in.readLine().trimmed();
    if (header.isEmpty())
        return false;

    QStringList headerParts = header.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
    if (headerParts.size() < 2)
        return false;

    bool ok = false;
    int numnp = headerParts.at(0).toInt(&ok);
    if (!ok || numnp <= 0)
        return false;

    int madof = headerParts.at(1).toInt(&ok);
    if (!ok || madof <= 0)
        madof = 3;

    m_constraints.clear();

    int index = 0;
    while (!in.atEnd() && index < m_model.nodes().size()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;
        QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < madof)
            continue;

        int start = parts.size() - madof;
        ConstraintState state;
        state.ux = parts.value(start + 0).toInt();
        state.uy = madof > 1 ? parts.value(start + 1).toInt() : 1;
        state.uz = madof > 2 ? parts.value(start + 2).toInt() : 1;

        int nodeId = m_model.nodes().value(index).id;
        m_constraints[nodeId] = state;
        ++index;
    }

    m_constraintFileName = fileName;
    ui->lineEditConstraintFile->setText(fileName);
    updateConstraintMarkers();
    return true;
}

void ConstraintSettingWidget::onPickedPoint(vtkIdType pointId)
{
    if (!m_enablePicking || pointId < 0)
        return;

    if (pointId >= m_model.nodes().size())
        return;

    int nodeId = m_model.nodes().at(static_cast<int>(pointId)).id;
    m_selectedNodeId = nodeId;
    ui->lineEditSelectedNode->setText(QString::number(nodeId));
}

void ConstraintSettingWidget::on_buttonLoadMesh_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("导入 IIIN.DAT"), QString(), tr("IIIN Grid (*.dat)") );
    loadMeshFromFile(fileName);
}

void ConstraintSettingWidget::on_buttonLoadConstraint_clicked()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("导入 IIID.DAT"), QString(), tr("Constraint (*.dat)") );
    if (fileName.isEmpty())
        return;
    loadConstraintFile(fileName);
}

void ConstraintSettingWidget::on_buttonSaveConstraint_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("保存 IIID.DAT"), m_constraintFileName, tr("Constraint (*.dat)") );
    if (fileName.isEmpty())
        return;
    saveConstraintFile(fileName);
}

void ConstraintSettingWidget::on_buttonApplyNode_clicked()
{
    bool ok = false;
    int nodeId = ui->lineEditSelectedNode->text().trimmed().toInt(&ok);
    if (!ok || nodeId <= 0)
        return;

    applyConstraintToNode(nodeId, constraintFromUi());
    if (!m_constraintFileName.isEmpty())
        saveConstraintFile(m_constraintFileName);
}

void ConstraintSettingWidget::on_buttonApplyMaterial_clicked()
{
    bool ok = false;
    int matype = ui->comboMaterialSet->currentData().toInt(&ok);
    if (!ok || matype <= 0)
        return;

    applyConstraintToMaterialSet(matype, constraintFromUi());
    if (!m_constraintFileName.isEmpty())
        saveConstraintFile(m_constraintFileName);
}

void ConstraintSettingWidget::on_checkShowUx_toggled(bool checked)
{
    Q_UNUSED(checked)
    setConstraintDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void ConstraintSettingWidget::on_checkShowUy_toggled(bool checked)
{
    Q_UNUSED(checked)
    setConstraintDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void ConstraintSettingWidget::on_checkShowUz_toggled(bool checked)
{
    Q_UNUSED(checked)
    setConstraintDisplayVisible();
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void ConstraintSettingWidget::on_checkEnablePicking_toggled(bool checked)
{
    m_enablePicking = checked;
}
