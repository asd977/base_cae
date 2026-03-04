#include "DerivedValueSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QEvent>
#include <QHeaderView>
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
    auto reader = vtkSmartPointer<vtkTecplotReader>::New();
    reader->SetFileName(file.toUtf8().constData());
    reader->Update();

    auto mb = reader->GetOutput();
    if (!mb)
        return nullptr;

    auto appender = vtkSmartPointer<vtkAppendFilter>::New();
    for (int i = 0; i < mb->GetNumberOfBlocks(); ++i)
    {
        if (auto g = vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(i)))
        {
            appender->AddInputData(g);
        }
    }
    appender->Update();
    return vtkUnstructuredGrid::SafeDownCast(appender->GetOutput());
}

struct Stats
{
    double minV{0}, maxV{0}, meanV{0};
    bool ok{false};
    vtkIdType count{0};
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

    double mn = 0, mx = 0, sum = 0;
    for (vtkIdType i = 0; i < nt; ++i)
    {
        double val = 0.0;
        if (magnitude && nc > 1)
        {
            double t = 0;
            for (int c = 0; c < nc; ++c)
            {
                const double v = arr->GetComponent(i, c);
                t += v * v;
            }
            val = std::sqrt(t);
        }
        else
        {
            val = arr->GetComponent(i, 0);
        }
        if (i == 0)
        {
            mn = mx = val;
        }
        else
        {
            mn = std::min(mn, val);
            mx = std::max(mx, val);
        }
        sum += val;
    }
    s.minV = mn;
    s.maxV = mx;
    s.meanV = sum / double(nt);
    s.ok = true;
    s.count = nt;
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
    ui->assocComboBox->addItem("Point Data");
    ui->assocComboBox->addItem("Cell Data");
    ui->assocComboBox->setCurrentIndex(0);

    // 表格外观
    ui->statsTable->setColumnCount(4);
    ui->statsTable->setHorizontalHeaderLabels(QStringList() << "File" << "Min"
                                                            << "Max" << "Mean");
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
    connect(ui->refreshButton, &QPushButton::clicked, this,
            &DerivedValueSettingWidget::refreshDatasets);
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
    m_dataPath =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString() + "/data";
    //    if (!m_loadedOnce) {
    //

    //        qDebug()<<m_dataPath;
    //        refreshDatasets();              // 扫描文件
    //        rebuildScalarCatalog();         // 汇总数组目录
    //        rebuildScalarComboForAssoc();   // 按当前域填充 scalar 下拉

    //        m_loadedOnce = true;

    //        if (m_hasPendingState) {
    //            applyState(m_pendingState);
    //            m_pendingState.clear();
    //            m_hasPendingState = false;
    //        }
    //    }

    //    // 每次进入仅更新“已选/总数”
    //    updateSelectionInfo();
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
    ui->datasetList->clear();
    m_frames.clear();

    QDir dir(m_dataPath);
    if (!dir.exists())
    {
        QMessageBox::warning(this, "提示", "数据目录不存在: " + m_dataPath);
        updateSelectionInfo();
        return;
    }

    QRegularExpression re(R"(stress_strain(\d+)-(\d+)\.txt)");
    for (const QString& f : dir.entryList(QDir::Files))
    {
        auto m = re.match(f);
        if (!m.hasMatch())
            continue;
        FrameItem it;
        it.group = m.captured(1).toInt();
        it.order = m.captured(2).toInt();
        it.filePath = dir.filePath(f);
        m_frames.push_back(it);
    }

    std::sort(m_frames.begin(), m_frames.end(),
              [](const FrameItem& a, const FrameItem& b)
              {
                  if (a.group != b.group)
                      return a.group < b.group;
                  return a.order < b.order;
              });

    for (const auto& fr : m_frames)
    {
        auto* item = new QListWidgetItem(QString("%1-%2").arg(fr.group).arg(fr.order));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled);
        item->setCheckState(Qt::Unchecked);
        // 备份数据以备扩展使用
        item->setData(Qt::UserRole, fr.filePath);
        item->setData(Qt::UserRole + 1, fr.group);
        item->setData(Qt::UserRole + 2, fr.order);
        // 初始视觉
        setItemCheckedVisual(item, false);
        ui->datasetList->addItem(item);
    }

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

    for (const auto& fr : m_frames)
    {
        auto g = loadGridFromFile(fr.filePath);
        if (!g)
            continue;

        if (auto pd = g->GetPointData())
        {
            for (int i = 0; i < pd->GetNumberOfArrays(); ++i)
            {
                if (const char* n = pd->GetArrayName(i))
                    if (*n)
                        m_pointArrays.insert(QString::fromUtf8(n));
            }
        }
        if (auto cd = g->GetCellData())
        {
            for (int i = 0; i < cd->GetNumberOfArrays(); ++i)
            {
                if (const char* n = cd->GetArrayName(i))
                    if (*n)
                        m_cellArrays.insert(QString::fromUtf8(n));
            }
        }
    }
}

void DerivedValueSettingWidget::rebuildScalarComboForAssoc()
{
    const int assoc = ui->assocComboBox->currentIndex(); // 0 点 1 元
    const QString keep = ui->scalarComboBox->currentText();

    ui->scalarComboBox->blockSignals(true);
    ui->scalarComboBox->clear();

    if (assoc == 0)
    {
        QStringList names = m_pointArrays.values();
        names.sort(Qt::CaseInsensitive);
        ui->scalarComboBox->addItems(names);
    }
    else
    {
        QStringList names = m_cellArrays.values();
        names.sort(Qt::CaseInsensitive);
        ui->scalarComboBox->addItems(names);
    }

    int idx = ui->scalarComboBox->findText(keep);
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
            picks.push_back(s);
        }
    }
    if (picks.isEmpty())
    {
        QMessageBox::information(this, "提示", "请至少选择一个数据集。");
        return;
    }

    const bool usePoint = (ui->assocComboBox->currentIndex() == 0);
    const QString scalar = ui->scalarComboBox->currentText();
    const bool mag = ui->magnitudeCheckBox->isChecked();

    bool totalInit = false;
    double gMin = 0, gMax = 0, gSum = 0;
    long long gCnt = 0;

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

        // File 列：g-o
        ui->statsTable->setItem(row, 0, new QTableWidgetItem(QString("%1-%2").arg(s.g).arg(s.o)));

        if (!arr)
        {
            ui->statsTable->setItem(row, 1, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 2, new QTableWidgetItem("N/A"));
            ui->statsTable->setItem(row, 3, new QTableWidgetItem("N/A"));
            continue;
        }

        Stats st = calcArrayStats(arr, mag);
        auto* itMin = new QTableWidgetItem(sci(st.minV));
        itMin->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* itMax = new QTableWidgetItem(sci(st.maxV));
        itMax->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* itMean = new QTableWidgetItem(sci(st.meanV));
        itMean->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ui->statsTable->setItem(row, 1, itMin);
        ui->statsTable->setItem(row, 2, itMax);
        ui->statsTable->setItem(row, 3, itMean);

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
        auto* lab = new QTableWidgetItem("Total");
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
        int i = ui->scalarComboBox->findText(s);
        if (i >= 0)
            ui->scalarComboBox->setCurrentIndex(i);
    }

    // 恢复模长
    if (state.contains("magnitude"))
    {
        ui->magnitudeCheckBox->setChecked(state.value("magnitude").toBool());
    }

    // 恢复选择的文件（text 即 "g-o"）
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
        const bool on = picked.contains(it->text());
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        // 视觉更新由 itemChanged 统一处理，无需手动 setItemCheckedVisual
    }
    updateSelectionInfo();
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
