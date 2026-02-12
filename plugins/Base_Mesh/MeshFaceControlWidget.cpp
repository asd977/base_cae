#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>
#include <QMessageBox>
#include <QTableWidgetItem>

#include "../Base_Gemo/GeomManagerInstance.h"
#include "../Base_Gemo/GeomShapeObject.h"

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <IVtkOCC_Shape.hxx>
#include <IVtkTools_ShapeDataSource.hxx>
#include <IVtkTools_DisplayModeFilter.hxx>
#include <Prs3d_Drawer.hxx>

#include <vtkActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkNew.h>

namespace {

vtkSmartPointer<vtkActor> buildFaceHighlightActor(const TopoDS_Shape& face)
{
    Handle(Prs3d_Drawer) drawer = new Prs3d_Drawer();
    drawer->SetDeviationCoefficient(0.0015);

    IVtkOCC_Shape::Handle occShape = new IVtkOCC_Shape(face);

    vtkNew<IVtkTools_ShapeDataSource> src;
    src->SetShape(occShape);

    vtkNew<IVtkTools_DisplayModeFilter> dmSurf;
    dmSurf->SetInputConnection(src->GetOutputPort());
    dmSurf->SetDisplayMode(DM_Shading);
    dmSurf->SetSmoothShading(true);
    dmSurf->SetFaceBoundaryDraw(true);
    dmSurf->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(dmSurf->GetOutput());
    mapper->ScalarVisibilityOff();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(1.0, 0.85, 0.2);
    actor->GetProperty()->SetOpacity(0.9);
    actor->GetProperty()->SetAmbient(0.4);
    actor->GetProperty()->SetDiffuse(0.6);
    return actor;
}

}

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
    , m_highlightPropId("mesh.face.control.highlight")
{
    ui->setupUi(this);
    setupUiState();

    connect(ui->btnAdd, &QPushButton::clicked, this, &MeshFaceControlWidget::onAddControl);
    connect(ui->btnUpdate, &QPushButton::clicked, this, &MeshFaceControlWidget::onUpdateControl);
    connect(ui->btnDelete, &QPushButton::clicked, this, &MeshFaceControlWidget::onDeleteControl);
    connect(ui->btnReset, &QPushButton::clicked, this, &MeshFaceControlWidget::onResetForm);

    connect(ui->tableControls, &QTableWidget::itemSelectionChanged, this, &MeshFaceControlWidget::onSelectionChanged);
    connect(ui->editFaceIds, &QLineEdit::textChanged, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMinSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinMaxSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->checkCurvature, &QCheckBox::toggled, this, &MeshFaceControlWidget::onParameterEdited);
    connect(ui->spinCurvatureLevel, qOverload<int>(&QSpinBox::valueChanged), this, &MeshFaceControlWidget::onParameterEdited);

    connect(ui->btnRefreshEntity, &QPushButton::clicked, this, &MeshFaceControlWidget::onRefreshEntities);
    connect(ui->btnAppendEntity, &QPushButton::clicked, this, &MeshFaceControlWidget::onAppendSelectedEntity);
    connect(ui->btnHighlightEntity, &QPushButton::clicked, this, &MeshFaceControlWidget::onHighlightSelectedEntity);
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    clearHighlight();
    delete ui;
}

void MeshFaceControlWidget::beforeLoad()
{
    rebuildFaceCandidates();
    refreshSummary();
}

void MeshFaceControlWidget::beforeUnload()
{
    clearHighlight();
}

void MeshFaceControlWidget::onAddControl()
{
    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("面控制"), err);
        return;
    }

    const FaceControlItem item = collectFormData();
    m_items.push_back(item);
    appendOrUpdateTableRow(ui->tableControls->rowCount(), item);
    ui->tableControls->clearSelection();
    onResetForm();
    refreshSummary();
}

void MeshFaceControlWidget::onUpdateControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    QString err;
    if (!validateInput(err))
    {
        QMessageBox::warning(this, tr("面控制"), err);
        return;
    }

    const FaceControlItem item = collectFormData();
    m_items[row] = item;
    appendOrUpdateTableRow(row, item);
    refreshSummary();
}

