#include "ExtrudePanel.h"
#include "ui_ExtrudeDialog.h"

#include "../workbench/GeometrySelectionVisuals.h"
#include "FeatureBuilder.h"
#include "../workbench/GeometryViewportPickController.h"
#include "OccToVtkConverter.h"

#include <GeomManagerInstance.h>
#include <GeometryDisplayManager.h>
#include <GeometryDisplayStyle.h>
#include <GeometryPickManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QJsonArray>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QSet>
#include <QVector3D>

#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>

#include <vtkActor.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include <algorithm>
#include <cmath>

#include <GeometryServiceAccess.h>

namespace
{
const QString kExtrudePickDomain = QStringLiteral("GeometryExtrudeSource");

QString previewFaceId(const QString& uuid)
{
    return uuid + QStringLiteral("_extrude_preview_face");
}

QString previewEdgeId(const QString& uuid)
{
    return uuid + QStringLiteral("_extrude_preview_edge");
}

int subShapeCount(GeomShapeObject* obj, TopAbs_ShapeEnum type)
{
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return 0;

    TopTools_IndexedMapOfShape indexed;
    TopExp::MapShapes(*obj->getShape(), type, indexed);
    return indexed.Extent();
}

QString sourceTag(const QString& kind, GeomShapeObject* obj)
{
    if (kind.contains(QStringLiteral("sketch"), Qt::CaseInsensitive))
        return QObject::tr("草图");

    const int solidCount = subShapeCount(obj, TopAbs_SOLID);
    const int faceCount = subShapeCount(obj, TopAbs_FACE);
    const int wireCount = subShapeCount(obj, TopAbs_WIRE);
    if (solidCount == 0 && faceCount > 0)
        return QObject::tr("面");
    if (wireCount > 0)
        return QObject::tr("轮廓");
    return QObject::tr("来源");
}

QString profileDisplayName(const QString& uuid, GeomShapeObject* obj)
{
    const QString baseName = (!obj || obj->getName().isEmpty()) ? uuid : obj->getName();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node)
        return baseName;

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    const bool dimmed = node->data(0, ProjectNode::RoleDimmed).toBool();
    const QString tag = sourceTag(kind, obj);
    return dimmed ? QObject::tr("%1（%2，已隐藏）").arg(baseName, tag)
                  : QObject::tr("%1（%2）").arg(baseName, tag);
}

bool geometryObjectExists(const QString& uuid)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    return mgr && !uuid.isEmpty() && mgr->getGeomShapeObject(uuid) != nullptr;
}

bool isSketchProfileKind(const QString& kind)
{
    static const QSet<QString> allowedKinds = {
        QStringLiteral("Sketch2D"),
        QStringLiteral("Circle"),
        QStringLiteral("Ellipse"),
        QStringLiteral("Polygon"),
        QStringLiteral("Rectangle")
    };
    return allowedKinds.contains(kind);
}

bool isExtrudeProfileCandidate(const QString& uuid, GeomShapeObject* obj)
{
    if (uuid.isEmpty() || !obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    if (subShapeCount(obj, TopAbs_SOLID) > 0)
        return false;

    // 工程树中的普通候选先走轻量 Kind 判断，避免大装配进入图形区拾取时
    // 对每个对象都做 OCC 轮廓分析；树外对象再用 FeatureBuilder 兜底。
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (node)
        return isSketchProfileKind(node->data(0, ProjectNode::RoleKind).toString());

    int closedRegionCount = 0;
    int openEdgeCount = 0;
    return FeatureBuilder::analyzeExtrudeProfile(uuid, &closedRegionCount, nullptr, nullptr,
                                                  &openEdgeCount) &&
           (closedRegionCount > 0 || openEdgeCount > 0);
}


bool validFaceRef(const QString& uuid, int faceIndex, TopoDS_Face* faceOut = nullptr)
{
    if (uuid.isEmpty() || faceIndex <= 0)
        return false;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, faces);
    if (faceIndex > faces.Extent())
        return false;

    const TopoDS_Face face = TopoDS::Face(faces(faceIndex));
    if (face.IsNull())
        return false;
    if (faceOut)
        *faceOut = face;
    return true;
}

QString faceRefText(const QString& uuid, int faceIndex)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    const QString name = obj && !obj->getName().isEmpty() ? obj->getName() : uuid;
    return QObject::tr("%1 / 面 %2").arg(name).arg(faceIndex);
}

