#include "DNNModel.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>

#include <QString>

#include "ui_DNNModel.h"

DNNModel::DNNModel(QWidget* parent) : ui(new Ui::DNNModel), BasePage("DNN")
{
    ui->setupUi(this);

    m_trainModelProcess = new QProcess;
    m_splitProcess = new QProcess;
    m_outputService = ServiceRegistry::instance().getService<OutputService>();
    setStyle();

    ui->sourceFileTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->layerWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    actualSeries = new QLineSeries();

    predictedSeries = new QLineSeries();

    // 创建图表
    chart = new QChart();
    chart->addSeries(actualSeries);
    chart->addSeries(predictedSeries);
    chart->setTitle(("腐蚀速度对比分析"));

    // 创建并设置轴
    axisX = new QValueAxis();
    axisX->setTitleText(("时间"));
    axisX->setLabelFormat("%.2f");
    chart->addAxis(axisX, Qt::AlignBottom);
    actualSeries->attachAxis(axisX);
    predictedSeries->attachAxis(axisX);

    axisY = new QValueAxis();
    axisY->setTitleText(("腐蚀速度"));
    axisY->setLabelFormat("%.6f");
    chart->addAxis(axisY, Qt::AlignLeft);
    actualSeries->attachAxis(axisY);
    predictedSeries->attachAxis(axisY);

    // 设置图例
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignTop);
    connectOutput();
}

DNNModel::~DNNModel()
{
    delete ui;
}

void DNNModel::beforeLoad()
{
    m_workPath = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
}

void DNNModel::beforeUnload()
{
}

void DNNModel::on_browseSourceFilepushButton_clicked()
{
    QString filePath =
        QFileDialog::getOpenFileName(this, ("选择源文件"), "", tr("所有文件 (*.*)"));

    if (!filePath.isEmpty())
    {
        ui->sourceFilelineEdit->setText(filePath);
    }
}

