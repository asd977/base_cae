#include "DerivedValueSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include "PostResultAutoPrepare.h"
#include "PostTecplotUtils.h"
#include "ResultDisplayNames.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidgetItem>

#include "ui_DerivedValueSettingWidget.h"

// VTK
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <cmath>

/* ---------------- 工具函数（仅本文件） ---------------- */
namespace
{
static QString sci(double v, int precision = 2)
{
    return QString::number(v, 'E', precision);
}

static vtkSmartPointer<vtkUnstructuredGrid> loadGridFromFile(const QString& file)
{
    return PostTecplotUtils::readGrid(file);
}

struct Stats
{
    double minV{0}, maxV{0}, meanV{0};
    bool ok{false};
    vtkIdType count{0};
    vtkIdType totalCount{0};
    vtkIdType invalidCount{0};
};

static Stats calcArrayStats(vtkDataArray* arr, bool magnitude)
{
    Stats s;
    if (!arr)
        return s;
    const vtkIdType nt = arr->GetNumberOfTuples();
    const int nc = arr->GetNumberOfComponents();
    if (nt <= 0 || nc <= 0)
        return s;

    s.totalCount = nt;
    long double sum = 0.0L;
    bool initialized = false;
    for (vtkIdType i = 0; i < nt; ++i)
    {
        double val = 0.0;
        bool finite = true;
        if (magnitude && nc > 1)
        {
            long double squared = 0.0L;
            for (int c = 0; c < nc; ++c)
            {
                const double component = arr->GetComponent(i, c);
                if (!std::isfinite(component))
                {
                    finite = false;
                    break;
                }
                squared += static_cast<long double>(component) * component;
            }
            if (finite)
                val = std::sqrt(static_cast<double>(squared));
        }
        else
        {
            val = arr->GetComponent(i, 0);
            finite = std::isfinite(val);
        }

        if (!finite || !std::isfinite(val))
        {
            ++s.invalidCount;
            continue;
        }

        if (!initialized)
        {
            s.minV = s.maxV = val;
            initialized = true;
        }
        else
        {
            s.minV = std::min(s.minV, val);
            s.maxV = std::max(s.maxV, val);
        }
        sum += val;
        ++s.count;
    }

    if (s.count > 0)
    {
        s.meanV = static_cast<double>(sum / static_cast<long double>(s.count));
        s.ok = true;
    }
    return s;
}

} // namespace

/* ---------------- 类实现 ---------------- */

DerivedValueSettingWidget::DerivedValueSettingWidget(QWidget* parent)
    : ui(new Ui::DerivedValueSettingWidget), BasePage(PublicConstants::kPagePostProcessDerivative)
{
    ui->setupUi(this);

    // 让我们可以捕捉点击位置，决定是否手动切换勾选
    ui->datasetList->viewport()->installEventFilter(this);

    // 顶部域选择
    ui->assocComboBox->clear();
    ui->assocComboBox->addItem(QStringLiteral("节点数据"), QStringLiteral("Point Data"));
    ui->assocComboBox->addItem(QStringLiteral("单元数据"), QStringLiteral("Cell Data"));
    ui->assocComboBox->setCurrentIndex(0);

    // 表格外观
    ui->statsTable->setColumnCount(5);
    ui->statsTable->setHorizontalHeaderLabels(QStringList() << QStringLiteral("数据集")
                                                            << QStringLiteral("最小值")
                                                            << QStringLiteral("最大值")
                                                            << QStringLiteral("平均值")
                                                            << QStringLiteral("有效/总数"));
    ui->statsTable->verticalHeader()->setVisible(false);
    ui->statsTable->horizontalHeader()->setStretchLastSection(true);
    ui->statsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->statsTable->setAlternatingRowColors(true);

    // —— 勾选变化：及时更新高亮和计数（关键修复点）——
    connect(ui->datasetList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* it)
            {
                setItemCheckedVisual(it, it->checkState() == Qt::Checked);
                updateSelectionInfo();
            });

    // 全选/全不选
    connect(ui->selectAllCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                // 批量设置会触发 itemChanged，多次刷新无妨
                for (int i = 0; i < ui->datasetList->count(); ++i)
                {
                    auto* it = ui->datasetList->item(i);
                    it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
                }
            });

    // 刷新 & 计算
    connect(ui->refreshButton, &QPushButton::clicked, this, [this]() {
        refreshDatasets();
        rebuildScalarCatalog();
        rebuildScalarComboForAssoc();
    });
    connect(ui->computeButton, &QPushButton::clicked, this,
            &DerivedValueSettingWidget::computeStats);

    // 切换 Point/Cell 时重建变量列表
    connect(ui->assocComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DerivedValueSettingWidget::rebuildScalarComboForAssoc);
}

