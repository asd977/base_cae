#include "BodyForcePage.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <algorithm>

#include "PickManager.h"
#include "ui_BodyForcePage.h"


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 0: return QStringLiteral("点边界");
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkIds(const QVector<MeshIdType>& ids)
{
    QVector<vtkIdType> out;
    out.reserve(ids.size());
    for (MeshIdType id : ids)
        out.push_back(static_cast<vtkIdType>(id));
    return out;
}

static MeshTagKey comboTagKey(QComboBox* combo, int row)
{
    if (!combo || row < 0)
        return {};
    return combo->itemData(row).value<MeshTagKey>();
}

static QString boundaryDisplayText(MeshModuleControllerService* meshSvc, const MeshTagSet& set, bool showDimPrefix)
{
    QString text = meshSvc ? meshSvc->displayTextOfTag(set.key) : set.name.trimmed();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("%1%2").arg(boundaryDimText(set.key.dim)).arg(set.key.id);
    return showDimPrefix ? QStringLiteral("%1：%2").arg(boundaryDimText(set.key.dim), text) : text;
}
}

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

    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     [this](const QString& domain, const QString& activeUuid, GridKind, vtkIdType,
                            int nodeId, double x, double y, double z, const QString& prevUuid)
                     {
                         if (domain != getPageType() || activeUuid != m_uuid)
                             return;
                         ui->labelNodeIdValue->setText(QString::number(nodeId));
                         ui->labelXValue->setText(QString::number(x, 'g', 12));
                         ui->labelYValue->setText(QString::number(y, 'g', 12));
                         ui->labelZValue->setText(QString::number(z, 'g', 12));
                         ui->labelPrevOwnerValue->setText(prevUuid);
                     },
                     Qt::UniqueConnection);

    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    QObject::connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
                     },
                     Qt::UniqueConnection);
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
}

void BodyForcePage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
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
    if (!ui)
        return;

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &BodyForcePage::onDimChanged, Qt::UniqueConnection);

    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0)
                        return;
                    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, row);
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                               toVtkIds(meshSvc->pointIdsOfTag(key)));
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
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
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    const bool showDimPrefix = (dimFilter < 0);

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, showDimPrefix), QVariant::fromValue(set.key));
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
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}

