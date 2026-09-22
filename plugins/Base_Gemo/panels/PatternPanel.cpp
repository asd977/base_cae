#include "PatternPanel.h"
#include "ui_PatternDialog.h"
#include "../workbench/GeometryWorkbench.h"

#include <GeomManagerInstance.h>
#include <GeometryDisplayManager.h>
#include <GeometryPickManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include <vtkActor.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>

#include <GeometryServiceAccess.h>
#include <OccToVtkConverter.h>
#include <GeometryDisplayStyle.h>
#include "../workbench/GeometrySelectionVisuals.h"

namespace
{
bool discardUnappliedGeometryNode(const QString& uuid)
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    auto* workbench = dynamic_cast<GeometryWorkbench*>(pre);
    return workbench && workbench->discardUnappliedGeometryNode(uuid);
}

const QString kPatternPickDomain = QStringLiteral("GeometryPatternSource");
constexpr double kDirectionEps = 1.0e-9;
constexpr double kPatternPi = 3.14159265358979323846;
constexpr qint64 kPreviewCopyLimit = 180;

enum DirectionMode
{
    DirectionX = 0,
    DirectionY = 1,
    DirectionZ = 2,
    DirectionCustom = 3
};

enum PatternMode
{
    PatternLinear = 0,
    PatternPolar = 1
};

QString previewFaceId(const QString& uuid)
{
    return uuid + QStringLiteral("_feature_preview_pattern_face");
}

QString previewEdgeId(const QString& uuid)
{
    return uuid + QStringLiteral("_feature_preview_pattern_edge");
}

QString patternSourceDisplayName(const QString& uuid, GeomShapeObject* obj)
{
    const QString baseName = (!obj || obj->getName().isEmpty()) ? uuid : obj->getName();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (node && node->data(0, ProjectNode::RoleDimmed).toBool())
        return QObject::tr("%1（已隐藏）").arg(baseName);
    return baseName;
}

void renderView()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

bool usableSource(const QString& uuid)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    return obj && obj->getShape() && !obj->getShape()->IsNull();
}

bool geometryObjectExists(const QString& uuid)
{
    return usableSource(uuid);
}

bool sourceShape(const QString& uuid, TopoDS_Shape& shape)
{
    shape.Nullify();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;
    shape = *obj->getShape();
    return !shape.IsNull();
}

PatternDirection legacyDirection(const QJsonObject& obj, int index)
{
    PatternDirection result;
    if (index == 0)
    {
        result.count = obj.value(QStringLiteral("countX")).toInt(
            obj.value(QStringLiteral("count")).toInt(4));
        result.step = QVector3D(float(obj.value(QStringLiteral("dx")).toDouble(10.0)), 0.0f, 0.0f);
    }
    else if (index == 1)
    {
        result.count = obj.value(QStringLiteral("countY")).toInt(1);
        result.step = QVector3D(0.0f, float(obj.value(QStringLiteral("dy")).toDouble(0.0)), 0.0f);
    }
    else
    {
        result.count = obj.value(QStringLiteral("countZ")).toInt(1);
        result.step = QVector3D(0.0f, 0.0f, float(obj.value(QStringLiteral("dz")).toDouble(0.0)));
    }
    return result;
}

PatternDirection jsonDirection(const QJsonObject& obj, int index)
{
    const QString prefix = QStringLiteral("direction%1").arg(index + 1);
    if (!obj.contains(prefix + QStringLiteral("Count")))
        return legacyDirection(obj, index);

    PatternDirection result;
    result.count = obj.value(prefix + QStringLiteral("Count")).toInt(1);
    result.step = QVector3D(
        float(obj.value(prefix + QStringLiteral("X")).toDouble()),
        float(obj.value(prefix + QStringLiteral("Y")).toDouble()),
        float(obj.value(prefix + QStringLiteral("Z")).toDouble()));
    return result;
}

void clearPatternPreview(const QString& uuid, bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(previewFaceId(uuid), false);
        vis->removeProp(previewEdgeId(uuid), renderNow);
    }
}

