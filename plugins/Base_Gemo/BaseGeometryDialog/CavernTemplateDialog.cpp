#include "CavernTemplateDialog.h"

#include "../builders/BooleanBuilder.h"
#include "../builders/GeometryBuildUtils.h"
#include "../core/CadDocument.h"
#include "../core/CadBody.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <GeomManagerInstance.h>
#include <GeometryCleanup.h>
#include <GeometryServiceAccess.h>
#include <PublicConstants.h>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QSizePolicy>
#include <QPixmap>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <QVector>
#include <ServiceRegistry.h>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <services/PreProcessingService.h>
#include <GeometryWorkbench.h>
#include <GeometryDisplayManager.h>

namespace
{
constexpr double kEps = 1.0e-7;

void setScaledDiagram(QLabel* label, const QPixmap& pixmap)
{
    if (!label || pixmap.isNull())
        return;

    QSize targetSize = label->size();
    if (targetSize.width() <= 1 || targetSize.height() <= 1)
        targetSize = QSize(640, 240);

    label->setPixmap(pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}


QLineEdit* findEdit(QWidget* owner, const char* objectName)
{
    return owner ? owner->findChild<QLineEdit*>(QString::fromLatin1(objectName)) : nullptr;
}

double readEdit(QWidget* owner, const char* objectName, double fallback)
{
    if (auto* edit = findEdit(owner, objectName))
    {
        bool ok = false;
        const double value = edit->text().toDouble(&ok);
        if (ok)
            return value;
    }
    return fallback;
}

void setEditIfExists(QWidget* owner, const char* objectName, double value)
{
    if (auto* edit = findEdit(owner, objectName))
        edit->setText(QString::number(value));
}

TopoDS_Shape makePrismFromProfile(const QVector<QPointF>& profile, double length)
{
    if (profile.size() < 3 || length <= kEps)
        return TopoDS_Shape();

    BRepBuilderAPI_MakePolygon polygon;
    QPointF lastPoint;
    bool hasLast = false;
    int validCount = 0;

    for (const QPointF& point : profile)
    {
        if (hasLast && qAbs(point.x() - lastPoint.x()) < kEps && qAbs(point.y() - lastPoint.y()) < kEps)
            continue;
        polygon.Add(gp_Pnt(point.x(), 0.0, point.y()));
        lastPoint = point;
        hasLast = true;
        ++validCount;
    }

    if (validCount < 3)
        return TopoDS_Shape();

    polygon.Close();
    BRepBuilderAPI_MakeFace faceMaker(polygon.Wire());
    if (!faceMaker.IsDone())
        return TopoDS_Shape();

    return BRepPrimAPI_MakePrism(faceMaker.Face(), gp_Vec(0.0, length, 0.0)).Shape();
}

bool addProfileSolid(TopoDS_Compound& compound, BRep_Builder& builder, const QVector<QPointF>& profile, double length)
{
    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;
    builder.Add(compound, solid);
    return true;
}

bool addBandSolid(TopoDS_Compound& compound,
                  BRep_Builder& builder,
                  double left0,
                  double right0,
                  double left1,
                  double right1,
                  double z0,
                  double z1,
                  double length)
{
    if (z1 - z0 <= kEps)
        return false;
    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    return addProfileSolid(compound, builder, profile, length);
}

// 洞口采用简化城门洞断面：直墙 + 折线拱顶。
// z=0 为洞底，z=wallH 为直墙顶，z=wallH+archH 为拱顶最高点。
double cavernLeftAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return 0.0;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span * 0.25 * t;
}

double cavernRightAtZ(double span, double wallH, double archH, double z)
{
    if (z <= wallH || archH <= kEps)
        return span;
    const double t = qBound(0.0, (z - wallH) / archH, 1.0);
    return span - span * 0.25 * t;
}

QVector<QPointF> buildCavernProfile(double span, double wallH, double archH)
{
    QVector<QPointF> profile;
    profile << QPointF(0.0, 0.0)
            << QPointF(span, 0.0)
            << QPointF(span, wallH)
            << QPointF(span * 0.75, wallH + archH)
            << QPointF(span * 0.25, wallH + archH)
            << QPointF(0.0, wallH);
    return profile;
}

} // namespace

