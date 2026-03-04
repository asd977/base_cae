#include "OccBooleanUnionWidget.h"

#include <CustomComboBox.h>
#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkProperty.h>

#include <BOPAlgo_CellsBuilder.hxx>
#include <BOPAlgo_GlueEnum.hxx>
#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopTools_ListOfShape.hxx>

namespace
{
TopoDS_Shape unionShapesKeepDomains(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
    {
        return TopoDS_Shape();
    }

    // Use General Fuse builder to create shared topology between domains.
    // This keeps multi-domain solids while avoiding duplicated coincident faces.
    TopTools_ListOfShape args;
    args.Append(a);
    args.Append(b);

    // Official OCCT multi-domain workflow: Cells Builder based on General Fuse.
    // Keep all cells, then build typed containers (COMPSOLID for solids),
    // so adjacent domains can share one interface instead of duplicated faces.
    BOPAlgo_CellsBuilder builder;
    builder.SetArguments(args);
    builder.SetNonDestructive(false);
    builder.SetGlue(BOPAlgo_GlueFull);
    builder.Perform();
    if (builder.HasErrors())
    {
        return TopoDS_Shape();
    }

    builder.AddAllToResult();
    builder.MakeContainers();

    // Try to merge same-domain duplicates early at geometry stage
    // so users can see the shared interface before meshing/export.
    ShapeUpgrade_UnifySameDomain unifier(builder.Shape(), true, true, false);
    unifier.Build();
    return unifier.Shape();
}
} // namespace

OccBooleanUnionWidget::OccBooleanUnionWidget(QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean)
{
    Q_UNUSED(parent);
    initUi();
    initConnections();
}

void OccBooleanUnionWidget::initUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    auto* pageNameLabel = new QLabel(tr("几何模块"), this);
    pageNameLabel->setObjectName("pageNameLabel");
    auto* sectionTitleButton = new QPushButton(tr("布尔运算"), this);
    sectionTitleButton->setObjectName("sectionTitleButton");
    sectionTitleButton->setEnabled(false);
    rootLayout->addWidget(pageNameLabel);
    rootLayout->addWidget(sectionTitleButton);

    auto* layout = new QGridLayout();

    auto* labelA = new QLabel(tr("a:"), this);
    auto* labelB = new QLabel(tr("b:"), this);

    m_comboBoxA = new CustomComboBox(this);
    m_comboBoxB = new CustomComboBox(this);
    m_comboBoxOp = new QComboBox(this);
    m_comboBoxOp->addItem(tr("Union (A ∪ B, keep domains)"));
    m_comboBoxOp->setCurrentIndex(0);
    m_comboBoxOp->setEnabled(false);
    m_comboBoxOp->setVisible(false);

    m_listView = new QListView(this);
    m_applyButton = new QPushButton(tr("应用"), this);
    m_refreshButton = new QPushButton(tr("test-生成文件"), this);

    layout->addWidget(labelA, 0, 0);
    layout->addWidget(m_comboBoxA, 0, 1);
    layout->addWidget(labelB, 1, 0);
    layout->addWidget(m_comboBoxB, 1, 1);
    layout->addWidget(m_comboBoxOp, 2, 0, 1, 2);
    layout->addWidget(m_listView, 3, 0, 1, 2);
    layout->addWidget(m_applyButton, 4, 1);
    layout->addWidget(m_refreshButton, 5, 1);
    layout->setRowStretch(6, 1);
    rootLayout->addLayout(layout);
    rootLayout->addStretch();
}

void OccBooleanUnionWidget::initConnections()
{
    connect(m_comboBoxA, SIGNAL(currentIndexChanged(int)), this, SLOT(onComboBoxAChanged(int)));
    connect(m_comboBoxB, SIGNAL(currentIndexChanged(int)), this, SLOT(onComboBoxBChanged(int)));
    connect(m_comboBoxA, SIGNAL(itemHovered(int)), this, SLOT(onComboBoxHovered(int)));
    connect(m_comboBoxB, SIGNAL(itemHovered(int)), this, SLOT(onComboBoxHovered(int)));
    connect(m_comboBoxA, SIGNAL(dropdownClosed()), this, SLOT(onComboBoxDropdownClosed()));
    connect(m_comboBoxB, SIGNAL(dropdownClosed()), this, SLOT(onComboBoxDropdownClosed()));
    connect(m_applyButton, SIGNAL(clicked()), this, SLOT(onApplyClicked()));
    connect(m_refreshButton, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
}

void OccBooleanUnionWidget::beforeLoad()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr)
    {
        qWarning() << "[OccBooleanUnionWidget] GeomManagerInstance is null";
        return;
    }

    m_colorBackup.clear();
    m_geomMap = m_geomMgr->getShapMap();
    fillComboBoxes();
}