DerivedValueSettingWidget::~DerivedValueSettingWidget()
{
    delete ui;
}

/* ---------- 生命周期 ---------- */

void DerivedValueSettingWidget::beforeLoad()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    m_dataPath = PostResultAutoPrepare::firstResultDirectory(projectDir);

    if (!m_loadedOnce)
    {
        refreshDatasets();
        rebuildScalarCatalog();
        rebuildScalarComboForAssoc();
        m_loadedOnce = true;

        if (m_hasPendingState)
        {
            applyState(m_pendingState);
            m_pendingState.clear();
            m_hasPendingState = false;
        }
    }
    updateSelectionInfo();
}

void DerivedValueSettingWidget::beforeUnload()
{
    // 保留状态以便回来继续使用；无需操作
}

/* ---------- 事件过滤：点整行也能切换勾选 ---------- */
bool DerivedValueSettingWidget::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == ui->datasetList->viewport())
    {
        if (ev->type() == QEvent::MouseButtonRelease)
        {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton)
            {
                const QModelIndex idx = ui->datasetList->indexAt(me->pos());
                if (idx.isValid())
                {
                    auto* item = ui->datasetList->item(idx.row());
                    // 计算复选框矩形
                    QStyleOptionViewItem opt;
                    opt.initFrom(ui->datasetList);
                    opt.rect = ui->datasetList->visualItemRect(item);
                    opt.state = QStyle::State_Active | QStyle::State_Enabled;
                    opt.features = QStyleOptionViewItem::HasCheckIndicator;

                    const QRect checkRect = ui->datasetList->style()->subElementRect(
                        QStyle::SE_ItemViewItemCheckIndicator, &opt, ui->datasetList);

                    // 如果点击位置不在小方框里 -> 我们手动切换
                    if (!checkRect.contains(me->pos()))
                    {
                        const bool nowChecked = (item->checkState() != Qt::Checked);
                        item->setCheckState(nowChecked ? Qt::Checked : Qt::Unchecked);
                        // itemChanged 槽会负责视觉更新与计数
                        return true; // 我们处理了逻辑
                    }
                }
            }
        }
    }
    return BasePage::eventFilter(obj, ev);
}

/* ---------- 内部流程 ---------- */

void DerivedValueSettingWidget::refreshDatasets()
{
    QSet<QString> checkedPaths;
    for (int i = 0; i < ui->datasetList->count(); ++i)
    {
        QListWidgetItem* item = ui->datasetList->item(i);
        if (item && item->checkState() == Qt::Checked)
            checkedPaths.insert(item->data(Qt::UserRole).toString());
    }

    ui->datasetList->clear();
    m_frames.clear();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);

    QSet<QString> seenPaths;
    int fallbackOrder = 0;
    const QRegularExpression legacyPattern(R"(stress_strain(\d+)-(\d+)\.(?:txt|tec))",
                                            QRegularExpression::CaseInsensitiveOption);

    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const auto& group = groups.at(groupIndex);
        const QVector<PostResultAutoPrepare::ResultFile> files =
            PostResultAutoPrepare::resultFiles(projectDir, group.sourcePath, false);
        for (const auto& file : files)
        {
            const QString canonical = QFileInfo(file.filePath).canonicalFilePath();
            const QString key = canonical.isEmpty() ? QFileInfo(file.filePath).absoluteFilePath()
                                                     : canonical;
            if (key.isEmpty() || seenPaths.contains(key))
                continue;
            seenPaths.insert(key);

            FrameItem item;
            item.filePath = file.filePath;
            item.group = groupIndex + 1;
            item.order = ++fallbackOrder;

            const QRegularExpressionMatch match =
                legacyPattern.match(QFileInfo(file.filePath).fileName());
            if (match.hasMatch())
            {
                item.group = match.captured(1).toInt();
                item.order = match.captured(2).toInt();
            }

            const QString fileName = file.displayName.trimmed().isEmpty()
                                         ? QFileInfo(file.filePath).completeBaseName()
                                         : file.displayName.trimmed();
            item.displayName = groups.size() > 1
                                   ? QStringLiteral("%1 / %2").arg(group.displayName, fileName)
                                   : fileName;
            m_frames.push_back(item);
        }
    }

    std::sort(m_frames.begin(), m_frames.end(), [](const FrameItem& a, const FrameItem& b) {
        if (a.group != b.group)
            return a.group < b.group;
        if (a.order != b.order)
            return a.order < b.order;
        return QString::compare(a.displayName, b.displayName, Qt::CaseInsensitive) < 0;
    });

    for (const auto& frame : m_frames)
    {
        auto* item = new QListWidgetItem(frame.displayName);
        item->setToolTip(QDir::toNativeSeparators(frame.filePath));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled);
        item->setData(Qt::UserRole, frame.filePath);
        item->setData(Qt::UserRole + 1, frame.group);
        item->setData(Qt::UserRole + 2, frame.order);
        const bool checked = checkedPaths.contains(frame.filePath);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        setItemCheckedVisual(item, checked);
        ui->datasetList->addItem(item);
    }

    if (m_frames.isEmpty())
        ui->selectionInfoLabel->setText(QStringLiteral("未找到可统计的结果文件"));
    else
        updateSelectionInfo();
}

