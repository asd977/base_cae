#include "BooleanPanel.h"


#include "CadDocument.h"
#include "ui_OccBooleanCommonWidget.h"
#include "ui_OccBooleanCutWidget.h"
#include "ui_OccBooleanFuseWidget.h"

#include <GeometryServiceAccess.h>
#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include "../workbench/GeometryWorkbench.h"
#include "../workbench/GeometrySelectionVisuals.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QShortcut>
#include <QLabel>

#include <BRepCheck_Analyzer.hxx>
#include <Standard_Failure.hxx>

#include <algorithm>
#include <sstream>


namespace
{
bool discardUnappliedGeometryNode(const QString& uuid)
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    auto* workbench = dynamic_cast<GeometryWorkbench*>(pre);
    return workbench && workbench->discardUnappliedGeometryNode(uuid);
}

void refreshGeometryTreePresentation(const QString& changedUuid)
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
        workbench->refreshTreePresentation(changedUuid);
}

void showBooleanWarning(QWidget* owner, const QString& message)
{
    QMessageBox::warning(owner, QObject::tr("输入错误"), message);
}

QStringList uniqueKeys(const QStringList& keys, const QString& selfUuid = QString())
{
    QStringList out;
    for (const QString& key : keys)
    {
        if (key.isEmpty() || key == selfUuid || out.contains(key))
            continue;
        out.append(key);
    }
    return out;
}

QStringList readKeyArray(const QJsonObject& obj, const QString& name)
{
    QStringList keys;
    const QJsonArray arr = obj.value(name).toArray();
    for (const auto& value : arr)
    {
        const QString key = value.toString();
        if (!key.isEmpty() && !keys.contains(key))
            keys.append(key);
    }
    return keys;
}

void writeKeyArray(QJsonObject& obj, const QString& name, const QStringList& keys)
{
    QJsonArray arr;
    for (const QString& key : keys)
        arr.append(key);
    obj[name] = arr;
}

QString objectDisplayName(GeomShapeObject* obj, const QString& fallbackKey)
{
    QString displayName = obj ? obj->getName() : QString();
    if (displayName.isEmpty())
    {
        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
        if (ProjectNode* node = pm ? pm->findNodeByUuid(fallbackKey) : nullptr)
            displayName = node->getName().isEmpty() ? node->text(0) : node->getName();
    }
    if (displayName.isEmpty())
        displayName = fallbackKey;
    return displayName;
}

bool hasUsableShape(GeomShapeObject* obj)
{
    return obj && obj->getShape() && !obj->getShape()->IsNull();
}

void fillSourceCombo(QComboBox* combo,
                     const QHash<QString, GeomShapeObject*>& geomMap,
                     const QString& selfUuid)
{
    if (!combo)
        return;

    combo->clear();
    for (auto it = geomMap.constBegin(); it != geomMap.constEnd(); ++it)
    {
        const QString& key = it.key();
        if (key.isEmpty() || key == selfUuid)
            continue;

        GeomShapeObject* obj = it.value();
        if (!hasUsableShape(obj))
            continue;

        combo->addItem(objectDisplayName(obj, key), key);
    }
}

QStringList validSelection(const QStringList& selectedKeys,
                           const QHash<QString, GeomShapeObject*>& geomMap,
                           const QString& selfUuid)
{
    QStringList validKeys;
    for (const QString& key : selectedKeys)
    {
        if (key.isEmpty() || key == selfUuid || validKeys.contains(key))
            continue;
        if (hasUsableShape(geomMap.value(key, nullptr)))
            validKeys.append(key);
    }
    return validKeys;
}

