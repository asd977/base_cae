#include "OccBooleanToolWidget.h"
#include "ui_OccBooleanToolWidget.h"

#include <BOPAlgo_Splitter.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>

#include <PreProcessing.h>

#include <QDebug>
#include <QColor>
#include <QUuid>

#include <vtkActor.h>
#include <vtkProperty.h>
#include <BRepBuilderAPI_Sewing.hxx>

// -------------------- OccBooleanTool 实现 --------------------

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

TopoDS_Shape OccBooleanTool::Splitter(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    // 1. 检查输入是否为空
    if (a.IsNull() && b.IsNull())
        return TopoDS_Shape();

    // 2. 实例化 BRepBuilderAPI_Sewing 类
    // Sewing 工具用于连接松散的面或壳
    BRepBuilderAPI_Sewing sewingOp;

    // 3. 添加要缝合的第一个形状 (a)
    if (!a.IsNull())
    {
        sewingOp.Add(a);
    }

    // 4. 添加要缝合的第二个形状 (b)
    if (!b.IsNull())
    {
        sewingOp.Add(b);
    }

    // 5. 【可选但推荐】设置容差
    // 可以设置一个容差，在这个距离内的边会被认为是重合的，从而进行缝合。
    // 例如：sewingOp.SetTolerance(1e-3);

    // 6. 执行操作
    sewingOp.Perform();


        return sewingOp.SewedShape();

}


// -------------------- OccBooleanToolWidget 实现 --------------------

OccBooleanToolWidget::OccBooleanToolWidget(BooleanOp opType, QWidget *parent)
    : BasePage(PublicConstants::kPagePostProcessOccBoolean)
    , ui(new Ui::OccBooleanToolWidget)
    , m_opType(opType)
{
    Q_UNUSED(parent);

    ui->setupUi(this);

    // 操作类型：0-Cut, 1-Fuse, 2-Common
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

OccBooleanToolWidget::~OccBooleanToolWidget()
{
    delete ui;
}

void OccBooleanToolWidget::initConnections()
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

void OccBooleanToolWidget::beforeLoad()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr) {
        qWarning() << "[OccBooleanToolWidget] GeomManagerInstance is null";
        return;
    }

    m_colorBackup.clear();

    // 取一份当前几何列表快照
    m_geomMap = m_geomMgr->getShapMap();

    fillComboBoxes();
}

void OccBooleanToolWidget::beforeUnload()
{
    // 离开页面时，把所有被改过的颜色恢复
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

QJsonObject OccBooleanToolWidget::saveJson()
{
    QJsonObject obj;
    obj["keyA"]    = m_keyA;
    obj["keyB"]    = m_keyB;
    return obj;
}

void OccBooleanToolWidget::loadJson(const QJsonObject &obj)
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

void OccBooleanToolWidget::fillComboBoxes()
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

    // 手动触发一次，以便高亮
    on_comboBoxA_currentIndexChanged(ui->comboBoxA->currentIndex());
    on_comboBoxB_currentIndexChanged(ui->comboBoxB->currentIndex());
}

void OccBooleanToolWidget::setHighlight(const QString& key,
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
        // 首次高亮时，备份原始颜色
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
            // 不移除，让 beforeUnload 统一清理；如果你想实时清掉，也可以 remove
        }
    }

    requestRender();
}

void OccBooleanToolWidget::setDimmed(const QString &key, bool on)
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

void OccBooleanToolWidget::requestRender()
{
    // TODO: 根据你的渲染系统来实现
    // 比如：
    // RenderManager::instance()->renderAll();
    // 或者：
    // if (auto rw = m_preProcessingWidget->renderWindow()) rw->Render();
}

QString OccBooleanToolWidget::opDisplayName() const
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

QString OccBooleanToolWidget::makeResultName() const
{
    static QHash<int, int> counters;
    int key = static_cast<int>(m_opType);
    int count = counters.value(key, 0) + 1;
    counters.insert(key, count);
    return QStringLiteral("%1(%2)").arg(opDisplayName()).arg(count);
}

// ---------------- 信号槽：选择 A/B 时高亮 ----------------

void OccBooleanToolWidget::on_comboBoxA_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->comboBoxA->count())
        return;

    QString newKey = ui->comboBoxA->itemData(index).toString();
    if (newKey.isEmpty()) {
        newKey = ui->comboBoxA->itemText(index);
    }

    // 先把旧的 A 恢复原色（前提是它不是当前 B）
    if (!m_keyA.isEmpty() && m_keyA != m_keyB)
        setHighlight(m_keyA, QColor(), false);

    m_keyA = newKey;

    // 给新的 A 上红色（如果 A/B 不同）
    if (!m_keyA.isEmpty() && m_keyA != m_keyB)
        setHighlight(m_keyA, Qt::red, true);
}

void OccBooleanToolWidget::on_comboBoxB_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->comboBoxB->count())
        return;

    QString newKey = ui->comboBoxB->itemData(index).toString();
    if (newKey.isEmpty()) {
        newKey = ui->comboBoxB->itemText(index);
    }

    // 先把旧的 B 恢复原色（前提是它不是当前 A）
    if (!m_keyB.isEmpty() && m_keyB != m_keyA)
        setHighlight(m_keyB, QColor(), false);

    m_keyB = newKey;

    // 给新的 B 上蓝色（如果 A/B 不同）
    if (!m_keyB.isEmpty() && m_keyB != m_keyA)
        setHighlight(m_keyB, Qt::blue, true);
}

