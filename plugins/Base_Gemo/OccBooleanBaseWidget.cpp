#include "OccBooleanBaseWidget.h"
#include "ui_OccBooleanBaseWidget.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>

#include <QDebug>
#include <QColor>
#include <QUuid>

#include <vtkActor.h>
#include <vtkProperty.h>

#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

TopoDS_Shape OccBooleanTool::Fuse(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
        return TopoDS_Shape();

    BRepAlgoAPI_Fuse op(a, b);
    op.Build();
    if (!op.IsDone())
        return TopoDS_Shape();

    return op.Shape();
}

TopoDS_Shape OccBooleanTool::Cut(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
        return TopoDS_Shape();

    BRepAlgoAPI_Cut op(a, b);
    op.Build();
    if (!op.IsDone())
        return TopoDS_Shape();

    return op.Shape();
}

TopoDS_Shape OccBooleanTool::Common(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
        return TopoDS_Shape();

    BRepAlgoAPI_Common op(a, b);
    op.Build();
    if (!op.IsDone())
        return TopoDS_Shape();

    return op.Shape();
}

OccBooleanBaseWidget::OccBooleanBaseWidget(BooleanOp opType, QWidget *parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean)
    , ui(new Ui::OccBooleanBaseWidget)
    , m_opType(opType)
{
    Q_UNUSED(parent);

    ui->setupUi(this);

    if (ui->comboBoxOp->count() == 0) {
        ui->comboBoxOp->addItem(tr("Cut (A - B)"));              // index 0
        ui->comboBoxOp->addItem(tr("Fuse (A ∪ B)"));             // index 1
        ui->comboBoxOp->addItem(tr("Common (A ∩ B)"));           // index 2
    }

    int opIndex = 0;
    switch (m_opType) {
    case BooleanOp::Cut:
        opIndex = 0;
        break;
    case BooleanOp::Fuse:
        opIndex = 1;
        break;
    case BooleanOp::Common:
        opIndex = 2;
        break;
    }

    ui->comboBoxOp->setCurrentIndex(opIndex);
    ui->comboBoxOp->setEnabled(false);
    ui->comboBoxOp->setVisible(false);

    initConnections();
}

OccBooleanBaseWidget::~OccBooleanBaseWidget()
{
    delete ui;
}

void OccBooleanBaseWidget::initConnections()
{
    connect(ui->comboBoxA, SIGNAL(currentIndexChanged(int)),
            this, SLOT(on_comboBoxA_currentIndexChanged(int)));
    connect(ui->comboBoxB, SIGNAL(currentIndexChanged(int)),
            this, SLOT(on_comboBoxB_currentIndexChanged(int)));
    connect(ui->pushButtonApply, SIGNAL(clicked()),
            this, SLOT(on_pushButtonApply_clicked()));
    connect(ui->pushButton, SIGNAL(clicked()),
            this, SLOT(on_pushButton_clicked()));
}

void OccBooleanBaseWidget::beforeLoad()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr) {
        qWarning() << "[OccBooleanBaseWidget] GeomManagerInstance is null";
        return;
    }

    m_colorBackup.clear();

    m_geomMap = m_geomMgr->getShapMap();

    fillComboBoxes();
}

void OccBooleanBaseWidget::beforeUnload()
{
    for (auto it = m_colorBackup.constBegin(); it != m_colorBackup.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = m_geomMap.value(key, nullptr);
        if (!obj)
            continue;

        vtkActor* actor = obj->getSurfActor();
        if (!actor)
            continue;

        if (m_dimmedBackup.contains(key))
            continue;
        const ColorBackup& cb = it.value();
        actor->GetProperty()->SetColor(cb.r, cb.g, cb.b);
        actor->GetProperty()->SetOpacity(cb.opacity);
    }

    m_colorBackup.clear();
    requestRender();
}

QJsonObject OccBooleanBaseWidget::saveJson()
{
    QJsonObject obj;
    obj["keyA"]    = m_keyA;
    obj["keyB"]    = m_keyB;
    return obj;
}

void OccBooleanBaseWidget::loadJson(const QJsonObject &obj)
{
    QString keyA = obj.value("keyA").toString();
    QString keyB = obj.value("keyB").toString();

    auto findIndexByKey = [](QComboBox *combo, const QString &key) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == key) {
                return i;
            }
        }
        return -1;
    };

    int indexA = findIndexByKey(ui->comboBoxA, keyA);
    if (indexA >= 0)
        ui->comboBoxA->setCurrentIndex(indexA);

    int indexB = findIndexByKey(ui->comboBoxB, keyB);
    if (indexB >= 0)
        ui->comboBoxB->setCurrentIndex(indexB);
}

