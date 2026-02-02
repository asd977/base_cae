#include "BodyForcePage.h"
#include "ui_BodyForcePage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDebug>

#include <algorithm>

#include "PickManager.h"

BodyForcePage::BodyForcePage(QWidget* parent)
    : BasePage("BodyForcePage")
{
    auto* ui = new Ui::BodyForcePage;
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
    if (m_ii) m_ii->setValue(1);
}

BodyForcePage::~BodyForcePage() = default;

void BodyForcePage::beforeLoad()
{
    // Part3：体积力按 JD(node_id) -> 用 Node 拾取
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
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
    const auto idsSolid = PickManager::instance().getIdList(m_type, m_uuid, PickMode::Node, GridKind::Solid);
    const auto idsLine  = PickManager::instance().getIdList(m_type, m_uuid, PickMode::Node, GridKind::Line);

    QSet<int> jdSet;
    for (auto v : idsSolid) jdSet.insert(int(v));
    for (auto v : idsLine)  jdSet.insert(int(v));

    // ✅ 没选任何节点：按“均布体力”输出 NFB=1, JD=0
    if (jdSet.isEmpty())
    {
        Part3Row r;
        r.jd = 0;
        r.fbx = fbx; r.fby = fby; r.fbz = fbz;
        rows.push_back(r);
        return rows;
    }

    // 有选择：每个 JD 一行（同一组分量）
    QVector<int> jds = jdSet.values();
    std::sort(jds.begin(), jds.end());

    rows.reserve(jds.size());
    for (int jd : jds)
    {
        Part3Row r;
        r.jd = jd;
        r.fbx = fbx; r.fby = fby; r.fbz = fbz;
        rows.push_back(r);
    }
    return rows;
}
