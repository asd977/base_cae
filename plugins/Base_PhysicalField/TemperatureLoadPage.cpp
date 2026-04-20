#include "TemperatureLoadPage.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "PickManager.h"
#include "ui_TemperatureLoadPage.h"

TemperatureLoadPage::TemperatureLoadPage(QWidget* parent) : BasePage("TemperatureLoadPage")
{
    ui = new Ui::TemperatureLoadPage;
    ui->setupUi(this);
    setupBatchGroupCombo();
}

TemperatureLoadPage::~TemperatureLoadPage() = default;

void TemperatureLoadPage::beforeLoad()
{
    PickManager::instance();
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
                         if (on)
                             PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
                         else
                             PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
                     },
                     Qt::UniqueConnection);
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

void TemperatureLoadPage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

double TemperatureLoadPage::alfaValue() const
{
    return ui->lineEditAlfa->text().toDouble();
}

double TemperatureLoadPage::dtValue() const
{
    return ui->lineEditDT->text().toDouble();
}

void TemperatureLoadPage::setupBatchGroupCombo()
{
    if (!ui)
        return;
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
            &TemperatureLoadPage::onDimChanged, Qt::UniqueConnection);
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

void TemperatureLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dim != dimFilter)
            continue;
        ui->comboBoxGroup->addItem(
            QString("%1  (dim=%2, id=%3)").arg(it.value()).arg(dim).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void TemperatureLoadPage::onDimChanged(int)
{
    refreshGroupCombo();
}

void TemperatureLoadPage::onGroupChanged(int index)
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