void OccBooleanUnionWidget::beforeUnload()
{
    for (auto it = m_colorBackup.constBegin(); it != m_colorBackup.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = m_geomMap.value(key, nullptr);
        if (!obj)
        {
            continue;
        }

        vtkActor* actor = obj->getSurfActor();
        if (!actor)
        {
            continue;
        }

        if (m_dimmedBackup.contains(key))
        {
            continue;
        }
        const ColorBackup& cb = it.value();
        actor->GetProperty()->SetColor(cb.r, cb.g, cb.b);
        actor->GetProperty()->SetOpacity(cb.opacity);
    }

    m_hoveredKey.clear();
    m_colorBackup.clear();
    requestRender();
}

QJsonObject OccBooleanUnionWidget::saveJson()
{
    QJsonObject obj;
    obj["keyA"] = m_keyA;
    obj["keyB"] = m_keyB;
    return obj;
}

void OccBooleanUnionWidget::loadJson(const QJsonObject& obj)
{
    m_keyA = obj.value("keyA").toString();
    m_keyB = obj.value("keyB").toString();

    auto findIndexByKey = [](QComboBox* combo, const QString& key)
    {
        for (int i = 0; i < combo->count(); ++i)
        {
            if (combo->itemData(i).toString() == key)
            {
                return i;
            }
        }
        return -1;
    };

    const int indexA = findIndexByKey(m_comboBoxA, m_keyA);
    if (indexA >= 0)
    {
        m_comboBoxA->setCurrentIndex(indexA);
    }

    const int indexB = findIndexByKey(m_comboBoxB, m_keyB);
    if (indexB >= 0)
    {
        m_comboBoxB->setCurrentIndex(indexB);
    }
}

void OccBooleanUnionWidget::fillComboBoxes()
{
    m_comboBoxA->clear();
    m_comboBoxB->clear();

    for (auto it = m_geomMap.constBegin(); it != m_geomMap.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = it.value();
        QString displayName = obj ? obj->getName() : key;
        if (displayName.isEmpty())
        {
            displayName = key;
        }
        m_comboBoxA->addItem(displayName, key);
        m_comboBoxB->addItem(displayName, key);
    }

    auto findIndexByKey = [](QComboBox* combo, const QString& key)
    {
        for (int i = 0; i < combo->count(); ++i)
        {
            if (combo->itemData(i).toString() == key)
            {
                return i;
            }
        }
        return -1;
    };

    int indexA = findIndexByKey(m_comboBoxA, m_keyA);
    if (indexA < 0 && m_comboBoxA->count() > 0)
    {
        indexA = 0;
    }

    int indexB = findIndexByKey(m_comboBoxB, m_keyB);
    if (indexB < 0)
    {
        if (m_comboBoxB->count() > 1)
            indexB = 1;
        else if (m_comboBoxB->count() > 0)
            indexB = 0;
    }

    if (indexA >= 0)
    {
        m_comboBoxA->setCurrentIndex(indexA);
    }
    if (indexB >= 0)
    {
        m_comboBoxB->setCurrentIndex(indexB);
    }

    onComboBoxAChanged(m_comboBoxA->currentIndex());
    onComboBoxBChanged(m_comboBoxB->currentIndex());
}

