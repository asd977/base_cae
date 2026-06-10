#include "DatasetEvaluation.h"

#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextStream>
#include <algorithm>

#include "ui_DatasetEvaluation.h"

// ===== 默认路径（按需修改） =====
QString DatasetEvaluation::defaultOrthFile()
{
    return QDir::toNativeSeparators(
        "C:/project/base_cae/build/release/scripts/Orthogonal_sample_result.txt");
}
QString DatasetEvaluation::defaultLhsFile()
{
    return QDir::toNativeSeparators(
        "C:/project/base_cae/build/release/scripts/LHS_sample_result.txt");
}
QString DatasetEvaluation::defaultPythonExe()
{
#ifdef Q_OS_WIN
    return R"(C:\project\project\pod\build-DB-ARC-Desktop_Qt_5_12_9_MSVC2015_64bit-Release\python312\python.exe)";
#else
    return "/usr/bin/python3";
#endif
}
QString DatasetEvaluation::defaultScriptPath()
{
    return QDir::toNativeSeparators("C:/project/base_cae/build/release/scripts/rbf_eval.py");
}

// ===== 构造/析构 =====
DatasetEvaluation::DatasetEvaluation(QWidget* parent)
    : BasePage("DatasetEvaluation"), ui(new Ui::DatasetEvaluation)
{
    ui->setupUi(this);

    // 预置
    ui->datasetCombo->setCurrentIndex(0);
    ui->pythonExeEdit->setText(defaultPythonExe());
    ui->scriptPathEdit->setText(defaultScriptPath());
    ui->thresholdSpin->setRange(0.0, 1.0);
    ui->thresholdSpin->setSingleStep(0.05);
    ui->thresholdSpin->setValue(0.6);

    // 表格基础设置
    ui->testTable->setAlternatingRowColors(true);
    ui->testTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->testTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->testTable->horizontalHeader()->setStretchLastSection(true);
    ui->testTable->installEventFilter(this);

    ui->resultTable->setAlternatingRowColors(true);
    ui->resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->resultTable->horizontalHeader()->setStretchLastSection(true);

    wireSignals();
    onDatasetKindChanged();    // 设定默认文件
    detectParamsFromDataset(); // 初次识别参数
    rebuildTestTable();        // 构造测试表列
}

DatasetEvaluation::~DatasetEvaluation()
{
    delete ui;
}

void DatasetEvaluation::beforeLoad()
{
}
void DatasetEvaluation::beforeUnload()
{
}

