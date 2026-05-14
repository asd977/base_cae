#include "LinearForcePage.h"

#include <PickManager.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGridLayout>

#include "ui_LinearForcePage.h"

LinearForcePage::LinearForcePage(QWidget* parent)
    : BasePage("LinearForcePage"), ui(new Ui::LinearForcePage)
{
    ui->setupUi(this);
    setupBatchGroupCombo();
}

LinearForcePage::~LinearForcePage()
{
    delete ui;
}

void LinearForcePage::beforeLoad()
{
    PickManager::instance();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);
    QObject::disconnect(ui->pickEnableCheckBox, nullptr, this, nullptr);
    QObject::connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
                     [this](bool on)
                     {
                         PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
                     },
                     Qt::UniqueConnection);
    ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    setupBatchGroupCombo();
}

void LinearForcePage::beforeUnload()
{
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Edge);
}

double LinearForcePage::fxValue()
{
    return ui->lineEditX->text().toDouble();
}

double LinearForcePage::fyValue()
{
    return ui->lineEditY->text().toDouble();
}

double LinearForcePage::fzValue()
{
    return ui->lineEditZ->text().toDouble();
}

void LinearForcePage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("线", 1);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    // 防止页面切换后重复连接导致槽函数被多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LinearForcePage::onDimChanged, Qt::UniqueConnection);
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
                    if (row < 0)
                        return;
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc)
                        return;
                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    QVector<vtkIdType> edgeKeys;
                    for (qint64 k : map.physicalEdgeKeys(physId))
                        edgeKeys.push_back(vtkIdType(k));
                    PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                               edgeKeys);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Edge);
                },
                Qt::UniqueConnection);
    }
    refreshGroupCombo();
}

void LinearForcePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    const auto nameMap = map.physicalNameMap();
    for (auto it = nameMap.constBegin(); it != nameMap.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physicalDim(physId);
        if (dim != 1)
            continue;
        if (dimFilter != 1)
            continue;
        ui->comboBoxGroup->addItem(QString("%1  (id=%2)").arg(it.value()).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void LinearForcePage::onDimChanged(int)
{
    refreshGroupCombo();
}

void LinearForcePage::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    const QVector<qint64> edgeKeys64 = map.physicalEdgeKeys(physId);
    QVector<vtkIdType> edgeKeys;
    edgeKeys.reserve(edgeKeys64.size());
    for (qint64 k : edgeKeys64)
        edgeKeys.push_back(vtkIdType(k));

    PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, edgeKeys,
                                                false);
}