ExtrudeDirectionMode directionModeFromLegacy(int direction, bool& reverse)
{
    switch (direction)
    {
    case 0: return ExtrudeDirectionMode::XAxis;
    case 1: reverse = !reverse; return ExtrudeDirectionMode::XAxis;
    case 2: return ExtrudeDirectionMode::YAxis;
    case 3: reverse = !reverse; return ExtrudeDirectionMode::YAxis;
    case 5: reverse = !reverse; return ExtrudeDirectionMode::ZAxis;
    case -1: return ExtrudeDirectionMode::ProfileNormal;
    case 4:
    default: return ExtrudeDirectionMode::ZAxis;
    }
}

int legacyDirection(const ExtrudeParameters& parameters)
{
    switch (parameters.directionMode)
    {
    case ExtrudeDirectionMode::ProfileNormal:
        return -1;
    case ExtrudeDirectionMode::XAxis:
        return parameters.reverse ? 1 : 0;
    case ExtrudeDirectionMode::YAxis:
        return parameters.reverse ? 3 : 2;
    case ExtrudeDirectionMode::ZAxis:
        return parameters.reverse ? 5 : 4;
    case ExtrudeDirectionMode::Custom:
        return 6;
    }
    return 4;
}
} // namespace

ExtrudeDialog::ExtrudeDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomExtrude),
      ui(new Ui::ExtrudeDialog)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    ui->profileCombo->setMaxVisibleItems(18);

    ui->extentCombo->addItem(tr("单向"), int(ExtrudeExtentMode::OneSide));
    ui->extentCombo->addItem(tr("两侧"), int(ExtrudeExtentMode::TwoSides));
    ui->extentCombo->addItem(tr("对称（总长度）"), int(ExtrudeExtentMode::Symmetric));

    ui->directionCombo->addItem(tr("沿截面法向"), int(ExtrudeDirectionMode::ProfileNormal));
    ui->directionCombo->addItem(tr("X 轴"), int(ExtrudeDirectionMode::XAxis));
    ui->directionCombo->addItem(tr("Y 轴"), int(ExtrudeDirectionMode::YAxis));
    ui->directionCombo->addItem(tr("Z 轴"), int(ExtrudeDirectionMode::ZAxis));
    ui->directionCombo->addItem(tr("自定义矢量"), int(ExtrudeDirectionMode::Custom));

    connect(ui->applyButton, &QPushButton::clicked, this, &ExtrudeDialog::onApplyClicked);
    connect(ui->previewCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!checked)
        {
            if (m_previewTimer)
                m_previewTimer->stop();
            clearPreview(true);
            return;
        }
        if (m_previewTimer)
            m_previewTimer->start();
    });

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(150);
    connect(m_previewTimer, &QTimer::timeout, this, &ExtrudeDialog::updatePreview);

    const auto refresh = [this]
    {
        updateParameterVisibility();
        updateCurrentProfileInfo();
        syncProfileHighlight();
        m_previewTimer->start();
    };


    connect(ui->profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, refresh](int index)
            {
                if (index >= 0 && !m_selectedFaces.isEmpty())
                {
                    m_selectedFaces.clear();
                    syncSelectedFaceHighlight();
                }
                refresh();
            });
    connect(ui->extentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [refresh](int) { refresh(); });
    connect(ui->directionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [refresh](int) { refresh(); });
    connect(ui->distanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [refresh](double) { refresh(); });
    connect(ui->secondDistanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [refresh](double) { refresh(); });
    connect(ui->reverseCheck, &QCheckBox::toggled, this,
            [refresh](bool) { refresh(); });
    connect(ui->customXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [refresh](double) { refresh(); });
    connect(ui->customYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [refresh](double) { refresh(); });
    connect(ui->customZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [refresh](double) { refresh(); });

    updateParameterVisibility();
}

ExtrudeDialog::~ExtrudeDialog()
{
    clearPreview(false);
    delete ui;
}

void ExtrudeDialog::beforeLoad()
{
    if (m_committedState.isEmpty() && geometryObjectExists(getUuid()))
        m_committedState = currentState();
    refreshProfiles();
    updatePreview();
    QTimer::singleShot(0, this, [this] { startDirectFaceSelection(); });
}

void ExtrudeDialog::beforeUnload()
{
    if (m_previewTimer)
        m_previewTimer->stop();
    GeometryViewportPickController::stop(this, true);
    GeometryPickManager::instance().clearSelection(kExtrudePickDomain, getUuid());
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    clearPreview(true);
}

