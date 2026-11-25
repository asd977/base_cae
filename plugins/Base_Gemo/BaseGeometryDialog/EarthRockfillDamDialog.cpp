#include "EarthRockfillDamDialog.h"

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
#include <QPixmap>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointF>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
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

bool appendBand(TopoDS_Compound& compound,
                BRep_Builder& builder,
                double outerLeft0,
                double outerRight0,
                double outerLeft1,
                double outerRight1,
                double regionLeft0,
                double regionRight0,
                double regionLeft1,
                double regionRight1,
                double z0,
                double z1,
                double length)
{
    const double left0 = qMax(outerLeft0, regionLeft0);
    const double right0 = qMin(outerRight0, regionRight0);
    const double left1 = qMax(outerLeft1, regionLeft1);
    const double right1 = qMin(outerRight1, regionRight1);

    if ((right0 - left0 <= kEps) && (right1 - left1 <= kEps))
        return false;

    QVector<QPointF> profile;
    if (right0 - left0 > kEps)
        profile << QPointF(left0, z0) << QPointF(right0, z0);
    if (right1 - left1 > kEps)
        profile << QPointF(right1, z1) << QPointF(left1, z1);

    TopoDS_Shape solid = makePrismFromProfile(profile, length);
    if (solid.IsNull())
        return false;

    builder.Add(compound, solid);
    return true;
}

} // namespace

EarthRockfillDamDialog::EarthRockfillDamDialog(QWidget* parent)
    : BasePage(PublicConstants::kPagePreprocessEarthRockfillDam)
{
    Q_UNUSED(parent);
    initUi();
}

QJsonObject EarthRockfillDamDialog::saveJson()
{
    QJsonObject object;
    object["h"] = hEdit->text().toDouble();
    object["crest"] = crestEdit->text().toDouble();
    object["upW"] = upEdit->text().toDouble();
    object["downW"] = downEdit->text().toDouble();
    object["len"] = lenEdit->text().toDouble();
    object["coreTop"] = coreWidthEdit->text().toDouble();
    object["coreBottom"] = readEdit(this, "coreBottomEdit", 22.0);
    object["filter"] = readEdit(this, "filterWidthEdit", 4.0);
    object["coreOffset"] = readEdit(this, "coreOffsetEdit", 0.0);
    object["layers"] = layerEdit->text().toInt();
    object["linkGeology"] = m_linkGeology ? m_linkGeology->isChecked() : false;
    object["targetGeology"] = m_geologyCombo ? m_geologyCombo->currentData().toString() : QString();
    object["posX"] = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
    object["posY"] = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
    object["posZ"] = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;
    return object;
}