void DNNModel::on_importSourceFilePushButton_clicked()
{
    // Get the source file path
    QString sourceFile = ui->sourceFilelineEdit->text();

    if (sourceFile.isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("请先选择源文件"));
        return;
    }

    // Open the source file
    QFile file(sourceFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, ("错误"), ("无法打开源文件"));
        return;
    }

    // Create output directory if it doesn't exist
    QDir dir;
    QString outputDir = m_workPath + "/model";
    if (!dir.exists(outputDir))
    {
        dir.mkpath(outputDir);
    }

    // Prepare output file
    QString outputFilePath = outputDir + "/Corr_data-2.txt";
    QFile outputFile(outputFilePath);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("错误"), tr("无法创建输出文件"));
        file.close();
        return;
    }

    QTextStream in(&file);
    QTextStream out(&outputFile);
    QStringList variableNames;
    int lineNumber = 0;

    while (!in.atEnd())
    {
        QString line = in.readLine();
        lineNumber++;

        if (lineNumber == 5)
        {
            // Process header line
            QStringList headers = line.split(",");
            if (!headers.isEmpty())
            {
                headers.removeFirst();   // Remove "Time (d)"
                variableNames = headers; // Store variable names
                continue;                // Skip writing this line to output
            }
        }
        else if (lineNumber >= 6)
        {
            // Process data lines
            QStringList values = line.split(",");
            if (values.size() >= 1)
            {
                values.removeFirst(); // Remove first column
                // Join remaining values with space and remove commas
                QString processedLine = values.join(" ");
                out << processedLine << "\n";
            }
        }
    }

    file.close();
    outputFile.close();

    // 可用文件生成完成

    // 调用拆分
    if (ui->verifyDataLineEdit->text().isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("请输入验证数据分数"));
        return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1"); // 禁用 Python 输出缓冲

    QString currentPath = QDir::currentPath();

    m_splitProcess->setProcessEnvironment(env);

    m_splitProcess->setWorkingDirectory(currentPath + "/python309");
    m_splitProcess->setProgram(currentPath + "/python309/python.exe");

    // 设置参数列表
    m_splitProcess->setArguments(QStringList() << QDir(currentPath).filePath("corrode/split_train_test.py")
                                               << QDir(m_workPath).filePath("model/Corr_data-2.txt")
                                               << QDir(m_workPath).filePath("model/Corr_data_train.txt")
                                               << QDir(m_workPath).filePath("model/Corr_data_validation.txt")
                                               << ui->verifyDataLineEdit->text());

    m_splitProcess->start();

    // Optionally, show success message
    qInfo() << ("文件已成功处理并保存为Corr_data-2.txt");

    m_variableNames = variableNames;
    qInfo() << m_variableNames;

    // 之后在ui->sourceFileTableWidget 中根据m_variableNames中的数量添加行
    // 第一列就是m_variableNames中的名称，第二列除了最后一行都是“变元”，最后一行是“函数值”，
    // 第三列除了除了最后一行都是“名称=x%d，缩放=to01”,%d从1开始递增，最后一行是“名称=dnn1,缩放=to01”

    // Clear the table widget first
    ui->sourceFileTableWidget->clearContents();
    ui->sourceFileTableWidget->setRowCount(0);

    // Set row count based on number of variable names
    ui->sourceFileTableWidget->setRowCount(m_variableNames.size());

    // Populate table rows
    for (int i = 0; i < m_variableNames.size(); ++i)
    {
        // First column: variable names
        ui->sourceFileTableWidget->setItem(i, 0, new QTableWidgetItem(m_variableNames[i]));

        // Second column: type ("变元" for all except last row, "函数值" for last
        // row)
        QString type = (i == m_variableNames.size() - 1) ? ("函数值") : ("变元");
        ui->sourceFileTableWidget->setItem(i, 1, new QTableWidgetItem(type));

        // Third column: parameters
        QString param;
        if (i == m_variableNames.size() - 1)
        {
            param = ("名称=dnn1,缩放=to01");
        }
        else
        {
            param = QString("名称=x%1,缩放=to01").arg(i + 1);
        }
        ui->sourceFileTableWidget->setItem(i, 2, new QTableWidgetItem(param));
    }

    // 清空ui->layerWidget，添加层，导入完成，应该自动添加两层输入层和输出层
    // 表头我已经定义好了
    // 两列,"输入","输入特征=%d".arg(ui->sourceFileTableWidget中的行数删除)
    // 两列，"密集","输出特征=1,活化等于=线性(无)"

    // 获取输入维度（sourceFileTableWidget 行数减去输出行）
    int rowCount = ui->sourceFileTableWidget->rowCount();
    if (rowCount < 2)
    {
        QMessageBox::critical(this, ("错误"), ("变量数量不足，至少需要1个输入变量和1个输出变量"));
        return;
    }
    int inputDim = rowCount - 1;

    // 清空 layerWidget
    ui->layerWidget->clearContents();
    ui->layerWidget->setRowCount(0);

    // 设置行数：输入层 + 输出层
    ui->layerWidget->setRowCount(2);

    // 输入层
    ui->layerWidget->setItem(0, 0, new QTableWidgetItem(("输入")));
    ui->layerWidget->setItem(0, 1, new QTableWidgetItem(QString("输入特征=%1").arg(inputDim)));

    // 输出层
    ui->layerWidget->setItem(1, 0, new QTableWidgetItem(("密集")));
    ui->layerWidget->setItem(1, 1, new QTableWidgetItem(("输出特征=1,活化=线性(无)")));

    QString destinationPath = QDir(m_workPath).filePath("model/Corr_data.txt");
    if (QFile::exists(sourceFile))
    {
        if (QFile::copy(sourceFile, destinationPath))
        {
        }
        else
        {
            qInfo() << "Failed to copy file to" << destinationPath;
        }
    }
    else
    {
        qInfo() << "Source file does not exist:" << sourceFile;
    }
}

void DNNModel::on_addLayerButton_clicked()
{
    // 在初始的两层中间添加，不能添加在输出层下面
    // 两列"密集","输出特征=%d,活化=%s".arg(用户通过lineEdit输入,用户通过下拉框选择)

    // 同时应该有槽函数，选中哪一行，就编辑哪一行的数据，通过linedit和下拉框

    // 同时帮我写一个获取这些参数到layerSizesStr和activationFunctionsStr的函数
    //  获取用户输入的神经元数量
    QString neuronCountStr = ui->neuronLineEdit->text().trimmed();
    bool ok;
    int neuronCount = 2;

    // 获取激活函数
    QString activation = "ReLu";
    if (!QStringList({"ReLu", "ELU", "Sigmoid", "tanh"}).contains(activation))
    {
        QMessageBox::warning(this, ("错误"), ("请选择有效的激活函数（ReLu, ELU, Sigmoid, tanh）"));
        return;
    }

    // 在输入层和输出层之间插入新行
    int rowCount = ui->layerWidget->rowCount();
    int insertRow = rowCount - 1; // 插入在输出层之前
    ui->layerWidget->insertRow(insertRow);

    // 设置新层数据
    ui->layerWidget->setItem(insertRow, 0, new QTableWidgetItem(("密集")));
    ui->layerWidget->setItem(
        insertRow, 1,
        new QTableWidgetItem(QString("输出特征=%1,活化=%2").arg(neuronCount).arg(activation)));
}