void fillSelectedList(QListWidget* list,
                      QStringList& selectedKeys,
                      const QHash<QString, GeomShapeObject*>& geomMap,
                      const QString& selfUuid,
                      bool showCutRole)
{
    if (!list)
        return;

    list->clear();
    // 列表展示必须保留工程中保存的弱引用。来源几何暂时尚未恢复，或已经
    // 被删除时，只标记为不可用，不能直接从 selectedKeys 中移除，否则再次
    // 保存工程会永久丢失原来的布尔历史。
    selectedKeys = uniqueKeys(selectedKeys, selfUuid);

    for (int i = 0; i < selectedKeys.size(); ++i)
    {
        const QString& key = selectedKeys.at(i);
        GeomShapeObject* object = geomMap.value(key, nullptr);
        const bool usable = hasUsableShape(object);
        QString displayName = objectDisplayName(object, key);
        if (!usable)
            displayName = QObject::tr("%1（来源不可用）").arg(displayName);
        if (showCutRole)
        {
            displayName = (i == 0)
                              ? QObject::tr("目标：%1").arg(displayName)
                              : QObject::tr("工具%1：%2").arg(i).arg(displayName);
        }

        auto* item = new QListWidgetItem(displayName, list);
        item->setData(Qt::UserRole, key);
        if (!usable)
            item->setToolTip(QObject::tr("来源关系已保留；上游几何恢复后可继续使用。"));
    }
}

void loadLegacyPairKeys(const QJsonObject& obj, QStringList& selectedKeys)
{
    if (!selectedKeys.isEmpty())
        return;

    const QString keyA = obj.value(QStringLiteral("keyA")).toString();
    const QString keyB = obj.value(QStringLiteral("keyB")).toString();
    if (!keyA.isEmpty())
        selectedKeys.append(keyA);
    if (!keyB.isEmpty() && keyB != keyA)
        selectedKeys.append(keyB);
}

void normalizeLoadedKeys(QStringList& selectedKeys,
                         QStringList& operationKeys,
                         const QString& selfUuid)
{
    const bool selectedOnlySelf = (selectedKeys.size() == 1 && selectedKeys.first() == selfUuid);
    selectedKeys = uniqueKeys(selectedKeys, selfUuid);
    operationKeys = uniqueKeys(operationKeys, selfUuid);

    // 老版本保存后会把 selectedKeys 改成“结果自身 uuid”。重新打开后继续编辑时，
    // 不能把结果自身再作为布尔输入，否则再次应用会变成“旧结果 + 新对象”参与运算。
    if ((selectedKeys.isEmpty() || selectedOnlySelf) && !operationKeys.isEmpty())
        selectedKeys = operationKeys;
    if (operationKeys.isEmpty() && !selectedKeys.isEmpty())
        operationKeys = selectedKeys;
}

QJsonObject saveBooleanState(GeomManager::GeomManagerInstance* mgr,
                             const QString& selfUuid,
                             const QStringList& selectedKeys,
                             const QStringList& operationKeys)
{
    QJsonObject obj;
    const QStringList currentSelection = uniqueKeys(selectedKeys, selfUuid);
    const QStringList sourceKeys = operationKeys.isEmpty()
                                       ? currentSelection
                                       : uniqueKeys(operationKeys, selfUuid);

    writeKeyArray(obj, QStringLiteral("selectedKeys"), currentSelection.isEmpty() ? sourceKeys : currentSelection);
    writeKeyArray(obj, QStringLiteral("operationKeys"), sourceKeys);

    // 不再在面板状态里重复保存布尔结果 BREP。
    // 工程保存时 GeometryWorkbench 已经把运行时 shape 写入 FeatureData::cachedBrep。
    // 旧逻辑每次刷新模型树/重命名都会调用 saveJson()，这里反复序列化复杂布尔结果，
    // 操作次数多后会导致整套几何模块明显卡顿。
    Q_UNUSED(mgr);
    Q_UNUSED(selfUuid);
    obj[QStringLiteral("resultBrep")] = QString();
    return obj;
}

void requestVisualRender()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

QString booleanPreviewFaceId(const QString& ownerUuid)
{
    return ownerUuid + QStringLiteral("_feature_preview_boolean_face");
}

QString booleanPreviewEdgeId(const QString& ownerUuid)
{
    return ownerUuid + QStringLiteral("_feature_preview_boolean_edge");
}

void clearBooleanShapePreview(const QString& ownerUuid, bool renderNow)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(booleanPreviewFaceId(ownerUuid), false);
        vis->removeProp(booleanPreviewEdgeId(ownerUuid), renderNow);
    }
}