// ===== 信号绑定 =====
void DatasetEvaluation::wireSignals()
{
    connect(ui->datasetCombo, &QComboBox::currentTextChanged, this,
            [this]
            {
                onDatasetKindChanged();
            });
    connect(ui->browseDatasetBtn, &QPushButton::clicked, this,
            [this]
            {
                const QString f = QFileDialog::getOpenFileName(this, tr("选择采样结果文件"));
                if (!f.isEmpty())
                {
                    ui->datasetPathEdit->setText(f);
                    detectParamsFromDataset();
                    rebuildTestTable();
                }
            });
    connect(ui->refreshBtn, &QPushButton::clicked, this,
            [this]
            {
                detectParamsFromDataset();
                rebuildTestTable();
            });

    connect(ui->selectAllBtn, &QPushButton::clicked, this,
            [this]
            {
                for (int i = 0; i < ui->paramList->count(); ++i)
                    ui->paramList->item(i)->setCheckState(Qt::Checked);
            });
    connect(ui->invertSelBtn, &QPushButton::clicked, this,
            [this]
            {
                for (int i = 0; i < ui->paramList->count(); ++i)
                {
                    auto* it = ui->paramList->item(i);
                    it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked
                                                                      : Qt::Checked);
                }
            });
    connect(ui->paramList, &QListWidget::itemChanged, this,
            [this]
            {
                rebuildTestTable();
            });

    connect(ui->browsePyBtn, &QPushButton::clicked, this,
            [this]
            {
                const QString f = QFileDialog::getOpenFileName(this, tr("选择 Python 解释器"));
                if (!f.isEmpty())
                    ui->pythonExeEdit->setText(f);
            });
    connect(ui->browseScBtn, &QPushButton::clicked, this,
            [this]
            {
                const QString f = QFileDialog::getOpenFileName(this, tr("选择 rbf_eval_var.py"));
                if (!f.isEmpty())
                    ui->scriptPathEdit->setText(f);
            });

    connect(ui->addRowBtn, &QPushButton::clicked, this,
            [this]
            {
                addTestRow();
            });
    connect(ui->delRowBtn, &QPushButton::clicked, this,
            [this]
            {
                removeSelectedTestRows();
            });

    // connect(ui->evaluateBtn, &QPushButton::clicked, this,
    //         [this]
    //         {
    //             if (m_usedParams.isEmpty())
    //             {
    //                 QMessageBox::warning(this, tr("未选择参数"),
    //                                      tr("请在“参数列”中至少勾选一个参数。"));
    //                 return;
    //             }
    //             const QString train = ui->datasetPathEdit->text().trimmed();
    //             if (train.isEmpty() || !QFileInfo::exists(train))
    //             {
    //                 QMessageBox::warning(this, tr("无效数据集"),
    //                                      tr("请先选择有效的采样结果文件。"));
    //                 return;
    //             }
    //             const QString testJsonFile = makeTestJsonFile();

    //             if (testJsonFile.isEmpty())
    //             {
    //                 QMessageBox::warning(
    //                     this, tr("无测试点"),
    //                     tr("请在“测试点”表中输入至少一行（所有已勾选参数需填写数值）。"));
    //                 return;
    //             }

    //             QString outTxt, outPng, consoleOut;
    //             if (!runRbfEval(train, testJsonFile, ui->thresholdSpin->value(), &outTxt, &outPng,
    //                             &consoleOut))
    //             {
    //                 QMessageBox::warning(this, tr("执行失败"),
    //                                      consoleOut.isEmpty() ? tr("RBF 评估脚本执行失败。")
    //                                                           : consoleOut);
    //                 return;
    //             }
    //             loadRbfOutputToTable(outTxt);
    //             //        loadImageIfExists(outPng);
    //         });
}

void DatasetEvaluation::onDatasetKindChanged()
{
    const QString kind = ui->datasetCombo->currentText();
    if (kind.contains("正交"))
        ui->datasetPathEdit->setText(defaultOrthFile());
    else if (kind.contains("拉丁"))
        ui->datasetPathEdit->setText(defaultLhsFile());
    // 自定义：保持现状
}

// ===== 参数识别：从文件中读表头 + 猜测数值列 =====
QStringList DatasetEvaluation::tryDetectHeaders(const QString& path, QString* err)
{
    QStringList headers;
    if (!QFileInfo::exists(path))
    {
        if (err)
            *err = tr("文件不存在");
        return headers;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (err)
            *err = tr("无法打开文件");
        return headers;
    }
    const QString firstLineRaw = QString::fromUtf8(f.readLine()); // ⚠️ 不要 trimmed()
    f.close();

    const QString firstTrim = firstLineRaw.trimmed();

    // 处理 JSON 格式
    if (firstTrim.startsWith('[') || firstTrim.startsWith('{'))
    {
        QFile jf(path);
        jf.open(QIODevice::ReadOnly | QIODevice::Text);
        const auto doc = QJsonDocument::fromJson(jf.readAll());
        jf.close();
        if (doc.isArray() && !doc.array().isEmpty() && doc.array().first().isObject())
        {
            const auto obj = doc.array().first().toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                headers << it.key();
        }
        return headers;
    }

    // 处理文本格式
    auto splitKeep = [](const QString& s) -> QStringList
    {
        if (s.contains('\t'))
            return s.split('\t', QString::SkipEmptyParts);
        if (s.contains(','))
            return s.split(',', QString::SkipEmptyParts);
        if (s.contains(';'))
            return s.split(';', QString::SkipEmptyParts);
        // 回退：空白分隔（无法保留空字段）
        return s.simplified().split(' ', QString::SkipEmptyParts);
    };

    // 使用 KeepEmptyParts 保留空字段
    const QStringList raw = splitKeep(firstLineRaw);

    int nonNum = 0;
    for (const auto& tok : raw)
    {
        const QString t = QString(tok).trimmed();
        bool ok = false;
        t.toDouble(&ok);
        if (!ok)
            nonNum++;
    }

    // 判断是否有非数字列
    if (nonNum >= 1)
    {
        headers.reserve(raw.size());
        for (const auto& tok : raw)
            headers << QString(tok).trimmed();
    }

    // 判断是否首列是索引列（正交采样格式的首列会是索引）
    if (headers.size() > 1 && headers.first().isEmpty())
    {
        // 如果第一列为空，则认为是索引列，忽略该列
        headers.removeFirst();
    }

    return headers;
}

