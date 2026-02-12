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
    if (m_groupCombo) return;
    auto* layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (!layout) return;
    m_groupCombo = new QComboBox(this);
    layout->insertWidget(0, m_groupCombo);

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        if (map.physIdToDim.value(physId, -1) != 2) continue;
        m_groupCombo->addItem(QString("%1  (dim=2, id=%2)").arg(it.value()).arg(physId), physId);
    }
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FaceLoadPage::onGroupChanged);
}

void FaceLoadPage::onGroupChanged(int index)
{
    if (!m_groupCombo || index < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = m_groupCombo->itemData(index).toInt();

    const QVector<qint64> faceKeys64 = map.bndFacePhysToFaceKeys.value(physId);
    QVector<vtkIdType> faceKeys;
    faceKeys.reserve(faceKeys64.size());
    for (qint64 k : faceKeys64) faceKeys.push_back(vtkIdType(k));

    PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, faceKeys, true);
}