QJsonObject ExtrudeDialog::currentState() const
{
    const ExtrudeParameters p = parameters();
    QJsonObject obj;
    obj["extrudeSchema"] = 5;
    obj["profileUuid"] = currentProfileUuid().isEmpty() ? m_profileUuid : currentProfileUuid();
    QJsonArray faceSelections;
    for (const CadFaceRef& ref : m_selectedFaces)
    {
        QJsonObject item;
        item["uuid"] = ref.uuid;
        item["face"] = ref.faceIndex;
        faceSelections.append(item);
    }
    obj["faceSelections"] = faceSelections;
    obj["distance"] = p.distance;
    obj["secondDistance"] = p.secondDistance;
    // 老版本以“正距离 + 已解析正负方向”保存；保持这一约定可避免
    // height 与 direction 同时带符号时出现二次反向。
    obj["height"] = p.distance;
    obj["extentMode"] = int(p.extentMode);
    obj["resultMode"] = int(ExtrudeResultMode::Auto); // 兼容旧工程字段
    obj["directionMode"] = int(p.directionMode);
    obj["direction"] = legacyDirection(p); // 兼容旧恢复逻辑
    obj["reverse"] = p.reverse;
    obj["customX"] = p.customDirection.x();
    obj["customY"] = p.customDirection.y();
    obj["customZ"] = p.customDirection.z();
    obj["preview"] = true;
    return obj;
}

QJsonObject ExtrudeDialog::saveJson()
{
    // 已生成结果时，工程和依赖关系只保存最后一次“应用”的参数。
    // 当前编辑值仍可实时预览，但不会在未应用时覆盖正式模型状态。
    if (geometryObjectExists(getUuid()) && !m_committedState.isEmpty())
        return m_committedState;
    return currentState();
}

void ExtrudeDialog::loadJson(const QJsonObject& obj)
{
    m_committedState = QJsonObject();
    m_profileUuid = obj.value("profileUuid").toString();
    m_selectedFaces.clear();
    for (const QJsonValue& value : obj.value("faceSelections").toArray())
    {
        const QJsonObject item = value.toObject();
        CadFaceRef ref;
        ref.uuid = item.value("uuid").toString();
        ref.faceIndex = item.value("face").toInt(0);
        if (!ref.uuid.isEmpty() && ref.faceIndex > 0)
            m_selectedFaces.push_back(ref);
    }
    if (!m_selectedFaces.isEmpty())
        m_profileUuid.clear();

    const QSignalBlocker profileBlocker(ui->profileCombo);
    const QSignalBlocker extentBlocker(ui->extentCombo);
    const QSignalBlocker directionBlocker(ui->directionCombo);
    const QSignalBlocker distanceBlocker(ui->distanceSpin);
    const QSignalBlocker secondDistanceBlocker(ui->secondDistanceSpin);
    const QSignalBlocker reverseBlocker(ui->reverseCheck);
    const QSignalBlocker xBlocker(ui->customXSpin);
    const QSignalBlocker yBlocker(ui->customYSpin);
    const QSignalBlocker zBlocker(ui->customZSpin);

    const int schema = obj.value("extrudeSchema").toInt(0);
    const double distance = obj.contains("distance")
        ? obj.value("distance").toDouble(10.0)
        : std::abs(obj.value("height").toDouble(10.0));
    ui->distanceSpin->setValue(std::max(1.0e-6, distance));
    ui->secondDistanceSpin->setValue(std::max(1.0e-6,
        obj.value("secondDistance").toDouble(distance)));

    bool reverse = obj.value("reverse").toBool(obj.value("height").toDouble(10.0) < 0.0);
    ExtrudeDirectionMode directionMode = ExtrudeDirectionMode::ProfileNormal;
    if (schema >= 2)
    {
        directionMode = ExtrudeDirectionMode(obj.value("directionMode").toInt(
            int(ExtrudeDirectionMode::ProfileNormal)));
    }
    else if (obj.contains("directionMode"))
    {
        // 上一版同时保存了“用户选择的方向类型”和反向复选框，优先按
        // 这两个字段恢复，既保持实际方向，也保留“沿截面法向”的语义。
        directionMode = directionModeFromLegacy(obj.value("directionMode").toInt(-1), reverse);
    }
    else if (obj.contains("direction"))
    {
        // 更早版本只有轴向和高度，二者的符号共同决定最终方向。
        directionMode = directionModeFromLegacy(obj.value("direction").toInt(-1), reverse);
    }

    const int directionIndex = ui->directionCombo->findData(int(directionMode));
    ui->directionCombo->setCurrentIndex(directionIndex >= 0 ? directionIndex : 0);
    ui->reverseCheck->setChecked(reverse);

    const ExtrudeExtentMode extentMode = schema >= 2
        ? ExtrudeExtentMode(obj.value("extentMode").toInt(int(ExtrudeExtentMode::OneSide)))
        : ExtrudeExtentMode::OneSide;
    const int extentIndex = ui->extentCombo->findData(int(extentMode));
    ui->extentCombo->setCurrentIndex(extentIndex >= 0 ? extentIndex : 0);

    ui->customXSpin->setValue(obj.value("customX").toDouble(0.0));
    ui->customYSpin->setValue(obj.value("customY").toDouble(0.0));
    ui->customZSpin->setValue(obj.value("customZ").toDouble(1.0));

    updateParameterVisibility();
    refreshProfiles();
    m_committedState = currentState();
    clearPreview(true);
}