void DNNModel::on_layerWidget_itemSelectionChanged()
{
    QList<QTableWidgetSelectionRange> selected = ui->layerWidget->selectedRanges();
    if (selected.isEmpty())
    {
        ui->neuronLineEdit->clear();
        ui->activationComboBox->setCurrentIndex(-1);
        ui->neuronLineEdit->setEnabled(false);
        ui->activationComboBox->setEnabled(false);
        return;
    }

    int row = selected.first().topRow();
    if (row == 0 || row == ui->layerWidget->rowCount() - 1)
    {
        // 输入层或输出层不可编辑
        ui->neuronLineEdit->setEnabled(false);
        ui->activationComboBox->setEnabled(false);
        ui->neuronLineEdit->clear();
        ui->activationComboBox->setCurrentIndex(-1);
        return;
    }

    ui->neuronLineEdit->setEnabled(true);
    ui->activationComboBox->setEnabled(true);

    // 解析参数列
    QString param = ui->layerWidget->item(row, 1)->text();
    QRegularExpression re(("输出特征=(\\d+),活化=(\\w+|线性\\(无\\))"));
    QRegularExpressionMatch match = re.match(param);
    if (match.hasMatch())
    {
        ui->neuronLineEdit->setText(match.captured(1));
        ui->activationComboBox->setCurrentText(match.captured(2));
    }
    else
    {
        ui->neuronLineEdit->clear();
        ui->activationComboBox->setCurrentIndex(-1);
    }
}

void DNNModel::on_neuronLineEdit_editingFinished()
{
    updateSelectedLayer();
}

void DNNModel::on_activationComboBox_currentTextChanged(const QString& text)
{
    updateSelectedLayer();
}

void DNNModel::updateSelectedLayer()
{
    QList<QTableWidgetSelectionRange> selected = ui->layerWidget->selectedRanges();
    if (selected.isEmpty())
        return;

    int row = selected.first().topRow();
    if (row == 0 || row == ui->layerWidget->rowCount() - 1)
        return; // 禁止修改输入层或输出层

    QString neuronCountStr = ui->neuronLineEdit->text().trimmed();
    bool ok;
    int neuronCount = neuronCountStr.toInt(&ok);
    if (!ok || neuronCount <= 0)
    {
        QMessageBox::warning(this, ("错误"), ("请输入有效的神经元数量（正整数）"));
        return;
    }

    QString activation = ui->activationComboBox->currentText();
    if (!QStringList({"ReLu", "ELU", "Sigmoid", "tanh"}).contains(activation))
    {
        QMessageBox::warning(this, ("错误"), ("请选择有效的激活函数（ReLu, ELU, Sigmoid, tanh）"));
        return;
    }

    ui->layerWidget->setItem(
        row, 1,
        new QTableWidgetItem(QString(("输出特征=%1,活化=%2")).arg(neuronCount).arg(activation)));
}

void DNNModel::on_deleteLayerButton_clicked()
{
    QList<QTableWidgetSelectionRange> selected = ui->layerWidget->selectedRanges();
    if (selected.isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("请先选择要删除的层"));
        return;
    }

    int row = selected.first().topRow();
    if (row == 0 || row == ui->layerWidget->rowCount() - 1)
    {
        QMessageBox::warning(this, ("错误"), ("不能删除输入层或输出层"));
        return;
    }

    ui->layerWidget->removeRow(row);
}

