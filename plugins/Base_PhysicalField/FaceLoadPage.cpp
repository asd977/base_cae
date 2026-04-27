#include "FaceLoadPage.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
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
    setupBatchGroupCombo();
}

void FaceLoadPage::beforeUnload()
{
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
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    const MshPickMap map = meshSvc ? meshSvc->mshPickMapSolid() : MshPickMap{};

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();

    if (!map.physicalEdgeKeyMap().isEmpty())
    {
        ui->comboBoxDim->addItem("线", 1);
    }
    if (!map.physicalFaceKeyMap().isEmpty())
    {
        ui->comboBoxDim->addItem("面", 2);
    }

    if (ui->comboBoxDim->count() == 0)
    {
        ui->comboBoxDim->addItem("面", 2);
    }

    ui->comboBoxDim->setCurrentIndex(0);
    ui->comboBoxDim->blockSignals(false);

    // 防止页面切换后重复连接导致槽函数被多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &FaceLoadPage::onDimChanged, Qt::UniqueConnection);
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
                    const int dim = ui->comboBoxDim->currentData().toInt();
                    if (dim == 1)
                    {
                        QVector<vtkIdType> edgeKeys;
                        for (qint64 k : map.physicalEdgeKeys(physId))
                            edgeKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   edgeKeys);
                    }
                    else
                    {
                        QVector<vtkIdType> faceKeys;
                        for (qint64 k : map.physicalFaceKeys(physId))
                            faceKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   faceKeys);
                    }
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    const int dim = ui->comboBoxDim->currentData().toInt();
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  dim == 1 ? PickMode::Edge : PickMode::Face);
                },
                Qt::UniqueConnection);
    }

    refreshGroupCombo();
}

void FaceLoadPage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    auto addByMap = [&](const QHash<int, QVector<qint64>>& keyMap)
    {
        QList<int> physIds = keyMap.keys();
        std::sort(physIds.begin(), physIds.end());
        for (int physId : physIds)
        {
            const QString name = map.physicalName(physId);
            ui->comboBoxGroup->addItem(QString("%1  (id=%2)").arg(name).arg(physId), physId);
        }
    };

    if (dimFilter == 1)
        addByMap(map.physicalEdgeKeyMap());
    else if (dimFilter == 2)
        addByMap(map.physicalFaceKeyMap());

    ui->comboBoxGroup->blockSignals(false);
}

void FaceLoadPage::onDimChanged(int)
{
    refreshGroupCombo();
}

void FaceLoadPage::onGroupChanged(int index)
{
    if (index < 0)
        return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;
    const MshPickMap& map = meshSvc->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();

    const int dim = ui->comboBoxDim->currentData().toInt();
    if (dim == 1)
    {
        const QVector<qint64> edgeKeys64 = map.physicalEdgeKeys(physId);
        QVector<vtkIdType> edgeKeys;
        edgeKeys.reserve(edgeKeys64.size());
        for (qint64 k : edgeKeys64)
            edgeKeys.push_back(vtkIdType(k));
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    edgeKeys, false);
    }
    else
    {
        const QVector<qint64> faceKeys64 = map.physicalFaceKeys(physId);
        QVector<vtkIdType> faceKeys;
        faceKeys.reserve(faceKeys64.size());
        for (qint64 k : faceKeys64)
            faceKeys.push_back(vtkIdType(k));
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    faceKeys, false);
    }
}
