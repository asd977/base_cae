#include "FaceLoadPage.h"
#include "ui_FaceLoadPage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <ServiceRegistry.h>

#include "PickManager.h"
#include "vtkPointData.h"

#include <services/MeshModuleControllerService.h>
#include <QVBoxLayout>
#include <struct/MeshMap.h>

FaceLoadPage::FaceLoadPage(QWidget* parent)
    : BasePage("FaceLoadPage")
    , ui(new Ui::FaceLoadPage)
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
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
    setupBatchGroupCombo();
}

void FaceLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

double FaceLoadPage::fxValue()  { return ui->lineEditX->text().toDouble(); }
double FaceLoadPage::fyValue()  { return ui->lineEditY->text().toDouble(); }
double FaceLoadPage::fzValue()  { return ui->lineEditZ->text().toDouble(); }

void FaceLoadPage::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(1);
    ui->comboBoxDim->blockSignals(false);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FaceLoadPage::onDimChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FaceLoadPage::onGroupChanged, Qt::UniqueConnection);

    refreshGroupCombo();
}

void FaceLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim != 2) continue;
        if (dimFilter >= 0 && dim != dimFilter) continue;
        ui->comboBoxGroup->addItem(QString("%1  (dim=2, id=%2)").arg(it.value()).arg(physId), physId);
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
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    const QVector<qint64> faceKeys64 = map.bndFacePhysToFaceKeys.value(physId);
    QVector<vtkIdType> faceKeys;
    faceKeys.reserve(faceKeys64.size());
    for (qint64 k : faceKeys64) faceKeys.push_back(vtkIdType(k));

    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, true);
}