void DNNModel::getLayerParameters(QString& layerSizesStr, QString& activationFunctionsStr)
{
    QStringList layerSizes;
    QStringList activationFunctions;

    // 激活函数映射：UI 名称 -> Python 脚本名称
    QMap<QString, QString> activationMap = {
        {"ReLu", "relu"}, {"ELU", "elu"}, {"Sigmoid", "sigmoid"}, {"tanh", "tanh"}};

    int rowCount = ui->layerWidget->rowCount();
    if (rowCount < 2)
    {
        QMessageBox::critical(this, ("错误"), ("模型结构必须包含输入层和输出层。"));
        layerSizesStr.clear();
        activationFunctionsStr.clear();
        return;
    }

    for (int i = 0; i < rowCount; ++i)
    {
        QString param = ui->layerWidget->item(i, 1)->text();
        QRegularExpression re;
        QRegularExpressionMatch match;

        if (i == 0)
        {
            // 输入层：解析 "输入特征=%d"
            re.setPattern(("输入特征=(\\d+)"));
            match = re.match(param);
            if (match.hasMatch())
            {
                layerSizes << match.captured(1);
            }
            else
            {
                QMessageBox::critical(this, ("错误"), QString("输入层参数格式错误：%1").arg(param));
                layerSizesStr.clear();
                activationFunctionsStr.clear();
                return;
            }
        }
        else if (i == rowCount - 1)
        {
            // 输出层：解析 "输出特征=%d,活化=线性(无)"
            re.setPattern(("输出特征=(\\d+),活化=线性\\(无\\)"));
            match = re.match(param);
            if (match.hasMatch())
            {
                layerSizes << match.captured(1);
            }
            else
            {
                QMessageBox::critical(this, ("错误"), QString("输出层参数格式错误：%1").arg(param));
                layerSizesStr.clear();
                activationFunctionsStr.clear();
                return;
            }
        }
        else
        {
            // 隐藏层：解析 "输出特征=%d,活化=%s"
            re.setPattern(("输出特征=(\\d+),活化=(\\w+|线性\\(无\\))"));
            match = re.match(param);
            if (match.hasMatch())
            {
                layerSizes << match.captured(1);
                QString activation = match.captured(2);
                if (!activationMap.contains(activation))
                {
                    QMessageBox::critical(this, ("错误"),
                                          QString("无效的激活函数：%1").arg(activation));
                    layerSizesStr.clear();
                    activationFunctionsStr.clear();
                    return;
                }
                activationFunctions << activationMap[activation];
            }
            else
            {
                QMessageBox::critical(this, ("错误"), QString("隐藏层参数格式错误：%1").arg(param));
                layerSizesStr.clear();
                activationFunctionsStr.clear();
                return;
            }
        }
    }

    layerSizesStr = layerSizes.join(",");
    activationFunctionsStr = activationFunctions.join(",");
}

void DNNModel::on_startTrainModel_clicked()
{
    // 先划分数据集 数据集地址 目标位置地址 分数

    ui->verifyDataLineEdit->text().toDouble();
    QString currentPath = QDir::currentPath();

    // 调用训练脚本

    // 设置环境变量以禁用 Python 输出缓冲
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    m_trainModelProcess->setProcessEnvironment(env);

    // 设置工作目录和 Python 可执行文件路径
    m_trainModelProcess->setWorkingDirectory(QDir(currentPath).filePath("python309"));
    m_trainModelProcess->setProgram(QDir(currentPath).filePath("python309/python.exe"));

    // 准备参数
    QString trainDataFile = QDir(m_workPath).filePath("model/Corr_data_train.txt");    // 训练数据文件
    QString valDataFile = QDir(m_workPath).filePath("model/Corr_data_validation.txt"); // 验证数据文件
    QString onnxFile = QDir(m_workPath).filePath("model/model.onnx");                  // ONNX 输出文件
    QString pthFile = QDir(m_workPath).filePath("model/model.pth");                    // PyTorch 权重文件

    // 动态层数和激活函数
    QString layerSizes = "", activationFunctions = "";

    getLayerParameters(layerSizes, activationFunctions);
    qInfo() << "layerSizes " << layerSizes;
    qInfo() << "activationFunctions " << activationFunctions;

    // 设置命令行参数
    m_trainModelProcess->setArguments(
        QStringList() << QDir(currentPath).filePath("corrode/corr_MLP_1.py") << "--train_data_file"
                      << trainDataFile << "--val_data_file" << valDataFile << "--onxx_file"
                      << onnxFile << "--pth_file" << pthFile << "--optimizer_type"
                      << QString::number(ui->methodComboBox->currentIndex()) << "--learning_rate"
                      << ui->learningRateLineEdit->text() << "--layer_sizes" << layerSizes
                      << "--activation_functions" << activationFunctions << "--batch_size"
                      << ui->batchSizelineEdit->text() << "--criterion_type"
                      << QString::number(ui->lossFuctionComboBox->currentIndex()) << "--epochs"
                      << ui->epochsLineEdit->text() << "--train_type" << QString::number(0));

    // 调试：打印参数
    qInfo() << "命令行参数：" << m_trainModelProcess->arguments();

    // 启动进程
    m_trainModelProcess->start();
    if (!m_trainModelProcess->waitForStarted())
    {
        QMessageBox::critical(this, ("错误"), ("无法启动模型训练脚本。"));
        return;
    }

    //    while (!isFinished) {
    //        QCoreApplication::processEvents();  // 处理事件循环
    //    }
}

