#include "OrthogonalSampling.h"
#include "ui_OrthogonalSampling.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QJsonDocument>
#include <QProcess>
#include <QKeyEvent>
#include <QTableWidget>
#include <QHeaderView>
#include <algorithm>
#include <QDesktopServices>
#include <QTextStream>

OrthogonalSampling::OrthogonalSampling(QWidget* parent)
    : BasePage("Orthogonal")
    , ui(new Ui::OrthogonalSampling)
{
    ui->setupUi(this);

    // 事件过滤：支持 Insert / Delete 快捷键
    ui->m_paramTable->installEventFilter(this);

    wireSignals();

    // 默认示例（可删）
    addParamRow(tr("高度"),   "0, 500, 1000, 1500", true /*已忽略*/);
    addParamRow(tr("马赫数"), "0.8, 1.0, 1.2",      true /*已忽略*/);
    addParamRow(tr("攻角"),   "-5; 0; 5",           true /*已忽略*/);

    // 如需默认路径，可在此设置
    // ui->m_pythonExeEdit->setText(R"(C:\Path\to\python.exe)");
    // ui->m_scriptPathEdit->setText(R"(C:\Path\to\Orthogonal.py)");
}

OrthogonalSampling::~OrthogonalSampling()
{
    delete ui;
}

void OrthogonalSampling::wireSignals()
{
    connect(ui->m_addBtn, &QPushButton::clicked, this, [this]{ addParamRow(); });
    connect(ui->m_delBtn, &QPushButton::clicked, this, [this]{ removeSelectedRows(); });
    connect(ui->m_runBtn, &QPushButton::clicked, this, [this]{ runPythonOrth_File(); });

    connect(ui->openFolderBtn, &QPushButton::clicked, this, [this]{
        const QString dir = ui->outputDirEdit->text().trimmed();
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(ui->reloadBtn, &QPushButton::clicked, this, [this]{
        const QString dir = ui->outputDirEdit->text().trimmed();
        if (!dir.isEmpty()) loadOrthOutputsFromDir(dir);
    });
}

int OrthogonalSampling::addParamRow(const QString& name, const QString& values, bool /*numeric*/)
{
    QTableWidget* t = ui->m_paramTable;
    const int r = t->rowCount();
    t->insertRow(r);

    // 第0列：参数名
    auto* nameIt = new QTableWidgetItem(name.isEmpty()? tr("Param%1").arg(r+1) : name);
    nameIt->setFlags(nameIt->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    t->setItem(r, 0, nameIt);

    // 第1列：值列表
    auto* valsIt = new QTableWidgetItem(values);
    valsIt->setFlags(valsIt->flags() | Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    t->setItem(r, 1, valsIt);

    return r;
}

void OrthogonalSampling::removeSelectedRows()
{
    auto* t = ui->m_paramTable;
    const auto rows = t->selectionModel()->selectedRows();
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end(),
              [](const QModelIndex& a, const QModelIndex& b){ return a.row() > b.row(); });
    for (const auto& idx : sorted) t->removeRow(idx.row());
}

void OrthogonalSampling::beforeLoad()
{

}

void OrthogonalSampling::beforeUnload()
{

}

QStringList OrthogonalSampling::splitValues(const QString& text) const
{
    QString t = text;
    t.replace('\n',' ').replace('\r',' ').replace(',', ' ').replace(';',' ');
    QStringList parts = t.split(' ', Qt::SkipEmptyParts);
    QStringList out;
    for (auto s : parts) {
        for (auto v : s.split('|', Qt::SkipEmptyParts)) {
            v = v.trimmed(); if (!v.isEmpty()) out << v;
        }
    }
    return out;
}

QJsonObject OrthogonalSampling::toJsonParams() const
{
    QJsonObject root, params;
    QTableWidget* t = ui->m_paramTable;
    const int rows = t->rowCount();
    for (int r=0; r<rows; ++r)
    {
        const QTableWidgetItem* nameItem = t->item(r,0);
        if (!nameItem) continue;
        const QString name = nameItem->text().trimmed();
        if (name.isEmpty()) continue;

        const QTableWidgetItem* valsItem = t->item(r,1);
        const QString valuesText = valsItem ? valsItem->text() : QString();
        const auto list = splitValues(valuesText);

        QJsonArray arr;
        for (const auto& s : list) {
            bool ok=false;
            const double v = s.toDouble(&ok);
            if (ok) {
                arr.append(v);      // 可转数字 → 存数字
            } else {
                arr.append(s);      // 否则 → 存字符串
            }
        }
        if (!arr.isEmpty())
            params.insert(name, arr);
    }
    root.insert(QStringLiteral("params"), params);
    return root;
}

void OrthogonalSampling::runPythonOrth_File()
{
//    const QString py = ui->m_pythonExeEdit->text().trimmed();
//    const QString sc = ui->m_scriptPathEdit->text().trimmed();
    const QString py = R"(C:\project\project\pod\build-DB-ARC-Desktop_Qt_5_12_9_MSVC2015_64bit-Release\python312\python.exe)";
    const QString sc = R"(C:\project\base_cae\build\release\scripts\Orthogonal.py)";
    if (py.isEmpty() || sc.isEmpty()) {
        QMessageBox::warning(this, tr("缺少路径"), tr("请先选择 Python 解释器与脚本文件。"));
        return;
    }
    if (!QFileInfo::exists(py) || !QFileInfo(py).isFile()) {
        QMessageBox::warning(this, tr("无效路径"), tr("Python 解释器不存在。"));
        return;
    }
    if (!QFileInfo::exists(sc) || !QFileInfo(sc).isFile()) {
        QMessageBox::warning(this, tr("无效路径"), tr("脚本文件不存在。"));
        return;
    }

    const auto jsonObj  = toJsonParams();
    const auto jsonText = QString::fromUtf8(QJsonDocument(jsonObj).toJson(QJsonDocument::Compact));
    if (!jsonObj.contains("params") || jsonObj["params"].toObject().isEmpty()) {
        QMessageBox::warning(this, tr("无参数"), tr("请至少添加一个参数且含有值。"));
        return;
    }

    QFile  tf("C:/project/base_cae/build/release/scripts/orthogonal_input.json");
    if (!tf.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("错误"), tr("无法创建临时 JSON 文件"));
        return;
    }
    tf.write(jsonText.toUtf8()); tf.close();

    QFileInfo scInfo(sc);
    auto* p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    p->setProcessEnvironment(env);
    p->setWorkingDirectory(scInfo.absolutePath());
    p->setProcessChannelMode(QProcess::MergedChannels);

    QStringList args; args << "-u" << sc << tf.fileName();
    p->start(py, args);
    if (!p->waitForStarted(10000)) {
        QMessageBox::warning(this, tr("错误"), tr("Python 未能启动"));
        p->deleteLater();
        return;
    }

    p->waitForFinished(-1);
        const int exitCode = p->exitCode();
        const QString out  = QString::fromUtf8(p->readAllStandardOutput());
        const QString scriptDir = QFileInfo(sc).absolutePath();

        if (exitCode != 0) {
            QMessageBox::warning(this, tr("脚本失败"),
                                 tr("返回码：%1\n输出：\n%2").arg(exitCode).arg(out));
        } else {
            QMessageBox::information(this, tr("完成"),
                                     tr("脚本已完成。\n结果文件在脚本目录：\nOrthogonal_sample_result.txt\nOrthogonal_statistical_evaluation.txt\n(若为2/3维，还会生成 Orthogonal_sample.png)"));
            // 显示输出目录并加载结果
            ui->outputDirEdit->setText(QDir::toNativeSeparators(scriptDir));
            loadOrthOutputsFromDir(scriptDir);

        }
        p->deleteLater();
    // QFile::remove(tf.fileName());
}

bool OrthogonalSampling::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == ui->m_paramTable && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Insert) { addParamRow(); return true; }
        if (ke->key() == Qt::Key_Delete) { removeSelectedRows(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

void OrthogonalSampling::loadOrthOutputsFromDir(const QString& dir)
{
    const QString sampleTxt = QDir(dir).filePath("Orthogonal_sample_result.txt");
    const QString statTxt   = QDir(dir).filePath("Orthogonal_statistical_evaluation.txt");
    const QString pngPath   = QDir(dir).filePath("Orthogonal_sample.png");

    loadSampleTable(sampleTxt);
    loadStatText(statTxt);
    loadImageIfExists(pngPath);
}

void OrthogonalSampling::loadSampleTable(const QString& path)
{
    ui->sampleTable->clear();
    ui->sampleTable->setRowCount(0);
    if (!QFileInfo::exists(path)) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream ts(&f);
    // ts.setCodec("UTF-8");
    const QString headerLine = ts.readLine().trimmed();
    if (headerLine.isEmpty()) { f.close(); return; }

    // 兼容 \t 或逗号/分号/空白
    auto split = [](const QString& s){
        if (s.contains('\t')) return s.split('\t', Qt::SkipEmptyParts);
        QString t = s; for (QChar ch : QString(",;")) t.replace(ch, ' ');
        return t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    };
    QStringList headers = split(headerLine);
    ui->sampleTable->setColumnCount(headers.size());
    ui->sampleTable->setHorizontalHeaderLabels(headers);
    ui->sampleTable->horizontalHeader()->setStretchLastSection(true);
    ui->sampleTable->setAlternatingRowColors(true);

    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList parts = split(line);
        if (parts.size() < headers.size()) continue; // 忽略缺列行
        const int r = ui->sampleTable->rowCount();
        ui->sampleTable->insertRow(r);
        for (int c=0; c<headers.size(); ++c) {
            auto* it = new QTableWidgetItem(c < parts.size() ? parts[c] : QString());
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->sampleTable->setItem(r, c, it);
        }
    }
    f.close();
}

void OrthogonalSampling::loadStatText(const QString& path)
{
//    ui->statText->clear();
//    if (!QFileInfo::exists(path)) return;
//    QFile f(path);
//    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
//    QTextStream ts(&f);
//    ts.setCodec("UTF-8");
//    ui->statText->setPlainText(ts.readAll());
//    f.close();
}

void OrthogonalSampling::loadImageIfExists(const QString& path)
{
//    if (!QFileInfo::exists(path)) { ui->resultImageLabel->clear(); return; }
//    QPixmap px(path);
//    ui->resultImageLabel->setPixmap(px);
//    ui->resultImageLabel->setScaledContents(true);
}
