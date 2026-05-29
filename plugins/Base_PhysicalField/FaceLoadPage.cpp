#include "FaceLoadPage.h"

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
#include <algorithm>

#include "PickManager.h"
#include "ui_FaceLoadPage.h"
#include "vtkPointData.h"


namespace
{
static QString boundaryDimText(int dim)
{
    switch (dim)
    {
    case 1: return QStringLiteral("线边界");
    case 2: return QStringLiteral("面边界");
    case 0: return QStringLiteral("点边界");
    default: return QStringLiteral("边界");
    }
}

static QVector<vtkIdType> toVtkKeys(const QVector<MeshIdType>& ids)
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

static MeshTagSet findBoundarySet(MeshModuleControllerService* meshSvc, const MeshTagKey& key)
{
    if (!meshSvc || !key.isValid())
        return {};
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(key.dim);
    for (const MeshTagSet& set : sets)
        if (set.key == key)
            return set;
    return {};
}
}

FaceLoadPage::FaceLoadPage(QWidget* parent) : BasePage("FaceLoadPage"), ui(new Ui::FaceLoadPage)
{
    ui->setupUi(this);
    setupBatchGroupCombo();
}

FaceLoadPage::~FaceLoadPage()
{
    delete ui;
}

void FaceLoadPage::beforeLoad()
{
    PickManager::instance();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
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

void FaceLoadPage::beforeUnload()
{
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

double FaceLoadPage::fxValue()
{
    return ui->lineEditX->text().toDouble();
}
double FaceLoadPage::fyValue()
{
    return ui->lineEditY->text().toDouble();
}
double FaceLoadPage::fzValue()
{
    return ui->lineEditZ->text().toDouble();
}


void FaceLoadPage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(1);
    ui->comboBoxDim->blockSignals(false);
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);
    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FaceLoadPage::onDimChanged, Qt::UniqueConnection);
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this, [this](){ onGroupChanged(ui->comboBoxGroup->currentIndex()); }, Qt::UniqueConnection);
    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0) return;
                    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, row));
                    if (set.key.dim == 1)
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.edgeKeys));
                    else if (set.key.dim == 2)
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.faceKeys));
                }, Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this](){ const int dim = ui->comboBoxDim->currentData().toInt(); PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, dim == 1 ? PickMode::Edge : PickMode::Face); }, Qt::UniqueConnection);
    }
    refreshGroupCombo();
}



void FaceLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(dimFilter);
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
    {
        if (dimFilter == 1 && set.edgeKeys.isEmpty()) continue;
        if (dimFilter == 2 && set.faceKeys.isEmpty()) continue;
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, false), QVariant::fromValue(set.key));
    }
    ui->comboBoxGroup->blockSignals(false);
}


void FaceLoadPage::onDimChanged(int)
{
    refreshGroupCombo();
}


void FaceLoadPage::onGroupChanged(int index)
{
    if (index < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, index));
    if (set.key.dim == 1)
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.edgeKeys), false);
    else if (set.key.dim == 2)
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.faceKeys), false);
}

