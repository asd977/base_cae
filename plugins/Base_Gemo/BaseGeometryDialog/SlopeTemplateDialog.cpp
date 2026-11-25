#include "SlopeTemplateDialog.h"

#include "../builders/BooleanBuilder.h"
#include "../builders/GeometryBuildUtils.h"
#include "../core/CadDocument.h"
#include "../core/CadBody.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_Transform.hxx>
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

// 边坡断面：
// 1) 左侧是远场竖向边界；
// 2) 右侧是多级开挖坡面；
// 3) 坡脚外延保留，但必须有“底部厚度 Hbase”，否则外延只是一条零厚度面；
// 4) 模型厚度 L 由拉伸方向控制，平面断面不再额外设置“宽度”。
QVector<QPointF> buildSlopeProfile2D(double height,
                                     double topPlatformWidth,
                                     double middleBenchWidth,
                                     double stageHorizontalWidth,
                                     double toeExtensionLength,
                                     double baseHeight,
                                     int stageCount)
{
    QVector<QPointF> profile;
    if (height <= kEps || topPlatformWidth < 0.0 || middleBenchWidth < 0.0 ||
        stageHorizontalWidth <= kEps || toeExtensionLength < 0.0 || baseHeight <= kEps ||
        stageCount <= 0)
        return profile;

    profile << QPointF(0.0, -baseHeight);   // 左下远场底边
    profile << QPointF(0.0, height);        // 左上远场地表

    double x = 0.0;
    double z = height;
    const double dz = height / static_cast<double>(stageCount);

    if (topPlatformWidth > kEps)
    {
        x += topPlatformWidth;
        profile << QPointF(x, z);           // 坡顶平台
    }

    for (int i = 0; i < stageCount; ++i)
    {
        x += stageHorizontalWidth;
        z = qMax(0.0, height - (i + 1) * dz);
        profile << QPointF(x, z);           // 第 i 级坡面终点

        if (i != stageCount - 1 && middleBenchWidth > kEps)
        {
            x += middleBenchWidth;
            profile << QPointF(x, z);       // 中间平台
        }
    }

    if (toeExtensionLength > kEps)
    {
        x += toeExtensionLength;
        profile << QPointF(x, 0.0);         // 坡脚外延平台面
    }

    profile << QPointF(x, -baseHeight);     // 外延/坡脚底部，有厚度，不是零厚度面
    return profile;
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

} // namespace

SlopeTemplateDialog::SlopeTemplateDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessSlopeTemplate)
{
    Q_UNUSED(parent);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    auto* pageHeaderLabel = new QLabel(QString("边坡参数模板"), this);
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
    auto* nonNegativeValidator = new QDoubleValidator(0.0, 1.0e9, 3, this);

    hEdit = new QLineEdit("65", this);
    hEdit->setMinimumWidth(10);
    benchWidthEdit = new QLineEdit("8", this);
    benchWidthEdit->setMinimumWidth(10);      // 中间平台宽 Bmid
    slopeEdit = new QLineEdit("18", this);
    slopeEdit->setMinimumWidth(10);          // 单级坡面水平宽 Ws
    lenEdit = new QLineEdit("120", this);
    lenEdit->setMinimumWidth(10);           // 拉伸厚度 L
    benchCountEdit = new QLineEdit("3", this);
    benchCountEdit->setMinimumWidth(10);      // 台阶级数 N

    auto* topPlatformEdit = new QLineEdit("20", this);
    topPlatformEdit->setMinimumWidth(10);
    topPlatformEdit->setObjectName("topPlatformEdit");
    auto* toeExtensionEdit = new QLineEdit("30", this);
    toeExtensionEdit->setMinimumWidth(10);
    toeExtensionEdit->setObjectName("toeExtensionEdit");
    auto* baseHeightEdit = new QLineEdit("15", this);
    baseHeightEdit->setMinimumWidth(10);
    baseHeightEdit->setObjectName("baseHeightEdit");

    hEdit->setValidator(positiveValidator);
    benchWidthEdit->setValidator(nonNegativeValidator);
    slopeEdit->setValidator(positiveValidator);
    lenEdit->setValidator(positiveValidator);
    benchCountEdit->setValidator(new QIntValidator(1, 50, this));
    topPlatformEdit->setValidator(nonNegativeValidator);
    toeExtensionEdit->setValidator(nonNegativeValidator);
    baseHeightEdit->setValidator(positiveValidator);

    form->addRow("坡高 H", hEdit);
    form->addRow("坡顶平台宽 Btop", topPlatformEdit);
    form->addRow("中间平台宽 Bmid", benchWidthEdit);
    form->addRow("单级坡面水平宽 Ws", slopeEdit);
    form->addRow("坡脚外延长度 Etoe", toeExtensionEdit);
    form->addRow("底部厚度 Hbase", baseHeightEdit);
    form->addRow("模型厚度 L", lenEdit);
    form->addRow("台阶级数 N", benchCountEdit);

    auto* geoGroup = new QGroupBox("地质体开挖联动", this);
    auto* geoForm = new QFormLayout(geoGroup);
    m_linkGeology = new QCheckBox("关联目标地质体进行边坡开挖", this);
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

    connect(m_linkGeology, &QCheckBox::toggled, this, &SlopeTemplateDialog::onLinkGeologyToggled);
    connect(m_geologyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyTargetGeologyDefaults();
    });

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* generateButton = new QPushButton("生成边坡", this);
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

    connect(generateButton, &QPushButton::clicked, this, &SlopeTemplateDialog::onGenerateClicked);

    updateDiagram();
}