bool showPatternPreview(const QString& uuid, const TopoDS_Shape& shape)
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || shape.IsNull())
        return false;

    clearPatternPreview(uuid, false);
    const OCC::DisplayPolyData display =
        OCC::BuildDisplayPolyData(shape, 0.9, 38.0, true, true);
    bool added = false;

    if (display.surface && display.surface->GetNumberOfCells() > 0)
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(display.surface);
        mapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configurePreviewSurfaceMapper(mapper);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        GeometryDisplayStyle::applyPreviewSurface(actor->GetProperty(), 0.24);
        const QString id = previewFaceId(uuid);
        vis->addProp(id, actor, QStringLiteral("Geometry"), id, false, false);
        added = true;
    }

    if (display.edges && display.edges->GetNumberOfCells() > 0)
    {
        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputData(display.edges);
        mapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configureCoincidentEdgeMapper(mapper, -16.0);
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->PickableOff();
        GeometryDisplayStyle::applyPreviewEdge(actor->GetProperty(), 1.25);
        const QString id = previewEdgeId(uuid);
        vis->addProp(id, actor, QStringLiteral("Geometry"), id, false, false);
        added = true;
    }

    vis->render();
    return added;
}

bool buildPatternPreviewShape(const TopoDS_Shape& source,
                              const PatternDirection directions[3],
                              TopoDS_Shape& result)
{
    result.Nullify();
    if (source.IsNull())
        return false;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    qint64 added = 0;
    for (int i1 = 0; i1 < directions[0].count && added < kPreviewCopyLimit; ++i1)
    {
        for (int i2 = 0; i2 < directions[1].count && added < kPreviewCopyLimit; ++i2)
        {
            for (int i3 = 0; i3 < directions[2].count && added < kPreviewCopyLimit; ++i3)
            {
                // 原对象已经在视图中显示并承担来源高亮；预览只画新增副本，避免颜色叠加。
                if (i1 == 0 && i2 == 0 && i3 == 0)
                    continue;

                const QVector3D translation =
                    directions[0].step * float(i1) +
                    directions[1].step * float(i2) +
                    directions[2].step * float(i3);

                TopoDS_Shape item = source;
                if (translation.lengthSquared() > float(kDirectionEps * kDirectionEps))
                {
                    gp_Trsf transform;
                    transform.SetTranslation(gp_Vec(double(translation.x()),
                                                     double(translation.y()),
                                                     double(translation.z())));
                    BRepBuilderAPI_Transform transformer(source, transform, Standard_False);
                    transformer.Build();
                    if (!transformer.IsDone() || transformer.Shape().IsNull())
                        continue;
                    item = transformer.Shape();
                }

                builder.Add(compound, item);
                ++added;
            }
        }
    }

    if (added == 0 || compound.IsNull())
        return false;
    result = compound;
    return true;
}

bool buildPolarPatternPreviewShape(const TopoDS_Shape& source,
                                   const PolarPatternParameters& parameters,
                                   TopoDS_Shape& result)
{
    result.Nullify();
    if (source.IsNull() || parameters.count < 2)
        return false;

    const QVector3D axisVector = parameters.axisDirection;
    if (axisVector.lengthSquared() <= float(kDirectionEps * kDirectionEps))
        return false;

    const double absoluteAngle = std::abs(parameters.totalAngleDeg);
    const bool fullCircle = std::abs(absoluteAngle - 360.0) <= 1.0e-7;
    const double intervals = fullCircle ? double(parameters.count)
                                        : double(parameters.count - 1);
    if (intervals <= 0.0)
        return false;

    const gp_Ax1 axis(
        gp_Pnt(double(parameters.axisOrigin.x()),
               double(parameters.axisOrigin.y()),
               double(parameters.axisOrigin.z())),
        gp_Dir(double(axisVector.x()),
               double(axisVector.y()),
               double(axisVector.z())));
    const double stepAngleRad = parameters.totalAngleDeg * kPatternPi / 180.0 / intervals;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    qint64 added = 0;
    const int previewCount = std::min<int>(parameters.count, int(kPreviewCopyLimit + 1));
    for (int index = 1; index < previewCount; ++index)
    {
        gp_Trsf transform;
        transform.SetRotation(axis, stepAngleRad * double(index));
        BRepBuilderAPI_Transform transformer(source, transform, Standard_False);
        transformer.Build();
        if (!transformer.IsDone() || transformer.Shape().IsNull())
            continue;

        builder.Add(compound, transformer.Shape());
        ++added;
    }

    if (added == 0 || compound.IsNull())
        return false;
    result = compound;
    return true;
}
} // namespace