void syncBooleanInputHighlight(const QStringList& keys)
{
    GeometrySelectionVisuals::clearSourcePreviewHighlight(false);
    for (const QString& key : keys)
        GeometrySelectionVisuals::applySourcePreviewHighlight(key);
    requestVisualRender();
}

void clearBooleanInputHighlight()
{
    GeometrySelectionVisuals::clearSourcePreviewHighlight(true);
}

void hideBooleanSourceObjects(const QStringList& sourceKeys, const QString& resultUuid)
{
    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    auto* workbench = dynamic_cast<GeometryWorkbench*>(pre);
    if (!workbench)
        return;

    // 与 FreeCAD 等 CAD 的布尔操作一致：结果生成后默认只显示结果，
    // 隐藏目标和工具。来源仍保留在模型树中，用户可随时重新勾选。
    // 这样也避免“差集结果 + 被切工具”同时显示时的完全贴合/Z-fighting，
    // 这种情况视觉上很像模型透明或隐藏线穿透。
    for (const QString& key : uniqueKeys(sourceKeys, resultUuid))
        workbench->setNodeVisible(key, false);

    workbench->setNodeVisible(resultUuid, true);
}

int firstSelectedRow(QListWidget* list)
{
    if (!list)
        return -1;

    const QList<QListWidgetItem*> items = list->selectedItems();
    if (!items.isEmpty())
        return list->row(items.first());
    return list->currentRow();
}

void moveStringListItem(QStringList& list, int from, int to)
{
    if (from < 0 || from >= list.size() || to < 0 || to >= list.size() || from == to)
        return;

    const QString value = list.takeAt(from);
    list.insert(to, value);
}


void warnIfBooleanResultInvalid(QWidget* owner, const QString& resultUuid)
{
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = manager ? manager->getGeomShapeObject(resultUuid) : nullptr;
    if (!hasUsableShape(object))
        return;

    try
    {
        BRepCheck_Analyzer analyzer(*(object->getShape()), Standard_False);
        if (!analyzer.IsValid())
        {
            QMessageBox::warning(
                owner,
                QObject::tr("几何警告"),
                QObject::tr("结果已经生成，但几何检查未通过。建议先进行几何修复，再用于网格划分。"));
        }
    }
    catch (const Standard_Failure&)
    {
        // 布尔结果已经生成；检查器自身异常不能把成功操作改成失败。
    }
    catch (...)
    {
    }
}

QString cutSelectionSummary(const QStringList& keys, const QHash<QString, GeomShapeObject*>& geomMap)
{
    if (keys.isEmpty())
        return QString();

    const QString targetName = objectDisplayName(geomMap.value(keys.first(), nullptr), keys.first());
    QStringList toolNames;
    for (int i = 1; i < keys.size(); ++i)
        toolNames.append(objectDisplayName(geomMap.value(keys.at(i), nullptr), keys.at(i)));

    return QObject::tr("当前目标：%1\n切割工具：%2")
        .arg(targetName, toolNames.join(QObject::tr("、")));
}
} // namespace

OccBooleanFuseWidget::OccBooleanFuseWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean), ui(new Ui::OccBooleanFuseWidget)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    connect(ui->addButton, &QPushButton::clicked, this, &OccBooleanFuseWidget::onAddClicked);
    connect(ui->selectedListWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onRemoveSelectedClicked(); });
    auto* removeShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), ui->selectedListWidget);
    removeShortcut->setContext(Qt::WidgetShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &OccBooleanFuseWidget::onRemoveSelectedClicked);
    connect(ui->applyButton, &QPushButton::clicked, this, &OccBooleanFuseWidget::onApplyClicked);
}

OccBooleanFuseWidget::~OccBooleanFuseWidget()
{
    delete ui;
}

void OccBooleanFuseWidget::beforeLoad()
{
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    if (m_committedKeys.isEmpty())
        m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    updatePreview();
}

void OccBooleanFuseWidget::beforeUnload()
{
    clearBooleanInputHighlight();
    clearPreview(true);
}