void DatasetEvaluation::detectParamsFromDataset()
{
    m_allParams.clear();
    m_usedParams.clear();
    ui->paramList->clear();

    const QString path = ui->datasetPathEdit->text().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    QString err;
    const QStringList headers = tryDetectHeaders(path, &err);
    qInfo() << "path " << path;
    qInfo() << "headers " << headers;

    // 过滤无效列（首列 index/编号、以及 colN 占位）
    QStringList filtered;
    QRegularExpression reCol("^col\\d+$", QRegularExpression::CaseInsensitiveOption);
    for (const auto& name : headers)
    {
        const QString s = name.trimmed();
        if (s.isEmpty())
            continue;
        if (s.compare("index", Qt::CaseInsensitive) == 0)
            continue;
        if (s.compare("编号", Qt::CaseInsensitive) == 0)
            continue;
        if (reCol.match(s).hasMatch())
            continue;
        filtered << s;
    }

    m_allParams = filtered;
    for (const auto& n : m_allParams)
    {
        auto* it = new QListWidgetItem(n, ui->paramList);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
    m_usedParams = m_allParams;
}

void DatasetEvaluation::rebuildTestTable()
{
    // 收集勾选列
    m_usedParams.clear();
    for (int i = 0; i < ui->paramList->count(); ++i)
        if (ui->paramList->item(i)->checkState() == Qt::Checked)
            m_usedParams << ui->paramList->item(i)->text();

    // 重建测试表头
    auto* t = ui->testTable;
    const int cols = m_usedParams.size();
    t->clear();
    t->setColumnCount(cols);
    t->setRowCount(0);
    t->setHorizontalHeaderLabels(m_usedParams);
}

void DatasetEvaluation::addTestRow()
{
    auto* t = ui->testTable;
    const int r = t->rowCount();
    t->insertRow(r);
    for (int c = 0; c < t->columnCount(); ++c)
    {
        auto* it = new QTableWidgetItem();
        it->setFlags(it->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        t->setItem(r, c, it);
    }
}

void DatasetEvaluation::removeSelectedTestRows()
{
    auto* t = ui->testTable;
    auto rows = t->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex& a, const QModelIndex& b)
              {
                  return a.row() > b.row();
              });
    for (const auto& idx : rows)
        t->removeRow(idx.row());
}

// 将测试点导成 JSON 数组文件：[{param1: v1, ...}, ...]
QString DatasetEvaluation::makeTestJsonFile() const
{
    QJsonArray arr;
    auto* t = ui->testTable;
    for (int r = 0; r < t->rowCount(); ++r)
    {
        QJsonObject obj;
        bool rowOK = true;
        for (int c = 0; c < t->columnCount(); ++c)
        {
            const QString key = m_usedParams.value(c);
            const auto* it = t->item(r, c);
            if (!it)
            {
                rowOK = false;
                break;
            }
            bool ok = false;
            const double v = it->text().trimmed().toDouble(&ok);
            if (!ok)
            {
                rowOK = false;
                break;
            }
            obj.insert(key, v);
        }
        if (rowOK)
            arr.append(obj);
    }
    if (arr.isEmpty())
        return {};

    const QString path = "C:/project/base_cae/build/release/scripts/rbf_test_points.json";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.close();
    return path;
}