PatternDialog::PatternDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomPattern),
      ui(new Ui::PatternDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    ui->patternTypeCombo->addItem(tr("线性阵列"), PatternLinear);
    ui->patternTypeCombo->addItem(tr("圆周阵列"), PatternPolar);

    for (int index = 0; index < 3; ++index)
    {
        QComboBox* combo = axisCombo(index);
        combo->addItem(tr("X 轴"), DirectionX);
        combo->addItem(tr("Y 轴"), DirectionY);
        combo->addItem(tr("Z 轴"), DirectionZ);
        combo->addItem(tr("自定义"), DirectionCustom);
        combo->setCurrentIndex(index);

        connect(directionGroup(index), &QGroupBox::toggled, this,
                [this](bool) { updatePreview(); });
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, index](int)
                {
                    updateDirectionUi(index);
                    updatePreview();
                });
        connect(countSpin(index), QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int) { updatePreview(); });
        connect(spacingSpin(index), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double) { updatePreview(); });
        for (int component = 0; component < 3; ++component)
        {
            connect(stepSpin(index, component),
                    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this](double) { updatePreview(); });
        }
        connect(reverseButton(index), &QPushButton::clicked, this,
                [this, index] { reverseDirection(index); });
        updateDirectionUi(index);
    }

    ui->polarAxisCombo->addItem(tr("X 轴"), DirectionX);
    ui->polarAxisCombo->addItem(tr("Y 轴"), DirectionY);
    ui->polarAxisCombo->addItem(tr("Z 轴"), DirectionZ);
    ui->polarAxisCombo->addItem(tr("自定义"), DirectionCustom);
    ui->polarAxisCombo->setCurrentIndex(DirectionZ);

    connect(ui->applyButton, &QPushButton::clicked, this, &PatternDialog::onApplyClicked);
    connect(ui->sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                syncSourceHighlight();
                updatePreview();
            });
    connect(ui->patternTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                updatePatternModeUi();
                updatePreview();
            });
    connect(ui->polarAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                updatePolarAxisUi();
                updatePreview();
            });
    connect(ui->polarCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updatePreview(); });
    connect(ui->polarAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { updatePreview(); });
    connect(ui->polarReverseButton, &QPushButton::clicked,
            this, &PatternDialog::reversePolarDirection);
    for (int component = 0; component < 3; ++component)
    {
        connect(polarOriginSpin(component),
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { updatePreview(); });
        connect(polarAxisSpin(component),
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { updatePreview(); });
    }

    updatePatternModeUi();
    updatePolarAxisUi();
}

PatternDialog::~PatternDialog()
{
    delete ui;
}

void PatternDialog::beforeLoad()
{
    refreshSources();
    if (m_committedState.isEmpty())
        m_committedState = saveJson();
    updatePreview();
}

void PatternDialog::beforeUnload()
{
    GeometryPickManager::instance().clearSelection(kPatternPickDomain, getUuid());
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    clearPreview(true);
}

QJsonObject PatternDialog::saveJson()
{
    QJsonObject obj;
    obj[QStringLiteral("sourceUuid")] = ui->sourceCombo ? ui->sourceCombo->currentData().toString() : m_sourceUuid;
    obj[QStringLiteral("patternVersion")] = 4;
    obj[QStringLiteral("patternMode")] = isPolarMode() ? QStringLiteral("polar")
                                                       : QStringLiteral("linear");

    for (int i = 0; i < 3; ++i)
    {
        const PatternDirection direction = directionValue(i);
        const QString prefix = QStringLiteral("direction%1").arg(i + 1);
        obj[prefix + QStringLiteral("Count")] = direction.count;
        obj[prefix + QStringLiteral("X")] = double(direction.step.x());
        obj[prefix + QStringLiteral("Y")] = double(direction.step.y());
        obj[prefix + QStringLiteral("Z")] = double(direction.step.z());
    }

    const PolarPatternParameters polar = polarValue();
    obj[QStringLiteral("polarCount")] = polar.count;
    obj[QStringLiteral("polarAngleDeg")] = polar.totalAngleDeg;
    obj[QStringLiteral("polarAxisMode")] = ui->polarAxisCombo
                                                ? ui->polarAxisCombo->currentData().toInt()
                                                : DirectionZ;
    obj[QStringLiteral("polarOriginX")] = double(polar.axisOrigin.x());
    obj[QStringLiteral("polarOriginY")] = double(polar.axisOrigin.y());
    obj[QStringLiteral("polarOriginZ")] = double(polar.axisOrigin.z());
    obj[QStringLiteral("polarAxisX")] = double(polar.axisDirection.x());
    obj[QStringLiteral("polarAxisY")] = double(polar.axisDirection.y());
    obj[QStringLiteral("polarAxisZ")] = double(polar.axisDirection.z());
    return obj;
}

void PatternDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_sourceUuid = obj.value(QStringLiteral("sourceUuid")).toString();

    const QString modeText = obj.value(QStringLiteral("patternMode")).toString();
    const bool polarMode = modeText.compare(QStringLiteral("polar"), Qt::CaseInsensitive) == 0;
    if (ui->patternTypeCombo)
    {
        const QSignalBlocker blocker(ui->patternTypeCombo);
        const int index = ui->patternTypeCombo->findData(polarMode ? PatternPolar : PatternLinear);
        ui->patternTypeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    for (int i = 0; i < 3; ++i)
        setDirectionValue(i, jsonDirection(obj, i));

    PolarPatternParameters polar;
    polar.count = obj.value(QStringLiteral("polarCount")).toInt(8);
    polar.totalAngleDeg = obj.value(QStringLiteral("polarAngleDeg")).toDouble(360.0);
    polar.axisOrigin = QVector3D(
        float(obj.value(QStringLiteral("polarOriginX")).toDouble()),
        float(obj.value(QStringLiteral("polarOriginY")).toDouble()),
        float(obj.value(QStringLiteral("polarOriginZ")).toDouble()));
    polar.axisDirection = QVector3D(
        float(obj.value(QStringLiteral("polarAxisX")).toDouble()),
        float(obj.value(QStringLiteral("polarAxisY")).toDouble()),
        float(obj.value(QStringLiteral("polarAxisZ")).toDouble(1.0)));
    setPolarValue(polar, obj.value(QStringLiteral("polarAxisMode")).toInt(DirectionZ));

    updatePatternModeUi();
    refreshSources();
    m_committedState = saveJson();
    clearPreview(true);
}

void PatternDialog::setInitialSource(const QString& uuid)
{
    m_sourceUuid = uuid;
    m_committedState = QJsonObject();
    refreshSources();
}

void PatternDialog::refreshSources()
{
    if (!ui->sourceCombo)
        return;

    const QString oldUuid = ui->sourceCombo->currentData().toString().isEmpty()
                                ? m_sourceUuid
                                : ui->sourceCombo->currentData().toString();
    const QSignalBlocker blocker(ui->sourceCombo);
    ui->sourceCombo->clear();

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        updateStatus(tr("几何管理器未初始化。"), false);
        clearPreview(true);
        return;
    }

    const auto map = mgr->getShapMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        GeomShapeObject* obj = it.value();
        if (!obj || !obj->getShape() || obj->getShape()->IsNull() || it.key() == getUuid())
            continue;
        ui->sourceCombo->addItem(patternSourceDisplayName(it.key(), obj), it.key());
    }

    const int index = ui->sourceCombo->findData(oldUuid);
    if (index >= 0)
        ui->sourceCombo->setCurrentIndex(index);
    else if (!oldUuid.isEmpty())
    {
        ui->sourceCombo->insertItem(0, tr("来源已删除"), oldUuid);
        ui->sourceCombo->setCurrentIndex(0);
    }
    else if (ui->sourceCombo->count() > 0)
        ui->sourceCombo->setCurrentIndex(0);

    syncSourceHighlight();
    if (ui->sourceCombo->count() == 0)
    {
        updateStatus(QString(), true);
        clearPreview(true);
    }
    else
    {
        updateStatus(QString(), true);
        updatePreview();
    }
}

