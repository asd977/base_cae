#include "LatinHypercubeSampling.h"

#include <qtemporaryfile.h>

#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtGlobal>

#include "ui_LatinHypercubeSampling.h"

/* =============== LHS 命名空间实现 =============== */
namespace LHS
{

QString distName(Dist d)
{
    switch (d)
    {
    case Dist::Normal:
        return ("Normal(u,σ)");
    case Dist::Uniform:
        return ("Uniform()");
    case Dist::RandInt:
        return ("randint()");
    case Dist::Gamma:
        return ("gamma()");
    }
    return {};
}

int distIndex(Dist d)
{
    return int(d);
}

Dist distFromIndex(int idx)
{
    switch (idx)
    {
    case 0:
        return Dist::Normal;
    case 1:
        return Dist::Uniform;
    case 2:
        return Dist::RandInt;
    default:
        return Dist::Gamma;
    }
}

} // namespace LHS
/* ================================================= */

LatinHypercubeSampling::LatinHypercubeSampling(QWidget* parent)
    : ui(new Ui::LatinHypercubeSampling), BasePage("Latin")
{
    ui->setupUi(this);

    // 表格：2 列（参数名 | 分布）
    ui->parameterTableWidget->setColumnCount(2);
    ui->parameterTableWidget->setHorizontalHeaderLabels({("参数名"), ("分布")});
    ui->parameterTableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->parameterTableWidget->verticalHeader()->setVisible(false);
    ui->parameterTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->parameterTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->parameterTableWidget->setEditTriggers(QAbstractItemView::DoubleClicked |
                                              QAbstractItemView::SelectedClicked |
                                              QAbstractItemView::EditKeyPressed);
    ui->parameterTableWidget->setAlternatingRowColors(true);

    ui->parameterTableWidget->installEventFilter(this);

    ensureOnceInit();

    // 连接
    connect(ui->parameterTableWidget, &QTableWidget::currentCellChanged, this,
            &LatinHypercubeSampling::onRowSelectionChanged);
    connect(ui->parameterTableWidget, &QTableWidget::itemChanged, this,
            &LatinHypercubeSampling::onCellNameEdited);

    // 默认至少一行
    ensureOneRowIfEmpty();

    // test 按钮
    connect(ui->test, &QPushButton::clicked, this, &LatinHypercubeSampling::on_test_clicked);
}

LatinHypercubeSampling::~LatinHypercubeSampling()
{
    delete ui;
}

/* ================== BasePage 生命周期 ================== */
void LatinHypercubeSampling::beforeLoad()
{
    if (!m_initedOnce)
    {
        m_initedOnce = true;
    }
}
void LatinHypercubeSampling::beforeUnload()
{
}

/* ================== 面板搭建 ================== */
void LatinHypercubeSampling::ensureOnceInit()
{
    if (m_stack)
        return;

    // 在 settingGroupBox 里插入一个垂直布局 + stacked widget
    auto vbox = new QVBoxLayout();
    vbox->setContentsMargins(8, 8, 8, 8);
    m_stack = new QStackedWidget(ui->settingGroupBox);
    vbox->addWidget(m_stack);
    ui->settingGroupBox->setLayout(vbox);

    // 1) Normal(u, σ)
    {
        QWidget* w = new QWidget;
        auto form = new QFormLayout(w);
        m_mu = new QDoubleSpinBox;
        m_mu->setDecimals(6);
        m_mu->setRange(-1e100, 1e100);
        m_mu->setValue(0.0);
        m_sigma = new QDoubleSpinBox;
        m_sigma->setDecimals(6);
        m_sigma->setRange(1e-12, 1e100);
        m_sigma->setValue(1.0);
        form->addRow(("均值 μ"), m_mu);
        form->addRow(("标准差 σ"), m_sigma);
        m_stack->addWidget(w);
        connect(m_mu, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onNormalChanged);
        connect(m_sigma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onNormalChanged);
    }
    // 2) Uniform()
    {
        QWidget* w = new QWidget;
        auto form = new QFormLayout(w);
        m_umin = new QDoubleSpinBox;
        m_umin->setDecimals(6);
        m_umin->setRange(-1e100, 1e100);
        m_umin->setValue(0.0);
        m_umax = new QDoubleSpinBox;
        m_umax->setDecimals(6);
        m_umax->setRange(-1e100, 1e100);
        m_umax->setValue(1.0);
        form->addRow(("下界"), m_umin);
        form->addRow(("上界"), m_umax);
        m_stack->addWidget(w);
        connect(m_umin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onUniformChanged);
        connect(m_umax, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onUniformChanged);
    }
    // 3) randint()
    {
        QWidget* w = new QWidget;
        auto form = new QFormLayout(w);
        m_rmin = new QSpinBox;
        m_rmin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        m_rmin->setValue(0);
        m_rmax = new QSpinBox;
        m_rmax->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        m_rmax->setValue(10);
        form->addRow(("下界"), m_rmin);
        form->addRow(("上界"), m_rmax);
        m_stack->addWidget(w);
        connect(m_rmin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onRandIntChanged);
        connect(m_rmax, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onRandIntChanged);
    }
    // 4) gamma()
    {
        QWidget* w = new QWidget;
        auto form = new QFormLayout(w);
        m_gshape = new QDoubleSpinBox;
        m_gshape->setDecimals(6);
        m_gshape->setRange(1e-12, 1e100);
        m_gshape->setValue(1.0);
        m_gloc = new QDoubleSpinBox;
        m_gloc->setDecimals(6);
        m_gloc->setRange(-1e100, 1e100);
        m_gloc->setValue(0.0);
        m_gscale = new QDoubleSpinBox;
        m_gscale->setDecimals(6);
        m_gscale->setRange(1e-12, 1e100);
        m_gscale->setValue(1.0);
        form->addRow(("形状"), m_gshape);
        form->addRow(("位置"), m_gloc);
        form->addRow(("比例"), m_gscale);
        m_stack->addWidget(w);
        connect(m_gshape, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onGammaChanged);
        connect(m_gloc, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onGammaChanged);
        connect(m_gscale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                &LatinHypercubeSampling::onGammaChanged);
    }
}