void ExtrudeDialog::setInitialProfile(const QString& uuid)
{
    m_selectedFaces.clear();
    m_profileUuid = uuid;
    m_committedState = QJsonObject();
    refreshProfiles();
    updatePreview();
}

QString ExtrudeDialog::currentProfileUuid() const
{
    if (!m_selectedFaces.isEmpty())
        return QString();
    return ui->profileCombo ? ui->profileCombo->currentData().toString() : QString();
}

bool ExtrudeDialog::currentProfileInfo(ProfileInfo& info) const
{
    const QString uuid = currentProfileUuid();
    if (uuid.isEmpty())
        return false;

    for (const ProfileInfo& item : m_profiles)
    {
        if (item.uuid == uuid)
        {
            info = item;
            return true;
        }
    }
    return false;
}

ExtrudeParameters ExtrudeDialog::parameters() const
{
    ExtrudeParameters p;
    p.distance = ui->distanceSpin ? ui->distanceSpin->value() : 10.0;
    p.secondDistance = ui->secondDistanceSpin ? ui->secondDistanceSpin->value() : p.distance;
    p.directionMode = ui->directionCombo
        ? ExtrudeDirectionMode(ui->directionCombo->currentData().toInt())
        : ExtrudeDirectionMode::ProfileNormal;
    p.extentMode = ui->extentCombo
        ? ExtrudeExtentMode(ui->extentCombo->currentData().toInt())
        : ExtrudeExtentMode::OneSide;
    p.resultMode = ExtrudeResultMode::Auto;
    p.reverse = p.extentMode != ExtrudeExtentMode::Symmetric &&
                ui->reverseCheck && ui->reverseCheck->isChecked();
    p.customDirection = QVector3D(float(ui->customXSpin ? ui->customXSpin->value() : 0.0),
                                  float(ui->customYSpin ? ui->customYSpin->value() : 0.0),
                                  float(ui->customZSpin ? ui->customZSpin->value() : 1.0));
    return p;
}

bool ExtrudeDialog::validateInputs(QString* message) const
{
    if (m_selectedFaces.isEmpty())
    {
        ProfileInfo info;
        if (!currentProfileInfo(info))
        {
            if (message)
            {
                *message = m_missingProfileUuid.isEmpty()
                    ? tr("请选择草图、轮廓或一个/多个几何面。")
                    : tr("原截面来源已删除，请重新选择。");
            }
            return false;
        }
        if (!info.valid)
        {
            if (message)
                *message = info.message;
            return false;
        }
        if (info.closedRegionCount <= 0 && info.openEdgeCount <= 0)
        {
            if (message)
                *message = tr("没有可拉伸的闭合区域或开放轮廓。");
            return false;
        }
    }
    else
    {
        for (const CadFaceRef& ref : m_selectedFaces)
        {
            if (!validFaceRef(ref.uuid, ref.faceIndex))
            {
                if (message)
                    *message = tr("所选面已失效，请重新选择。");
                return false;
            }
        }
    }

    const ExtrudeParameters p = parameters();
    if (p.distance <= 1.0e-9)
    {
        if (message)
            *message = tr("拉伸距离必须大于 0 mm。");
        return false;
    }
    if (p.extentMode == ExtrudeExtentMode::TwoSides && p.secondDistance <= 1.0e-9)
    {
        if (message)
            *message = tr("反向距离必须大于 0 mm。");
        return false;
    }
    if (p.directionMode == ExtrudeDirectionMode::Custom &&
        p.customDirection.lengthSquared() <= 1.0e-16f)
    {
        if (message)
            *message = tr("自定义方向矢量不能为零。");
        return false;
    }
    return true;
}

