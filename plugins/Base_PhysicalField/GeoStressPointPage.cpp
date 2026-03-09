#include "GeoStressPointPage.h"

#include <PickManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDebug>
#include <QMessageBox>
#include <QPushButton>

#include "PhySicalField.h"
#include "ui_GeoStressPointPage.h"

// VTK
#include <vtkUnstructuredGrid.h>

GeoStressPointPage::GeoStressPointPage(QWidget* parent)
    : BasePage("GeoStressPointPage"), ui(new Ui::GeoStressPointPage)
{
    ui->setupUi(this);

    // 默认值
    ui->spinSx->setValue(0.0);
    ui->spinSy->setValue(0.0);
    ui->spinSz->setValue(0.0);
    ui->spinSxy->setValue(0.0);
    ui->spinSxz->setValue(0.0);
    ui->spinSyz->setValue(0.0);

    setupBatchGroupCombo();
}

GeoStressPointPage::~GeoStressPointPage()
{
    delete ui;
}

void GeoStressPointPage::beforeLoad()
{
    PickManager::instance();

    m_dim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (m_dim != 2)
        m_dim = 3;

    applyDimensionUi(m_dim);

    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &GeoStressPointPage::onNodePicked, Qt::UniqueConnection);

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    setupBatchGroupCombo();
}

void GeoStressPointPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);

    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
                        &GeoStressPointPage::onNodePicked);
}

void GeoStressPointPage::applyDimensionUi(int dim)
{
    const bool is2D = (dim == 2);

    auto disableWidget = [&](QWidget* w)
    {
        w->setEnabled(!is2D);
        w->setProperty("dimDisabled", is2D);
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    };

    disableWidget(ui->spinSz);
    disableWidget(ui->spinSxz);
    disableWidget(ui->spinSyz);
    disableWidget(ui->labelSz);
    disableWidget(ui->labelSxz);
    disableWidget(ui->labelSyz);
    disableWidget(ui->labelZ);
    disableWidget(ui->labelZValue);

    if (is2D)
    {
        ui->spinSz->setValue(0.0);
        ui->spinSxz->setValue(0.0);
        ui->spinSyz->setValue(0.0);
        ui->labelZValue->setText("0");
    }
}

void GeoStressPointPage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    // 防止页面切换后重复连接导致槽函数被多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GeoStressPointPage::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = findChild<QPushButton*>("btnBatchAdd");
    if (!batchAddBtn)
    {
        batchAddBtn = new QPushButton(tr("添加"), this);
        batchAddBtn->setObjectName("btnBatchAdd");
        if (auto* host = ui->comboBoxGroup->parentWidget())
        {
            if (auto* lay = host->layout())
                lay->addWidget(batchAddBtn);
        }
    }
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
                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = map.physIdToDim.value(physId, -1);
                    QVector<int> pointIdsInt;
                    if (dim == 0)
                        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
                    else if (dim == 1)
                        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
                    else if (dim == 2)
                        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
                    QVector<vtkIdType> pids;
                    for (int pid : pointIdsInt)
                        pids.push_back(vtkIdType(pid));
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid, pids);
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

void GeoStressPointPage::refreshGroupCombo()
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

void GeoStressPointPage::onDimChanged(int)
{
    refreshGroupCombo();
}

void GeoStressPointPage::onGroupChanged(int index)
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
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, false);
}

double GeoStressPointPage::sxValue() const
{
    return ui->spinSx->value();
}
double GeoStressPointPage::syValue() const
{
    return ui->spinSy->value();
}
double GeoStressPointPage::szValue() const
{
    return ui->spinSz->value();
}
double GeoStressPointPage::sxyValue() const
{
    return ui->spinSxy->value();
}
double GeoStressPointPage::sxzValue() const
{
    return ui->spinSxz->value();
}
double GeoStressPointPage::syzValue() const
{
    return ui->spinSyz->value();
}

void GeoStressPointPage::refreshPrevOwnerTitle(const QString& prevUuid)
{
    // 你说 prevUuid 没有互斥就设为 -1（或空字符串），这里都兼容
    if (prevUuid.isEmpty() || prevUuid == "-1")
    {
        ui->labelPrevOwnerValue->clear();
        return;
    }

    auto* svc = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (!svc)
    {
        ui->labelPrevOwnerValue->setText(prevUuid); // 兜底：显示 uuid
        return;
    }

    const QString title = svc->pageTitleByUuid(prevUuid);
    ui->labelPrevOwnerValue->setText(title.isEmpty() ? prevUuid : title);
}

void GeoStressPointPage::onNodePicked(const QString& domain, const QString& activeUuid, GridKind,
                                      vtkIdType, int nodeId, double x, double y, double z,
                                      const QString& prevUuid)
{
    if (domain != getPageType())
        return;
    if (activeUuid != m_uuid)
        return;

    ui->labelNodeIdValue->setText(QString::number(nodeId));
    ui->labelXValue->setText(QString::number(x, 'g', 12));
    ui->labelYValue->setText(QString::number(y, 'g', 12));

    if (m_dim == 2)
        ui->labelZValue->setText("0");
    else
        ui->labelZValue->setText(QString::number(z, 'g', 12));

    refreshPrevOwnerTitle(prevUuid);
}