void DNNModel::connectOutput()
{
    // 捕获输出和错误
    connect(m_trainModelProcess, &QProcess::readyReadStandardOutput, this,
            [=]()
            {
                QString output = QString::fromUtf8(m_trainModelProcess->readAllStandardOutput());
                qInfo() << ("Python 输出：") << output;
                if (output.contains("finish training"))
                {
                    updateLineGraph();
                }
                m_outputService->outputLog(output);
            });

    connect(m_trainModelProcess, &QProcess::readyReadStandardError, this,
            [=]()
            {
                QString error = QString::fromUtf8(m_trainModelProcess->readAllStandardError());
                qInfo() << ("Python 错误：") << error;
                QMessageBox::warning(this, ("训练错误"), error);
            });

    connect(m_trainModelProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [=](int exitCode, QProcess::ExitStatus exitStatus)
            {
                if (exitCode == 0 && exitStatus == QProcess::NormalExit)
                {
                    //            QMessageBox::information(this, ("成功"),
                    //            ("模型训练完成"));
                }
                else
                {
                    QMessageBox::critical(this, ("错误"),
                                          QString("模型训练失败，退出码：%1").arg(exitCode));
                }
            });
}
void DNNModel::on_continueTrainModel_clicked()
{
    // 先划分数据集 数据集地址 目标位置地址 分数

    ui->verifyDataLineEdit->text().toDouble();
    QString currentPath = QDir::currentPath();

    // 调用训练脚本

    // 设置环境变量以禁用 Python 输出缓冲
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    m_trainModelProcess->setProcessEnvironment(env);

    // 设置工作目录和 Python 可执行文件路径
    m_trainModelProcess->setWorkingDirectory(currentPath + "/python309");
    m_trainModelProcess->setProgram(currentPath + "/python309/python.exe");

    // 准备参数
    QString trainDataFile = m_workPath + "/model/Corr_data_train.txt";    // 训练数据文件
    QString valDataFile = m_workPath + "/model/Corr_data_validation.txt"; // 验证数据文件
    QString onnxFile = m_workPath + "/model/model.onnx";                  // ONNX 输出文件
    QString pthFile = m_workPath + "/model/model.pth";                    // PyTorch 权重文件

    // 示例：动态层数和激活函数（可以从 UI 获取）
    QString layerSizes, activationFunctions;

    getLayerParameters(layerSizes, activationFunctions);
    qInfo() << "layerSizes " << layerSizes;
    qInfo() << "activationFunctions " << activationFunctions;

    // 设置命令行参数
    m_trainModelProcess->setArguments(
        QStringList() << QDir(currentPath).filePath("corrode/corr_MLP_1.py") << "--train_data_file"
                      << trainDataFile << "--val_data_file" << valDataFile << "--onxx_file"
                      << onnxFile << "--pth_file" << pthFile << "--optimizer_type"
                      << QString::number(ui->methodComboBox->currentIndex()) << "--learning_rate"
                      << ui->learningRateLineEdit->text() << "--layer_sizes" << layerSizes
                      << "--activation_functions" << activationFunctions << "--batch_size"
                      << ui->batchSizelineEdit->text() << "--criterion_type"
                      << QString::number(ui->lossFuctionComboBox->currentIndex()) << "--epochs"
                      << ui->epochsLineEdit->text() << "--train_type" << QString::number(1));

    // 调试：打印参数
    qInfo() << "命令行参数：" << m_trainModelProcess->arguments();

    // 启动进程
    m_trainModelProcess->start();
    if (!m_trainModelProcess->waitForStarted())
    {
        QMessageBox::critical(this, ("错误"), ("无法启动模型训练脚本。"));
        return;
    }
}

void DNNModel::setStyle()
{
    ui->sourceFileTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->sourceFileTableWidget->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);

    ui->layerWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->layerWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
}