// ---------------- Apply：执行布尔运算 ----------------

void OccBooleanToolWidget::on_pushButtonApply_clicked()
{

    if (m_keyA.isEmpty() || m_keyB.isEmpty()) {
        qWarning() << "[OccBooleanToolWidget] A or B key is empty.";
        return;
    }

    if (m_keyA == m_keyB) {
        qWarning() << "[OccBooleanToolWidget] A and B are the same object.";
        return;
    }

    GeomShapeObject* objA = m_geomMap.value(m_keyA, nullptr);
    GeomShapeObject* objB = m_geomMap.value(m_keyB, nullptr);
    if (!objA || !objB) {
        qWarning() << "[OccBooleanToolWidget] invalid GeomShapeObject for A or B.";
        return;
    }

    TopoDS_Shape* shapeA = objA->getShape();
    TopoDS_Shape* shapeB = objB->getShape();
    if (!shapeA || !shapeB || shapeA->IsNull() || shapeB->IsNull()) {
        qWarning() << "[OccBooleanToolWidget] A or B shape is null.";
        return;
    }

    int opIndex = ui->comboBoxOp->currentIndex();
    TopoDS_Shape resultShape;

    switch (opIndex)
    {
    case 0: // Cut (A - B)
        resultShape = OccBooleanTool::Cut(*shapeA, *shapeB);
        break;
    case 1: // Fuse (A ∪ B)
        resultShape = OccBooleanTool::Fuse(*shapeA, *shapeB);
        break;
    case 2: // Common (A ∩ B)
        resultShape = OccBooleanTool::Common(*shapeA, *shapeB);
        break;
    default:
        qWarning() << "[OccBooleanToolWidget] invalid op index:" << opIndex;
        return;
    }

    if (resultShape.IsNull()) {
        qWarning() << "[OccBooleanToolWidget] Boolean result is null.";
        return;
    }

    // 生成新的几何对象并注册
    QString resultUuid = QUuid::createUuid().toString();
    QString resultName = makeResultName();
    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject(resultUuid, newShape, resultName);

    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert(resultUuid, resultObj);

    // 设置结果几何的颜色，比如黄色
    if (vtkSmartPointer<vtkActor> resultActor = resultObj->getSurfActor()) {
        resultActor->GetProperty()->SetColor(1.0, 1.0, 0.0); // 黄
    }

    ui->comboBoxA->addItem(resultName, resultUuid);
    ui->comboBoxB->addItem(resultName, resultUuid);

    // if (vtkSmartPointer<vtkActor> edgeActor = resultObj->getEdgeActor())
    //     m_preProcessingWidget->addActor(edgeActor);
    // if (vtkSmartPointer<vtkActor> surfActor = resultObj->getSurfActor())
    //     m_preProcessingWidget->addActor(surfActor);



    // // 删除旧的 A/B 几何
    // if (opIndex == 0) {
    //     // Cut：删除 A，保留 B
    //     if (vtkSmartPointer<vtkActor> aEdge = objA->getEdgeActor())
    //         m_preProcessingWidget->removeActor(aEdge);
    //     if (vtkSmartPointer<vtkActor> aSurf = objA->getSurfActor())
    //         m_preProcessingWidget->removeActor(aSurf);

    //     m_geomMgr->removeGeomShapeObject(objA);
    //     m_geomMap.remove(m_keyA);
    //     m_colorBackup.remove(m_keyA);
    // } else {
    //     // Fuse / Common / Fuse+keepCommon：删除 A 和 B
    //     if (vtkSmartPointer<vtkActor> aEdge = objA->getEdgeActor())
    //         m_preProcessingWidget->removeActor(aEdge);
    //     if (vtkSmartPointer<vtkActor> aSurf = objA->getSurfActor())
    //         m_preProcessingWidget->removeActor(aSurf);
    //     m_geomMgr->removeGeomShapeObject(objA);
    //     m_geomMap.remove(m_keyA);
    //     m_colorBackup.remove(m_keyA);

    //     if (vtkSmartPointer<vtkActor> bEdge = objB->getEdgeActor())
    //         m_preProcessingWidget->removeActor(bEdge);
    //     if (vtkSmartPointer<vtkActor> bSurf = objB->getSurfActor())
    //         m_preProcessingWidget->removeActor(bSurf);
    //     m_geomMgr->removeGeomShapeObject(objB);
    //     m_geomMap.remove(m_keyB);
    //     m_colorBackup.remove(m_keyB);
    // }

    int idxA = ui->comboBoxA->findData(resultUuid);
    if (idxA >= 0)
        ui->comboBoxA->setCurrentIndex(idxA);
    int idxB = ui->comboBoxB->findData(resultUuid);
    if (idxB >= 0)
        ui->comboBoxB->setCurrentIndex(idxB);

    setHighlight(m_keyA, QColor(), false);
    setHighlight(m_keyB, QColor(), false);
    setDimmed(m_keyA, true);
    setDimmed(m_keyB, true);

    requestRender();
}

void OccBooleanToolWidget::on_pushButton_clicked()
{
    // 这里假设 pushButton 是“刷新几何列表”
    if (!m_geomMgr) {
        m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
        if (!m_geomMgr) {
            qWarning() << "[OccBooleanToolWidget] GeomManagerInstance is null (refresh).";
            return;
        }
    }

    // 恢复已有高亮颜色
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
