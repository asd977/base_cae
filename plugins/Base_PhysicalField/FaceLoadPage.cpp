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

FaceLoadPage::FaceLoadPage(QWidget* parent)
    : BasePage("FaceLoadPage")
{
    auto* ui = new Ui::FaceLoadPage;
    ui->setupUi(this);

    m_mode = ui->comboMode;
    m_ke   = ui->spinKE;

    m_fx   = ui->spinFx;
    m_fy   = ui->spinFy;
    m_fz   = ui->spinFz;

    m_ii   = ui->spinII;

    m_btnGenerate = ui->btnGenerate;

    // 默认
    m_mode->setCurrentIndex(0); // 0: LXJMET=0
    m_ke->setValue(0);
    m_fx->setValue(0.0);
    m_fy->setValue(0.0);
    m_fz->setValue(0.0);
    m_ii->setValue(1);

    connect(m_btnGenerate, &QPushButton::clicked,
            this, &FaceLoadPage::onGenerate);

    // UX：当选择 LXJMET=1 才允许填 KE
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        const bool needKE = (idx == 1);
        m_ke->setEnabled(needKE);
    });
    m_ke->setEnabled(false);
}

FaceLoadPage::~FaceLoadPage() = default;

void FaceLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);
}

void FaceLoadPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}


void FaceLoadPage::onGenerate()
{
    dumpGeneratedToDebug();
}

void FaceLoadPage::dumpGeneratedToDebug()
{
    // 1) 取“面集合”（faceKey）
    const auto faceKeys = PickManager::instance().getIdList(getPageType(), m_uuid, PickMode::Face);
    if (faceKeys.isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("请先在视图中点选一些面（面力作用面）。"));
        return;
    }

    // 2) 网格
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
    if (!grid)
    {
        QMessageBox::warning(this, tr("错误"), tr("当前没有有效网格（vtkUnstructuredGrid）。"));
        return;
    }

    // （可选）如果你有 node_id，就把 vtkPid 映射成求解器节点号
    vtkIntArray* nodeIdArr = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));

    const int LXJMET = (m_mode->currentIndex() == 0) ? 0 : 1;

    const double Fx = m_fx->value();
    const double Fy = m_fy->value();
    const double Fz = m_fz->value();
    const int II    = m_ii->value();

    const int NFT = faceKeys.size();

    qDebug().noquote() << "========== IIWL.DAT Part6 (Face Load) ==========";
    qDebug().noquote() << QString("%1  %2").arg(NFT).arg(LXJMET);
    qDebug().noquote() << "FACES: (i,NFAJD) then J Fx Fy Fz ...   [unit: N/m^2]  II=" + QString::number(II);

    int faceIdx = 0;
    for (auto faceKey : faceKeys)
    {
        ++faceIdx;

        const vtkIdType cellId = PickManager::instance().unpackFaceCellId(faceKey);
        const int faceLocalId  = PickManager::instance().unpackFaceLocalId(faceKey);

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell) continue;

        QVector<vtkIdType> pids;

        if (cell->GetCellDimension() == 2)
        {
            // 2D：cell 自己就是面
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3) continue;
            pids.reserve(ids->GetNumberOfIds());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                pids.push_back(ids->GetId(i));
        }
        else
        {
            // 3D：指定 faceLocalId
            if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
                continue;

            vtkCell* face = cell->GetFace(faceLocalId);
            if (!face) continue;

            vtkIdList* ids = face->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3) continue;

            pids.reserve(ids->GetNumberOfIds());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                pids.push_back(ids->GetId(i));
        }

        const int NFAJD = pids.size();

        if (LXJMET == 0)
        {
            qDebug().noquote() << QString("%1, %2").arg(faceIdx).arg(NFAJD);
        }
        else
        {
            // 主流：KE 用“该面所属的单元编号”
            // 这里直接用 cellId 当 KE；如果你有 elem_id 可替换
            const int KE = int(cellId);
            qDebug().noquote() << QString("%1  %2, %3").arg(KE).arg(faceIdx).arg(NFAJD);
        }

        for (auto pid : pids)
        {
            // J：优先 node_id；否则退化用 vtk point id
            const int J = nodeIdArr ? nodeIdArr->GetValue(pid) : int(pid);

            const QString line = QString("%1  %2  %3  %4")
                                     .arg(J)
                                     .arg(Fx, 0, 'f', 6)
                                     .arg(Fy, 0, 'f', 6)
                                     .arg(Fz, 0, 'f', 6);

            qDebug().noquote() << line;
        }
    }

    qDebug().noquote() << "  [note] KE uses owner cellId now. If you have CellData['elem_id'], replace KE with that.";
    qDebug().noquote() << "================================================";
}