void EarthRockfillDamDialog::loadJson(const QJsonObject& object)
{
    if (object.contains("h"))
        hEdit->setText(QString::number(object["h"].toDouble()));
    if (object.contains("crest"))
        crestEdit->setText(QString::number(object["crest"].toDouble()));
    if (object.contains("upW"))
        upEdit->setText(QString::number(object["upW"].toDouble()));
    else if (object.contains("up"))
        upEdit->setText(QString::number(object["up"].toDouble()));
    if (object.contains("downW"))
        downEdit->setText(QString::number(object["downW"].toDouble()));
    else if (object.contains("down"))
        downEdit->setText(QString::number(object["down"].toDouble()));
    if (object.contains("len"))
        lenEdit->setText(QString::number(object["len"].toDouble()));
    if (object.contains("coreTop"))
        coreWidthEdit->setText(QString::number(object["coreTop"].toDouble()));
    else if (object.contains("core"))
        coreWidthEdit->setText(QString::number(object["core"].toDouble()));
    if (object.contains("coreBottom"))
        setEditIfExists(this, "coreBottomEdit", object["coreBottom"].toDouble());
    if (object.contains("filter"))
        setEditIfExists(this, "filterWidthEdit", object["filter"].toDouble());
    if (object.contains("coreOffset"))
        setEditIfExists(this, "coreOffsetEdit", object["coreOffset"].toDouble());
    if (object.contains("layers"))
        layerEdit->setText(QString::number(object["layers"].toInt()));

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

void EarthRockfillDamDialog::initUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    auto* pageHeaderLabel = new QLabel(QString("土石坝参数模板"), this);
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
    auto* coordValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);

    hEdit = new QLineEdit("80", this);
    hEdit->setMinimumWidth(10);
    crestEdit = new QLineEdit("20", this);
    crestEdit->setMinimumWidth(10);
    upEdit = new QLineEdit("160", this);
    upEdit->setMinimumWidth(10);       // 上游坝坡水平宽 Wu
    downEdit = new QLineEdit("140", this);
    downEdit->setMinimumWidth(10);     // 下游坝坡水平宽 Wd
    lenEdit = new QLineEdit("220", this);
    lenEdit->setMinimumWidth(10);

    coreWidthEdit = new QLineEdit("6", this);
    coreWidthEdit->setMinimumWidth(10);  // 心墙顶宽 Ct
    layerEdit = new QLineEdit("8", this);
    layerEdit->setMinimumWidth(10);

    auto* coreBottomEdit = new QLineEdit("28", this);
    coreBottomEdit->setMinimumWidth(10);
    coreBottomEdit->setObjectName("coreBottomEdit");

    auto* filterWidthEdit = new QLineEdit("3", this);
    filterWidthEdit->setMinimumWidth(10);
    filterWidthEdit->setObjectName("filterWidthEdit");

    auto* coreOffsetEdit = new QLineEdit("0", this);
    coreOffsetEdit->setMinimumWidth(10);
    coreOffsetEdit->setObjectName("coreOffsetEdit");

    for (auto* edit : {hEdit, crestEdit, upEdit, downEdit, lenEdit, coreWidthEdit, coreBottomEdit, filterWidthEdit})
        edit->setValidator(positiveValidator);
    coreOffsetEdit->setValidator(coordValidator);
    layerEdit->setValidator(new QIntValidator(1, 200, this));

    form->addRow("坝高 H", hEdit);
    form->addRow("坝顶宽 Bt", crestEdit);
    form->addRow("上游坝坡水平宽 Wu", upEdit);
    form->addRow("下游坝坡水平宽 Wd", downEdit);
    form->addRow("坝轴线长度 L", lenEdit);
    form->addRow("心墙顶宽 Ct", coreWidthEdit);
    form->addRow("心墙底宽 Cb", coreBottomEdit);
    form->addRow("反滤层宽 F", filterWidthEdit);
    form->addRow("心墙中心偏移 Xc", coreOffsetEdit);
    form->addRow("填筑层数 N", layerEdit);

    auto* geoGroup = new QGroupBox("河谷地形贴合联动", this);
    auto* geoForm = new QFormLayout(geoGroup);
    m_linkGeology = new QCheckBox("关联目标地质体进行河谷坝底贴合", this);
    m_geologyCombo = new QComboBox(this);
    m_geologyCombo->setEnabled(false);
    m_posXEdit = new QLineEdit("0", this);
    m_posYEdit = new QLineEdit("0", this);
    m_posZEdit = new QLineEdit("0", this);
    auto* posValidator = new QDoubleValidator(-1.0e9, 1.0e9, 3, this);
    m_posXEdit->setValidator(posValidator);
    m_posYEdit->setValidator(posValidator);
    m_posZEdit->setValidator(posValidator);
    m_posXEdit->setEnabled(false);
    m_posYEdit->setEnabled(false);
    m_posZEdit->setEnabled(false);

    geoForm->addRow(m_linkGeology);
    geoForm->addRow("目标地质体", m_geologyCombo);
    auto* posRow = new QHBoxLayout();
    posRow->addWidget(new QLabel("X:")); posRow->addWidget(m_posXEdit);
    posRow->addWidget(new QLabel("Y:")); posRow->addWidget(m_posYEdit);
    posRow->addWidget(new QLabel("Z:")); posRow->addWidget(m_posZEdit);
    geoForm->addRow("大坝定位坐标 (X,Y,Z)", posRow);

    connect(m_linkGeology, &QCheckBox::toggled, this, &EarthRockfillDamDialog::onLinkGeologyToggled);
    connect(m_geologyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyTargetGeologyDefaults();
    });

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* generateButton = new QPushButton("生成土石坝", this);
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

    connect(generateButton, &QPushButton::clicked, this, &EarthRockfillDamDialog::onGenerateClicked);

    updateDiagram();
}

