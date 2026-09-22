#include "GeoStressFitFacePage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QComboBox>
#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QLabel>
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

    // 梯形边界最常见的是对一个完整平面施加荷载。复用 PickManager 的面拾取，
    // 单击一个小网格面后自动扩展为当前可见表面中的同一连续共面区域；
    // 框选仍保持普通批量面选择，不强制扩展。
    m_wholeFaceCheckBox = new QCheckBox(QStringLiteral("点选整面"), this);
    m_wholeFaceCheckBox->setChecked(true);
    m_wholeFaceCheckBox->setToolTip(
        QStringLiteral("单击任意一个网格面时，自动选择与它连续且共面的整片表面；框选仍按框内网格面选择。"));
    m_selectionStatusLabel = new QLabel(this);
    m_selectionStatusLabel->setWordWrap(true);
    m_selectionStatusLabel->setText(QStringLiteral("尚未选择边界。"));
    if (ui->contentLayout)
    {
        ui->contentLayout->insertWidget(0, m_selectionStatusLabel);
        ui->contentLayout->insertWidget(0, m_wholeFaceCheckBox);
    }

    // 默认值：2D 默认选边，3D 默认选面（用户仍可改）
    ensureDefaultPickTarget();

    connect(ui->radionButtonEdge, &QRadioButton::toggled, this,
            &GeoStressFitFacePage::onPickTargetChanged);
    connect(ui->radionButtonFace, &QRadioButton::toggled, this,
            &GeoStressFitFacePage::onPickTargetChanged);

    PickManager& pm = PickManager::instance();
    connect(&pm, &PickManager::facePicked, this, &GeoStressFitFacePage::onFacePicked);
    connect(&pm, &PickManager::selectionChanged, this,
            [this](const QString& domain, const QString& uuid, PickMode mode, GridKind)
            {
                if (domain == getPageType() && uuid == m_uuid && mode == m_activeMode)
                    refreshSelectionSummary();
            });
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

    PickManager& pm = PickManager::instance();
    // 显示筛选可能刚刚替换过 mesh.imported.surface。先同步当前可见网格，
    // 再激活 Face/Edge 模式，避免必须“切一次边再切回面”才有响应。
    pm.updateStructure();
    pm.activate(getPageType(), m_uuid, m_activeMode);
    pm.setClickPickEnabled(getPageType(), m_uuid, true);
    pm.setBoxSelectEnabled(getPageType(), m_uuid, true);

    if (m_wholeFaceCheckBox)
        m_wholeFaceCheckBox->setVisible(pickFace());
    refreshSelectionSummary();

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
    // “网格边/网格面”是实际拾取模式；分组维度与它保持一致，避免出现
    // 选了“网格面”却从“线边界”分组中添加数据的双重状态。
    const int wantDim = pickEdge() ? 1 : 2;
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
    // 分组维度和实际拾取模式保持同步；切换这里只切模式，不自动把第一组加入选择。
    const int dim = ui->comboBoxDim->currentData().toInt();
    if (dim == 1 && !pickEdge())
    {
        ui->radionButtonEdge->setChecked(true);
        return;
    }
    if (dim == 2 && !pickFace())
    {
        ui->radionButtonFace->setChecked(true);
        return;
    }

    refreshGroupCombo();
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                  pickEdge() ? PickMode::Edge : PickMode::Face);
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

    PickManager& pm = PickManager::instance();
    pm.clearSelection(getPageType(), m_uuid, m_activeMode);
    pm.deactivate(getPageType(), m_uuid, m_activeMode);

    // 模式切换前重新同步当前筛选后的显示网格。这里只刷新结构，
    // 不再借“切边/切面”去间接修复拾取状态。
    pm.updateStructure();
    m_activeMode = newMode;
    pm.activate(getPageType(), m_uuid, m_activeMode);
    pm.setClickPickEnabled(getPageType(), m_uuid, true);
    pm.setBoxSelectEnabled(getPageType(), m_uuid, true);

    if (m_wholeFaceCheckBox)
        m_wholeFaceCheckBox->setVisible(pickFace());

    refreshGroupCombo();
    refreshSelectionSummary();
}

void GeoStressFitFacePage::onFacePicked(const QString& domain, const QString& activeUuid,
                                             GridKind kind, vtkIdType faceKey, bool selected)
{
    if (domain != getPageType() || activeUuid != m_uuid || kind != GridKind::Solid
        || !pickFace() || !m_wholeFaceCheckBox || !m_wholeFaceCheckBox->isChecked())
        return;

    PickManager& pm = PickManager::instance();
    const QVector<vtkIdType> patch = pm.connectedVisibleFacePatch(faceKey, 5.0);
    if (patch.size() <= 1)
    {
        refreshSelectionSummary();
        return;
    }

    QSet<vtkIdType> selectedKeys;
    const QVector<vtkIdType> current =
        pm.getIdList(getPageType(), m_uuid, PickMode::Face, GridKind::Solid);
    for (vtkIdType key : current)
        selectedKeys.insert(key);

    for (vtkIdType key : patch)
    {
        if (selected)
            selectedKeys.insert(key);
        else
            selectedKeys.remove(key);
    }

    QVector<vtkIdType> keys;
    keys.reserve(selectedKeys.size());
    for (vtkIdType key : selectedKeys)
        keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    pm.setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid, keys, true);
    refreshSelectionSummary();
}

void GeoStressFitFacePage::refreshSelectionSummary()
{
    if (!m_selectionStatusLabel)
        return;

    PickManager& pm = PickManager::instance();
    const PickMode mode = decidePickMode();
    const int solidCount =
        pm.getIdList(getPageType(), m_uuid, mode, GridKind::Solid).size();
    const int lineCount =
        pm.getIdList(getPageType(), m_uuid, mode, GridKind::Line).size();
    const int total = solidCount + lineCount;

    if (mode == PickMode::Face)
    {
        m_selectionStatusLabel->setText( total > 0
            ? QStringLiteral("已选择 %1 个网格面。可继续单击、框选或取消选择。").arg(total)
            : QStringLiteral("请选择荷载作用面；勾选“点选整面”后，单击一个小面即可选择整片共面区域。"));
    }
    else
    {
        m_selectionStatusLabel->setText(total > 0
            ? QStringLiteral("已选择 %1 条网格边。可继续单击、框选或取消选择。").arg(total)
            : QStringLiteral("请选择荷载作用边。"));
    }
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