QJsonObject OccBooleanFuseWidget::saveJson()
{
    return saveBooleanState(m_geomMgr, getUuid(), m_selectedKeys, m_operationKeys);
}

void OccBooleanFuseWidget::loadJson(const QJsonObject& obj)
{
    m_selectedKeys = readKeyArray(obj, QStringLiteral("selectedKeys"));
    loadLegacyPairKeys(obj, m_selectedKeys);
    m_operationKeys = readKeyArray(obj, QStringLiteral("operationKeys"));
    normalizeLoadedKeys(m_selectedKeys, m_operationKeys, getUuid());
    m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    m_savedResultBrep = obj.value(QStringLiteral("resultBrep")).toString();

    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
}

void OccBooleanFuseWidget::onAddClicked()
{
    const QString key = ui->sourceComboBox->currentData().toString();
    if (key.isEmpty() || key == getUuid() || m_selectedKeys.contains(key))
        return;

    m_selectedKeys.append(key);
    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanFuseWidget::onRemoveSelectedClicked()
{
    const QList<QListWidgetItem*> items = ui->selectedListWidget->selectedItems();
    for (QListWidgetItem* item : items)
        m_selectedKeys.removeAll(item->data(Qt::UserRole).toString());

    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanFuseWidget::onApplyClicked()
{
    refreshGeometryMap();
    const QStringList sourceKeys = validSelection(m_selectedKeys, m_geomMap, getUuid());
    if (sourceKeys.size() < 2)
    {
        showBooleanWarning(this, tr("至少选择两个有效对象。"));
        return;
    }

    const QString resultUuid = getUuid();
    const QString resultName = getDisplayName().isEmpty() ? tr("布尔并集") : getDisplayName();

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool success = service->recreateBooleanFuse(resultUuid, resultName, sourceKeys, sourceKeys, QString());
    if (!success)
    {
        QMessageBox::warning(this, tr("生成失败"), tr("布尔并集失败：请检查输入对象是否相交/接触、是否存在自交或无效面。"));
        return;
    }

    m_operationKeys = sourceKeys;
    m_selectedKeys = sourceKeys;
    m_committedKeys = sourceKeys;
    hideBooleanSourceObjects(sourceKeys, resultUuid);
    clearPreview(false);
    warnIfBooleanResultInvalid(this, resultUuid);
    refreshGeometryTreePresentation(getUuid());
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanFuseWidget::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    m_selectedKeys = m_committedKeys;
    m_operationKeys = m_committedKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    clearPreview(true);
}

void OccBooleanFuseWidget::restoreSavedResult()
{
    if (m_savedResultBrep.isEmpty())
        return;

    refreshGeometryMap();
    if (!m_geomMgr || m_geomMap.contains(getUuid()))
        return;

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    if (!service->recreateBooleanFuse(getUuid(), getDisplayName(), QStringList(), m_operationKeys, m_savedResultBrep))
        return;

    if (m_selectedKeys.isEmpty())
        m_selectedKeys = m_operationKeys;
    m_committedKeys = m_operationKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanFuseWidget::refreshGeometryMap()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr)
    {
        m_geomMap.clear();
        return;
    }
    m_geomMap = m_geomMgr->getShapMap();
}

void OccBooleanFuseWidget::refreshSourceCombo()
{
    fillSourceCombo(ui->sourceComboBox, m_geomMap, getUuid());
}

void OccBooleanFuseWidget::refreshSelectedList()
{
    fillSelectedList(ui->selectedListWidget, m_selectedKeys, m_geomMap, getUuid(), false);
    ui->applyButton->setEnabled(validSelection(m_selectedKeys, m_geomMap, getUuid()).size() >= 2);
    syncBooleanInputHighlight(m_selectedKeys);
    updatePreview();
}

void OccBooleanFuseWidget::updatePreview()
{
    // 布尔运算不做结果预览：复杂 OCC 运算只在点击“应用”时执行。
    clearPreview(false);
}

void OccBooleanFuseWidget::clearPreview(bool renderNow)
{
    clearBooleanShapePreview(getUuid(), renderNow);
}

void OccBooleanFuseWidget::requestRender()
{
    requestVisualRender();
}

OccBooleanCutWidget::OccBooleanCutWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean), ui(new Ui::OccBooleanCutWidget)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    connect(ui->addButton, &QPushButton::clicked, this, &OccBooleanCutWidget::onAddClicked);
    connect(ui->selectedListWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onRemoveSelectedClicked(); });
    auto* removeShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), ui->selectedListWidget);
    removeShortcut->setContext(Qt::WidgetShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &OccBooleanCutWidget::onRemoveSelectedClicked);
    connect(ui->applyButton, &QPushButton::clicked, this, &OccBooleanCutWidget::onApplyClicked);
}

