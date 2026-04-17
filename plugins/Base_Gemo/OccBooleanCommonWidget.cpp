#include "OccBooleanCommonWidget.h"

#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <QJsonArray>
#include <QListWidgetItem>
#include <QMessageBox>
#include <sstream>

#include "ui_OccBooleanCommonWidget.h"

namespace
{
TopoDS_Shape commonShapes(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
    {
        return TopoDS_Shape();
    }

    BRepAlgoAPI_Common op(a, b);
    op.Build();
    if (!op.IsDone())
    {
        return TopoDS_Shape();
    }
    return op.Shape();
}
} // namespace

OccBooleanCommonWidget::OccBooleanCommonWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean), ui(new Ui::OccBooleanCommonWidget)
{
    ui->setupUi(this);
    if (parent)
    {
        setParent(parent);
    }

    connect(ui->addButton, &QPushButton::clicked, this, &OccBooleanCommonWidget::onAddClicked);
    connect(ui->removeButton, &QPushButton::clicked, this,
            &OccBooleanCommonWidget::onRemoveSelectedClicked);
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
}

void OccBooleanCommonWidget::beforeUnload()
{
    requestRender();
}

QJsonObject OccBooleanCommonWidget::saveJson()
{
    refreshGeometryMap();
    QJsonObject obj;
    QJsonArray selectedKeys;
    for (QStringList::const_iterator it = m_selectedKeys.constBegin(); it != m_selectedKeys.constEnd();
         ++it)
    {
        selectedKeys.append(*it);
    }
    obj["selectedKeys"] = selectedKeys;

    QJsonArray operationKeys;
    const QStringList keysToSave = m_operationKeys.isEmpty() ? m_selectedKeys : m_operationKeys;
    for (QStringList::const_iterator it = keysToSave.constBegin(); it != keysToSave.constEnd(); ++it)
    {
        operationKeys.append(*it);
    }
    obj["operationKeys"] = operationKeys;

    obj["resultBrep"] = QString();
    if (m_geomMgr)
    {
        if (GeomShapeObject* resultObj = m_geomMgr->getGeomShapeObject(getUuid()))
        {
            TopoDS_Shape* shape = resultObj->getShape();
            if (shape && !shape->IsNull())
            {
                std::ostringstream oss;
                BRepTools::Write(*shape, oss);
                obj["resultBrep"] = QString::fromStdString(oss.str());
            }
        }
    }
    return obj;
}

void OccBooleanCommonWidget::loadJson(const QJsonObject& obj)
{
    m_selectedKeys.clear();
    const QJsonArray selectedKeys = obj.value("selectedKeys").toArray();
    for (int i = 0; i < selectedKeys.size(); ++i)
    {
        const QString key = selectedKeys.at(i).toString();
        if (!key.isEmpty())
        {
            m_selectedKeys.append(key);
        }
    }

    if (m_selectedKeys.isEmpty())
    {
        const QString keyA = obj.value("keyA").toString();
        const QString keyB = obj.value("keyB").toString();
        if (!keyA.isEmpty())
        {
            m_selectedKeys.append(keyA);
        }
        if (!keyB.isEmpty() && keyB != keyA)
        {
            m_selectedKeys.append(keyB);
        }
    }

    m_operationKeys.clear();
    const QJsonArray operationKeys = obj.value("operationKeys").toArray();
    for (int i = 0; i < operationKeys.size(); ++i)
    {
        const QString key = operationKeys.at(i).toString();
        if (!key.isEmpty())
        {
            m_operationKeys.append(key);
        }
    }
    if (m_operationKeys.isEmpty())
    {
        m_operationKeys = m_selectedKeys;
    }
    if (m_selectedKeys.isEmpty())
    {
        m_selectedKeys = m_operationKeys;
    }

    m_savedResultBrep = obj.value("resultBrep").toString();

    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onAddClicked()
{
    const QString key = ui->sourceComboBox->currentData().toString();
    if (key.isEmpty() || m_selectedKeys.contains(key))
    {
        return;
    }
    m_selectedKeys.append(key);
    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onRemoveSelectedClicked()
{
    const QList<QListWidgetItem*> items = ui->selectedListWidget->selectedItems();
    for (QList<QListWidgetItem*>::const_iterator it = items.constBegin(); it != items.constEnd(); ++it)
    {
        m_selectedKeys.removeAll((*it)->data(Qt::UserRole).toString());
    }
    m_operationKeys.clear();
    refreshSelectedList();
}

void OccBooleanCommonWidget::onApplyClicked()
{
    if (m_selectedKeys.size() < 2)
    {
        QMessageBox::warning(this, tr("提示"), tr("交集至少需要选择两个几何体。"));
        return;
    }

    refreshGeometryMap();
    QList<QString> sourceKeys;
    for (QStringList::const_iterator it = m_selectedKeys.constBegin(); it != m_selectedKeys.constEnd(); ++it)
    {
        if (m_geomMap.contains(*it))
        {
            sourceKeys.append(*it);
        }
    }

    if (sourceKeys.size() < 2)
    {
        QMessageBox::warning(this, tr("提示"), tr("有效几何体数量不足，无法执行交集。"));
        return;
    }

    GeomShapeObject* firstObj = m_geomMap.value(sourceKeys.front(), nullptr);
    if (!firstObj || !firstObj->getShape() || firstObj->getShape()->IsNull())
    {
        QMessageBox::warning(this, tr("提示"), tr("第一个几何体无效。"));
        return;
    }

    TopoDS_Shape resultShape = *(firstObj->getShape());
    for (int i = 1; i < sourceKeys.size(); ++i)
    {
        GeomShapeObject* toolObj = m_geomMap.value(sourceKeys.at(i), nullptr);
        TopoDS_Shape* toolShape = toolObj ? toolObj->getShape() : nullptr;
        if (!toolShape || toolShape->IsNull())
        {
            QMessageBox::warning(this, tr("提示"), tr("存在无效几何体，已终止交集。"));
            return;
        }
        resultShape = commonShapes(resultShape, *toolShape);
        if (resultShape.IsNull())
        {
            QMessageBox::warning(this, tr("提示"), tr("交集失败，请检查几何体是否可布尔运算。"));
            return;
        }
    }

    if (!m_geomMgr)
    {
        return;
    }

    const QString resultUuid = getUuid();
    const QString resultName = getDisplayName().isEmpty() ? tr("布尔求交") : getDisplayName();
    GeometryCleanup::removeExistingGeometry(resultUuid);

    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject(resultUuid, newShape, resultName);
    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert(resultUuid, resultObj);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        if (resultObj->getSurfActor())
        {
            vis->addProp(resultUuid + "_face", resultObj->getSurfActor(), "Geometry", resultUuid + "_face");
        }
        if (resultObj->getEdgeActor())
        {
            vis->addProp(resultUuid + "_edge", resultObj->getEdgeActor(), "Geometry", resultUuid + "_edge");
        }
    }

    for (QList<QString>::const_iterator keyIt = sourceKeys.constBegin(); keyIt != sourceKeys.constEnd(); ++keyIt)
    {
        if (*keyIt == resultUuid)
        {
            continue;
        }
        removeGeometryAndDimNode(*keyIt);
    }

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(resultUuid, false);
    }

    m_operationKeys.clear();
    for (QList<QString>::const_iterator keyIt = sourceKeys.constBegin(); keyIt != sourceKeys.constEnd();
         ++keyIt)
    {
        m_operationKeys.append(*keyIt);
    }
    m_selectedKeys.clear();
    m_selectedKeys.append(resultUuid);
    refreshGeometryMap();
    refreshSourceCombo();
    refreshSelectedList();
    requestRender();
}