bool PatternDialog::validateDirections(PatternDirection directions[3], qint64* totalCount)
{
    qint64 total = 1;
    bool anyEnabled = false;
    for (int i = 0; i < 3; ++i)
    {
        directions[i] = directionValue(i);
        if (directionGroup(i) && directionGroup(i)->isChecked())
        {
            anyEnabled = true;
            if (directions[i].count < 2 ||
                directions[i].step.lengthSquared() <= float(kDirectionEps * kDirectionEps))
            {
                if (totalCount)
                    *totalCount = 0;
                return false;
            }
        }
        total *= directions[i].count;
    }

    if (!anyEnabled || total < 2 || total > 200000)
    {
        if (totalCount)
            *totalCount = total;
        return false;
    }

    if (totalCount)
        *totalCount = total;
    return true;
}

void PatternDialog::onApplyClicked()
{
    const QString sourceUuid = ui->sourceCombo ? ui->sourceCombo->currentData().toString() : QString();
    if (sourceUuid.isEmpty() || !usableSource(sourceUuid))
    {
        updateStatus(tr("请选择源对象。"), false);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("几何服务不可用。"), false);
        return;
    }

    bool ok = false;
    if (isPolarMode())
    {
        PolarPatternParameters parameters;
        if (!validatePolar(parameters))
        {
            updateStatus(tr("请设置有效的圆周阵列参数。"), false);
            return;
        }
        ok = service->recreatePolarPattern(m_uuid, getDisplayName(), sourceUuid, parameters);
    }
    else
    {
        PatternDirection directions[3];
        qint64 totalCount = 0;
        if (!validateDirections(directions, &totalCount))
        {
            updateStatus(totalCount > 200000 ? tr("阵列数量过多。")
                                             : tr("请设置有效的阵列方向。"), false);
            return;
        }
        ok = service->recreatePattern(m_uuid, getDisplayName(), sourceUuid,
                                      directions[0], directions[1], directions[2]);
    }

    if (ok)
    {
        m_sourceUuid = sourceUuid;
        m_committedState = saveJson();
        clearPreview(true);
        updateStatus(QString(), true);
    }
    else
    {
        updateStatus(tr("生成失败。"), false);
    }
}

void PatternDialog::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    const QJsonObject state = m_committedState;
    if (!state.isEmpty())
        loadJson(state);
    clearPreview(true);
    syncSourceHighlight();
    updateStatus(QString(), true);
}

void PatternDialog::updateStatus(const QString& text, bool ok)
{
    if (!ui->statusLabel)
        return;

    const QString trimmed = text.trimmed();
    ui->statusLabel->setVisible(!trimmed.isEmpty());
    ui->statusLabel->setText(trimmed);
    if (!trimmed.isEmpty())
    {
        ui->statusLabel->setStyleSheet(ok ? QStringLiteral("color: #167a36;")
                                        : QStringLiteral("color: #a33a00;"));
    }
}

void PatternDialog::syncSourceHighlight()
{
    GeometryPickManager::instance().clearSelection(kPatternPickDomain, getUuid());
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);

    const QString uuid = ui->sourceCombo ? ui->sourceCombo->currentData().toString() : QString();
    if (usableSource(uuid) && GeometryDisplayManager::isVisible(uuid))
        GeometrySelectionVisuals::applySourcePreviewHighlight(uuid);
    renderView();
}

void PatternDialog::reverseDirection(int directionIndex)
{
    if (directionIndex < 0 || directionIndex >= 3)
        return;

    const int mode = axisCombo(directionIndex)
                         ? axisCombo(directionIndex)->currentData().toInt()
                         : DirectionX;
    if (mode == DirectionCustom)
    {
        for (int component = 0; component < 3; ++component)
        {
            if (stepSpin(directionIndex, component))
            {
                const QSignalBlocker blocker(stepSpin(directionIndex, component));
                stepSpin(directionIndex, component)->setValue(
                    -stepSpin(directionIndex, component)->value());
            }
        }
    }
    else if (spacingSpin(directionIndex))
    {
        const QSignalBlocker blocker(spacingSpin(directionIndex));
        spacingSpin(directionIndex)->setValue(-spacingSpin(directionIndex)->value());
    }
    updatePreview();
}

void PatternDialog::reversePolarDirection()
{
    if (!ui->polarAngleSpin)
        return;
    const QSignalBlocker blocker(ui->polarAngleSpin);
    ui->polarAngleSpin->setValue(-ui->polarAngleSpin->value());
    updatePreview();
}