/* ================== 行管理 ================== */
void LatinHypercubeSampling::ensureOneRowIfEmpty()
{
    if (ui->parameterTableWidget->rowCount() == 0)
    {
        LHS::RowConfig r;
        r.name = ("param1");
        addRow(r);
        ui->parameterTableWidget->setCurrentCell(0, 0);
    }
}

LHS::RowConfig& LatinHypercubeSampling::rowCfg(int row)
{
    return m_rows[row];
}
const LHS::RowConfig& LatinHypercubeSampling::rowCfg(int row) const
{
    return m_rows[row];
}

void LatinHypercubeSampling::addRow(const LHS::RowConfig& rc)
{
    int row = ui->parameterTableWidget->rowCount();
    ui->parameterTableWidget->insertRow(row);
    m_rows.push_back(rc);

    // 列0：参数名
    auto* nameItem =
        new QTableWidgetItem(rc.name.isEmpty() ? QString("param%1").arg(row + 1) : rc.name);
    nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable |
                       Qt::ItemIsEnabled);
    ui->parameterTableWidget->setItem(row, 0, nameItem);

    // 列1：分布下拉
    rebuildRowWidgets(row);

    // 重新标号 property("row")
    updateComboRowProperties();
}

void LatinHypercubeSampling::removeCurrentRow()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row < 0 || row >= ui->parameterTableWidget->rowCount())
        return;

    ui->parameterTableWidget->removeRow(row);
    m_rows.removeAt(row);

    // 重新设置当前行 & 同步面板
    if (ui->parameterTableWidget->rowCount() > 0)
    {
        int newRow = qMin(row, ui->parameterTableWidget->rowCount() - 1);
        ui->parameterTableWidget->setCurrentCell(newRow, 0);
    }
    else
    {
        ensureOneRowIfEmpty();
    }
    // 删除后重标 property("row")
    updateComboRowProperties();
}

void LatinHypercubeSampling::rebuildRowWidgets(int row)
{
    using namespace LHS;
    // 分布下拉
    auto combo = new QComboBox(ui->parameterTableWidget);
    combo->addItems({distName(Dist::Normal), distName(Dist::Uniform), distName(Dist::RandInt),
                     distName(Dist::Gamma)});
    combo->setCurrentIndex(distIndex(rowCfg(row).dist));
    combo->setProperty("row", row);
    ui->parameterTableWidget->setCellWidget(row, 1, combo);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LatinHypercubeSampling::onDistComboChanged);
}

void LatinHypercubeSampling::updateComboRowProperties()
{
    // 保证每个 combo 的 "row" 属性与当前行号一致（删除/插入后必须重置）
    const int rows = ui->parameterTableWidget->rowCount();
    for (int r = 0; r < rows; ++r)
    {
        if (auto* w = ui->parameterTableWidget->cellWidget(r, 1))
        {
            if (auto* combo = qobject_cast<QComboBox*>(w))
                combo->setProperty("row", r);
        }
    }
}

