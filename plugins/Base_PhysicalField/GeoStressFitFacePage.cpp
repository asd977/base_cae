#include "GeoStressFitFacePage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

#include "PickManager.h"
#include "ui_GeoStressFitFacePage.h"
#include "vtkPointData.h"

static inline QString ensureDatSuffix(QString n)
{
    n = n.trimmed();
    if (n.isEmpty())
        return n;
    if (!n.endsWith(".DAT", Qt::CaseInsensitive))
        n += ".DAT";
    return n;
}

QString GeoStressFitFacePage::fnameForExport() const
{
    QString n = ensureDatSuffix(fname());
    if (!n.isEmpty())
        return n;
    // 没填就自动命名
    return QString("LEFT_%1.DAT").arg(m_uuid);
}

GeoStressFitFacePage::GeoStressFitFacePage(QWidget* parent)
    : BasePage("GeoStressFitFacePage"), ui(new Ui::GeoStressFitFacePage)
{
    ui->setupUi(this);

    // 默认值：2D 默认选边，3D 默认选面（用户仍可改）
    ensureDefaultPickTarget();

    connect(ui->radionButtonEdge, &QRadioButton::toggled, this,
            &GeoStressFitFacePage::onPickTargetChanged);
    connect(ui->radionButtonFace, &QRadioButton::toggled, this,
            &GeoStressFitFacePage::onPickTargetChanged);
}

GeoStressFitFacePage::~GeoStressFitFacePage()
{
    delete ui;
}

void GeoStressFitFacePage::ensureDefaultPickTarget()
{
    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (!ui->radionButtonEdge->isChecked() && !ui->radionButtonFace->isChecked())
    {
        if (NDIM == 2)
            ui->radionButtonEdge->setChecked(true);
        else
            ui->radionButtonFace->setChecked(true);
    }
}

PickMode GeoStressFitFacePage::decidePickMode() const
{
    // 用户决定：Edge/Face
    if (ui->radionButtonFace->isChecked())
        return PickMode::Face;
    return PickMode::Edge;
}

void GeoStressFitFacePage::beforeLoad()
{
    ensureDefaultPickTarget();
    m_activeMode = decidePickMode();
    PickManager::instance().activate(getPageType(), m_uuid, m_activeMode);

    // 1) 先刷新下拉框内容（按当前模式：Edge / Face）
    refreshGroupCombo();

    // 防止页面切换后重复连接导致槽函数被触发多次
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    // 2) 绑定“下拉框改变” -> 批量设置 keys
    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GeoStressFitFacePage::onDimComboChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &GeoStressFitFacePage::onGroupComboChanged, Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    onGroupComboChanged(row);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    onGroupComboChanged(ui->comboBoxGroup->currentIndex());
                },
                Qt::UniqueConnection);
    }

    // 3) 进入页面时，默认把当前选中的组应用一次（立刻高亮）
    onGroupComboChanged(ui->comboBoxGroup->currentIndex());
}

void GeoStressFitFacePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& mapS = meshSvc->mshPickMapSolid();
    //    const MshPickMap& mapL = meshSvc->mshPickMapLine();

    const int wantDim = ui->comboBoxDim->currentData().toInt() >= 0
                            ? ui->comboBoxDim->currentData().toInt()
                            : (pickEdge() ? 1 : 2);

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(wantDim == 1 ? 1 : 2);
    ui->comboBoxDim->blockSignals(false);

    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    // 合并两份 map 的 physId（避免只在 line 或只在 solid 的组漏掉）
    QSet<int> allPhys;
    for (auto it = mapS.physIdToDim.constBegin(); it != mapS.physIdToDim.constEnd(); ++it)
        if (it.value() == wantDim)
            allPhys.insert(it.key());
    //    for (auto it = mapL.physIdToDim.constBegin(); it !=
    //    mapL.physIdToDim.constEnd(); ++it)
    //        if (it.value() == wantDim) allPhys.insert(it.key());

    QVector<int> ids = allPhys.values().toVector();
    std::sort(ids.begin(), ids.end());

    for (int physId : ids)
    {
        // 显示名：优先用 PhysicalNames 的 name，否则 fallback
        QString name = mapS.physIdToName.value(physId);
        //        if (name.isEmpty()) name = mapL.physIdToName.value(physId);
        if (name.isEmpty())
            name = QString("phys_%1").arg(physId);

        // itemData 存 physId，后面取出来查 map
        ui->comboBoxGroup->addItem(name, physId);
    }

    ui->comboBoxGroup->blockSignals(false);
}

static inline QVector<vtkIdType> toVtkIds(const QVector<qint64>& in)
{
    QVector<vtkIdType> out;
    out.reserve(in.size());
    for (qint64 v : in)
        out.push_back(static_cast<vtkIdType>(v));
    return out;
}