CavernTemplateDialog::CavernTemplateDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessCavernTemplate)
{
    Q_UNUSED(parent);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    auto* pageHeaderLabel = new QLabel(QString("洞室参数模板"), this);
    pageHeaderLabel->setObjectName("pageHeaderLabel");
    mainLayout->addWidget(pageHeaderLabel);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(9, 9, 9, 9);
    contentLayout->setSpacing(6);
    mainLayout->addLayout(contentLayout);


    auto* inputGroup = new QGroupBox("参数输入", this);
    auto* form = new QFormLayout(inputGroup);

    auto* positiveValidator = new QDoubleValidator(0.01, 1.0e9, 3, this);

    spanEdit = new QLineEdit("18", this);
    spanEdit->setMinimumWidth(10);
    wallHEdit = new QLineEdit("12", this);
    wallHEdit->setMinimumWidth(10);
    archHEdit = new QLineEdit("4", this);
    archHEdit->setMinimumWidth(10);
    lenEdit = new QLineEdit("80", this);
    lenEdit->setMinimumWidth(10);
    liningEdit = new QLineEdit("4", this);
    liningEdit->setMinimumWidth(10); // 复用原成员：这里表示洞口竖向开挖级数 N。

    auto* sideRockEdit = new QLineEdit("18", this);
    sideRockEdit->setMinimumWidth(10);
    sideRockEdit->setObjectName("sideRockEdit");
    auto* topRockEdit = new QLineEdit("16", this);
    topRockEdit->setMinimumWidth(10);
    topRockEdit->setObjectName("topRockEdit");
    auto* bottomRockEdit = new QLineEdit("8", this);
    bottomRockEdit->setMinimumWidth(10);
    bottomRockEdit->setObjectName("bottomRockEdit");

    for (auto* edit : {spanEdit, wallHEdit, archHEdit, lenEdit, sideRockEdit, topRockEdit, bottomRockEdit})
        edit->setValidator(positiveValidator);
    liningEdit->setValidator(new QIntValidator(1, 50, this));

    form->addRow("洞口宽度 B", spanEdit);
    form->addRow("直墙高度 Hw", wallHEdit);
    form->addRow("拱顶高度 Ha", archHEdit);
    form->addRow("洞室进深 L", lenEdit);
    form->addRow("洞口开挖级数 N", liningEdit);
    form->addRow("左右围岩余量 Wr", sideRockEdit);
    form->addRow("顶部围岩厚度 Htop", topRockEdit);
    form->addRow("底部围岩厚度 Hbot", bottomRockEdit);

    auto* geoGroup = new QGroupBox("地质体开挖联动", this);
    auto* geoForm = new QFormLayout(geoGroup);
    m_linkGeology = new QCheckBox("关联目标地质体进行地下开挖", this);
    m_geologyCombo = new QComboBox(this);
    m_geologyCombo->setEnabled(false);
    m_posXEdit = new QLineEdit("0", this);
    m_posYEdit = new QLineEdit("0", this);
    m_posZEdit = new QLineEdit("0", this);
    auto* coordValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);
    m_posXEdit->setValidator(coordValidator);
    m_posYEdit->setValidator(coordValidator);
    m_posZEdit->setValidator(coordValidator);
    m_posXEdit->setEnabled(false);
    m_posYEdit->setEnabled(false);
    m_posZEdit->setEnabled(false);

    geoForm->addRow(m_linkGeology);
    geoForm->addRow("目标地质体", m_geologyCombo);
    auto* posRow = new QHBoxLayout();
    posRow->addWidget(new QLabel("X:")); posRow->addWidget(m_posXEdit);
    posRow->addWidget(new QLabel("Y:")); posRow->addWidget(m_posYEdit);
    posRow->addWidget(new QLabel("Z:")); posRow->addWidget(m_posZEdit);
    geoForm->addRow("定位坐标 (X,Y,Z)", posRow);

    connect(m_linkGeology, &QCheckBox::toggled, this, &CavernTemplateDialog::onLinkGeologyToggled);
    connect(m_geologyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyTargetGeologyDefaults();
    });

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* generateButton = new QPushButton("生成洞室", this);
    row->addStretch();
    row->addWidget(generateButton);

    diagramLabel = new QLabel(this);
    diagramLabel->setAlignment(Qt::AlignCenter);
    diagramLabel->setMinimumSize(0, 0);
    diagramLabel->setScaledContents(false);
    diagramLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    contentLayout->addWidget(inputGroup);
    contentLayout->addWidget(geoGroup);
    contentLayout->addLayout(row);
    contentLayout->addWidget(diagramLabel, 2);
    contentLayout->addStretch(1);

    connect(generateButton, &QPushButton::clicked, this, &CavernTemplateDialog::onGenerateClicked);

    updateDiagram();
}