void SlopeTemplateDialog::applyTargetGeologyDefaults()
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
    if (m_posXEdit) m_posXEdit->setText(QString::number(xmin + (xmax - xmin) * 0.25, 'f', 1));
    if (m_posYEdit) m_posYEdit->setText(QString::number((ymin + ymax) * 0.5, 'f', 1));
    if (m_posZEdit) m_posZEdit->setText(QString::number(zmin + (zmax - zmin) * 0.4, 'f', 1));
    if (lenEdit && lenEdit->text().toDouble() < 100.0)
        lenEdit->setText(QString::number(qMax(100.0, (ymax - ymin) * 0.3), 'f', 1));
}

void SlopeTemplateDialog::refreshGeologyCombo()
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

void SlopeTemplateDialog::onLinkGeologyToggled(bool checked)
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

void SlopeTemplateDialog::showEvent(QShowEvent* event)
{
    BasePage::showEvent(event);
    refreshGeologyCombo();
    if (m_linkGeology && m_linkGeology->isChecked())
        applyTargetGeologyDefaults();
}

QJsonObject SlopeTemplateDialog::saveJson()
{
    QJsonObject object;
    object["h"] = hEdit->text().toDouble();
    object["top"] = readEdit(this, "topPlatformEdit", 20.0);
    object["bw"] = benchWidthEdit->text().toDouble();
    object["w"] = slopeEdit->text().toDouble();
    object["toe"] = readEdit(this, "toeExtensionEdit", 30.0);
    object["baseH"] = readEdit(this, "baseHeightEdit", 15.0);
    object["l"] = lenEdit->text().toDouble();
    object["bc"] = benchCountEdit->text().toInt();
    object["linkGeology"] = m_linkGeology ? m_linkGeology->isChecked() : false;
    object["targetGeology"] = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
    object["posX"] = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
    object["posY"] = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
    object["posZ"] = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;
    return object;
}

void SlopeTemplateDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("h"))
        hEdit->setText(QString::number(object["h"].toDouble()));
    if (object.contains("top"))
        setEditIfExists(this, "topPlatformEdit", object["top"].toDouble());
    if (object.contains("bw"))
        benchWidthEdit->setText(QString::number(object["bw"].toDouble()));
    if (object.contains("w"))
        slopeEdit->setText(QString::number(object["w"].toDouble()));
    else if (object.contains("s"))
        slopeEdit->setText(QString::number(object["s"].toDouble()));
    if (object.contains("toe"))
        setEditIfExists(this, "toeExtensionEdit", object["toe"].toDouble());
    if (object.contains("baseH"))
        setEditIfExists(this, "baseHeightEdit", object["baseH"].toDouble());
    if (object.contains("l"))
        lenEdit->setText(QString::number(object["l"].toDouble()));
    if (object.contains("bc"))
        benchCountEdit->setText(QString::number(object["bc"].toInt()));

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

void SlopeTemplateDialog::resizeEvent(QResizeEvent* event)
{
    BasePage::resizeEvent(event);
    if (!diagramPixmap.isNull() && diagramLabel)
        setScaledDiagram(diagramLabel, diagramPixmap);
}

void SlopeTemplateDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/slope_parameter_diagram.png");
    diagramPixmap.load(resourcePath);
    if (diagramPixmap.isNull())
    {
        diagramLabel->setText("参数示意图加载失败：resources/slope_parameter_diagram.png");
        return;
    }

    setScaledDiagram(diagramLabel, diagramPixmap);
}




void SlopeTemplateDialog::onPreviewClicked()
{
    updateDiagram();
}