void GeoStressFitFacePage::onDimComboChanged(int)
{
    refreshGroupCombo();
    onGroupComboChanged(ui->comboBoxGroup->currentIndex());
}

void GeoStressFitFacePage::onGroupComboChanged(int idx)
{
    if (idx < 0)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& mapS = meshSvc->mshPickMapSolid();

    const int physId = ui->comboBoxGroup->itemData(idx).toInt();

    PickManager& pm = PickManager::instance();
    const QString domain = getPageType();
    const QString uuid = m_uuid;

    const bool replace = true; // 下拉框切组：替换当前选择

    if (pickEdge())
    {
        // dim=1：edgeKeys（只用 solid）
        const QVector<vtkIdType> keysSolid = toVtkIds(mapS.bndEdgePhysToEdgeKeys.value(physId));

        pm.setSelectedEdgeKeys(domain, uuid, GridKind::Solid, keysSolid, replace);

        // 可选：清空 Line 侧（防止你之前在别的网格上选过）
        pm.setSelectedEdgeKeys(domain, uuid, GridKind::Line, {}, true);
    }
    else
    {
        // dim=2：faceKeys（只用 solid）
        const QVector<vtkIdType> keysSolid = toVtkIds(mapS.bndFacePhysToFaceKeys.value(physId));

        pm.setSelectedFaceKeys(domain, uuid, GridKind::Solid, keysSolid, replace);

        // 可选：清空 Line 侧
        pm.setSelectedFaceKeys(domain, uuid, GridKind::Line, {}, true);
    }
}

void GeoStressFitFacePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, m_activeMode);
}

void GeoStressFitFacePage::onPickTargetChanged()
{
    PickMode newMode = decidePickMode();
    if (newMode == m_activeMode)
        return;

    PickManager::instance().clearSelection(getPageType(), m_uuid, m_activeMode);

    PickManager::instance().deactivate(getPageType(), m_uuid, m_activeMode);
    m_activeMode = newMode;
    PickManager::instance().activate(getPageType(), m_uuid, m_activeMode);

    refreshGroupCombo();
    onGroupComboChanged(ui->comboBoxGroup->currentIndex());
}

QString GeoStressFitFacePage::fname() const
{
    return ui->editFname->text().trimmed();
}
int GeoStressFitFacePage::idir() const
{
    // combo: 0->1, 1->2, 2->3
    return ui->comboIdir->currentIndex() + 1;
}