void CavernTemplateDialog::applyTargetGeologyDefaults()
{
    if (!m_geologyCombo) return;
    const QString uuid = m_geologyCombo->currentData().toString();
    if (uuid.isEmpty()) return;
    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc) return;
    CadBody* b = doc->getGeomShapeObject(uuid);
    if (!b || !b->getShape() || b->getShape()->IsNull()) return;
    Bnd_Box box;
    BRepBndLib::Add(*(b->getShape()), box);
    if (box.IsVoid()) return;
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    if (m_posXEdit) m_posXEdit->setText(QString::number((xmin + xmax) * 0.5, 'f', 1));
    if (m_posYEdit) m_posYEdit->setText(QString::number((ymin + ymax) * 0.5, 'f', 1));
    if (m_posZEdit) m_posZEdit->setText(QString::number(zmin + (zmax - zmin) * 0.35, 'f', 1));
}

void CavernTemplateDialog::refreshGeologyCombo()
{
    if (!m_geologyCombo) return;
    const QString currentUuid = m_geologyCombo->currentData().toString();
    const QSignalBlocker blocker(m_geologyCombo);
    m_geologyCombo->clear();
    auto* doc = GeomManager::CadDocument::getInstance();
    if (!doc) return;
    const auto map = doc->getShapMap();
    int defaultIdx = -1;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        if (it.key() == m_uuid) continue;
        CadBody* b = it.value();
        if (!b || !b->getShape() || b->getShape()->IsNull()) continue;
        m_geologyCombo->addItem(b->getName().isEmpty() ? it.key() : b->getName(), it.key());
        if (b->getName().contains("地形") || b->getName().contains("地质") || !b->faceGroups().isEmpty())
            defaultIdx = m_geologyCombo->count() - 1;
    }
    if (!currentUuid.isEmpty())
    {
        int idx = m_geologyCombo->findData(currentUuid);
        if (idx >= 0) m_geologyCombo->setCurrentIndex(idx);
    }
    else if (defaultIdx >= 0)
    {
        m_geologyCombo->setCurrentIndex(defaultIdx);
    }
}

void CavernTemplateDialog::onLinkGeologyToggled(bool checked)
{
    if (m_geologyCombo) m_geologyCombo->setEnabled(checked);
    if (m_posXEdit) m_posXEdit->setEnabled(checked);
    if (m_posYEdit) m_posYEdit->setEnabled(checked);
    if (m_posZEdit) m_posZEdit->setEnabled(checked);
    if (checked)
    {
        refreshGeologyCombo();
        applyTargetGeologyDefaults();
    }
}

void CavernTemplateDialog::showEvent(QShowEvent* event)
{
    BasePage::showEvent(event);
    refreshGeologyCombo();
    if (m_linkGeology && m_linkGeology->isChecked())
        applyTargetGeologyDefaults();
}

QJsonObject CavernTemplateDialog::saveJson()
{
    QJsonObject object;
    object["span"] = spanEdit->text().toDouble();
    object["wh"] = wallHEdit->text().toDouble();
    object["ah"] = archHEdit->text().toDouble();
    object["len"] = lenEdit->text().toDouble();
    object["stage"] = liningEdit->text().toInt();
    object["sideRock"] = readEdit(this, "sideRockEdit", 18.0);
    object["topRock"] = readEdit(this, "topRockEdit", 16.0);
    object["bottomRock"] = readEdit(this, "bottomRockEdit", 8.0);
    object["linkGeology"] = m_linkGeology ? m_linkGeology->isChecked() : false;
    object["targetGeology"] = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
    object["posX"] = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
    object["posY"] = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
    object["posZ"] = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;
    return object;
}

void CavernTemplateDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("span"))
        spanEdit->setText(QString::number(object["span"].toDouble()));
    if (object.contains("wh"))
        wallHEdit->setText(QString::number(object["wh"].toDouble()));
    if (object.contains("ah"))
        archHEdit->setText(QString::number(object["ah"].toDouble()));
    if (object.contains("len"))
        lenEdit->setText(QString::number(object["len"].toDouble()));
    if (object.contains("stage"))
        liningEdit->setText(QString::number(object["stage"].toInt()));
    else if (object.contains("lining"))
        liningEdit->setText(QString::number(qMax(1, qRound(object["lining"].toDouble()))));
    if (object.contains("sideRock"))
        setEditIfExists(this, "sideRockEdit", object["sideRock"].toDouble());
    if (object.contains("topRock"))
        setEditIfExists(this, "topRockEdit", object["topRock"].toDouble());
    if (object.contains("bottomRock"))
        setEditIfExists(this, "bottomRockEdit", object["bottomRock"].toDouble());

    if (m_linkGeology && object.contains("linkGeology"))
    {
        m_linkGeology->setChecked(object["linkGeology"].toBool());
        onLinkGeologyToggled(m_linkGeology->isChecked());
    }
    if (m_geologyCombo && object.contains("targetGeology"))
    {
        const QString target = object["targetGeology"].toString();
        int idx = m_geologyCombo->findData(target);
        if (idx >= 0) m_geologyCombo->setCurrentIndex(idx);
    }
    if (m_posXEdit && object.contains("posX"))
        m_posXEdit->setText(QString::number(object["posX"].toDouble()));
    if (m_posYEdit && object.contains("posY"))
        m_posYEdit->setText(QString::number(object["posY"].toDouble()));
    if (m_posZEdit && object.contains("posZ"))
        m_posZEdit->setText(QString::number(object["posZ"].toDouble()));

    updateDiagram();
}