void DNNModel::updateLineGraph()
{
    QString currentPath = QDir::currentPath();

    QString filePath = m_workPath + "/model/" + "Corr_data.txt";
    if (filePath.isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("请输入文件路径"));
        return;
    }

    // 打开文件
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, ("错误"), QString("无法打开文件：%1").arg(filePath));
        return;
    }

    // 读取文件，跳过前5行
    QTextStream in(&file);
    QStringList lines;
    int lineCount = 0;
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (lineCount >= 5 && !line.isEmpty() && !line.startsWith('%'))
        {
            lines.append(line);
        }
        lineCount++;
    }
    file.close();

    if (lines.isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("文件从第六行开始没有有效数据"));
        return;
    }

    // 解析数据
    QVector<double> times, temperatures, humidities, actualRates;
    for (const QString& line : lines)
    {
        QStringList values = line.split(",", Qt::SkipEmptyParts);
        if (values.size() != 4)
        {
            QMessageBox::warning(this, ("错误"), QString("数据格式错误，期望4列：%1").arg(line));
            return;
        }

        bool ok;
        double time = values[0].toDouble(&ok);
        if (!ok)
            continue;
        double humidity = values[1].toDouble(&ok);
        if (!ok)
            continue;
        double temperature = values[2].toDouble(&ok);
        if (!ok)
            continue;
        double rate = values[3].toDouble(&ok);
        if (!ok)
            continue;

        times.append(time);
        humidities.append(humidity);
        temperatures.append(temperature);
        actualRates.append(rate);
    }

    if (times.isEmpty())
    {
        QMessageBox::warning(this, ("错误"), ("没有有效的数据行"));
        return;
    }

    // 创建输入文件
    QDir workDir(m_workPath);
    QString inputFilePath = workDir.absoluteFilePath("model/predict_input.txt");
    QFile inputFile(inputFilePath);
    if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, ("错误"), QString("无法创建输入文件：%1").arg(inputFilePath));
        return;
    }

    // 写入输入数据（每行：温度 湿度）
    QTextStream out(&inputFile);
    for (int i = 0; i < temperatures.size(); ++i)
    {
        out << QString("%1 %2\n").arg(humidities[i]).arg(temperatures[i]);
    }
    inputFile.close();

    // 设置 QProcess
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    process.setProcessEnvironment(env);

    process.setWorkingDirectory(currentPath + "/python309");
    process.setProgram(currentPath + "/python309/python.exe");

    QString trainDataFile = workDir.absoluteFilePath("model/Corr_data_train.txt");
    QString onnxModelFile = workDir.absoluteFilePath("model/model.onnx");
    QString scriptPath = currentPath + "/corrode/load_model_predict.py";

    if (!QFileInfo(trainDataFile).exists())
    {
        QMessageBox::critical(this, ("错误"), QString("训练数据文件不存在：%1").arg(trainDataFile));
        return;
    }
    if (!QFileInfo(onnxModelFile).exists())
    {
        QMessageBox::critical(this, ("错误"), QString("模型文件不存在：%1").arg(onnxModelFile));
        return;
    }
    if (!QFileInfo(scriptPath).exists())
    {
        QMessageBox::critical(this, ("错误"), QString("脚本文件不存在：%1").arg(scriptPath));
        return;
    }

    process.setArguments(QStringList()
                         << scriptPath << trainDataFile << inputFilePath << onnxModelFile);

    // 捕获输出
    QStringList predictions;
    connect(&process, &QProcess::readyReadStandardOutput,
            [&]()
            {
                QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();

                QStringList lines = output.split("\n", Qt::SkipEmptyParts);
                for (QString& line : lines)
                {
                    line = line.trimmed(); // 移除 \r 和空格
                    if (line.isEmpty())
                        continue;

                    qInfo() << ("处理行:") << line;

                    if (line == "ok")
                    {
                        //                isFinished = true;
                        qInfo() << "预测结束";
                        continue;
                    }
                    predictions.append(line);
                }
            });

    connect(&process, &QProcess::readyReadStandardError,
            [&]()
            {
                QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
                if (!error.isEmpty())
                {
                    qInfo() << "Python 错误：" << error;
                }
            });

    // 启动进程
    process.start();
    if (!process.waitForStarted())
    {
        QMessageBox::critical(this, ("错误"), ("无法启动模型预测脚本。"));
        return;
    }

    // 等待进程完成
    if (!process.waitForFinished(-1))
    {
        QMessageBox::critical(this, ("错误"), ("预测脚本执行失败"));
        return;
    }

    qInfo() << ("执行完成");
    if (process.exitCode() != 0)
    {
        QMessageBox::critical(this, ("错误"),
                              QString("预测脚本执行失败，退出码：%1").arg(process.exitCode()));
        return;
    }

    // 验证预测结果数量
    if (predictions.size() != times.size())
    {
        QMessageBox::warning(this, ("错误"),
                             QString("预测结果数量（%1）与输入行数（%2）不匹配")
                                 .arg(predictions.size())
                                 .arg(times.size()));
        return;
    }

    // 转换预测结果
    QVector<double> predictedRates;
    for (const QString& pred : predictions)
    {
        bool ok;
        double value = pred.toDouble(&ok);
        if (ok)
        {
            predictedRates.append(value);
        }
        else
        {
            QMessageBox::warning(this, ("错误"), QString("无效的预测值：%1").arg(pred));
            return;
        }
    }
    // 绘制曲线
    actualSeries->setName(("计算值"));
    actualSeries->setPen(QPen(Qt::blue, 2));

    predictedSeries->setName(("预测值"));
    predictedSeries->setPen(QPen(Qt::red, 2));

    actualSeries->clear();
    predictedSeries->clear();

    // 填充数据
    double minTime = times[0];
    double maxTime = times[0];
    double minRate = qMin(actualRates[0], predictedRates[0]);
    double maxRate = qMax(actualRates[0], predictedRates[0]);

    for (int i = 0; i < times.size(); ++i)
    {
        actualSeries->append(times[i], actualRates[i]);
        predictedSeries->append(times[i], predictedRates[i]);

        // 更新 x 轴范围
        minTime = qMin(minTime, times[i]);
        maxTime = qMax(maxTime, times[i]);

        // 更新 y 轴范围
        minRate = qMin(minRate, qMin(actualRates[i], predictedRates[i]));
        maxRate = qMax(maxRate, qMax(actualRates[i], predictedRates[i]));
    }

    // 添加 5% 缓冲以美化显示
    double timeRange = maxTime - minTime;
    double rateRange = maxRate - minRate;
    double timePadding = timeRange * 0.05; // 5% 缓冲
    double ratePadding = rateRange * 0.05;

    // 避免范围为 0（单一数据点）

    if (qFuzzyIsNull(rateRange))
    {
        ratePadding = maxRate == 0 ? 0.000001 : maxRate * 0.05;
        minRate -= ratePadding;
        maxRate += ratePadding;
    }
    else
    {
        minRate -= ratePadding;
        maxRate += ratePadding;
    }

    // 设置轴范围
    axisX->setRange(minTime, maxTime);
    axisY->setRange(minRate, maxRate);

    //------
    //    visualWidgetService->setModelChart(chart);
}

