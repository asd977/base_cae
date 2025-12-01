#include "OccBooleanToolWidget.h"
#include "ui_OccBooleanToolWidget.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <GeomManagerInstance.h>
#include <PublicConstants.h>
#include <PreProcessing.h>

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

OccBooleanToolWidget::OccBooleanToolWidget(PreProcessingWidget *preProcessingWidget,QWidget *parent)
    : BasePage(PublicConstants::PAGE_POSTPROCESS_OCCBOOLEN)
    , ui(new Ui::OccBooleanToolWidget)
{
    ui->setupUi(this);

    m_preProcessingWidget = preProcessingWidget;
    // 可以在 .ui 里预先添加 comboBoxOp 的选项，也可以这里添加
    if (ui->comboBoxOp->count() == 0) {
        ui->comboBoxOp->addItem(tr("Cut (A - B)"));     // index 0
        ui->comboBoxOp->addItem(tr("Fuse (A ∪ B)"));    // index 1
        ui->comboBoxOp->addItem(tr("Common (A ∩ B)"));  // index 2
    }

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
}

void OccBooleanToolWidget::beforeLoad()
{
    m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    if (!m_geomMgr) {
        qWarning() << "[OccBooleanToolWidget] GeomManagerInstance is null";
        return;
    }

    // 取一份快照
    m_geomMap = m_geomMgr->getShapMap();

    // 填充下拉框
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

        const ColorBackup& cb = it.value();
        actor->GetProperty()->SetColor(cb.r, cb.g, cb.b);
    }

    m_colorBackup.clear();
    requestRender();
}

QJsonObject OccBooleanToolWidget::saveJson()
{
    QJsonObject obj;
    // 这里先简单保存一下当前选择（看你需不需要更复杂）
    obj["keyA"] = m_keyA;
    obj["keyB"] = m_keyB;
    obj["opIndex"] = ui->comboBoxOp->currentIndex();
    return obj;
}

void OccBooleanToolWidget::loadJson(const QJsonObject &obj)
{
    if (obj.contains("opIndex"))
        ui->comboBoxOp->setCurrentIndex(obj["opIndex"].toInt());

    // A/B 的恢复可以稍微做个查找
    QString keyA = obj.value("keyA").toString();
    QString keyB = obj.value("keyB").toString();

    int indexA = ui->comboBoxA->findText(keyA);
    if (indexA >= 0)
        ui->comboBoxA->setCurrentIndex(indexA);

    int indexB = ui->comboBoxB->findText(keyB);
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
        ui->comboBoxA->addItem(key);
        ui->comboBoxB->addItem(key);
    }

    if (ui->comboBoxA->count() > 0)
        ui->comboBoxA->setCurrentIndex(0);
    if (ui->comboBoxB->count() > 1)
        ui->comboBoxB->setCurrentIndex(1);

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

    if (on) {
        // 首次高亮时，备份原始颜色
        if (!m_colorBackup.contains(key)) {
            ColorBackup cb;
            double rgb[3] = {0.8, 0.8, 0.8};
            prop->GetColor(rgb);
            cb.r = rgb[0];
            cb.g = rgb[1];
            cb.b = rgb[2];
            m_colorBackup.insert(key, cb);
        }

        prop->SetColor(color.redF(), color.greenF(), color.blueF());
    } else {
        if (m_colorBackup.contains(key)) {
            const ColorBackup& cb = m_colorBackup.value(key);
            prop->SetColor(cb.r, cb.g, cb.b);
            // 这里不 remove，让 beforeUnload 一次性清理
        }
    }

    requestRender();
}

void OccBooleanToolWidget::requestRender()
{
    // TODO: 根据你的渲染系统来实现
    // 比如有一个全局的 RenderManager：
    // RenderManager::instance()->renderAll();
    // 或者直接拿到一个 renderer->GetRenderWindow()->Render();
}

// ---------------- 信号槽：选择 A/B 时高亮 ----------------

void OccBooleanToolWidget::on_comboBoxA_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->comboBoxA->count())
        return;

    QString newKey = ui->comboBoxA->itemText(index);

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

    QString newKey = ui->comboBoxB->itemText(index);

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
    case 0: // Cut
        resultShape = OccBooleanTool::Cut(*shapeA, *shapeB);
        break;
    case 1: // Fuse
        resultShape = OccBooleanTool::Fuse(*shapeA, *shapeB);
        break;
    case 2: // Common
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

    TopoDS_Shape* newShape = new TopoDS_Shape(resultShape);
    GeomShapeObject* resultObj = new GeomShapeObject("666", newShape); // 假设构造函数接收 TopoDS_Shape*
    m_geomMgr->appendGeomShapeObject(resultObj);
    m_geomMap.insert("666", resultObj);

    // 设置结果几何的颜色，比如黄色
    vtkActor* resultActor = resultObj->getSurfActor();
    if (resultActor) {
        resultActor->GetProperty()->SetColor(1.0, 1.0, 0.0);
    }

    m_preProcessingWidget->addActor(resultObj->getEdgeActor());
    m_preProcessingWidget->addActor(resultObj->getSurfActor());
    // objA->getEdgeActor();
    // objA->getSurfActor();
    if(opIndex == 0)
    {
        m_preProcessingWidget->removeActor(objA->getEdgeActor());
        m_preProcessingWidget->removeActor(objA->getSurfActor());

        m_geomMgr->removeGeomShapeObject(objA);
    }
    else
    {
        m_preProcessingWidget->removeActor(objA->getEdgeActor());
        m_preProcessingWidget->removeActor(objA->getSurfActor());

        m_geomMgr->removeGeomShapeObject(objA);

        m_preProcessingWidget->removeActor(objB->getEdgeActor());
        m_preProcessingWidget->removeActor(objB->getSurfActor());

        m_geomMgr->removeGeomShapeObject(objB);
    }

    // 你也可以把 A/B 改成新的结果，方便连续操作
    // int idx = ui->comboBoxA->findText(resultKey);
    // if (idx >= 0) ui->comboBoxA->setCurrentIndex(idx);

    requestRender();
}

void OccBooleanToolWidget::on_pushButton_clicked()
{

}