void OccBooleanBaseWidget::fillComboBoxes()
{
    ui->comboBoxA->clear();
    ui->comboBoxB->clear();

    for (auto it = m_geomMap.constBegin(); it != m_geomMap.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = it.value();
        QString displayName = obj ? obj->getName() : key;
        if (displayName.isEmpty()) {
            displayName = key;
        }
        ui->comboBoxA->addItem(displayName, key);
        ui->comboBoxB->addItem(displayName, key);
    }

    if (ui->comboBoxA->count() > 0)
        ui->comboBoxA->setCurrentIndex(0);
    if (ui->comboBoxB->count() > 1)
        ui->comboBoxB->setCurrentIndex(1);
    else if (ui->comboBoxB->count() > 0)
        ui->comboBoxB->setCurrentIndex(0);

    on_comboBoxA_currentIndexChanged(ui->comboBoxA->currentIndex());
    on_comboBoxB_currentIndexChanged(ui->comboBoxB->currentIndex());
}

void OccBooleanBaseWidget::setHighlight(const QString& key,
                                        const QColor& color,
                                        bool on)
{
    GeomShapeObject* obj = m_geomMap.value(key, nullptr);
    if (!obj)
        return;

    vtkActor* actor = obj->getSurfActor();
    if (!actor)
        return;

    vtkProperty* prop = actor->GetProperty();
    if (!prop)
        return;

    if (m_dimmedBackup.contains(key)) {
        return;
    }

    if (on) {
        if (!m_colorBackup.contains(key)) {
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
    } else {
        if (m_colorBackup.contains(key)) {
            const ColorBackup& cb = m_colorBackup.value(key);
            prop->SetColor(cb.r, cb.g, cb.b);
            prop->SetOpacity(cb.opacity);
        }
    }

    requestRender();
}

void OccBooleanBaseWidget::setDimmed(const QString &key, bool on)
{
    GeomShapeObject* obj = m_geomMap.value(key, nullptr);
    if (!obj)
        return;

    vtkActor* actor = obj->getSurfActor();
    if (!actor)
        return;

    vtkProperty* prop = actor->GetProperty();
    if (!prop)
        return;

    if (on) {
        if (!m_dimmedBackup.contains(key)) {
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
    } else {
        if (m_dimmedBackup.contains(key)) {
            const ColorBackup& cb = m_dimmedBackup.value(key);
            prop->SetColor(cb.r, cb.g, cb.b);
            prop->SetOpacity(cb.opacity);
            m_dimmedBackup.remove(key);
        }
    }

    requestRender();
}

void OccBooleanBaseWidget::requestRender()
{
}

QString OccBooleanBaseWidget::opDisplayName() const
{
    switch (m_opType) {
    case BooleanOp::Cut:
        return tr("切割");
    case BooleanOp::Fuse:
        return tr("融合");
    case BooleanOp::Common:
        return tr("求交");
    }
    return tr("布尔");
}

QString OccBooleanBaseWidget::makeResultName() const
{
    static QHash<int, int> counters;
    int key = static_cast<int>(m_opType);
    int count = counters.value(key, 0) + 1;
    counters.insert(key, count);
    return QStringLiteral("%1(%2)").arg(opDisplayName()).arg(count);
}

void OccBooleanBaseWidget::removeComboItem(QComboBox *combo, const QString &key)
{
    if (!combo) {
        return;
    }
    int index = combo->findData(key);
    if (index >= 0) {
        combo->removeItem(index);
    }
}

void OccBooleanBaseWidget::removeGeometryAndDimNode(const QString &key)
{
    if (key.isEmpty()) {
        return;
    }
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>()) {
        vis->removeProp(key + "_face", false);
        vis->removeProp(key + "_edge", true);
    }
    if (m_geomMgr) {
        m_geomMgr->removeGeomShapeObject(key);
    }
    m_geomMap.remove(key);
    m_colorBackup.remove(key);
    m_dimmedBackup.remove(key);

    if (auto* projectManagerService = ServiceRegistry::instance().getService<ProjectManagerService>()) {
        projectManagerService->setNodeDimmed(key, true);
    }
    removeComboItem(ui->comboBoxA, key);
    removeComboItem(ui->comboBoxB, key);
}

void OccBooleanBaseWidget::on_comboBoxA_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->comboBoxA->count())
        return;

    QString newKey = ui->comboBoxA->itemData(index).toString();
    if (newKey.isEmpty()) {
        newKey = ui->comboBoxA->itemText(index);
    }

    if (!m_keyA.isEmpty() && m_keyA != m_keyB)
        setHighlight(m_keyA, QColor(), false);

    m_keyA = newKey;

    if (!m_keyA.isEmpty() && m_keyA != m_keyB)
        setHighlight(m_keyA, Qt::red, true);
}