void ExtrudeDialog::refreshProfiles()
{
    if (!ui->profileCombo)
        return;

    GeometryViewportPickController::stop(this, true);

    const QString oldUuid = currentProfileUuid().isEmpty() ? m_profileUuid : currentProfileUuid();
    m_profiles.clear();

    const QSignalBlocker blocker(ui->profileCombo);
    ui->profileCombo->clear();
    ui->profileCombo->setMaxVisibleItems(18);

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        updateStatus(tr("当前无法读取几何对象，请重新打开拉伸功能后再试。"), false);
        clearPreview(true);
        return;
    }

    constexpr int kComboItemLimit = 160;
    constexpr int kComboScanLimit = 640;

    auto appendProfile = [&](const QString& uuid, GeomShapeObject* obj,
                             bool requireTreeKind) -> bool
    {
        if (uuid.isEmpty() || uuid == getUuid() || !obj || !obj->getShape() || obj->getShape()->IsNull())
            return false;
        if (subShapeCount(obj, TopAbs_SOLID) > 0)
            return false;

        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
        ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
        if (requireTreeKind && node &&
            !isSketchProfileKind(node->data(0, ProjectNode::RoleKind).toString()))
        {
            return false;
        }

        ProfileInfo info;
        info.uuid = uuid;
        info.displayName = profileDisplayName(uuid, obj);
        int suggestedDirection = 4;
        info.valid = FeatureBuilder::analyzeExtrudeProfile(uuid,
                                                            &info.closedRegionCount,
                                                            &suggestedDirection,
                                                            &info.message,
                                                            &info.openEdgeCount);
        if (info.closedRegionCount <= 0 && info.openEdgeCount <= 0)
            return false;

        m_profiles.append(info);
        const QString summary = tr("%1  ·  %2 闭合 / %3 开放轮廓")
                                    .arg(info.displayName)
                                    .arg(info.closedRegionCount)
                                    .arg(info.openEdgeCount);
        ui->profileCombo->addItem(summary, info.uuid);
        return true;
    };

    // 当前/工程中已保存的来源永远优先恢复；即使它不是常规草图 Kind，
    // 只要 FeatureBuilder 仍能识别，就不因为大型场景候选截断而丢失。
    if (!oldUuid.isEmpty())
        appendProfile(oldUuid, mgr->getGeomShapeObject(oldUuid), false);

    int inspected = 0;
    bool truncated = false;
    const auto map = mgr->getShapMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (it.key() == oldUuid)
            continue;
        if (ui->profileCombo->count() >= kComboItemLimit || inspected >= kComboScanLimit)
        {
            truncated = true;
            break;
        }
        ++inspected;
        appendProfile(it.key(), it.value(), true);
    }

    const int index = ui->profileCombo->findData(oldUuid);
    m_missingProfileUuid.clear();
    if (!m_selectedFaces.isEmpty())
    {
        ui->profileCombo->setCurrentIndex(-1);
    }
    else if (index >= 0)
    {
        ui->profileCombo->setCurrentIndex(index);
    }
    else if (!oldUuid.isEmpty())
    {
        m_missingProfileUuid = oldUuid;
        ui->profileCombo->insertItem(0, tr("来源已删除或已失效（请重新选择）"), oldUuid);
        ui->profileCombo->setCurrentIndex(0);
    }
    else
    {
        // New Extrude commands start with no implicit source.  The user can click a
        // planar face in the viewport immediately, or explicitly choose a sketch
        // from the list.  Never consume the first unrelated profile silently.
        ui->profileCombo->setCurrentIndex(-1);
    }

    ui->profileCombo->setToolTip(truncated
        ? tr("大型模型不会在列表中显示全部草图；也可以直接在模型中单击几何面。")
        : tr("可从列表选择草图/轮廓；模型中左键选择面，Ctrl + 左键追加多选。"));

    updateCurrentProfileInfo();
    syncProfileHighlight();
    updatePreview();
}