void DerivedValueSettingWidget::updateSelectionInfo()
{
    int total = ui->datasetList->count();
    int sel = 0;
    for (int i = 0; i < total; ++i)
        if (ui->datasetList->item(i)->checkState() == Qt::Checked)
            ++sel;

    ui->selectionInfoLabel->setText(QString("已选 %1 / 共 %2").arg(sel).arg(total));
}

void DerivedValueSettingWidget::rebuildScalarCatalog()
{
    m_pointArrays.clear();
    m_cellArrays.clear();

    // 同一结果组的变量结构通常在各步一致。只扫描每组首、末两个代表文件，
    // 避免进入页面时同步读取全部大结果文件。
    QVector<int> representativeIndexes;
    QHash<int, int> firstByGroup;
    QHash<int, int> lastByGroup;
    for (int i = 0; i < m_frames.size(); ++i)
    {
        const int group = m_frames.at(i).group;
        if (!firstByGroup.contains(group))
            firstByGroup.insert(group, i);
        lastByGroup.insert(group, i);
    }
    for (auto it = firstByGroup.constBegin(); it != firstByGroup.constEnd(); ++it)
    {
        representativeIndexes.push_back(it.value());
        const int lastIndex = lastByGroup.value(it.key(), it.value());
        if (lastIndex != it.value())
            representativeIndexes.push_back(lastIndex);
    }
    std::sort(representativeIndexes.begin(), representativeIndexes.end());
    representativeIndexes.erase(
        std::unique(representativeIndexes.begin(), representativeIndexes.end()),
        representativeIndexes.end());

    for (const int index : representativeIndexes)
    {
        const auto& frame = m_frames.at(index);
        auto grid = loadGridFromFile(frame.filePath);
        if (!grid)
            continue;

        if (auto* pointData = grid->GetPointData())
        {
            for (int i = 0; i < pointData->GetNumberOfArrays(); ++i)
            {
                vtkDataArray* array = pointData->GetArray(i);
                const char* name = pointData->GetArrayName(i);
                if (array && name && *name)
                {
                    const QString qName = QString::fromUtf8(name);
                    if (!ResultDisplayNames::isInternalField(qName))
                        m_pointArrays.insert(qName);
                }
            }
        }
        if (auto* cellData = grid->GetCellData())
        {
            for (int i = 0; i < cellData->GetNumberOfArrays(); ++i)
            {
                vtkDataArray* array = cellData->GetArray(i);
                const char* name = cellData->GetArrayName(i);
                if (array && name && *name)
                {
                    const QString qName = QString::fromUtf8(name);
                    if (!ResultDisplayNames::isInternalField(qName))
                        m_cellArrays.insert(qName);
                }
            }
        }
    }
}

void DerivedValueSettingWidget::rebuildScalarComboForAssoc()
{
    const int assoc = ui->assocComboBox->currentIndex(); // 0 点 1 元
    const QString keep = ResultDisplayNames::internalName(ui->scalarComboBox);

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();

    QStringList names = assoc == 0 ? m_pointArrays.values() : m_cellArrays.values();
    names.sort(Qt::CaseInsensitive);
    for (const QString& name : names)
        ResultDisplayNames::addScalarItem(ui->scalarComboBox, name);

    int idx = ResultDisplayNames::findInternalName(ui->scalarComboBox, keep);
    if (idx >= 0)
        ui->scalarComboBox->setCurrentIndex(idx);
    else if (ui->scalarComboBox->count() > 0)
        ui->scalarComboBox->setCurrentIndex(0);

    ui->scalarComboBox->blockSignals(false);
}