bool DatasetEvaluation::runRbfEval(const QString& trainPayloadPath, const QString& testPayloadPath,
                                   double threshold, QString* outTxtPath, QString* outPngPath,
                                   QString* consoleOut)
{
    const QString py = ui->pythonExeEdit->text().trimmed().isEmpty()
                           ? defaultPythonExe()
                           : ui->pythonExeEdit->text().trimmed();
    const QString sc = ui->scriptPathEdit->text().trimmed().isEmpty()
                           ? defaultScriptPath()
                           : ui->scriptPathEdit->text().trimmed();

    if (!QFileInfo::exists(py))
    {
        if (consoleOut)
            *consoleOut = tr("Python 解释器不存在");
        return false;
    }
    if (!QFileInfo::exists(sc))
    {
        if (consoleOut)
            *consoleOut = tr("评估脚本不存在");
        return false;
    }
    if (m_usedParams.isEmpty())
    {
        if (consoleOut)
            *consoleOut = tr("未选择任何参数列");
        return false;
    }

    const QString paramsArg = m_usedParams.join(',');

    // 输出前缀在临时目录
    const QString prefix = "C:/project/base_cae/build/release/scripts";

    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QFileInfo(sc).absolutePath());
    p.setProcessChannelMode(QProcess::MergedChannels);

    QStringList args;
    args << "-u" << sc << trainPayloadPath << testPayloadPath
         << QString("--params=%1").arg(paramsArg) << QString("--threshold=%1").arg(threshold)
         << QString("--out=%1").arg(prefix);

    p.start(py, args);
    if (!p.waitForStarted(10000))
    {
        if (consoleOut)
            *consoleOut = tr("Python 启动失败");
        return false;
    }
    p.waitForFinished(-1);
    const int code = p.exitCode();
    if (consoleOut)
        *consoleOut = QString::fromUtf8(p.readAllStandardOutput());
    if (code != 0)
        return false;

    if (outTxtPath)
        *outTxtPath = prefix + "_output.txt";
    if (outPngPath)
        *outPngPath = prefix + ".png";
    return true;
}

void DatasetEvaluation::loadRbfOutputToTable(const QString& outTxtPath)
{
    QFile f(outTxtPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream ts(&f);
    // ts.setCodec("UTF-8"); // 设置正确的编码

    const QString header = ts.readLine().trimmed(); // 读取表头并去掉前后空格
    const QStringList cols = header.split('\t', QString::SkipEmptyParts);

    // 重建结果表头
    ui->resultTable->clear();
    ui->resultTable->setRowCount(0);
    ui->resultTable->setColumnCount(cols.size());
    ui->resultTable->setHorizontalHeaderLabels(cols);

    while (!ts.atEnd())
    {
        const QString line = ts.readLine().trimmed(); // 去掉每行的前后空格
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split('\t'); // 使用制表符分割列
        if (parts.size() != cols.size())
            continue; // 确保列数一致

        const int r = ui->resultTable->rowCount();
        ui->resultTable->insertRow(r);
        for (int c = 0; c < parts.size(); ++c)
        {
            auto* it = new QTableWidgetItem(parts[c]);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

            // 处理结果列的颜色
            if (cols[c] == "结果")
            {
                if (parts[c].compare("yes", Qt::CaseInsensitive) == 0)
                    it->setForeground(QColor(0, 128, 0)); // 绿色表示yes
                else
                    it->setForeground(QColor(200, 0, 0)); // 红色表示no
            }
            ui->resultTable->setItem(r, c, it);
        }
    }
    f.close();
}

void DatasetEvaluation::loadImageIfExists(const QString& pngPath)
{
    if (!QFileInfo::exists(pngPath))
    {
        ui->imageLabel->clear();
        return;
    }
    QPixmap px(pngPath);
    ui->imageLabel->setPixmap(px);
    ui->imageLabel->setScaledContents(true);
}

bool DatasetEvaluation::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == ui->testTable && ev->type() == QEvent::KeyPress)
    {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Insert)
        {
            addTestRow();
            return true;
        }
        if (ke->key() == Qt::Key_Delete)
        {
            removeSelectedTestRows();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}