void OccBooleanCommonWidget::restoreSavedResult()
{
    if (m_savedResultBrep.isEmpty())
    {
        return;
    }
    refreshGeometryMap();
    if (!m_geomMgr || m_geomMap.contains(getUuid()))
    {
        return;
    }

    std::istringstream iss(m_savedResultBrep.toStdString());
    BRep_Builder builder;
    TopoDS_Shape resultShape;
    BRepTools::Read(resultShape, iss, builder);
    if (resultShape.IsNull())
    {
        return;
    }

    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject(getUuid(), newShape, getDisplayName());
    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert(getUuid(), resultObj);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        if (resultObj->getSurfActor())
        {
            vis->addProp(getUuid() + "_face", resultObj->getSurfActor(), "Geometry",
                         getUuid() + "_face");
        }
        if (resultObj->getEdgeActor())
        {
            vis->addProp(getUuid() + "_edge", resultObj->getEdgeActor(), "Geometry",
                         getUuid() + "_edge");
        }
    }
    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(getUuid(), false);
    }

    for (QStringList::const_iterator it = m_operationKeys.constBegin(); it != m_operationKeys.constEnd();
         ++it)
    {
        if (it->isEmpty() || *it == getUuid())
        {
            continue;
        }
        removeGeometryAndDimNode(*it);
    }

    m_selectedKeys.clear();
    m_selectedKeys.append(getUuid());
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
    ui->sourceComboBox->clear();
    for (auto it = m_geomMap.constBegin(); it != m_geomMap.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = it.value();
        QString displayName = obj ? obj->getName() : key;
        if (displayName.isEmpty())
        {
            displayName = key;
        }
        ui->sourceComboBox->addItem(displayName, key);
    }
}

void OccBooleanCommonWidget::refreshSelectedList()
{
    ui->selectedListWidget->clear();
    QStringList validKeys;
    for (QStringList::const_iterator it = m_selectedKeys.constBegin(); it != m_selectedKeys.constEnd(); ++it)
    {
        if (!m_geomMap.contains(*it))
        {
            continue;
        }
        validKeys.append(*it);
        GeomShapeObject* obj = m_geomMap.value(*it, nullptr);
        QString displayName = obj ? obj->getName() : *it;
        if (displayName.isEmpty())
        {
            displayName = *it;
        }
        auto* item = new QListWidgetItem(displayName, ui->selectedListWidget);
        item->setData(Qt::UserRole, *it);
    }
    m_selectedKeys = validKeys;
}

void OccBooleanCommonWidget::requestRender()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->render();
    }
}

void OccBooleanCommonWidget::removeGeometryAndDimNode(const QString& key)
{
    if (key.isEmpty())
    {
        return;
    }
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(key + "_face", false);
        vis->removeProp(key + "_edge", true);
    }
    if (m_geomMgr)
    {
        m_geomMgr->removeGeomShapeObject(key);
    }
    m_geomMap.remove(key);

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(key, true);
    }
}
