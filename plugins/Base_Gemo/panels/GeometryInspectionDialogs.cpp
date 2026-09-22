#include "GeometryInspectionDialogs.h"
#include "ui_GeometryCheckDialog.h"
#include "ui_GeometryMeasureDialog.h"

#include "../builders/GeometryBuildUtils.h"
#include "../core/GeomManagerInstance.h"
#include "../core/GeomShapeObject.h"
#include "../workbench/GeometryEntityTools.h"
#include "../workbench/GeometryInspectionTools.h"
#include "../workbench/GeometryWorkbench.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

namespace
{
QString number(double value)
{
    return QString::number(value, 'g', 10);
}

QString vectorText(const QVector3D& value)
{
    return QStringLiteral("(%1, %2, %3)")
        .arg(number(value.x()))
        .arg(number(value.y()))
        .arg(number(value.z()));
}

void configureTable(QTableWidget* table)
{
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({QObject::tr("项目"), QObject::tr("结果")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setWordWrap(true);
    table->setCornerButtonEnabled(false);
}

void setRows(QTableWidget* table, const QList<QPair<QString, QString>>& rows)
{
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        table->setItem(i, 0, new QTableWidgetItem(rows.at(i).first));
        table->setItem(i, 1, new QTableWidgetItem(rows.at(i).second));
    }
    table->resizeRowsToContents();
}

TopoDS_Shape* runtimeShape(const QString& uuid)
{
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
    if (!object || !object->getShape() || object->getShape()->IsNull())
        return nullptr;
    return object->getShape();
}

QString runtimeName(const QString& uuid)
{
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = manager ? manager->getGeomShapeObject(uuid) : nullptr;
    return object ? object->getName() : QString();
}

void fillObjectCombo(QComboBox* combo,
                     const QList<QPair<QString, QString>>& objects,
                     const QString& preferred)
{
    if (!combo)
        return;

    const QSignalBlocker blocker(combo);
    const QString oldUuid = combo->currentData().toString();
    combo->clear();
    for (const auto& item : objects)
        combo->addItem(item.second, item.first);

    const QString target = !preferred.isEmpty() ? preferred : oldUuid;
    const int index = combo->findData(target);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void fillEntityTypes(QComboBox* combo)
{
    combo->addItem(QObject::tr("整体"), 0);
    combo->addItem(QObject::tr("边"), 1);
    combo->addItem(QObject::tr("面"), 2);
    combo->addItem(QObject::tr("体"), 3);
}
}

GeometryCheckDialog::GeometryCheckDialog(GeometryWorkbench* workbench, QWidget* parent)
    : QWidget(parent), ui(new Ui::GeometryCheckDialog), m_workbench(workbench)
{
    ui->setupUi(this);
    configureTable(ui->reportTable);

    connect(ui->checkButton, &QPushButton::clicked, this, &GeometryCheckDialog::runCheck);
    connect(ui->repairButton, &QPushButton::clicked, this, &GeometryCheckDialog::repairCurrent);
    connect(ui->focusButton, &QPushButton::clicked, this, &GeometryCheckDialog::focusCurrent);
    connect(ui->objectCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int)
            {
                ui->reportTable->setRowCount(0);
                ui->summaryLabel->setText(tr("点击“检查”查看当前对象的几何状态。"));
            });

    refreshObjects();
}

GeometryCheckDialog::~GeometryCheckDialog()
{
    delete ui;
}

QString GeometryCheckDialog::currentUuid() const
{
    return ui->objectCombo ? ui->objectCombo->currentData().toString() : QString();
}

void GeometryCheckDialog::refreshObjects()
{
    if (!m_workbench)
        return;
    fillObjectCombo(ui->objectCombo, m_workbench->geometryObjectChoices(),
                    m_workbench->selectedGeometryUuidForTools());
    ui->reportTable->setRowCount(0);
    ui->summaryLabel->setText(ui->objectCombo->count() > 0
                                  ? tr("点击“检查”查看当前对象的几何状态。")
                                  : tr("当前没有可检查的几何对象。"));
}

void GeometryCheckDialog::runCheck()
{
    const QString uuid = currentUuid();
    TopoDS_Shape* shape = runtimeShape(uuid);
    if (!shape)
    {
        ui->summaryLabel->setText(tr("当前对象没有可检查的几何结果。"));
        ui->reportTable->setRowCount(0);
        return;
    }

    GeometryInspectionTools::CheckOptions options;
    options.smallEdgeLength = ui->smallEdgeSpin->value();
    options.smallFaceArea = ui->smallFaceSpin->value();
    options.repairTolerance = ui->toleranceSpin->value();
    options.sewFaces = ui->sewCheck->isChecked();
    options.unifySameDomain = ui->unifyCheck->isChecked();
    m_report = GeometryInspectionTools::inspect(*shape, options);
    fillReportTable();
}

void GeometryCheckDialog::fillReportTable()
{
    const auto& report = m_report;
    const QString state = report.valid
        ? (report.closed ? tr("检查通过：有效封闭实体。") : tr("几何有效，但不是封闭实体。"))
        : tr("检查未通过：建议查看问题列表或尝试自动修复。");
    ui->summaryLabel->setText(state);
    ui->summaryLabel->setStyleSheet(report.valid
                                      ? QStringLiteral("color:#217346;font-weight:600;")
                                      : QStringLiteral("color:#b42318;font-weight:600;"));

    QList<QPair<QString, QString>> rows = {
        {tr("几何类型"), report.shapeType},
        {tr("几何有效"), report.valid ? tr("是") : tr("否")},
        {tr("BRep 拓扑检查"), report.brepValid ? tr("通过") : tr("失败")},
        {tr("Boolean / 自干涉检查"), report.bopValid ? tr("通过") : tr("失败")},
        {tr("封闭实体"), report.closed ? tr("是") : tr("否")},
        {tr("点 / 边 / 线框"), tr("%1 / %2 / %3").arg(report.vertexCount).arg(report.edgeCount).arg(report.wireCount)},
        {tr("面 / 壳 / 体"), tr("%1 / %2 / %3").arg(report.faceCount).arg(report.shellCount).arg(report.solidCount)},
        {tr("开放边"), QString::number(report.freeEdgeCount)},
        {tr("非流形边"), QString::number(report.nonManifoldEdgeCount)},
        {tr("退化边"), QString::number(report.degeneratedEdgeCount)},
        {tr("小边 / 小面"), tr("%1 / %2").arg(report.smallEdgeCount).arg(report.smallFaceCount)},
        {tr("总边长"), number(report.totalLength)},
        {tr("总面积"), number(report.totalArea)},
        {tr("总体积"), number(report.totalVolume)},
        {tr("质心"), vectorText(report.center)},
        {tr("包围盒最小"), vectorText(report.boundsMin)},
        {tr("包围盒最大"), vectorText(report.boundsMax)},
        {tr("最大误差"), number(report.maxTolerance)},
        {tr("问题"), report.issues.isEmpty() ? tr("无") : report.issues.join(QStringLiteral("\n"))}
    };
    setRows(ui->reportTable, rows);
}

void GeometryCheckDialog::repairCurrent()
{
    const QString uuid = currentUuid();
    TopoDS_Shape* shape = runtimeShape(uuid);
    if (!shape)
        return;

    GeometryInspectionTools::CheckOptions options;
    options.smallEdgeLength = ui->smallEdgeSpin->value();
    options.smallFaceArea = ui->smallFaceSpin->value();
    options.repairTolerance = ui->toleranceSpin->value();
    options.sewFaces = ui->sewCheck->isChecked();
    options.unifySameDomain = ui->unifyCheck->isChecked();

    TopoDS_Shape repaired;
    QStringList actions;
    QString error;
    if (!GeometryInspectionTools::repair(*shape, options, repaired, actions, error))
    {
        ui->summaryLabel->setText(tr("修复失败：%1").arg(error));
        ui->summaryLabel->setStyleSheet(
            QStringLiteral("color:#b42318;font-weight:600;"));
        return;
    }

    const QString name = runtimeName(uuid);
    GeometryBuildUtils::registerShape(uuid, name, new TopoDS_Shape(repaired));
    if (m_workbench)
    {
        m_workbench->refreshTreePresentation();
        m_workbench->focusGeometryForTools(uuid);
    }

    runCheck();
    ui->summaryLabel->setText(
        actions.isEmpty() ? tr("修复完成：几何已修复。")
                          : tr("修复完成：%1").arg(actions.join(QStringLiteral("；"))));
    ui->summaryLabel->setStyleSheet(
        QStringLiteral("color:#217346;font-weight:600;"));
}

void GeometryCheckDialog::focusCurrent()
{
    if (m_workbench)
        m_workbench->focusGeometryForTools(currentUuid());
}

GeometryMeasureDialog::GeometryMeasureDialog(GeometryWorkbench* workbench, QWidget* parent)
    : QWidget(parent), ui(new Ui::GeometryMeasureDialog), m_workbench(workbench)
{
    ui->setupUi(this);
    fillEntityTypes(ui->singleTypeCombo);
    fillEntityTypes(ui->typeACombo);
    fillEntityTypes(ui->typeBCombo);
    configureTable(ui->singleTable);
    configureTable(ui->pairTable);

    connect(ui->singleObjectCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &GeometryMeasureDialog::refreshSingleIds);
    connect(ui->singleTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &GeometryMeasureDialog::refreshSingleIds);
    connect(ui->singleMeasureButton, &QPushButton::clicked, this, [this]
    {
        refreshSingleIds();
        refreshSingleInfo();
    });
    connect(ui->focusButton, &QPushButton::clicked,
            this, &GeometryMeasureDialog::focusSingleObject);
    auto pairChanged = [this](int) { refreshPairIds(); };
    connect(ui->objectACombo, qOverload<int>(&QComboBox::currentIndexChanged), this, pairChanged);
    connect(ui->typeACombo, qOverload<int>(&QComboBox::currentIndexChanged), this, pairChanged);
    connect(ui->objectBCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, pairChanged);
    connect(ui->typeBCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, pairChanged);
    connect(ui->calculateButton, &QPushButton::clicked, this, [this]
    {
        refreshPairIds();
        calculatePair();
    });
    refreshObjects();
}

GeometryMeasureDialog::~GeometryMeasureDialog()
{
    clearHighlights();
    delete ui;
}

QString GeometryMeasureDialog::comboUuid(QComboBox* combo) const
{
    return combo ? combo->currentData().toString() : QString();
}

int GeometryMeasureDialog::comboDim(QComboBox* combo) const
{
    return combo ? combo->currentData().toInt() : 0;
}

void GeometryMeasureDialog::refreshObjects()
{
    if (!m_workbench)
        return;
    const auto objects = m_workbench->geometryObjectChoices();
    const QString selected = m_workbench->selectedGeometryUuidForTools();
    fillObjectCombo(ui->singleObjectCombo, objects, selected);
    fillObjectCombo(ui->objectACombo, objects, selected);
    fillObjectCombo(ui->objectBCombo, objects, QString());
    if (ui->objectBCombo->count() > 1 && ui->objectBCombo->currentIndex() == ui->objectACombo->currentIndex())
    {
        const QSignalBlocker blocker(ui->objectBCombo);
        ui->objectBCombo->setCurrentIndex(1);
    }
    // 打开测量工具时只列出对象，不立即遍历 OCC 子实体。
    // 边/面/体索引在用户真正选择类型或点击“测量”时再生成，避免工具一打开
    // 就对大型或尚未完全恢复的 STEP 做拓扑遍历。
    ui->singleIdCombo->clear();
    ui->idACombo->clear();
    ui->idBCombo->clear();
    ui->singleTable->setRowCount(0);
    ui->pairTable->setRowCount(0);
}

void GeometryMeasureDialog::fillIdCombo(QComboBox* objectCombo, QComboBox* typeCombo, QComboBox* idCombo)
{
    const QSignalBlocker blocker(idCombo);
    idCombo->clear();
    TopoDS_Shape* shape = runtimeShape(comboUuid(objectCombo));
    if (!shape)
        return;

    const int dim = comboDim(typeCombo);
    if (dim == 0)
    {
        idCombo->addItem(QObject::tr("整体"), 1);
        return;
    }

    TopAbs_ShapeEnum type = TopAbs_EDGE;
    if (dim == 2)
        type = TopAbs_FACE;
    else if (dim == 3)
        type = TopAbs_SOLID;

    TopTools_IndexedMapOfShape indexed;
    TopExp::MapShapes(*shape, type, indexed);
    for (int i = 1; i <= indexed.Extent(); ++i)
        idCombo->addItem(QString::number(i), i);
}

void GeometryMeasureDialog::refreshSingleIds()
{
    fillIdCombo(ui->singleObjectCombo, ui->singleTypeCombo, ui->singleIdCombo);
    ui->singleTable->setRowCount(0);
}

void GeometryMeasureDialog::refreshSingleInfo()
{
    GeometryEntityTools::clearEntityHighlight();

    const QString uuid = comboUuid(ui->singleObjectCombo);
    TopoDS_Shape* shape = runtimeShape(uuid);
    const int dim = comboDim(ui->singleTypeCombo);
    const int id = ui->singleIdCombo->currentData().toInt();
    if (!shape || id <= 0)
    {
        ui->singleTable->setRowCount(0);
        return;
    }

    const auto info = GeometryInspectionTools::entityInfo(*shape, dim, id);
    if (!info.valid)
    {
        ui->singleTable->setRowCount(0);
        return;
    }

    QList<QPair<QString, QString>> rows = {
        {tr("实体"), info.typeName},
        {tr("几何类型"), info.geometryType},
        {dim == 0 ? tr("主测量值") : dim == 1 ? tr("长度") : dim == 2 ? tr("面积") : tr("体积"), number(info.measure)},
        {tr("中心"), vectorText(info.center)},
        {tr("包围盒最小"), vectorText(info.boundsMin)},
        {tr("包围盒最大"), vectorText(info.boundsMax)}
    };
    for (const QString& property : info.properties)
        rows.append({tr("属性"), property});
    setRows(ui->singleTable, rows);

    if (dim > 0)
        GeometryEntityTools::highlightEntities(QStringList{uuid}, dim, QList<int>{id}, false);
}

void GeometryMeasureDialog::refreshPairIds()
{
    fillIdCombo(ui->objectACombo, ui->typeACombo, ui->idACombo);
    fillIdCombo(ui->objectBCombo, ui->typeBCombo, ui->idBCombo);
}

void GeometryMeasureDialog::calculatePair()
{
    GeometryEntityTools::clearEntityHighlight();

    const QString uuidA = comboUuid(ui->objectACombo);
    const QString uuidB = comboUuid(ui->objectBCombo);
    TopoDS_Shape* shapeA = runtimeShape(uuidA);
    TopoDS_Shape* shapeB = runtimeShape(uuidB);
    const int dimA = comboDim(ui->typeACombo);
    const int dimB = comboDim(ui->typeBCombo);
    const int idA = ui->idACombo->currentData().toInt();
    const int idB = ui->idBCombo->currentData().toInt();
    if (!shapeA || !shapeB || idA <= 0 || idB <= 0)
        return;

    const auto result = GeometryInspectionTools::measurePair(*shapeA, dimA, idA, *shapeB, dimB, idB);
    if (!result.valid)
    {
        setRows(ui->pairTable, {{tr("结果"), result.message}});
        return;
    }

    QList<QPair<QString, QString>> rows = {
        {tr("最短距离"), number(result.distance)},
        {tr("最近点 A"), vectorText(result.pointA)},
        {tr("最近点 B"), vectorText(result.pointB)},
        {tr("夹角"), result.hasAngle ? tr("%1°").arg(number(result.angleDeg)) : tr("—")}
    };
    setRows(ui->pairTable, rows);

    if (dimA > 0)
        GeometryEntityTools::highlightEntities(QStringList{uuidA}, dimA, QList<int>{idA}, false);
    if (dimB > 0)
        GeometryEntityTools::highlightEntities(QStringList{uuidB}, dimB, QList<int>{idB}, true);
}

void GeometryMeasureDialog::focusSingleObject()
{
    if (m_workbench)
        m_workbench->focusGeometryForTools(comboUuid(ui->singleObjectCombo));
}

void GeometryMeasureDialog::clearHighlights()
{
    GeometryEntityTools::clearEntityHighlight();
}