void OccBooleanBaseWidget::on_comboBoxB_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->comboBoxB->count())
        return;

    QString newKey = ui->comboBoxB->itemData(index).toString();
    if (newKey.isEmpty()) {
        newKey = ui->comboBoxB->itemText(index);
    }

    if (!m_keyB.isEmpty() && m_keyB != m_keyA)
        setHighlight(m_keyB, QColor(), false);

    m_keyB = newKey;

    if (!m_keyB.isEmpty() && m_keyB != m_keyA)
        setHighlight(m_keyB, Qt::blue, true);
}

void OccBooleanBaseWidget::on_pushButtonApply_clicked()
{
    if (m_keyA.isEmpty() || m_keyB.isEmpty()) {
        qWarning() << "[OccBooleanBaseWidget] A or B key is empty.";
        return;
    }

    if (m_keyA == m_keyB) {
        qWarning() << "[OccBooleanBaseWidget] A and B are the same object.";
        return;
    }

    GeomShapeObject* objA = m_geomMap.value(m_keyA, nullptr);
    GeomShapeObject* objB = m_geomMap.value(m_keyB, nullptr);
    if (!objA || !objB) {
        qWarning() << "[OccBooleanBaseWidget] invalid GeomShapeObject for A or B.";
        return;
    }

    TopoDS_Shape* shapeA = objA->getShape();
    TopoDS_Shape* shapeB = objB->getShape();
    if (!shapeA || !shapeB || shapeA->IsNull() || shapeB->IsNull()) {
        qWarning() << "[OccBooleanBaseWidget] A or B shape is null.";
        return;
    }

    int opIndex = ui->comboBoxOp->currentIndex();
    TopoDS_Shape resultShape;

    switch (opIndex)
    {
    case 0:
        resultShape = OccBooleanTool::Cut(*shapeA, *shapeB);
        break;
    case 1:
        resultShape = OccBooleanTool::Fuse(*shapeA, *shapeB);
        break;
    case 2:
        resultShape = OccBooleanTool::Common(*shapeA, *shapeB);
        break;
    default:
        qWarning() << "[OccBooleanBaseWidget] invalid op index:" << opIndex;
        return;
    }

    if (resultShape.IsNull()) {
        qWarning() << "[OccBooleanBaseWidget] Boolean result is null.";
        return;
    }

    QString resultUuid = QUuid::createUuid().toString();
    QString resultName = makeResultName();
    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject(resultUuid, newShape, resultName);

    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert(resultUuid, resultObj);

    if (vtkSmartPointer<vtkActor> resultActor = resultObj->getSurfActor()) {
        resultActor->GetProperty()->SetColor(1.0, 1.0, 0.0);
    }

    ui->comboBoxA->addItem(resultName, resultUuid);
    ui->comboBoxB->addItem(resultName, resultUuid);

    int idxA = ui->comboBoxA->findData(resultUuid);
    if (idxA >= 0)
        ui->comboBoxA->setCurrentIndex(idxA);
    int idxB = ui->comboBoxB->findData(resultUuid);
    if (idxB >= 0)
        ui->comboBoxB->setCurrentIndex(idxB);

    setHighlight(m_keyA, QColor(), false);
    setHighlight(m_keyB, QColor(), false);
    removeGeometryAndDimNode(m_keyA);
    removeGeometryAndDimNode(m_keyB);

    requestRender();
}

void OccBooleanBaseWidget::on_pushButton_clicked()
{
    if (!m_geomMgr) {
        m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
        if (!m_geomMgr) {
            qWarning() << "[OccBooleanBaseWidget] GeomManagerInstance is null (refresh).";
            return;
        }
    }

    for (auto it = m_colorBackup.constBegin(); it != m_colorBackup.constEnd(); ++it)
    {
        const QString& key = it.key();
        GeomShapeObject* obj = m_geomMap.value(key, nullptr);
        if (!obj)
            continue;

        if (vtkSmartPointer<vtkActor> actor = obj->getSurfActor()) {
            if (m_dimmedBackup.contains(key))
                continue;
            const ColorBackup& cb = it.value();
            actor->GetProperty()->SetColor(cb.r, cb.g, cb.b);
            actor->GetProperty()->SetOpacity(cb.opacity);
        }
    }
    m_colorBackup.clear();

    m_geomMap = m_geomMgr->getShapMap();
    m_keyA.clear();
    m_keyB.clear();
    fillComboBoxes();
}