void SlopeTemplateDialog::buildLayeredSlope(double H, double Bmid, double Ws, double L, int N)
{
    const double top = readEdit(this, "topPlatformEdit", 20.0);
    const double Etoe = readEdit(this, "toeExtensionEdit", 30.0);
    const double Hbase = readEdit(this, "baseHeightEdit", 15.0);

    if (H <= kEps || top < 0.0 || Bmid < 0.0 || Ws <= kEps || Etoe < 0.0 ||
        Hbase <= kEps || L <= kEps || N <= 0)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：坡高、单级坡面水平宽、底部厚度、模型厚度必须大于0；平台宽和坡脚外延长度可以为0；台阶级数必须大于0。");
        return;
    }

    const QVector<QPointF> profile = buildSlopeProfile2D(H, top, Bmid, Ws, Etoe, Hbase, N);
    TopoDS_Shape slopeSolid = makePrismFromProfile(profile, L);
    if (slopeSolid.IsNull())
    {
        QMessageBox::warning(this, "生成失败", "边坡断面无效，无法生成三维实体。");
        return;
    }

    auto* object = new GeomShapeObject(m_uuid, new TopoDS_Shape(slopeSolid), getDisplayName());
    GeomManager::GeomManagerInstance::getInstance()->appendGeomShapeObject(object);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->addProp(m_uuid + "_face", object->getSurfActor(), "Geometry", m_uuid + "_face", true);
        vis->addProp(m_uuid + "_edge", object->getEdgeActor(), "Geometry", m_uuid + "_face", true);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManagerService->setNodeDimmed(m_uuid, false);
}

void SlopeTemplateDialog::onGenerateClicked()
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

        const double H = hEdit->text().toDouble();
        const double Bmid = benchWidthEdit->text().toDouble();
        const double Ws = slopeEdit->text().toDouble();
        const double L = lenEdit->text().toDouble();
        const int N = benchCountEdit->text().toInt();
        const double top = readEdit(this, "topPlatformEdit", 20.0);
        const double Etoe = readEdit(this, "toeExtensionEdit", 30.0);
        const double Hbase = readEdit(this, "baseHeightEdit", 15.0);
        const double posX = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
        const double posY = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
        const double posZ = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;

        if (H <= kEps || top < 0.0 || Bmid < 0.0 || Ws <= kEps || Etoe < 0.0 ||
            Hbase <= kEps || L <= kEps || N <= 0)
        {
            QMessageBox::warning(this, tr("参数错误"), tr("请检查边坡参数设置。"));
            return;
        }

        const QVector<QPointF> profile = buildSlopeProfile2D(H, top, Bmid, Ws, Etoe, Hbase, N);
        TopoDS_Shape slopeSolid = makePrismFromProfile(profile, L);
        if (slopeSolid.IsNull())
        {
            QMessageBox::warning(this, tr("生成失败"), tr("无法构建边坡开挖体实体。"));
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
        trsf.SetTranslation(gp_Vec(actualX, actualY - L * 0.5, actualZ));
        BRepBuilderAPI_Transform xform(slopeSolid, trsf);
        TopoDS_Shape placedSlope = xform.Shape();

        TopoDS_Shape cutGeology;
        QVector<int> newFaceIndices;
        QString errorMsg;
        if (!BooleanBuilder::excavateFromGeology(targetUuid, placedSlope, cutGeology, newFaceIndices, errorMsg))
        {
            QMessageBox::warning(this, tr("开挖失败"), tr("边坡开挖布尔裁剪失败：%1").arg(errorMsg));
            return;
        }

        const QString targetName = targetBody ? targetBody->getName() : tr("地质体");

        GeometryBuildUtils::registerShape(targetUuid, targetName, new TopoDS_Shape(cutGeology));
        if (auto* body = doc ? doc->getGeomShapeObject(targetUuid) : nullptr)
        {
            body->addFaceGroup({QStringLiteral("边坡开挖面组"), QStringLiteral("Excavation"), newFaceIndices});
        }

        // 移除旧的地形高程伪彩预览，确保布尔开挖后的边坡台阶立即可见
        GeometryDisplayManager::removeTerrainPreview(targetUuid);

        // 注册开挖体实体（青蓝色半透明显示）
        GeometryBuildUtils::registerShape(m_uuid, getDisplayName(), new TopoDS_Shape(placedSlope));
        if (auto* voidBody = doc ? doc->getGeomShapeObject(m_uuid) : nullptr)
        {
            voidBody->setDisplayColor(QColor(51, 178, 217, 102));
            if (auto surfActor = voidBody->getSurfActor())
            {
                surfActor->GetProperty()->SetColor(0.2, 0.7, 0.85);
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
            tr("已在目标地质体中成功切出边坡，新产生边坡开挖面 %1 个，已自动归入【边坡开挖面组】。")
            .arg(newFaceIndices.size()));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool ok = service->recreateSlopeTemplate(
        m_uuid, getDisplayName(),
        hEdit->text().toDouble(),
        benchWidthEdit->text().toDouble(),
        slopeEdit->text().toDouble(),
        lenEdit->text().toDouble(),
        benchCountEdit->text().toInt(),
        readEdit(this, "topPlatformEdit", 20.0),
        readEdit(this, "toeExtensionEdit", 30.0),
        readEdit(this, "baseHeightEdit", 15.0));
    if (!ok)
        QMessageBox::warning(this, tr("生成失败"), tr("边坡参数无效，无法生成三维实体。"));
}