void ExtrudeDialog::updateCurrentProfileInfo()
{
    if (!m_selectedFaces.isEmpty())
    {
        QStringList names;
        const int shown = std::min(3, static_cast<int>(m_selectedFaces.size()));
        for (int i = 0; i < shown; ++i)
            names << faceRefText(m_selectedFaces.at(i).uuid, m_selectedFaces.at(i).faceIndex);
        QString text = tr("已选择 %1 个几何面；Ctrl + 左键可追加或取消选择。").arg(m_selectedFaces.size());
        if (!names.isEmpty())
            text += QStringLiteral("  ") + names.join(QStringLiteral("，"));
        if (m_selectedFaces.size() > shown)
            text += tr(" 等");
        ui->profileInfoLabel->setText(text);
        ui->profileInfoLabel->setStyleSheet(QStringLiteral("color:#5f6670;"));
        ui->profileInfoLabel->setVisible(true);
        return;
    }

    ProfileInfo info;
    if (!currentProfileInfo(info))
    {
        const QString text = m_missingProfileUuid.isEmpty()
            ? tr("请选择草图/轮廓，或在模型中左键选择面；Ctrl + 左键可追加多选。")
            : tr("原截面来源已删除；请重新选择草图/轮廓或几何面。");
        ui->profileInfoLabel->setText(text);
        ui->profileInfoLabel->setStyleSheet(QStringLiteral("color:#a33a00;"));
        ui->profileInfoLabel->setVisible(true);
        return;
    }

    QString text;
    if (info.closedRegionCount > 0 && info.openEdgeCount > 0)
    {
        text = tr("将同时拉伸 %1 个闭合区域和 %2 组开放轮廓：闭合区域生成实体，开放轮廓生成曲面。")
                   .arg(info.closedRegionCount).arg(info.openEdgeCount);
    }
    else if (info.closedRegionCount > 0)
    {
        text = tr("将拉伸 %1 个闭合区域并生成实体。")
                   .arg(info.closedRegionCount);
    }
    else
    {
        text = tr("将拉伸 %1 组开放轮廓并生成曲面。")
                   .arg(info.openEdgeCount);
    }

    ui->profileInfoLabel->setText(text);
    ui->profileInfoLabel->setStyleSheet(QStringLiteral("color:#5f6670;"));
    ui->profileInfoLabel->setVisible(!text.trimmed().isEmpty());
}

void ExtrudeDialog::updateParameterVisibility()
{
    const ExtrudeExtentMode extentMode = ui->extentCombo
        ? ExtrudeExtentMode(ui->extentCombo->currentData().toInt())
        : ExtrudeExtentMode::OneSide;
    const bool twoSides = extentMode == ExtrudeExtentMode::TwoSides;

    if (ui->distanceLabel)
    {
        if (extentMode == ExtrudeExtentMode::TwoSides)
            ui->distanceLabel->setText(tr("正向距离"));
        else if (extentMode == ExtrudeExtentMode::Symmetric)
            ui->distanceLabel->setText(tr("总长度"));
        else
            ui->distanceLabel->setText(tr("距离"));
    }
    if (ui->secondDistanceLabel)
        ui->secondDistanceLabel->setVisible(twoSides);
    if (ui->secondDistanceSpin)
        ui->secondDistanceSpin->setVisible(twoSides);

    // 对称拉伸在两个方向上的距离完全相同，反向不会改变结果，隐藏它可
    // 避免出现“勾选了但模型没变化”的无效交互。
    const bool reverseRelevant = extentMode != ExtrudeExtentMode::Symmetric;
    if (ui->reverseCheck)
    {
        ui->reverseCheck->setVisible(reverseRelevant);
        ui->reverseCheck->setEnabled(reverseRelevant);
    }

    const bool custom = ui->directionCombo &&
        ExtrudeDirectionMode(ui->directionCombo->currentData().toInt()) == ExtrudeDirectionMode::Custom;
    if (ui->customDirectionGroup)
        ui->customDirectionGroup->setVisible(custom);
}