bool PatternDialog::isPolarMode() const
{
    return ui->patternTypeCombo &&
           ui->patternTypeCombo->currentData().toInt() == PatternPolar;
}

void PatternDialog::updatePatternModeUi()
{
    const bool polar = isPolarMode();
    if (ui->linearWidget)
        ui->linearWidget->setVisible(!polar);
    if (ui->polarGroup)
        ui->polarGroup->setVisible(polar);
}

void PatternDialog::updatePolarAxisUi()
{
    const int mode = ui->polarAxisCombo
                         ? ui->polarAxisCombo->currentData().toInt()
                         : DirectionZ;
    auto* form = ui->polarGroup ? qobject_cast<QFormLayout*>(ui->polarGroup->layout()) : nullptr;
    if (form && ui->polarDirectionRow)
        form->setRowVisible(ui->polarDirectionRow, mode == DirectionCustom);
    else if (ui->polarDirectionRow)
        ui->polarDirectionRow->setVisible(mode == DirectionCustom);
}

void PatternDialog::updateDirectionUi(int directionIndex)
{
    if (directionIndex < 0 || directionIndex >= 3 || !directionGroup(directionIndex))
        return;

    const int mode = axisCombo(directionIndex)
                         ? axisCombo(directionIndex)->currentData().toInt()
                         : DirectionX;
    auto* form = qobject_cast<QFormLayout*>(directionGroup(directionIndex)->layout());
    if (!form)
        return;

    form->setRowVisible(spacingSpin(directionIndex)->parentWidget(), mode != DirectionCustom);
    form->setRowVisible(customRow(directionIndex), mode == DirectionCustom);
}

void PatternDialog::updatePreview()
{
    const QString uuid = ui->sourceCombo ? ui->sourceCombo->currentData().toString() : QString();
    TopoDS_Shape source;
    if (!sourceShape(uuid, source))
    {
        clearPreview(true);
        if (ui->applyButton)
            ui->applyButton->setEnabled(false);
        updateStatus(uuid.isEmpty() ? tr("请选择来源对象。") : tr("来源已删除。"), false);
        return;
    }

    bool valid = false;
    qint64 totalCount = 0;
    if (isPolarMode())
    {
        PolarPatternParameters parameters;
        valid = validatePolar(parameters);
        totalCount = valid ? parameters.count : 0;
    }
    else
    {
        PatternDirection directions[3];
        valid = validateDirections(directions, &totalCount);
    }

    if (ui->applyButton)
        ui->applyButton->setEnabled(valid);

    // 阵列可能生成大量实例，不在参数变化时构建完整 OCC/VTK 结果。
    // 只保留来源和方向提示，真正几何仅在点击“应用”时生成。
    clearPreview(true);
    if (!valid)
    {
        updateStatus(totalCount > 200000 ? tr("阵列数量过多。")
                                         : tr("请检查数量、间距、角度和方向设置。"), false);
        return;
    }

    updateStatus(totalCount > 1
                     ? tr("将生成 %1 个实例；点击“应用”后执行阵列。").arg(totalCount)
                     : tr("至少需要生成两个实例。"),
                 totalCount > 1);
}

void PatternDialog::clearPreview(bool renderNow)
{
    clearPatternPreview(getUuid(), renderNow);
}

PatternDirection PatternDialog::directionValue(int directionIndex) const
{
    PatternDirection result;
    if (directionIndex < 0 || directionIndex >= 3)
        return result;

    const bool enabled = directionGroup(directionIndex) && directionGroup(directionIndex)->isChecked();
    result.count = enabled && countSpin(directionIndex)
                       ? countSpin(directionIndex)->value()
                       : 1;

    const int mode = axisCombo(directionIndex)
                         ? axisCombo(directionIndex)->currentData().toInt()
                         : DirectionX;
    if (mode == DirectionCustom)
    {
        result.step = QVector3D(
            stepSpin(directionIndex, 0) ? float(stepSpin(directionIndex, 0)->value()) : 0.0f,
            stepSpin(directionIndex, 1) ? float(stepSpin(directionIndex, 1)->value()) : 0.0f,
            stepSpin(directionIndex, 2) ? float(stepSpin(directionIndex, 2)->value()) : 0.0f);
    }
    else
    {
        const float spacing = spacingSpin(directionIndex)
                                  ? float(spacingSpin(directionIndex)->value())
                                  : 0.0f;
        if (mode == DirectionY)
            result.step = QVector3D(0.0f, spacing, 0.0f);
        else if (mode == DirectionZ)
            result.step = QVector3D(0.0f, 0.0f, spacing);
        else
            result.step = QVector3D(spacing, 0.0f, 0.0f);
    }
    return result;
}