void EarthRockfillDamDialog::applyTargetGeologyDefaults()
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

    const double cx = (xmin + xmax) * 0.5;
    const double cy = (ymin + ymax) * 0.5;
    const double cz = zmin + 10.0;
    if (m_posXEdit) m_posXEdit->setText(QString::number(cx, 'f', 1));
    if (m_posYEdit) m_posYEdit->setText(QString::number(cy, 'f', 1));
    if (m_posZEdit) m_posZEdit->setText(QString::number(cz, 'f', 1));

    const double targetH = (zmax - cz) * 1.05;
    if (hEdit && hEdit->text().toDouble() < targetH * 0.5)
        hEdit->setText(QString::number(targetH, 'f', 1));
    const double targetL = (ymax - ymin) * 0.6;
    if (lenEdit && lenEdit->text().toDouble() < targetL * 0.5)
        lenEdit->setText(QString::number(targetL, 'f', 1));
}

void EarthRockfillDamDialog::refreshGeologyCombo()
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

void EarthRockfillDamDialog::onLinkGeologyToggled(bool checked)
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

void EarthRockfillDamDialog::showEvent(QShowEvent* event)
{
    BasePage::showEvent(event);
    refreshGeologyCombo();
    if (m_linkGeology && m_linkGeology->isChecked())
        applyTargetGeologyDefaults();
}

void EarthRockfillDamDialog::resizeEvent(QResizeEvent* event)
{
    BasePage::resizeEvent(event);
    if (!diagramPixmap.isNull() && diagramLabel)
        setScaledDiagram(diagramLabel, diagramPixmap);
}

void EarthRockfillDamDialog::updateDiagram()
{
    const QString resourcePath = QStringLiteral(":/resources/earth_rockfill_dam_parameter_diagram.png");
    diagramPixmap.load(resourcePath);
    if (diagramPixmap.isNull())
    {
        diagramLabel->setText("参数示意图加载失败：resources/earth_rockfill_dam_parameter_diagram.png");
        return;
    }

    setScaledDiagram(diagramLabel, diagramPixmap);
}




void EarthRockfillDamDialog::onPreviewClicked()
{
    updateDiagram();
}

void EarthRockfillDamDialog::buildLayeredDam(double H, double Bt, double Wu, double Wd, double L, int layers)
{
    const double Ct = coreWidthEdit->text().toDouble();
    const double Cb = readEdit(this, "coreBottomEdit", 22.0);
    const double F = readEdit(this, "filterWidthEdit", 4.0);
    const double Xc = readEdit(this, "coreOffsetEdit", 0.0);

    if (H <= kEps || Bt <= kEps || Wu <= kEps || Wd <= kEps || L <= kEps || layers <= 0 ||
        Ct <= kEps || Cb <= kEps || F <= kEps)
    {
        QMessageBox::warning(this, "参数错误", "请检查参数：坝高、坝顶宽、上下游水平宽、长度、心墙宽、反滤层宽和层数必须为正值。");
        return;
    }

    const double totalWidth = Wu + Bt + Wd;
    const double center = Wu + Bt * 0.5 + Xc;

    const double topLeftWithFilter = center - Ct * 0.5 - F;
    const double topRightWithFilter = center + Ct * 0.5 + F;
    const double bottomLeftWithFilter = center - Cb * 0.5 - F;
    const double bottomRightWithFilter = center + Cb * 0.5 + F;

    if (topLeftWithFilter <= Wu + kEps || topRightWithFilter >= Wu + Bt - kEps)
    {
        QMessageBox::warning(this, "参数错误", "心墙顶宽 Ct + 两侧反滤层宽 2F 必须位于坝顶宽 Bt 内。请减小 Ct/F，或调整心墙中心偏移 Xc。");
        return;
    }
    if (bottomLeftWithFilter <= kEps || bottomRightWithFilter >= totalWidth - kEps)
    {
        QMessageBox::warning(this, "参数错误", "心墙底宽 Cb + 两侧反滤层宽 2F 必须位于坝体底宽内。请减小 Cb/F，或调整心墙中心偏移 Xc。");
        return;
    }

    const double layerH = H / static_cast<double>(layers);

    auto outerLeftAt = [&](double z)
    {
        return Wu * z / H;
    };
    auto outerRightAt = [&](double z)
    {
        return totalWidth - Wd * z / H;
    };
    auto coreHalfAt = [&](double z)
    {
        return Cb * 0.5 + (Ct - Cb) * 0.5 * z / H;
    };

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);

    for (int i = 0; i < layers; ++i)
    {
        const double z0 = i * layerH;
        const double z1 = (i + 1) * layerH;

        const double outerL0 = outerLeftAt(z0);
        const double outerR0 = outerRightAt(z0);
        const double outerL1 = outerLeftAt(z1);
        const double outerR1 = outerRightAt(z1);

        const double half0 = coreHalfAt(z0);
        const double half1 = coreHalfAt(z1);
        const double coreL0 = center - half0;
        const double coreR0 = center + half0;
        const double coreL1 = center - half1;
        const double coreR1 = center + half1;

        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   -1.0e12, coreL0 - F, -1.0e12, coreL1 - F, z0, z1, L); // 上游坝壳
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0 - F, coreL0, coreL1 - F, coreL1, z0, z1, L);    // 左反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreL0, coreR0, coreL1, coreR1, z0, z1, L);            // 心墙
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0, coreR0 + F, coreR1, coreR1 + F, z0, z1, L);    // 右反滤层
        appendBand(compound, builder, outerL0, outerR0, outerL1, outerR1,
                   coreR0 + F, 1.0e12, coreR1 + F, 1.0e12, z0, z1, L);    // 下游坝壳
    }

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