void ExtrudeDialog::onApplyClicked()
{
    QString validationMessage;
    if (!validateInputs(&validationMessage))
    {
        updateStatus(validationMessage, false);
        clearPreview(true);
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
    {
        updateStatus(tr("当前无法执行拉伸，请重新打开此功能后再试。"), false);
        return;
    }

    const QString profileUuid = currentProfileUuid();
    const ExtrudeParameters p = parameters();
    const QJsonObject previousCommittedState = m_committedState;
    const QJsonObject pendingState = currentState();

    // recreate 成功后会立即刷新模型树并记录撤销历史，因此必须在调用
    // 服务前暂存本次参数，让历史快照拿到的是新状态而不是上一次状态。
    m_committedState = pendingState;
    if (service->recreateExtrude(getUuid(), getDisplayName(), profileUuid, m_selectedFaces, p))
    {
        m_profileUuid = profileUuid;
        m_missingProfileUuid.clear();
        clearPreview(true);
        updateStatus(tr("拉伸已应用。"), true);
        syncProfileHighlight();
    }
    else
    {
        m_committedState = previousCommittedState;
        TopoDS_Shape unused;
        QString buildMessage;
        FeatureBuilder::buildExtrudeShape(profileUuid, m_selectedFaces, p, unused, nullptr, &buildMessage);
        updateStatus(buildMessage.isEmpty() ? tr("拉伸生成失败。") : buildMessage, false);
    }
}

void ExtrudeDialog::updateStatus(const QString& text, bool ok)
{
    if (!ui->statusLabel)
        return;
    const QString trimmed = text.trimmed();
    ui->statusLabel->setText(trimmed);
    ui->statusLabel->setVisible(!trimmed.isEmpty());
    if (!trimmed.isEmpty())
    {
        ui->statusLabel->setStyleSheet(ok ? QStringLiteral("color:#167a36;")
                                        : QStringLiteral("color:#a33a00;"));
    }
}

void ExtrudeDialog::syncSelectedFaceHighlight()
{
    QVector<GeometryPickItem> items;
    items.reserve(m_selectedFaces.size());
    for (const CadFaceRef& ref : m_selectedFaces)
    {
        if (validFaceRef(ref.uuid, ref.faceIndex))
            items.push_back({ref.uuid, GeometryPickMode::Face, ref.faceIndex});
    }
    GeometryPickManager::instance().setSelection(kExtrudePickDomain, getUuid(), items);
}

void ExtrudeDialog::syncProfileHighlight()
{
    GeometryPickManager::instance().clearSelection(kExtrudePickDomain, getUuid());
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);

    if (!m_selectedFaces.isEmpty())
    {
        syncSelectedFaceHighlight();
    }
    else
    {
        const QString profileUuid = currentProfileUuid();
        if (!profileUuid.isEmpty() && GeometryDisplayManager::isVisible(profileUuid))
            GeometrySelectionVisuals::applySourcePreviewHighlight(profileUuid);
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void ExtrudeDialog::toggleSelectedFace(const QString& uuid, int faceIndex)
{
    if (!validFaceRef(uuid, faceIndex))
    {
        updateStatus(tr("请选择几何面。"), false);
        return;
    }

    int existing = -1;
    for (int i = 0; i < m_selectedFaces.size(); ++i)
    {
        if (m_selectedFaces.at(i).uuid == uuid && m_selectedFaces.at(i).faceIndex == faceIndex)
        {
            existing = i;
            break;
        }
    }
    // Standard CAD selection semantics:
    //   left click        -> replace the current face selection;
    //   Ctrl + left click -> add/remove one face from the current selection.
    // Keep the modifier decision here, at the feature callback, so the generic
    // viewport picker remains reusable by fillet/chamfer and other commands.
    const bool additive = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    if (additive)
    {
        if (existing >= 0)
            m_selectedFaces.removeAt(existing);
        else
            m_selectedFaces.push_back(CadFaceRef{uuid, faceIndex});
    }
    else
    {
        m_selectedFaces.clear();
        m_selectedFaces.push_back(CadFaceRef{uuid, faceIndex});
    }

    if (!m_selectedFaces.isEmpty())
    {
        m_profileUuid.clear();
        m_missingProfileUuid.clear();
        const QSignalBlocker blocker(ui->profileCombo);
        ui->profileCombo->setCurrentIndex(-1);
    }

    // A viewport click may also update the normal geometry/tree selection before
    // the feature pick callback runs.  That whole-object highlight must not be
    // stacked on top of the face-selection overlay; extrusion owns the visual
    // feedback while its direct face picker is active.
    GeometrySelectionVisuals::clearTreeSelectionHighlight(false);
    GeometryPickManager::instance().clearPreview(kExtrudePickDomain, getUuid());
    syncProfileHighlight();
    updateCurrentProfileInfo();
    if (m_previewTimer)
        m_previewTimer->start();
}

void ExtrudeDialog::startDirectFaceSelection()
{
    const bool started = GeometryViewportPickController::startTopologyScene(
        this, nullptr, GeometryPickMode::Face,
        []() -> QString { return QString(); },
        [this](const QString& uuid, int faceIndex)
        {
            toggleSelectedFace(uuid, faceIndex);
        },
        getUuid(), {}, {}, tr("直接选择一个或多个几何面"),
        [this](const QString& uuid, int faceIndex)
        {
            if (!uuid.isEmpty() && faceIndex > 0 && validFaceRef(uuid, faceIndex))
            {
                GeometryPickManager::instance().setPreview(
                    kExtrudePickDomain, getUuid(),
                    {uuid, GeometryPickMode::Face, faceIndex});
            }
            else
            {
                GeometryPickManager::instance().clearPreview(kExtrudePickDomain, getUuid());
            }
        },
        [this]()
        {
            GeometryPickManager::instance().clearPreview(kExtrudePickDomain, getUuid());
        });

    if (!started && m_selectedFaces.isEmpty() && currentProfileUuid().isEmpty())
        updateStatus(tr("当前没有可直接选择的几何面，也可以从来源列表选择草图或轮廓。"), true);
}

void ExtrudeDialog::updatePreview()
{
    QString validationMessage;
    const bool valid = validateInputs(&validationMessage);
    if (ui->applyButton)
        ui->applyButton->setEnabled(valid);
    if (!valid)
    {
        clearPreview(true);
        updateStatus(validationMessage, false);
        return;
    }

    updateStatus(QString(), true);

    // 结果预览默认关闭。拉伸页面一直保持面选择状态，若实时实体预览
    // 长时间覆盖在来源几何上，会明显干扰连续选面。用户显式勾选后才构建预览。
    if (!ui->previewCheck || !ui->previewCheck->isChecked())
    {
        clearPreview(true);
        return;
    }

    // 已应用参数与当前参数一致时直接显示正式结果，不叠加一层虚影。
    if (geometryObjectExists(getUuid()) && !m_committedState.isEmpty() &&
        currentState() == m_committedState)
    {
        clearPreview(true);
        return;
    }

    TopoDS_Shape previewShape;
    QString message;
    if (!FeatureBuilder::buildExtrudeShape(currentProfileUuid(), m_selectedFaces, parameters(),
                                            previewShape, nullptr, &message) ||
        previewShape.IsNull())
    {
        clearPreview(true);
        updateStatus(message, false);
        return;
    }

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis)
        return;

    vis->removeProp(previewFaceId(getUuid()), false);
    vis->removeProp(previewEdgeId(getUuid()), false);

    auto display = OCC::BuildDisplayPolyData(previewShape);

    if (display.surface && display.surface->GetNumberOfPoints() > 0)
    {
        vtkNew<vtkPolyDataMapper> faceMapper;
        faceMapper->SetInputData(display.surface);
        faceMapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configurePreviewSurfaceMapper(faceMapper);

        vtkSmartPointer<vtkActor> faceActor = vtkSmartPointer<vtkActor>::New();
        faceActor->SetMapper(faceMapper);
        faceActor->PickableOff();
        GeometryDisplayStyle::applyPreviewSurface(faceActor->GetProperty(), 0.28);
        vis->addProp(previewFaceId(getUuid()), faceActor, QStringLiteral("Geometry"),
                     previewFaceId(getUuid()), false, false);
    }

    if (display.edges && display.edges->GetNumberOfPoints() > 0)
    {
        vtkNew<vtkPolyDataMapper> edgeMapper;
        edgeMapper->SetInputData(display.edges);
        edgeMapper->ScalarVisibilityOff();
        GeometryDisplayStyle::configureCoincidentEdgeMapper(edgeMapper, -16.0);

        vtkSmartPointer<vtkActor> edgeActor = vtkSmartPointer<vtkActor>::New();
        edgeActor->SetMapper(edgeMapper);
        edgeActor->PickableOff();
        GeometryDisplayStyle::applyPreviewEdge(edgeActor->GetProperty(), 1.25);
        vis->addProp(previewEdgeId(getUuid()), edgeActor, QStringLiteral("Geometry"),
                     previewEdgeId(getUuid()), false, false);
    }
    vis->render();
}

void ExtrudeDialog::clearPreview(bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(previewFaceId(getUuid()), false);
        vis->removeProp(previewEdgeId(getUuid()), renderNow);
    }
}