void CavernTemplateDialog::resizeEvent(QResizeEvent* event)
{
    BasePage::resizeEvent(event);
    if (!diagramPixmap.isNull() && diagramLabel)
        setScaledDiagram(diagramLabel, diagramPixmap);
}

void CavernTemplateDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/cavern_parameter_diagram.png");
    diagramPixmap.load(resourcePath);
    if (diagramPixmap.isNull())
    {
        diagramLabel->setText("参数示意图加载失败：resources/cavern_parameter_diagram.png");
        return;
    }

    setScaledDiagram(diagramLabel, diagramPixmap);
}




void CavernTemplateDialog::onPreviewClicked()
{
    updateDiagram();
}

void CavernTemplateDialog::buildLayeredCavern(double span, double wallH, double archH, double length, double stageValue)
{
    const int stageCount = qMax(1, qRound(stageValue));
    const double sideRock = readEdit(this, "sideRockEdit", 18.0);
    const double topRock = readEdit(this, "topRockEdit", 16.0);
    const double bottomRock = readEdit(this, "bottomRockEdit", 8.0);

    if (span <= kEps || wallH <= kEps || archH <= kEps || length <= kEps || stageCount <= 0 ||
        sideRock <= kEps || topRock <= kEps || bottomRock <= kEps)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：洞口尺寸、进深、开挖级数和围岩余量必须为正值。");
        return;
    }

    const double cavernH = wallH + archH;
    const double minX = -sideRock;
    const double maxX = span + sideRock;
    const double minZ = -bottomRock;
    const double maxZ = cavernH + topRock;

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    // 底部围岩：完整铺底。
    addBandSolid(compound, builder, minX, maxX, minX, maxX, minZ, 0.0, length);

    // 洞口高度范围内，按每个开挖级横向拆成：左围岩 + 开挖块 + 右围岩。
    // 左右围岩和开挖块使用同一组 left/right 函数，边界完全一致，所以不会出现空隙。
    for (int i = 0; i < stageCount; ++i)
    {
        const double z0 = cavernH * i / static_cast<double>(stageCount);
        const double z1 = cavernH * (i + 1) / static_cast<double>(stageCount);
        const double left0 = cavernLeftAtZ(span, wallH, archH, z0);
        const double right0 = cavernRightAtZ(span, wallH, archH, z0);
        const double left1 = cavernLeftAtZ(span, wallH, archH, z1);
        const double right1 = cavernRightAtZ(span, wallH, archH, z1);

        addBandSolid(compound, builder, minX, left0, minX, left1, z0, z1, length);       // 左围岩
        addBandSolid(compound, builder, left0, right0, left1, right1, z0, z1, length);  // 开挖级
        addBandSolid(compound, builder, right0, maxX, right1, maxX, z0, z1, length);    // 右围岩
    }

    // 顶部围岩：从拱顶最高点往上完整覆盖。
    addBandSolid(compound, builder, minX, maxX, minX, maxX, cavernH, maxZ, length);

    auto* object = new GeomShapeObject(m_uuid, new TopoDS_Shape(compound), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}