void MeshFaceControlWidget::onDeleteControl()
{
    const int row = ui->tableControls->currentRow();
    if (row < 0 || row >= m_items.size())
    {
        return;
    }

    m_items.removeAt(row);
    ui->tableControls->removeRow(row);
    onResetForm();
    refreshSummary();
}

void MeshFaceControlWidget::onResetForm()
{
    ui->editFaceIds->clear();
    ui->spinMinSize->setValue(0.2);
    ui->spinMaxSize->setValue(1.0);
    ui->checkCurvature->setChecked(true);
    ui->spinCurvatureLevel->setValue(2);
    ui->tableControls->clearSelection();
    refreshButtons();
    refreshSummary();
}

void MeshFaceControlWidget::onSelectionChanged()
{
    const int row = ui->tableControls->currentRow();
    if (row >= 0 && row < m_items.size())
    {
        fillFormData(m_items[row]);
    }

    refreshButtons();
}

void MeshFaceControlWidget::onParameterEdited()
{
    ui->spinCurvatureLevel->setEnabled(ui->checkCurvature->isChecked());
    refreshButtons();
    refreshSummary();
}

void MeshFaceControlWidget::onRefreshEntities()
{
    rebuildFaceCandidates();
    if (m_faceMap.isEmpty())
    {
        QMessageBox::information(this, tr("面控制"), tr("当前几何中未找到可用面实体。"));
    }
}

void MeshFaceControlWidget::onAppendSelectedEntity()
{
    const int id = ui->cmbEntity->currentData().toInt();
    if (id <= 0)
    {
        return;
    }

    appendEntityIdToEdit(id);
}

void MeshFaceControlWidget::onHighlightSelectedEntity()
{
    clearHighlight();

    const int id = ui->cmbEntity->currentData().toInt();
    if (!m_faceMap.contains(id))
    {
        return;
    }

    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!visual)
    {
        return;
    }

    auto actor = buildFaceHighlightActor(m_faceMap.value(id));
    visual->addProp(m_highlightPropId, actor, "MeshControl", tr("面高亮 #%1").arg(id), false, true);
}

void MeshFaceControlWidget::setupUiState()
{
    ui->tableControls->setColumnCount(5);
    ui->tableControls->setHorizontalHeaderLabels({tr("面 ID"), tr("最小尺寸"), tr("最大尺寸"), tr("曲率加密"), tr("加密等级")});
    ui->tableControls->horizontalHeader()->setStretchLastSection(true);
    ui->tableControls->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableControls->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableControls->setEditTriggers(QAbstractItemView::NoEditTriggers);

    onResetForm();
    rebuildFaceCandidates();
}

bool MeshFaceControlWidget::validateInput(QString &err) const
{
    if (ui->editFaceIds->text().trimmed().isEmpty())
    {
        err = tr("请先输入或选择面 ID。");
        return false;
    }

    if (ui->spinMinSize->value() > ui->spinMaxSize->value())
    {
        err = tr("最小尺寸不能大于最大尺寸。");
        return false;
    }

    return true;
}

MeshFaceControlWidget::FaceControlItem MeshFaceControlWidget::collectFormData() const
{
    FaceControlItem item;
    item.faceIds = ui->editFaceIds->text().trimmed();
    item.minSize = ui->spinMinSize->value();
    item.maxSize = ui->spinMaxSize->value();
    item.curvatureRefine = ui->checkCurvature->isChecked();
    item.curvatureLevel = ui->spinCurvatureLevel->value();
    return item;
}

void MeshFaceControlWidget::fillFormData(const FaceControlItem &item)
{
    ui->editFaceIds->setText(item.faceIds);
    ui->spinMinSize->setValue(item.minSize);
    ui->spinMaxSize->setValue(item.maxSize);
    ui->checkCurvature->setChecked(item.curvatureRefine);
    ui->spinCurvatureLevel->setValue(item.curvatureLevel);
}