bool DNNModel::writeToStream(QTextStream& out)
{
    // 写入字段
    out << ui->verifyDataLineEdit->text() << "_";
    out << ui->neuronLineEdit->text() << "_";
    out << ui->activationComboBox->currentIndex() << "_";
    out << ui->methodComboBox->currentIndex() << "_";
    out << ui->learningRateLineEdit->text() << "_";
    out << ui->batchSizelineEdit->text() << "_";
    out << ui->lossFuctionComboBox->currentIndex() << "_";
    out << ui->epochsLineEdit->text();

    // 写入 sourceFileTableWidget（3列）
    out << "|";
    int sourceRowCount = ui->sourceFileTableWidget->rowCount();
    for (int row = 0; row < sourceRowCount; ++row)
    {
        QStringList rowData;
        for (int col = 0; col < 3; ++col)
        {
            QTableWidgetItem* item = ui->sourceFileTableWidget->item(row, col);
            QString cellValue = item ? item->text() : "";
            cellValue.replace(",", "\\,");
            cellValue.replace(";", "\\;");
            cellValue.replace("|", "\\|"); // 转义新的分隔符
            rowData << cellValue;
        }
        out << rowData.join(",");
        if (row < sourceRowCount - 1)
        {
            out << ";";
        }
    }

    // 写入 layerWidget（2列）
    out << "|";
    int layerRowCount = ui->layerWidget->rowCount();
    for (int row = 0; row < layerRowCount; ++row)
    {
        QStringList rowData;
        for (int col = 0; col < 2; ++col)
        {
            QTableWidgetItem* item = ui->layerWidget->item(row, col);
            QString cellValue = item ? item->text() : "";
            cellValue.replace(",", "\\,");
            cellValue.replace(";", "\\;");
            cellValue.replace("|", "\\|"); // 转义新的分隔符
            rowData << cellValue;
        }
        out << rowData.join(",");
        if (row < layerRowCount - 1)
        {
            out << ";";
        }
    }
    return true;
}