bool GeoStressFitFacePage::exportLeftDatFile()
{
    PickManager& pm = PickManager::instance();

    QString domain = this->getPageType();
    const QString uuid = m_uuid;

    const bool wantEdge = pickEdge();
    const PickMode mode = wantEdge ? PickMode::Edge : PickMode::Face;

    // 取 solid/line 两套
    const QVector<vtkIdType> keysSolid = pm.getIdList(domain, uuid, mode, GridKind::Solid);
    const QVector<vtkIdType> keysLine = pm.getIdList(domain, uuid, mode, GridKind::Line);

    QVector<vtkIdType> keysS = keysSolid;
    QVector<vtkIdType> keysL = keysLine;
    std::sort(keysS.begin(), keysS.end());
    std::sort(keysL.begin(), keysL.end());

    struct Row
    {
        int jd1 = 0, jd2 = 0, jd3 = 0, jd4 = 0;
    };
    QVector<Row> rows;
    rows.reserve(keysS.size() + keysL.size());

    auto pushRowFromIds = [&](const QVector<int>& nodeIds)
    {
        if (nodeIds.size() < 2)
            return;
        Row r;
        r.jd1 = nodeIds.value(0, 0);
        r.jd2 = nodeIds.value(1, r.jd1);
        r.jd3 = nodeIds.value(2, r.jd2);
        r.jd4 = nodeIds.value(3, r.jd3);
        rows.push_back(r);
    };

    auto nodeIdsFromEdgeKey = [&](vtkUnstructuredGrid* g, vtkIntArray* nodeArr,
                                  vtkIdType edgeKey) -> QVector<int>
    {
        QVector<int> out;
        if (!g || !nodeArr)
            return out;

        const vtkIdType cellId = pm.unpackEdgeCellId(edgeKey);
        const int edgeLocalId = pm.unpackEdgeLocalId(edgeKey);

        vtkCell* cell = g->GetCell(cellId);
        if (!cell)
            return out;

        vtkIdList* ids = nullptr;
        if (cell->GetCellDimension() == 1 || edgeLocalId == 15)
        {
            ids = cell->GetPointIds();
        }
        else
        {
            if (edgeLocalId < 0 || edgeLocalId >= cell->GetNumberOfEdges())
                return out;
            vtkCell* e = cell->GetEdge(edgeLocalId);
            if (!e)
                return out;
            ids = e->GetPointIds();
        }
        if (!ids || ids->GetNumberOfIds() < 2)
            return out;

        // ✅ 二次边可能 3 点，保留前 4 个（LEFT.DAT 最多 JD1..JD4）
        const int n = int(ids->GetNumberOfIds());
        const int m = std::min(n, 4);
        out.reserve(m);
        for (int i = 0; i < m; ++i)
        {
            const vtkIdType pid = ids->GetId(i);
            out.push_back(nodeArr->GetValue(int(pid)));
        }
        return out;
    };

    auto nodeIdsFromFaceKey = [&](vtkUnstructuredGrid* g, vtkIntArray* nodeArr,
                                  vtkIdType faceKey) -> QVector<int>
    {
        QVector<int> out;
        if (!g || !nodeArr)
            return out;

        const vtkIdType cellId = pm.unpackFaceCellId(faceKey);
        const int faceLocalId = pm.unpackFaceLocalId(faceKey);

        vtkCell* cell = g->GetCell(cellId);
        if (!cell)
            return out;

        vtkIdList* ids = nullptr;

        if (cell->GetCellDimension() == 2)
        {
            ids = cell->GetPointIds();
        }
        else
        {
            if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
                return out;
            vtkCell* f = cell->GetFace(faceLocalId);
            if (!f)
                return out;
            ids = f->GetPointIds();
        }
        if (!ids || ids->GetNumberOfIds() < 3)
            return out;

        // ✅ 只取角点（LEFT.DAT JD1..JD4）
        // VTK_QUADRATIC_QUAD 的前 4 个通常是角点；三角面则 3 个
        const int n = int(ids->GetNumberOfIds());
        const int m = std::min(n, 4);
        out.reserve(m);
        for (int i = 0; i < m; ++i)
        {
            const vtkIdType pid = ids->GetId(i);
            out.push_back(nodeArr->GetValue(int(pid)));
        }
        return out;
    };

    // grid + node_id array
    vtkUnstructuredGrid* gS = pm.solidGrid();
    vtkUnstructuredGrid* gL = pm.lineGrid();
    vtkIntArray* nS =
        gS ? vtkIntArray::SafeDownCast(gS->GetPointData()->GetArray("node_id")) : nullptr;
    vtkIntArray* nL =
        gL ? vtkIntArray::SafeDownCast(gL->GetPointData()->GetArray("node_id")) : nullptr;

    auto consume =
        [&](vtkUnstructuredGrid* g, vtkIntArray* arr, const QVector<vtkIdType>& keys, bool solid)
    {
        if (!g || !arr)
            return;
        for (vtkIdType k : keys)
        {
            QVector<int> nodeIds =
                wantEdge ? nodeIdsFromEdgeKey(g, arr, k) : nodeIdsFromFaceKey(g, arr, k);
            pushRowFromIds(nodeIds);
        }
    };

    consume(gS, nS, keysS, true);
    consume(gL, nL, keysL, false);

    // 写文件
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(projectDir);
    if (!wd.exists("PREP"))
        wd.mkpath("PREP");

    const QString fileName = fnameForExport();
    const QString path = wd.filePath("PREP/" + fileName);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&f);

    const int NJX = rows.size();
    const int MAKOD = 5; // 先按你样例写 5
    const int KE2 = 0;
    const int JAT = 3; // 先按你样例写 3（后续可改成 UI 输入）

    os << NJX << "\t" << MAKOD << "\tNJX\tMAKOD\n";
    os << "NJX\tKE1\tKE2\tJD1\tJD2\tJD3\tJD4\tJAT\n";

    for (int i = 0; i < rows.size(); ++i)
    {
        const int idx = i + 1;
        const Row& r = rows[i];
        os << idx << "\t" // NJX
           << idx << "\t" // KE1（先按样例等于序号）
           << KE2 << "\t" << r.jd1 << "\t" << r.jd2 << "\t" << r.jd3 << "\t" << r.jd4 << "\t" << JAT
           << "\n";
    }

    f.close();
    return true;
}

double GeoStressFitFacePage::height() const
{
    return ui->spinHeight->value();
}
double GeoStressFitFacePage::rou() const
{
    return ui->spinRou->value();
}
double GeoStressFitFacePage::pgrout() const
{
    return ui->spinPgrout->value();
}
int GeoStressFitFacePage::ii() const
{
    return ui->spinII->value();
}

bool GeoStressFitFacePage::pickEdge() const
{
    return ui->radionButtonEdge->isChecked();
}
bool GeoStressFitFacePage::pickFace() const
{
    return ui->radionButtonFace->isChecked();
}