/* ================== 右侧面板同步 ================== */
void LatinHypercubeSampling::syncPanelFromRow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const auto& r = rowCfg(row);
    m_stack->setCurrentIndex(LHS::distIndex(r.dist));

    // Normal
    m_mu->blockSignals(true);
    m_mu->setValue(r.mu);
    m_mu->blockSignals(false);
    m_sigma->blockSignals(true);
    m_sigma->setValue(r.sigma);
    m_sigma->blockSignals(false);
    // Uniform
    m_umin->blockSignals(true);
    m_umin->setValue(r.umin);
    m_umin->blockSignals(false);
    m_umax->blockSignals(true);
    m_umax->setValue(r.umax);
    m_umax->blockSignals(false);
    // RandInt
    m_rmin->blockSignals(true);
    m_rmin->setValue(r.rmin);
    m_rmin->blockSignals(false);
    m_rmax->blockSignals(true);
    m_rmax->setValue(r.rmax);
    m_rmax->blockSignals(false);
    // Gamma
    m_gshape->blockSignals(true);
    m_gshape->setValue(r.gshape);
    m_gshape->blockSignals(false);
    m_gloc->blockSignals(true);
    m_gloc->setValue(r.gloc);
    m_gloc->blockSignals(false);
    m_gscale->blockSignals(true);
    m_gscale->setValue(r.gscale);
    m_gscale->blockSignals(false);
}

void LatinHypercubeSampling::syncRowFromPanel(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    auto& r = rowCfg(row);
    using namespace LHS;
    // 根据当前页写回
    switch (r.dist)
    {
    case Dist::Normal:
        r.mu = m_mu->value();
        r.sigma = m_sigma->value();
        break;
    case Dist::Uniform:
        r.umin = m_umin->value();
        r.umax = m_umax->value();
        break;
    case Dist::RandInt:
        r.rmin = m_rmin->value();
        r.rmax = m_rmax->value();
        break;
    case Dist::Gamma:
        r.gshape = m_gshape->value();
        r.gloc = m_gloc->value();
        r.gscale = m_gscale->value();
        break;
    }
}

/* ================== 事件/槽 ================== */
bool LatinHypercubeSampling::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == ui->parameterTableWidget && ev->type() == QEvent::KeyPress)
    {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Insert)
        {
            addRow(LHS::RowConfig{});
            return true;
        }
        if (ke->key() == Qt::Key_Delete)
        {
            removeCurrentRow();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void LatinHypercubeSampling::on_addRowButton_clicked()
{
    addRow(LHS::RowConfig{});
}
void LatinHypercubeSampling::on_removeRowButton_clicked()
{
    removeCurrentRow();
}

void LatinHypercubeSampling::onRowSelectionChanged(int currentRow, int, int prevRow, int)
{
    // 先把旧行（若存在）写回
    if (prevRow >= 0 && prevRow < m_rows.size())
        syncRowFromPanel(prevRow);
    // 再用新行刷新面板
    syncPanelFromRow(currentRow);
}

void LatinHypercubeSampling::onCellNameEdited(QTableWidgetItem* item)
{
    if (!item || item->column() != 0)
        return;
    int row = item->row();
    if (row >= 0 && row < m_rows.size())
        m_rows[row].name = item->text().trimmed();
}

void LatinHypercubeSampling::onDistComboChanged(int idx)
{
    auto* combo = qobject_cast<QComboBox*>(sender());
    if (!combo)
        return;
    int row = combo->property("row").toInt();
    if (row < 0 || row >= m_rows.size())
        return;

    // 改分布：先把旧参数写回（当前页），再切换 dist，最后刷新右侧面板
    syncRowFromPanel(row);
    m_rows[row].dist = LHS::distFromIndex(idx);
    syncPanelFromRow(row);
}

void LatinHypercubeSampling::onDistributionPageSwitchByRow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_stack->setCurrentIndex(LHS::distIndex(rowCfg(row).dist));
}

void LatinHypercubeSampling::onNormalChanged()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row < 0 || row >= m_rows.size())
        return;
    auto& r = rowCfg(row);
    r.mu = m_mu->value();
    r.sigma = m_sigma->value();
}

void LatinHypercubeSampling::onUniformChanged()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row < 0 || row >= m_rows.size())
        return;
    auto& r = rowCfg(row);
    r.umin = m_umin->value();
    r.umax = m_umax->value();
}

void LatinHypercubeSampling::onRandIntChanged()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row < 0 || row >= m_rows.size())
        return;
    auto& r = rowCfg(row);
    r.rmin = m_rmin->value();
    r.rmax = m_rmax->value();
}