void OccBooleanUnionWidget::setHighlight(const QString& key, const QColor& color, bool on)
{
    GeomShapeObject* obj = m_geomMap.value(key, nullptr);
    if (!obj)
    {
        return;
    }

    vtkActor* actor = obj->getSurfActor();
    if (!actor)
    {
        return;
    }

    vtkProperty* prop = actor->GetProperty();
    if (!prop)
    {
        return;
    }

    if (m_dimmedBackup.contains(key))
    {
        return;
    }

    if (on)
    {
        if (!m_colorBackup.contains(key))
        {
            ColorBackup cb;
            double rgb[3] = {0.8, 0.8, 0.8};
            prop->GetColor(rgb);
            cb.r = rgb[0];
            cb.g = rgb[1];
            cb.b = rgb[2];
            cb.opacity = prop->GetOpacity();
            m_colorBackup.insert(key, cb);
        }

        prop->SetColor(color.redF(), color.greenF(), color.blueF());
    }
    else
    {
        if (m_colorBackup.contains(key))
        {
            const ColorBackup& cb = m_colorBackup.value(key);
            prop->SetColor(cb.r, cb.g, cb.b);
            prop->SetOpacity(cb.opacity);
        }
    }

    requestRender();
}

void OccBooleanUnionWidget::setDimmed(const QString& key, bool on)
{
    GeomShapeObject* obj = m_geomMap.value(key, nullptr);
    if (!obj)
    {
        return;
    }

    vtkActor* actor = obj->getSurfActor();
    if (!actor)
    {
        return;
    }

    vtkProperty* prop = actor->GetProperty();
    if (!prop)
    {
        return;
    }

    if (on)
    {
        if (!m_dimmedBackup.contains(key))
        {
            ColorBackup cb;
            double rgb[3] = {0.8, 0.8, 0.8};
            prop->GetColor(rgb);
            cb.r = rgb[0];
            cb.g = rgb[1];
            cb.b = rgb[2];
            cb.opacity = prop->GetOpacity();
            m_dimmedBackup.insert(key, cb);
        }
        prop->SetColor(0.6, 0.6, 0.6);
        prop->SetOpacity(0.25);
    }
    else
    {
        if (m_dimmedBackup.contains(key))
        {
            const ColorBackup& cb = m_dimmedBackup.value(key);
            prop->SetColor(cb.r, cb.g, cb.b);
            prop->SetOpacity(cb.opacity);
            m_dimmedBackup.remove(key);
        }
    }

    requestRender();
}

void OccBooleanUnionWidget::requestRender()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->render();
    }
}

void OccBooleanUnionWidget::removeComboItem(QComboBox* combo, const QString& key)
{
    if (!combo)
    {
        return;
    }
    int index = combo->findData(key);
    if (index >= 0)
    {
        combo->removeItem(index);
    }
}

void OccBooleanUnionWidget::removeGeometryAndDimNode(const QString& key)
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
    m_colorBackup.remove(key);
    m_dimmedBackup.remove(key);

    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(key, true);
    }
    removeComboItem(m_comboBoxA, key);
    removeComboItem(m_comboBoxB, key);
}

void OccBooleanUnionWidget::onComboBoxAChanged(int index)
{
    if (index < 0 || index >= m_comboBoxA->count())
    {
        return;
    }

    QString newKey = m_comboBoxA->itemData(index).toString();
    if (newKey.isEmpty())
    {
        newKey = m_comboBoxA->itemText(index);
    }

    m_keyA = newKey;
}

void OccBooleanUnionWidget::onComboBoxBChanged(int index)
{
    if (index < 0 || index >= m_comboBoxB->count())
    {
        return;
    }

    QString newKey = m_comboBoxB->itemData(index).toString();
    if (newKey.isEmpty())
    {
        newKey = m_comboBoxB->itemText(index);
    }

    m_keyB = newKey;
}

void OccBooleanUnionWidget::onComboBoxHovered(int index)
{
    auto* combo = qobject_cast<CustomComboBox*>(sender());
    if (!combo || index < 0 || index >= combo->count())
    {
        return;
    }

    QString key = combo->itemData(index).toString();
    if (key.isEmpty())
    {
        key = combo->itemText(index);
    }
    if (key.isEmpty() || key == m_hoveredKey)
    {
        return;
    }

    if (!m_hoveredKey.isEmpty())
    {
        setHighlight(m_hoveredKey, QColor(), false);
    }

    m_hoveredKey = key;
    setHighlight(m_hoveredKey, QColor(255, 243, 191), true);
}

void OccBooleanUnionWidget::onComboBoxDropdownClosed()
{
    if (m_hoveredKey.isEmpty())
    {
        return;
    }

    setHighlight(m_hoveredKey, QColor(), false);
    m_hoveredKey.clear();
}