void PatternDialog::setDirectionValue(int directionIndex, const PatternDirection& direction)
{
    if (directionIndex < 0 || directionIndex >= 3)
        return;

    const QSignalBlocker groupBlocker(directionGroup(directionIndex));
    const QSignalBlocker axisBlocker(axisCombo(directionIndex));
    const QSignalBlocker countBlocker(countSpin(directionIndex));
    const QSignalBlocker spacingBlocker(spacingSpin(directionIndex));

    if (directionGroup(directionIndex))
        directionGroup(directionIndex)->setChecked(direction.count > 1);
    if (countSpin(directionIndex))
        countSpin(directionIndex)->setValue(std::max(2, direction.count));

    const double x = direction.step.x();
    const double y = direction.step.y();
    const double z = direction.step.z();
    const double eps = 1.0e-8;
    int mode = DirectionCustom;
    double spacing = 0.0;
    if (std::abs(y) <= eps && std::abs(z) <= eps)
    {
        mode = DirectionX;
        spacing = x;
    }
    else if (std::abs(x) <= eps && std::abs(z) <= eps)
    {
        mode = DirectionY;
        spacing = y;
    }
    else if (std::abs(x) <= eps && std::abs(y) <= eps)
    {
        mode = DirectionZ;
        spacing = z;
    }

    if (axisCombo(directionIndex))
    {
        const int comboIndex = axisCombo(directionIndex)->findData(mode);
        axisCombo(directionIndex)->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    }
    if (spacingSpin(directionIndex))
        spacingSpin(directionIndex)->setValue(spacing);

    const double values[3] = {x, y, z};
    for (int component = 0; component < 3; ++component)
    {
        if (stepSpin(directionIndex, component))
        {
            const QSignalBlocker blocker(stepSpin(directionIndex, component));
            stepSpin(directionIndex, component)->setValue(values[component]);
        }
    }
    updateDirectionUi(directionIndex);
}

bool PatternDialog::validatePolar(PolarPatternParameters& parameters) const
{
    parameters = polarValue();
    return parameters.count >= 2 &&
           parameters.count <= 200000 &&
           std::isfinite(parameters.totalAngleDeg) &&
           std::abs(parameters.totalAngleDeg) > kDirectionEps &&
           std::abs(parameters.totalAngleDeg) <= 360.0 + 1.0e-7 &&
           parameters.axisDirection.lengthSquared() > float(kDirectionEps * kDirectionEps);
}

PolarPatternParameters PatternDialog::polarValue() const
{
    PolarPatternParameters result;
    result.count = ui->polarCountSpin ? ui->polarCountSpin->value() : 4;
    result.totalAngleDeg = ui->polarAngleSpin ? ui->polarAngleSpin->value() : 360.0;
    result.axisOrigin = QVector3D(
        polarOriginSpin(0) ? float(polarOriginSpin(0)->value()) : 0.0f,
        polarOriginSpin(1) ? float(polarOriginSpin(1)->value()) : 0.0f,
        polarOriginSpin(2) ? float(polarOriginSpin(2)->value()) : 0.0f);

    const int mode = ui->polarAxisCombo
                         ? ui->polarAxisCombo->currentData().toInt()
                         : DirectionZ;
    if (mode == DirectionX)
        result.axisDirection = QVector3D(1.0f, 0.0f, 0.0f);
    else if (mode == DirectionY)
        result.axisDirection = QVector3D(0.0f, 1.0f, 0.0f);
    else if (mode == DirectionZ)
        result.axisDirection = QVector3D(0.0f, 0.0f, 1.0f);
    else
    {
        result.axisDirection = QVector3D(
            polarAxisSpin(0) ? float(polarAxisSpin(0)->value()) : 0.0f,
            polarAxisSpin(1) ? float(polarAxisSpin(1)->value()) : 0.0f,
            polarAxisSpin(2) ? float(polarAxisSpin(2)->value()) : 1.0f);
    }
    return result;
}