void LatinHypercubeSampling::onGammaChanged()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row < 0 || row >= m_rows.size())
        return;
    auto& r = rowCfg(row);
    r.gshape = m_gshape->value();
    r.gloc = m_gloc->value();
    r.gscale = m_gscale->value();
}

/* ================== JSON 导出 ================== */
QJsonObject LatinHypercubeSampling::toJson() const
{
    QJsonObject root;
    root.insert("采样点数", ui->numSpinBox->value());

    QJsonObject paramsObj;

    for (int i = 0; i < m_rows.size(); ++i)
    {
        const auto& r = m_rows[i];

        const QString name = r.name.trimmed();

        QJsonArray packed; // [code, ...args]
        switch (r.dist)
        {
        case LHS::Dist::Normal:
        { // code=0, [mu, sigma]
            packed.append(0);
            packed.append(r.mu);
            packed.append(r.sigma);
            break;
        }
        case LHS::Dist::Uniform:
        { // code=1, [loc, scale]  (由[min,max]转换)
            const double vmin = qMin(r.umin, r.umax);
            const double vmax = qMax(r.umin, r.umax);
            packed.append(1);
            packed.append(vmin);        // loc
            packed.append(vmax - vmin); // scale
            break;
        }
        case LHS::Dist::RandInt:
        { // code=2, [low, high)
            const int low = qMin(r.rmin, r.rmax);
            const int high = qMax(r.rmin, r.rmax);
            packed.append(2);
            packed.append(low);
            packed.append(high);
            break;
        }
        case LHS::Dist::Gamma:
        { // code=3, [a, loc, scale]
            packed.append(3);
            packed.append(r.gshape);
            packed.append(r.gloc);
            packed.append(r.gscale);
            break;
        }
        }

        paramsObj.insert(name, packed);
    }

    root.insert(("参数"), paramsObj);
    return root;
}

/* =================== 启动 Python（文件传参） =================== */
void LatinHypercubeSampling::runPythonLHS_File()
{
    // 确保当前行参数写回
    int row = ui->parameterTableWidget->currentRow();
    if (row >= 0 && row < m_rows.size())
        syncRowFromPanel(row);

    // 直接使用统一结构
    const auto jsonObj = toJson();
    const auto jsonText = QString::fromUtf8(QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
    qDebug().noquote() << ">>> JSON to Python:\n" << jsonText;

    // 写临时 JSON 文件
    QFile tf("C:/project/base_cae/build/release/scripts/lhs_input.json");
    if (!tf.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("错误"), tr("无法创建临时 JSON 文件"));
        return;
    }
    tf.write(jsonText.toUtf8());
    tf.close();

    // Python 与脚本路径（替换为你的实际路径）
    const QString pythonExe =
        R"(C:\project\project\pod\build-DB-ARC-Desktop_Qt_5_12_9_MSVC2015_64bit-Release\python312\python.exe)";
    const QString scriptPath = R"(C:\project\base_cae\build\release\scripts\Latin.py)";

    QFileInfo scriptFi(scriptPath);
    const QString workDir = scriptFi.absolutePath();

    auto* p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    p->setProcessEnvironment(env);
    p->setWorkingDirectory(workDir);
    p->setProcessChannelMode(QProcess::MergedChannels);

    QStringList args;
    args << "-u" << scriptPath << tf.fileName();

    p->start(pythonExe, args);
    if (!p->waitForStarted(10000))
    {
        QMessageBox::warning(this, tr("错误"), tr("Python 未能启动"));
        p->deleteLater();
        return;
    }

    p->waitForFinished(-1);
    const int exitCode = p->exitCode();
    const QString out = QString::fromUtf8(p->readAllStandardOutput());
    qDebug().noquote() << ">>> Python output:\n" << out;
    qDebug() << ">>> exitCode =" << exitCode << " input =" << tf.fileName();

    if (exitCode != 0)
    {
        QMessageBox::warning(this, tr("脚本失败"),
                             tr("Python 返回码：%1\n输出：\n%2").arg(exitCode).arg(out));
    }
    else
    {
        QMessageBox::information(this, tr("完成"), tr("LHS 采样完成。输出目录：\n%1").arg(workDir));
    }

    QFile::remove(tf.fileName()); // 确认 OK 后再启用自动清理
    p->deleteLater();
}

void LatinHypercubeSampling::on_test_clicked()
{
    int row = ui->parameterTableWidget->currentRow();
    if (row >= 0 && row < m_rows.size())
        syncRowFromPanel(row);

    qDebug().noquote() << QString::fromUtf8(
        QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    runPythonLHS_File();
}