bool DNNModel::readFromStream(QTextStream& in)
{
    QString line = in.readLine();
    QStringList sections = line.split("|", Qt::SkipEmptyParts);
    // 检查字段部分是否存在
    if (sections.isEmpty())
    {
        qInfo() << "错误：DNNModel 数据为空";
        return false;
    }

    // 解析字段
    QStringList values = sections[0].split("_");
    if (values.size() < 8)
    {
        qInfo() << "错误：字段数量不足，预期8个，实际" << values.size();
        return false;
    }

    ui->verifyDataLineEdit->setText(values[0]);
    ui->neuronLineEdit->setText(values[1]);
    ui->activationComboBox->setCurrentIndex(values[2].toInt());
    ui->methodComboBox->setCurrentIndex(values[3].toInt());
    ui->learningRateLineEdit->setText(values[4]);
    ui->batchSizelineEdit->setText(values[5]);
    ui->lossFuctionComboBox->setCurrentIndex(values[6].toInt());
    ui->epochsLineEdit->setText(values[7]);

    // 解析 sourceFileTableWidget（3列）
    ui->sourceFileTableWidget->setRowCount(0);
    QString sourceFileData = sections.size() > 1 ? sections[1] : "";
    if (!sourceFileData.isEmpty())
    {
        QStringList sourceRows = sourceFileData.split(";", Qt::KeepEmptyParts);
        ui->sourceFileTableWidget->setRowCount(sourceRows.size());
        ui->sourceFileTableWidget->setColumnCount(3);

        int row = 0;
        for (const QString& rowData : sourceRows)
        {
            if (rowData.isEmpty() && row < sourceRows.size() - 1)
            {
                continue; // 跳过非最后一行的空数据
            }
            QStringList cells;
            QString cell;
            bool escaped = false;
            for (const QChar& c : rowData)
            {
                if (escaped)
                {
                    cell += c;
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == ',' && !escaped)
                {
                    cells << cell;
                    cell.clear();
                }
                else
                {
                    cell += c;
                }
            }
            cells << cell;
            if (cells.size() != 3 && !rowData.isEmpty())
            {
                qInfo() << "错误：sourceFileTableWidget 第" << row + 1
                         << "行列数不符，预期3列，实际" << cells.size();
                return false;
            }
            if (!rowData.isEmpty())
            {
                for (int col = 0; col < 3; ++col)
                {
                    QTableWidgetItem* item = new QTableWidgetItem(cells[col]);
                    ui->sourceFileTableWidget->setItem(row, col, item);
                }
                ++row;
            }
        }
        ui->sourceFileTableWidget->setRowCount(row); // 调整实际行数
    }

    // 解析 layerWidget（2列）
    ui->layerWidget->setRowCount(0);
    QString layerData = sections.size() > 2 ? sections[2] : "";
    if (!layerData.isEmpty())
    {
        QStringList layerRows = layerData.split(";", Qt::KeepEmptyParts);
        ui->layerWidget->setRowCount(layerRows.size());
        ui->layerWidget->setColumnCount(2);

        int row = 0;
        for (const QString& rowData : layerRows)
        {
            if (rowData.isEmpty() && row < layerRows.size() - 1)
            {
                continue; // 跳过非最后一行的空数据
            }
            QStringList cells;
            QString cell;
            bool escaped = false;
            for (const QChar& c : rowData)
            {
                if (escaped)
                {
                    cell += c;
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == ',' && !escaped)
                {
                    cells << cell;
                    cell.clear();
                }
                else
                {
                    cell += c;
                }
            }
            cells << cell;
            if (cells.size() != 2 && !rowData.isEmpty())
            {
                qInfo() << "错误：layerWidget 第" << row + 1 << "行列数不符，预期2列，实际"
                         << cells.size();
                return false;
            }
            if (!rowData.isEmpty())
            {
                for (int col = 0; col < 2; ++col)
                {
                    QTableWidgetItem* item = new QTableWidgetItem(cells[col]);
                    ui->layerWidget->setItem(row, col, item);
                }
                ++row;
            }
        }
        ui->layerWidget->setRowCount(row); // 调整实际行数
    }

    return true;
}