void EarthRockfillDamDialog::onGenerateClicked()
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
        const double Bt = crestEdit->text().toDouble();
        const double Wu = upEdit->text().toDouble();
        const double Wd = downEdit->text().toDouble();
        const double L = lenEdit->text().toDouble();
        const double posX = m_posXEdit ? m_posXEdit->text().toDouble() : 0.0;
        const double posY = m_posYEdit ? m_posYEdit->text().toDouble() : 0.0;
        const double posZ = m_posZEdit ? m_posZEdit->text().toDouble() : 0.0;

        if (H <= kEps || Bt <= kEps || Wu <= kEps || Wd <= kEps || L <= kEps)
        {
            QMessageBox::warning(this, tr("参数错误"), tr("大坝尺寸必须大于 0。"));
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
        const double actualH = hEdit->text().toDouble();
        const double actualL = lenEdit->text().toDouble();

        const double bottomW = Wu + Bt + Wd;
        QVector<QPointF> profile;
        profile << QPointF(0.0, 0.0)
                << QPointF(bottomW, 0.0)
                << QPointF(Wu + Bt, actualH)
                << QPointF(Wu, actualH);
        TopoDS_Shape rawDam = makePrismFromProfile(profile, actualL);
        if (rawDam.IsNull())
        {
            QMessageBox::warning(this, tr("生成失败"), tr("无法构建大坝基础实体。"));
            return;
        }

        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(actualX - bottomW * 0.5, actualY - actualL * 0.5, actualZ));
        BRepBuilderAPI_Transform xform(rawDam, trsf);
        TopoDS_Shape placedDam = xform.Shape();

        TopoDS_Shape fittedDam;
        QVector<int> contactFaces;
        QString errorMsg;
        if (!BooleanBuilder::fitDamToValley(targetUuid, placedDam, fittedDam, contactFaces, errorMsg))
        {
            QMessageBox::warning(this, tr("贴合失败"), tr("大坝与河谷贴合布尔裁剪失败：%1").arg(errorMsg));
            return;
        }

        GeometryBuildUtils::registerShape(m_uuid, getDisplayName(), new TopoDS_Shape(fittedDam));
        if (auto* body = doc ? doc->getGeomShapeObject(m_uuid) : nullptr)
        {
            body->addFaceGroup({QStringLiteral("土石坝基底接触面组"), QStringLiteral("Contact"), contactFaces});
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

        QMessageBox::information(this, tr("贴合完成"),
            tr("大坝已成功贴合河谷地形，自动裁剪嵌入山体部分，提取河谷接触面 %1 个，已归入【土石坝基底接触面组】。")
            .arg(contactFaces.size()));
        return;
    }

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool ok = service->recreateEarthRockfillDam(
        m_uuid, getDisplayName(),
        hEdit->text().toDouble(),
        crestEdit->text().toDouble(),
        upEdit->text().toDouble(),
        downEdit->text().toDouble(),
        lenEdit->text().toDouble(),
        layerEdit->text().toInt(),
        coreWidthEdit->text().toDouble(),
        readEdit(this, "coreBottomEdit", 22.0),
        readEdit(this, "filterWidthEdit", 4.0),
        readEdit(this, "coreOffsetEdit", 0.0));
    if (!ok)
        QMessageBox::warning(this, tr("生成失败"), tr("土石坝参数无效，无法生成三维实体。"));
}