void DerivedValueSettingWidget::computeStats()
{
    ui->statsTable->setRowCount(0);

    // 收集选中项
    struct Sel
    {
        int g;
        int o;
        QString path;
        QString label;
    };
    QVector<Sel> picks;
    picks.reserve(ui->datasetList->count());
    for (int i = 0; i < ui->datasetList->count(); ++i)
    {
        auto* it = ui->datasetList->item(i);
        if (it->checkState() == Qt::Checked)
        {
            Sel s;
            s.g = it->data(Qt::UserRole + 1).toInt();
            s.o = it->data(Qt::UserRole + 2).toInt();
            s.path = it->data(Qt::UserRole).toString();
            s.label = it->text();
            picks.push_back(s);
        }
    }
    if (picks.isEmpty())
    {
        QMessageBox::information(this, "提示", "请至少选择一个数据集。");
        return;
    }

    const bool usePoint = (ui->assocComboBox->currentIndex() == 0);
    const QString scalar = ResultDisplayNames::internalName(ui->scalarComboBox);
    const bool mag = ui->magnitudeCheckBox->isChecked();

    bool totalInit = false;
    double gMin = 0, gMax = 0, gSum = 0;
    long long gCnt = 0;
    long long gTotal = 0;

    for (const auto& s : picks)
    {
        auto g = loadGridFromFile(s.path);
        if (!g)
            continue;

        vtkDataArray* arr = nullptr;
        if (usePoint)
        {
            if (auto pd = g->GetPointData())
                arr = pd->GetArray(scalar.toUtf8().data());
        }
        else
        {
            if (auto cd = g->GetCellData())
                arr = cd->GetArray(scalar.toUtf8().data());
        }

        const int row = ui->statsTable->rowCount();
        ui->statsTable->insertRow(row);

        ui->statsTable->setItem(row, 0, new QTableWidgetItem(
                                              s.label.isEmpty()
                                                  ? QString("%1-%2").arg(s.g).arg(s.o)
                                                  : s.label));

        if (!arr)
        {
            ui->statsTable->setItem(row, 1, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 2, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 3, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 4, new QTableWidgetItem("0/0"));
            continue;
        }

        Stats st = calcArrayStats(arr, mag);
        gTotal += st.totalCount;
        if (!st.ok)
        {
            ui->statsTable->setItem(row, 1, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 2, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 3, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 4,
                                    new QTableWidgetItem(QStringLiteral("0/%1").arg(st.totalCount)));
            continue;
        }

        auto* itMin = new QTableWidgetItem(sci(st.minV));
        itMin->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* itMax = new QTableWidgetItem(sci(st.maxV));
        itMax->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* itMean = new QTableWidgetItem(sci(st.meanV));
        itMean->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->statsTable->setItem(row, 1, itMin);
        ui->statsTable->setItem(row, 2, itMax);
        ui->statsTable->setItem(row, 3, itMean);
        ui->statsTable->setItem(
            row, 4,
            new QTableWidgetItem(QStringLiteral("%1/%2").arg(st.count).arg(st.totalCount)));

        if (st.ok)
        {
            if (!totalInit)
            {
                gMin = st.minV;
                gMax = st.maxV;
                totalInit = true;
            }
            else
            {
                gMin = std::min(gMin, st.minV);
                gMax = std::max(gMax, st.maxV);
            }
            gSum += st.meanV * double(st.count);
            gCnt += st.count;
        }
    }

    if (gCnt > 0)
    {
        const int row = ui->statsTable->rowCount();
        ui->statsTable->insertRow(row);
        auto* lab = new QTableWidgetItem(QStringLiteral("汇总"));
        QFont f = lab->font();
        f.setBold(true);
        lab->setFont(f);
        ui->statsTable->setItem(row, 0, lab);

        auto* tMin = new QTableWidgetItem(sci(gMin));
        tMin->setFont(f);
        tMin->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* tMax = new QTableWidgetItem(sci(gMax));
        tMax->setFont(f);
        tMax->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* tMean = new QTableWidgetItem(sci(gSum / double(gCnt)));
        tMean->setFont(f);
        tMean->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->statsTable->setItem(row, 1, tMin);
        ui->statsTable->setItem(row, 2, tMax);
        ui->statsTable->setItem(row, 3, tMean);
        auto* tCount = new QTableWidgetItem(
            QStringLiteral("%1/%2").arg(gCnt).arg(gTotal));
        tCount->setFont(f);
        tCount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->statsTable->setItem(row, 4, tCount);
    }
    for (int r = 0; r < ui->statsTable->rowCount(); ++r)
        for (int c = 0; c < ui->statsTable->columnCount(); ++c)
            if (auto* it = ui->statsTable->item(r, c))
                it->setTextAlignment(Qt::AlignCenter);
}