void MeshFaceControlWidget::appendOrUpdateTableRow(int row, const FaceControlItem &item)
{
    if (row < ui->tableControls->rowCount())
    {
        ui->tableControls->item(row, 0)->setText(item.faceIds);
        ui->tableControls->item(row, 1)->setText(QString::number(item.minSize, 'f', 3));
        ui->tableControls->item(row, 2)->setText(QString::number(item.maxSize, 'f', 3));
        ui->tableControls->item(row, 3)->setText(item.curvatureRefine ? tr("启用") : tr("关闭"));
        ui->tableControls->item(row, 4)->setText(item.curvatureRefine ? QString::number(item.curvatureLevel) : "-");
        return;
    }

    const int newRow = ui->tableControls->rowCount();
    ui->tableControls->insertRow(newRow);
    ui->tableControls->setItem(newRow, 0, new QTableWidgetItem(item.faceIds));
    ui->tableControls->setItem(newRow, 1, new QTableWidgetItem(QString::number(item.minSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 2, new QTableWidgetItem(QString::number(item.maxSize, 'f', 3)));
    ui->tableControls->setItem(newRow, 3, new QTableWidgetItem(item.curvatureRefine ? tr("启用") : tr("关闭")));
    ui->tableControls->setItem(newRow, 4, new QTableWidgetItem(item.curvatureRefine ? QString::number(item.curvatureLevel) : "-"));
}

void MeshFaceControlWidget::refreshButtons()
{
    const bool hasSelection = ui->tableControls->currentRow() >= 0;
    const bool hasInput = !ui->editFaceIds->text().trimmed().isEmpty();

    ui->btnAdd->setEnabled(hasInput);
    ui->btnUpdate->setEnabled(hasSelection && hasInput);
    ui->btnDelete->setEnabled(hasSelection);
}

void MeshFaceControlWidget::refreshSummary()
{
    const int count = m_items.size();
    const QString modeHint = ui->checkCurvature->isChecked()
                                 ? tr("已开启曲率加密，可提升圆角/小特征处质量。")
                                 : tr("已关闭曲率加密，适合追求速度的粗网格。");

    ui->labelSummary->setText(tr("已配置 %1 条面控制规则，可选面实体 %2 个。%3")
                                  .arg(count)
                                  .arg(m_faceMap.size())
                                  .arg(modeHint));
}

void MeshFaceControlWidget::rebuildFaceCandidates()
{
    clearHighlight();
    m_faceMap.clear();
    ui->cmbEntity->clear();

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || mgr->isEmpty())
    {
        updateEntityUiState();
        refreshSummary();
        return;
    }

    auto map = mgr->getShapMap();
    if (map.isEmpty())
    {
        updateEntityUiState();
        refreshSummary();
        return;
    }

    GeomShapeObject* selectedObject = nullptr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (it.value() && it.value()->getShape())
        {
            selectedObject = it.value();
            m_activeGeomUuid = it.key();
            break;
        }
    }

    if (!selectedObject || !selectedObject->getShape())
    {
        updateEntityUiState();
        refreshSummary();
        return;
    }

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(*selectedObject->getShape(), TopAbs_FACE, faceMap);
    for (int i = 1; i <= faceMap.Extent(); ++i)
    {
        m_faceMap.insert(i, faceMap(i));
        ui->cmbEntity->addItem(tr("面 #%1").arg(i), i);
    }

    updateEntityUiState();
    refreshSummary();
}

void MeshFaceControlWidget::clearHighlight()
{
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        visual->removeProp(m_highlightPropId, true);
    }
}

void MeshFaceControlWidget::updateEntityUiState()
{
    const bool hasEntities = !m_faceMap.isEmpty();
    ui->cmbEntity->setEnabled(hasEntities);
    ui->btnAppendEntity->setEnabled(hasEntities);
    ui->btnHighlightEntity->setEnabled(hasEntities);
}

bool MeshFaceControlWidget::appendEntityIdToEdit(int id)
{
    const QString current = ui->editFaceIds->text().trimmed();
    QStringList parts = current.isEmpty() ? QStringList{} : current.split(',', Qt::SkipEmptyParts);

    for (QString& p : parts) p = p.trimmed();
    const QString idStr = QString::number(id);
    if (!parts.contains(idStr))
    {
        parts.append(idStr);
        ui->editFaceIds->setText(parts.join(','));
    }
    return true;
}