void OccBooleanUnionWidget::onApplyClicked()
{
    if (m_keyA.isEmpty() || m_keyB.isEmpty())
    {
        qWarning() << "[OccBooleanUnionWidget] A or B key is empty.";
        return;
    }

    if (m_keyA == m_keyB)
    {
        qWarning() << "[OccBooleanUnionWidget] A and B are the same object.";
        return;
    }

    const QString sourceKeyA = m_keyA;
    const QString sourceKeyB = m_keyB;
    GeomShapeObject* objA = m_geomMap.value(m_keyA, nullptr);
    GeomShapeObject* objB = m_geomMap.value(m_keyB, nullptr);
    if (!objA || !objB)
    {
        qWarning() << "[OccBooleanUnionWidget] invalid GeomShapeObject for A or B.";
        return;
    }

    TopoDS_Shape* shapeA = objA->getShape();
    TopoDS_Shape* shapeB = objB->getShape();
    if (!shapeA || !shapeB || shapeA->IsNull() || shapeB->IsNull())
    {
        qWarning() << "[OccBooleanUnionWidget] A or B shape is null.";
        return;
    }

    TopoDS_Shape resultShape = unionShapesKeepDomains(*shapeA, *shapeB);
    if (resultShape.IsNull())
    {
        qWarning() << "[OccBooleanUnionWidget] Boolean result is null.";
        return;
    }

    const QString resultUuid = getUuid();
    const QString resultName = getDisplayName().isEmpty() ? tr("联合体") : getDisplayName();
    GeometryCleanup::removeExistingGeometry(resultUuid);

    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject(resultUuid, newShape, resultName);

    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert(resultUuid, resultObj);

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        if (resultObj->getSurfActor())
        {
            vis->addProp(resultUuid + "_face", resultObj->getSurfActor(), "Geometry",
                         resultUuid + "_face");
        }
        if (resultObj->getEdgeActor())
        {
            vis->addProp(resultUuid + "_edge", resultObj->getEdgeActor(), "Geometry",
                         resultUuid + "_edge");
        }
    }

    removeComboItem(m_comboBoxA, resultUuid);
    removeComboItem(m_comboBoxB, resultUuid);
    m_comboBoxA->addItem(resultName, resultUuid);
    m_comboBoxB->addItem(resultName, resultUuid);

    int idxA = m_comboBoxA->findData(resultUuid);
    if (idxA >= 0)
    {
        m_comboBoxA->setCurrentIndex(idxA);
    }
    int idxB = m_comboBoxB->findData(resultUuid);
    if (idxB >= 0)
    {
        m_comboBoxB->setCurrentIndex(idxB);
    }

    setHighlight(sourceKeyA, QColor(), false);
    setHighlight(sourceKeyB, QColor(), false);
    removeGeometryAndDimNode(sourceKeyA);
    removeGeometryAndDimNode(sourceKeyB);
    if (auto* projectManagerService =
            ServiceRegistry::instance().getService<ProjectManagerService>())
    {
        projectManagerService->setNodeDimmed(resultUuid, false);
    }

    m_hoveredKey.clear();
    requestRender();
}

void OccBooleanUnionWidget::onRefreshClicked()
{
    if (!m_geomMgr)
    {
        m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
        if (!m_geomMgr)
        {
            qWarning() << "[OccBooleanUnionWidget] GeomManagerInstance is null (refresh).";
            return;
        }
    }

    for (auto it = m_colorBackup.constBegin(); it != m_colorBackup.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = m_geomMap.value(key, nullptr);
        if (!obj)
        {
            continue;
        }

        if (vtkSmartPointer<vtkActor> actor = obj->getSurfActor())
        {
            if (m_dimmedBackup.contains(key))
            {
                continue;
            }
            const ColorBackup& cb = it.value();
            actor->GetProperty()->SetColor(cb.r, cb.g, cb.b);
            actor->GetProperty()->SetOpacity(cb.opacity);
        }
    }
    m_hoveredKey.clear();
    m_colorBackup.clear();

    m_geomMap = m_geomMgr->getShapMap();
    m_keyA.clear();
    m_keyB.clear();
    fillComboBoxes();
}
