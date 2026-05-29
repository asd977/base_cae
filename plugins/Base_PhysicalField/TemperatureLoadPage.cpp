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

void TemperatureLoadPage::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this, nullptr);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
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
    ui->comboBoxDim->addItem(QStringLiteral("全部边界"), -1);
    ui->comboBoxDim->addItem(QStringLiteral("点边界"), 0);
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

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



void TemperatureLoadPage::refreshGroupCombo()
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
    const MeshTagKey key = comboTagKey(ui->comboBoxGroup, index);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                toVtkIds(meshSvc->pointIdsOfTag(key)), false);
}

