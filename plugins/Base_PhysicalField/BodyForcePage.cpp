#include "BodyForcePage.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <CustomComboBox.h>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <algorithm>

#include "PickManager.h"
#include "ui_BodyForcePage.h"

BodyForcePage::BodyForcePage(QWidget* parent) : BasePage("BodyForcePage")
{
    ui = new Ui::BodyForcePage;
    ui->setupUi(this);

    m_fbx = ui->spinFbx;
    m_fby = ui->spinFby;
    m_fbz = ui->spinFbz;

    // 兼容：你当前 UI 没有 spinII，但你以后加上也无需改代码
    m_ii = this->findChild<QSpinBox*>("spinII");

    // 默认值
    m_fbx->setValue(0.0);
    m_fby->setValue(0.0);
    m_fbz->setValue(-9.81);
    if (m_ii)
        m_ii->setValue(1);

    setupBatchGroupCombo();
}

BodyForcePage::~BodyForcePage()
{
    delete ui;
}

void BodyForcePage::beforeLoad()
{
    // Part3：体积力按 JD(node_id) -> 用 Node 拾取
    PickManager::instance();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();
}

void BodyForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

int BodyForcePage::ii() const
{
    return m_ii ? m_ii->value() : 1;
}

QVector<BodyForcePage::Part3Row> BodyForcePage::buildPart3Rows() const
{
    QVector<Part3Row> rows;

    const double fbx = m_fbx ? m_fbx->value() : 0.0;
    const double fby = m_fby ? m_fby->value() : 0.0;
    const double fbz = m_fbz ? m_fbz->value() : 0.0;

    // ✅ 取选中节点（PickManager::getIdList(Node) 目前返回的是 node_id）
    // solid + line 都合并（去重）
    const auto idsSolid =
        PickManager::instance().getIdList(m_type, m_uuid, PickMode::Node, GridKind::Solid);
    const auto idsLine =
        PickManager::instance().getIdList(m_type, m_uuid, PickMode::Node, GridKind::Line);

    QSet<int> jdSet;
    for (auto v : idsSolid)
        jdSet.insert(int(v));
    for (auto v : idsLine)
        jdSet.insert(int(v));

    // ✅ 没选任何节点：按“均布体力”输出 NFB=1, JD=0
    if (jdSet.isEmpty())
    {
        Part3Row r;
        r.jd = 0;
        r.fbx = fbx;
        r.fby = fby;
        r.fbz = fbz;
        rows.push_back(r);
        return rows;
    }

    // 有选择：每个 JD 一行（同一组分量）
    QVector<int> jds = QVector<int>::fromList(jdSet.values());
    std::sort(jds.begin(), jds.end());

    rows.reserve(jds.size());
    for (int jd : jds)
    {
        Part3Row r;
        r.jd = jd;
        r.fbx = fbx;
        r.fby = fby;
        r.fbz = fbz;
        rows.push_back(r);
    }
    return rows;
}

void BodyForcePage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    QObject::connect(
        ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int idx)
        {
            onDimChanged(idx);
        },
        Qt::UniqueConnection);
    QObject::connect(
        ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int idx)
        {
            onGroupChanged(idx);
        },
        Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::connect(groupCombo, &CustomComboBox::itemHovered, this,
                         [this](const int& row)
                         {
                             onGroupChanged(row);
                         },
                         Qt::UniqueConnection);
        QObject::connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                         [this]()
                         {
                             onGroupChanged(ui->comboBoxGroup->currentIndex());
                         },
                         Qt::UniqueConnection);
    }
    refreshGroupCombo();
}

void BodyForcePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dimFilter != dim)
            continue;
        ui->comboBoxGroup->addItem(
            QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void BodyForcePage::onDimChanged(int)
{
    refreshGroupCombo();
}

void BodyForcePage::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);
    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1)
        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2)
        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else
        return;

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
        pids.push_back(vtkIdType(pid));
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, true);
}
