#include "GeoStressFitFacePage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
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
    auto* batchAddBtn = ui->btnBatchAdd;
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupComboChanged(ui->comboBoxGroup->currentIndex());
            });

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
                    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, row));
                    if (pickEdge())
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.edgeKeys));
                    else
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid, toVtkKeys(set.faceKeys));
                });
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  pickEdge() ? PickMode::Edge : PickMode::Face);
                });
    }

    // 3) 下拉框只做预览，不自动应用；由“添加”按钮触发
}


void GeoStressFitFacePage::refreshGroupCombo()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const int wantDim = ui->comboBoxDim->currentData().toInt() >= 0 ? ui->comboBoxDim->currentData().toInt() : (pickEdge() ? 1 : 2);
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem(QStringLiteral("线边界"), 1);
    ui->comboBoxDim->addItem(QStringLiteral("面边界"), 2);
    ui->comboBoxDim->setCurrentIndex(wantDim == 1 ? 0 : 1);
    ui->comboBoxDim->blockSignals(false);
    const QVector<MeshTagSet> sets = meshSvc->boundaryTagSets(wantDim);
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (const MeshTagSet& set : sets)
    {
        if (wantDim == 1 && set.edgeKeys.isEmpty()) continue;
        if (wantDim == 2 && set.faceKeys.isEmpty()) continue;
        ui->comboBoxGroup->addItem(boundaryDisplayText(meshSvc, set, false), QVariant::fromValue(set.key));
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
    if (idx < 0) return;
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;
    const MeshTagSet set = findBoundarySet(meshSvc, comboTagKey(ui->comboBoxGroup, idx));
    PickManager& pm = PickManager::instance();
    const QString domain = getPageType();
    const QString uuid = m_uuid;
    const bool replace = false;
    if (pickEdge())
    {
        pm.setSelectedEdgeKeys(domain, uuid, GridKind::Solid, toVtkKeys(set.edgeKeys), replace);
        pm.setSelectedEdgeKeys(domain, uuid, GridKind::Line, {}, true);
    }
    else
    {
        pm.setSelectedFaceKeys(domain, uuid, GridKind::Solid, toVtkKeys(set.faceKeys), replace);
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
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    const QString fileName = fnameForExport();
    const QString path = inputDir.filePath(fileName);

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