OccBooleanCutWidget::~OccBooleanCutWidget()
{
    delete ui;
}

void OccBooleanCutWidget::beforeLoad()
{
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    if (m_committedKeys.isEmpty())
        m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    updatePreview();
}

void OccBooleanCutWidget::beforeUnload()
{
    clearBooleanInputHighlight();
    clearPreview(true);
}

QJsonObject OccBooleanCutWidget::saveJson()
{
    return saveBooleanState(m_geomMgr, getUuid(), m_selectedKeys, m_operationKeys);
}

void OccBooleanCutWidget::loadJson(const QJsonObject& obj)
{
    m_selectedKeys = readKeyArray(obj, QStringLiteral("selectedKeys"));
    loadLegacyPairKeys(obj, m_selectedKeys);
    m_operationKeys = readKeyArray(obj, QStringLiteral("operationKeys"));
    normalizeLoadedKeys(m_selectedKeys, m_operationKeys, getUuid());
    m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    m_savedResultBrep = obj.value(QStringLiteral("resultBrep")).toString();

    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
}

void OccBooleanCutWidget::onAddClicked()
{
    const QString key = ui->sourceComboBox->currentData().toString();
    if (key.isEmpty() || key == getUuid() || m_selectedKeys.contains(key))
        return;

    m_selectedKeys.append(key);
    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCutWidget::onRemoveSelectedClicked()
{
    const QList<QListWidgetItem*> items = ui->selectedListWidget->selectedItems();
    for (QListWidgetItem* item : items)
        m_selectedKeys.removeAll(item->data(Qt::UserRole).toString());

    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCutWidget::onSetTargetClicked()
{
    m_selectedKeys = uniqueKeys(m_selectedKeys, getUuid());

    const int row = firstSelectedRow(ui->selectedListWidget);
    if (row <= 0 || row >= m_selectedKeys.size())
        return;

    moveStringListItem(m_selectedKeys, row, 0);
    m_operationKeys.clear();
    refreshSelectedList();
    ui->selectedListWidget->setCurrentRow(0);
}

void OccBooleanCutWidget::onMoveUpClicked()
{
    m_selectedKeys = uniqueKeys(m_selectedKeys, getUuid());

    const int row = firstSelectedRow(ui->selectedListWidget);
    if (row <= 0 || row >= m_selectedKeys.size())
        return;

    moveStringListItem(m_selectedKeys, row, row - 1);
    m_operationKeys.clear();
    refreshSelectedList();
    ui->selectedListWidget->setCurrentRow(row - 1);
}

void OccBooleanCutWidget::onMoveDownClicked()
{
    m_selectedKeys = uniqueKeys(m_selectedKeys, getUuid());

    const int row = firstSelectedRow(ui->selectedListWidget);
    if (row < 0 || row >= m_selectedKeys.size() - 1)
        return;

    moveStringListItem(m_selectedKeys, row, row + 1);
    m_operationKeys.clear();
    refreshSelectedList();
    ui->selectedListWidget->setCurrentRow(row + 1);
}

void OccBooleanCutWidget::onApplyClicked()
{
    refreshGeometryMap();
    const QStringList sourceKeys = validSelection(m_selectedKeys, m_geomMap, getUuid());
    if (sourceKeys.size() < 2)
    {
        showBooleanWarning(this, tr("差集至少需要 1 个目标对象和 1 个切割工具。"));
        return;
    }

    const QString resultUuid = getUuid();
    const QString resultName = getDisplayName().isEmpty() ? tr("布尔差集") : getDisplayName();

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool success = service->recreateBooleanCut(resultUuid, resultName, sourceKeys, sourceKeys, QString());
    if (!success)
    {
        QMessageBox::warning(
            this,
            tr("生成失败"),
            tr("布尔差集失败。\n%1\n\n请确认列表第一个对象确实是被切目标；如果顺序不对，选中被切对象后点击“设为目标”。另外目标和工具需要真实相交，且最好都是实体或同维度几何。")
                .arg(cutSelectionSummary(sourceKeys, m_geomMap)));
        return;
    }

    m_operationKeys = sourceKeys;
    m_selectedKeys = sourceKeys;
    m_committedKeys = sourceKeys;
    hideBooleanSourceObjects(sourceKeys, resultUuid);
    clearPreview(false);
    warnIfBooleanResultInvalid(this, resultUuid);
    refreshGeometryTreePresentation(getUuid());
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanCutWidget::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    m_selectedKeys = m_committedKeys;
    m_operationKeys = m_committedKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    clearPreview(true);
}

void OccBooleanCutWidget::restoreSavedResult()
{
    if (m_savedResultBrep.isEmpty())
        return;

    refreshGeometryMap();
    if (!m_geomMgr || m_geomMap.contains(getUuid()))
        return;

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    if (!service->recreateBooleanCut(getUuid(), getDisplayName(), QStringList(), m_operationKeys, m_savedResultBrep))
        return;

    if (m_selectedKeys.isEmpty())
        m_selectedKeys = m_operationKeys;
    m_committedKeys = m_operationKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanCutWidget::refreshGeometryMap()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr)
    {
        m_geomMap.clear();
        return;
    }
    m_geomMap = m_geomMgr->getShapMap();
}

void OccBooleanCutWidget::refreshSourceCombo()
{
    fillSourceCombo(ui->sourceComboBox, m_geomMap, getUuid());
}

void OccBooleanCutWidget::refreshSelectedList()
{
    fillSelectedList(ui->selectedListWidget, m_selectedKeys, m_geomMap, getUuid(), true);
    ui->applyButton->setEnabled(validSelection(m_selectedKeys, m_geomMap, getUuid()).size() >= 2);
    syncBooleanInputHighlight(m_selectedKeys);
    updatePreview();
}

void OccBooleanCutWidget::updatePreview()
{
    // 布尔运算不做结果预览：复杂 OCC 运算只在点击“应用”时执行。
    clearPreview(false);
}

void OccBooleanCutWidget::clearPreview(bool renderNow)
{
    clearBooleanShapePreview(getUuid(), renderNow);
}

void OccBooleanCutWidget::requestRender()
{
    requestVisualRender();
}

OccBooleanCommonWidget::OccBooleanCommonWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean), ui(new Ui::OccBooleanCommonWidget)
{
    ui->setupUi(this);
    if (parent)
        setParent(parent);

    connect(ui->addButton, &QPushButton::clicked, this, &OccBooleanCommonWidget::onAddClicked);
    connect(ui->selectedListWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onRemoveSelectedClicked(); });
    auto* removeShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), ui->selectedListWidget);
    removeShortcut->setContext(Qt::WidgetShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &OccBooleanCommonWidget::onRemoveSelectedClicked);
    connect(ui->applyButton, &QPushButton::clicked, this, &OccBooleanCommonWidget::onApplyClicked);
}