/* ---------- 状态应用 ---------- */

void DerivedValueSettingWidget::applyState(const QVariantMap& state)
{
    // 恢复域
    if (state.contains("assocIndex"))
    {
        int idx = state.value("assocIndex").toInt();
        if (idx < 0 || idx > 1)
            idx = 0;
        ui->assocComboBox->setCurrentIndex(idx);
        rebuildScalarComboForAssoc();
    }

    // 恢复标量名
    if (state.contains("scalarName"))
    {
        const QString s = state.value("scalarName").toString();
        int i = ResultDisplayNames::findInternalName(ui->scalarComboBox, s);
        if (i >= 0)
            ui->scalarComboBox->setCurrentIndex(i);
    }

    // 恢复模长
    if (state.contains("magnitude"))
    {
        ui->magnitudeCheckBox->setChecked(state.value("magnitude").toBool());
    }

    // 恢复选择的文件。新版本保存绝对路径，同时兼容旧版本保存的显示文本。
    QSet<QString> picked;
    if (state.contains("picked"))
    {
        const auto list = state.value("picked").toStringList();
        for (const auto& k : list)
            picked.insert(k);
    }

    for (int i = 0; i < ui->datasetList->count(); ++i)
    {
        auto* it = ui->datasetList->item(i);
        const bool on = picked.contains(it->data(Qt::UserRole).toString()) ||
                        picked.contains(it->text());
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        // 视觉更新由 itemChanged 统一处理，无需手动 setItemCheckedVisual
    }
    updateSelectionInfo();
}

QJsonObject DerivedValueSettingWidget::saveJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("assocIndex"), ui->assocComboBox->currentIndex());
    object.insert(QStringLiteral("scalarName"), ResultDisplayNames::internalName(ui->scalarComboBox));
    object.insert(QStringLiteral("magnitude"), ui->magnitudeCheckBox->isChecked());

    QJsonArray picked;
    for (int i = 0; i < ui->datasetList->count(); ++i)
    {
        QListWidgetItem* item = ui->datasetList->item(i);
        if (item && item->checkState() == Qt::Checked)
            picked.append(item->data(Qt::UserRole).toString());
    }
    object.insert(QStringLiteral("picked"), picked);
    return object;
}

void DerivedValueSettingWidget::loadJson(const QJsonObject& object)
{
    QVariantMap state;
    state.insert(QStringLiteral("assocIndex"), object.value(QStringLiteral("assocIndex")).toInt(0));
    state.insert(QStringLiteral("scalarName"), object.value(QStringLiteral("scalarName")).toString());
    state.insert(QStringLiteral("magnitude"), object.value(QStringLiteral("magnitude")).toBool(false));

    QStringList picked;
    const QJsonArray array = object.value(QStringLiteral("picked")).toArray();
    for (const QJsonValue& value : array)
        picked.push_back(value.toString());
    state.insert(QStringLiteral("picked"), picked);

    if (!m_loadedOnce)
    {
        m_pendingState = state;
        m_hasPendingState = true;
        return;
    }
    applyState(state);
}

/* ---------- UI 辅助 ---------- */
void DerivedValueSettingWidget::setItemCheckedVisual(QListWidgetItem* it, bool checked)
{
    if (!it)
        return;
    if (checked)
    {
        it->setBackground(QColor("#E6F2FF")); // 淡蓝底
        it->setForeground(QColor("#0B57D0")); // 深蓝字
        it->setFont(QFont(it->font().family(), it->font().pointSize(), QFont::DemiBold));
    }
    else
    {
        it->setBackground(QBrush());
        it->setForeground(QBrush(Qt::black));
        QFont f = it->font();
        f.setBold(false);
        it->setFont(f);
    }
}