void CavernTemplateDialog::onGenerateClicked()
{
    updateDiagram();

    if (m_linkGeology && m_linkGeology->isChecked())
    {
        const QString targetUuid = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
        if (targetUuid.isEmpty())
        {
            QMessageBox::warning(this, tr("参数提示"), tr("请选择要关联的目标地质体。"));
            return;
        }

        const double span = spanEdit->text().toDouble();
        const double wallH = wallHEdit->text().toDouble();
        const double archH = archHEdit->text().toDouble();
        const double len = lenEdit->text().toDouble();
        const double posX = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
        const double posY = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
        const double posZ = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;

        if (span <= kEps || wallH <= kEps || archH <= kEps || len <= kEps)
        {
            QMessageBox::warning(this, tr("参数错误"), tr("洞室尺寸必须大于 0。"));
            return;
        }

        const QVector<QPointF> profile = buildCavernProfile(span, wallH, archH);
        TopoDS_Shape voidSolid = makePrismFromProfile(profile, len);
        if (voidSolid.IsNull())
        {
            QMessageBox::warning(this, tr("生成失败"), tr("无法构建洞室开挖断面实体。"));
            return;
        }

        auto* doc = GeomManager::CadDocument::getInstance();
        CadBody* targetBody = doc ? doc->getGeomShapeObject(targetUuid) : nullptr;
        if (targetBody && targetBody->getShape())
        {
            Bnd_Box geoBox;
            BRepBndLib::Add(*(targetBody->getShape()), geoBox);
            if (!geoBox.IsVoid())
            {
                double xmin, ymin, zmin, xmax, ymax, zmax;
                geoBox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                if (qAbs(posX) < 1.0 && qAbs(posY) < 1.0 && (xmin > 10.0 || xmax < -10.0))
                {
                    applyTargetGeologyDefaults();
                }
            }
        }

        const double actualX = m_posXEdit ? m_posXEdit->text().toDouble() : posX;
        const double actualY = m_posYEdit ? m_posYEdit->text().toDouble() : posY;
        const double actualZ = m_posZEdit ? m_posZEdit->text().toDouble() : posZ;

        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(actualX - span * 0.5, actualY - len * 0.5, actualZ));
        BRepBuilderAPI_Transform xform(voidSolid, trsf);
        TopoDS_Shape placedVoid = xform.Shape();

        TopoDS_Shape cutGeology;
        QVector<int> newFaceIndices;
        QString errorMsg;
        if (!BooleanBuilder::excavateFromGeology(targetUuid, placedVoid, cutGeology, newFaceIndices, errorMsg))
        {
            QMessageBox::warning(this, tr("开挖失败"), tr("地下洞室开挖布尔裁剪失败：%1").arg(errorMsg));
            return;
        }

        const QString targetName = targetBody ? targetBody->getName() : tr("地质体");

        GeometryBuildUtils::registerShape(targetUuid, targetName, new TopoDS_Shape(cutGeology));
        if (auto* body = doc ? doc->getGeomShapeObject(targetUuid) : nullptr)
        {
            body->addFaceGroup({QStringLiteral("洞室开挖面组"), QStringLiteral("Excavation"), newFaceIndices});
        }

        // 移除旧的地形高程伪彩预览，确保布尔开挖后的三维洞室空腔立即可见
        GeometryDisplayManager::removeTerrainPreview(targetUuid);

        // 注册开挖体实体（橙色半透明显示，方便观察围岩与开挖块相对位置）
        GeometryBuildUtils::registerShape(m_uuid, getDisplayName(), new TopoDS_Shape(placedVoid));
        if (auto* voidBody = doc ? doc->getGeomShapeObject(m_uuid) : nullptr)
        {
            voidBody->setDisplayColor(QColor(242, 140, 38, 102));
            if (auto surfActor = voidBody->getSurfActor())
            {
                surfActor->GetProperty()->SetColor(0.95, 0.55, 0.15);
                surfActor->GetProperty()->SetOpacity(0.4);
            }
        }

        // 同步树节点状态、勾选并提交历史
        if (auto* pre = ServiceRegistry::instance().getService<PreProcessingService>())
        {
            if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
            {
                workbench->refreshTreePresentation(m_uuid);
                workbench->refreshTreePresentation(targetUuid);
            }
        }

        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();

        QMessageBox::information(this, tr("开挖完成"),
            tr("已在目标地质体中成功挖出地下洞室，新产生开挖内壁面 %1 个，已自动归入【洞室开挖面组】。")
            .arg(newFaceIndices.size()));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool ok = service->recreateCavernTemplate(
        m_uuid, getDisplayName(),
        spanEdit->text().toDouble(),
        wallHEdit->text().toDouble(),
        archHEdit->text().toDouble(),
        lenEdit->text().toDouble(),
        liningEdit->text().toDouble(),
        readEdit(this, "sideRockEdit", 18.0),
        readEdit(this, "topRockEdit", 16.0),
        readEdit(this, "bottomRockEdit", 8.0));
    if (!ok)
        QMessageBox::warning(this, tr("生成失败"), tr("洞室参数无效，无法生成三维实体。"));
}