OccBooleanCommonWidget::~OccBooleanCommonWidget()
{
    delete ui;
}

void OccBooleanCommonWidget::beforeLoad()
{
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    if (m_committedKeys.isEmpty())
        m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    updatePreview();
}

void OccBooleanCommonWidget::beforeUnload()
{
    clearBooleanInputHighlight();
    clearPreview(true);
}

QJsonObject OccBooleanCommonWidget::saveJson()
{
    return saveBooleanState(m_geomMgr, getUuid(), m_selectedKeys, m_operationKeys);
}

void OccBooleanCommonWidget::loadJson(const QJsonObject& obj)
{
    m_selectedKeys = readKeyArray(obj, QStringLiteral("selectedKeys"));
    loadLegacyPairKeys(obj, m_selectedKeys);
    m_operationKeys = readKeyArray(obj, QStringLiteral("operationKeys"));
    normalizeLoadedKeys(m_selectedKeys, m_operationKeys, getUuid());
    m_committedKeys = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    m_savedResultBrep = obj.value(QStringLiteral("resultBrep")).toString();

    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onAddClicked()
{
    const QString key = ui->sourceComboBox->currentData().toString();
    if (key.isEmpty() || key == getUuid() || m_selectedKeys.contains(key))
        return;

    m_selectedKeys.append(key);
    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onRemoveSelectedClicked()
{
    const QList<QListWidgetItem*> items = ui->selectedListWidget->selectedItems();
    for (QListWidgetItem* item : items)
        m_selectedKeys.removeAll(item->data(Qt::UserRole).toString());

    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onApplyClicked()
{
    refreshGeometryMap();
    const QStringList sourceKeys = validSelection(m_selectedKeys, m_geomMap, getUuid());
    if (sourceKeys.size() < 2)
    {
        showBooleanWarning(this, tr("至少选择两个有效对象。"));
        return;
    }

    const QString resultUuid = getUuid();
    const QString resultName = getDisplayName().isEmpty() ? tr("布尔交集") : getDisplayName();

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    const bool success = service->recreateBooleanCommon(resultUuid, resultName, sourceKeys, sourceKeys, QString());
    if (!success)
    {
        QMessageBox::warning(this, tr("生成失败"), tr("交集失败：对象可能没有共同重叠区域，或输入几何存在问题。"));
        return;
    }

    m_operationKeys = sourceKeys;
    m_selectedKeys = sourceKeys;
    m_committedKeys = sourceKeys;
    hideBooleanSourceObjects(sourceKeys, resultUuid);
    clearPreview(false);
    warnIfBooleanResultInvalid(this, resultUuid);
    refreshGeometryTreePresentation(getUuid());
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanCommonWidget::onCancelClicked()
{
    if (discardUnappliedGeometryNode(getUuid()))
        return;
    m_selectedKeys = m_committedKeys;
    m_operationKeys = m_committedKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    clearPreview(true);
}

void OccBooleanCommonWidget::restoreSavedResult()
{
    if (m_savedResultBrep.isEmpty())
        return;

    refreshGeometryMap();
    if (!m_geomMgr || m_geomMap.contains(getUuid()))
        return;

    IGeometryService* service = requireGeometryService(this);
    if (!service)
        return;

    if (!service->recreateBooleanCommon(getUuid(), getDisplayName(), QStringList(), m_operationKeys, m_savedResultBrep))
        return;

    if (m_selectedKeys.isEmpty())
        m_selectedKeys = m_operationKeys;
    m_committedKeys = m_operationKeys;
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanCommonWidget::refreshGeometryMap()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr)
    {
        m_geomMap.clear();
        return;
    }
    m_geomMap = m_geomMgr->getShapMap();
}

void OccBooleanCommonWidget::refreshSourceCombo()
{
    fillSourceCombo(ui->sourceComboBox, m_geomMap, getUuid());
}

void OccBooleanCommonWidget::refreshSelectedList()
{
    fillSelectedList(ui->selectedListWidget, m_selectedKeys, m_geomMap, getUuid(), false);
    ui->applyButton->setEnabled(validSelection(m_selectedKeys, m_geomMap, getUuid()).size() >= 2);
    syncBooleanInputHighlight(m_selectedKeys);
    updatePreview();
}

void OccBooleanCommonWidget::updatePreview()
{
    // 布尔运算不做结果预览：复杂 OCC 运算只在点击“应用”时执行。
    clearPreview(false);
}

void OccBooleanCommonWidget::clearPreview(bool renderNow)
{
    clearBooleanShapePreview(getUuid(), renderNow);
}

void OccBooleanCommonWidget::requestRender()
{
    requestVisualRender();
}