void PatternDialog::setPolarValue(const PolarPatternParameters& parameters, int axisMode)
{
    if (ui->polarCountSpin)
    {
        const QSignalBlocker blocker(ui->polarCountSpin);
        ui->polarCountSpin->setValue(std::max(2, parameters.count));
    }
    if (ui->polarAngleSpin)
    {
        const QSignalBlocker blocker(ui->polarAngleSpin);
        ui->polarAngleSpin->setValue(parameters.totalAngleDeg);
    }
    if (ui->polarAxisCombo)
    {
        const QSignalBlocker blocker(ui->polarAxisCombo);
        const int index = ui->polarAxisCombo->findData(axisMode);
        ui->polarAxisCombo->setCurrentIndex(index >= 0 ? index : DirectionZ);
    }

    const double originValues[3] = {
        parameters.axisOrigin.x(), parameters.axisOrigin.y(), parameters.axisOrigin.z()};
    const double axisValues[3] = {
        parameters.axisDirection.x(), parameters.axisDirection.y(), parameters.axisDirection.z()};
    for (int component = 0; component < 3; ++component)
    {
        if (polarOriginSpin(component))
        {
            const QSignalBlocker blocker(polarOriginSpin(component));
            polarOriginSpin(component)->setValue(originValues[component]);
        }
        if (polarAxisSpin(component))
        {
            const QSignalBlocker blocker(polarAxisSpin(component));
            polarAxisSpin(component)->setValue(axisValues[component]);
        }
    }
    updatePolarAxisUi();
}


QGroupBox* PatternDialog::directionGroup(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1Group;
    case 1: return ui->direction2Group;
    case 2: return ui->direction3Group;
    default: return nullptr;
    }
}

QComboBox* PatternDialog::axisCombo(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1AxisCombo;
    case 1: return ui->direction2AxisCombo;
    case 2: return ui->direction3AxisCombo;
    default: return nullptr;
    }
}

QSpinBox* PatternDialog::countSpin(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1CountSpin;
    case 1: return ui->direction2CountSpin;
    case 2: return ui->direction3CountSpin;
    default: return nullptr;
    }
}

QDoubleSpinBox* PatternDialog::spacingSpin(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1SpacingSpin;
    case 1: return ui->direction2SpacingSpin;
    case 2: return ui->direction3SpacingSpin;
    default: return nullptr;
    }
}

QDoubleSpinBox* PatternDialog::stepSpin(int directionIndex, int component) const
{
    QDoubleSpinBox* values[3][3] = {
        {ui->direction1XSpin, ui->direction1YSpin, ui->direction1ZSpin},
        {ui->direction2XSpin, ui->direction2YSpin, ui->direction2ZSpin},
        {ui->direction3XSpin, ui->direction3YSpin, ui->direction3ZSpin}};
    if (directionIndex < 0 || directionIndex >= 3 || component < 0 || component >= 3)
        return nullptr;
    return values[directionIndex][component];
}

QWidget* PatternDialog::customRow(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1CustomRow;
    case 1: return ui->direction2CustomRow;
    case 2: return ui->direction3CustomRow;
    default: return nullptr;
    }
}

QPushButton* PatternDialog::reverseButton(int index) const
{
    switch (index)
    {
    case 0: return ui->direction1ReverseButton;
    case 1: return ui->direction2ReverseButton;
    case 2: return ui->direction3ReverseButton;
    default: return nullptr;
    }
}

QDoubleSpinBox* PatternDialog::polarOriginSpin(int component) const
{
    switch (component)
    {
    case 0: return ui->polarOriginXSpin;
    case 1: return ui->polarOriginYSpin;
    case 2: return ui->polarOriginZSpin;
    default: return nullptr;
    }
}

QDoubleSpinBox* PatternDialog::polarAxisSpin(int component) const
{
    switch (component)
    {
    case 0: return ui->polarAxisXSpin;
    case 1: return ui->polarAxisYSpin;
    case 2: return ui->polarAxisZSpin;
    default: return nullptr;
    }
}
