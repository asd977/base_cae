#include "ResultRuleConfigDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
QString cellText(QTableWidget* table, int row, int col)
{
    if (!table)
        return {};

    auto* item = table->item(row, col);
    return item ? item->text().trimmed() : QString();
}

void setCellText(QTableWidget* table, int row, int col, const QString& text)
{
    auto* item = table->item(row, col);
    if (!item)
    {
        item = new QTableWidgetItem;
        table->setItem(row, col, item);
    }

    item->setText(text);
}

bool editDirectoryRule(ResultDirectoryRule& rule, QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(rule.dirName.isEmpty()
                           ? QStringLiteral("添加结果目录规则")
                           : QStringLiteral("编辑结果目录规则"));

    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;

    auto* displayEdit = new QLineEdit(rule.displayName, &dlg);
    auto* dirEdit = new QLineEdit(rule.dirName, &dlg);
    auto* descEdit = new QLineEdit(rule.description, &dlg);

    form->addRow(QStringLiteral("显示名"), displayEdit);
    form->addRow(QStringLiteral("文件夹名/相对路径"), dirEdit);
    form->addRow(QStringLiteral("说明"), descEdit);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    rule.displayName = displayEdit->text().trimmed();
    rule.dirName = dirEdit->text().trimmed();
    rule.description = descEdit->text().trimmed();

    if (rule.dirName.isEmpty())
    {
        QMessageBox::warning(parent, QStringLiteral("结果目录规则"), QStringLiteral("文件夹名不能为空。"));
        return false;
    }

    if (rule.displayName.isEmpty())
        rule.displayName = rule.dirName;

    return true;
}

bool editFileRule(ResultFileRule& rule, QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(rule.fileName.isEmpty()
                           ? QStringLiteral("添加结果文件规则")
                           : QStringLiteral("编辑结果文件规则"));

    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;

    auto* displayEdit = new QLineEdit(rule.displayName, &dlg);
    auto* fileEdit = new QLineEdit(rule.fileName, &dlg);
    auto* formatBox = new QComboBox(&dlg);
    auto* descEdit = new QLineEdit(rule.description, &dlg);

    formatBox->addItem(QStringLiteral("Tecplot"), QStringLiteral("tecplot"));
    formatBox->addItem(QStringLiteral("VTU"), QStringLiteral("vtu"));

    const int formatIndex = formatBox->findData(rule.format.toLower());
    if (formatIndex >= 0)
        formatBox->setCurrentIndex(formatIndex);

    form->addRow(QStringLiteral("显示名"), displayEdit);
    form->addRow(QStringLiteral("文件名"), fileEdit);
    form->addRow(QStringLiteral("格式"), formatBox);
    form->addRow(QStringLiteral("说明"), descEdit);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    rule.displayName = displayEdit->text().trimmed();
    rule.fileName = fileEdit->text().trimmed();
    rule.format = formatBox->currentData().toString();
    rule.description = descEdit->text().trimmed();

    if (rule.fileName.isEmpty())
    {
        QMessageBox::warning(parent, QStringLiteral("结果文件规则"), QStringLiteral("文件名不能为空。"));
        return false;
    }

    if (rule.displayName.isEmpty())
        rule.displayName = rule.fileName;

    return true;
}
}

ResultRuleConfigDialog::ResultRuleConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("后处理结果规则配置"));
    resize(760, 520);

    buildUi();
    loadFromManager();
}

void ResultRuleConfigDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    auto* dirPage = new QWidget(tabs);
    auto* dirLayout = new QVBoxLayout(dirPage);

    m_dirTable = new QTableWidget(dirPage);
    m_dirTable->setColumnCount(3);
    m_dirTable->setHorizontalHeaderLabels(
        {QStringLiteral("显示名"), QStringLiteral("文件夹名/相对路径"), QStringLiteral("说明")});
    m_dirTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dirTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dirTable->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* dirButtonRow = new QHBoxLayout;
    auto* addDirBtn = new QPushButton(QStringLiteral("添加"), dirPage);
    auto* removeDirBtn = new QPushButton(QStringLiteral("删除"), dirPage);
    dirButtonRow->addWidget(addDirBtn);
    dirButtonRow->addWidget(removeDirBtn);
    dirButtonRow->addStretch();

    dirLayout->addWidget(m_dirTable);
    dirLayout->addLayout(dirButtonRow);

    tabs->addTab(dirPage, QStringLiteral("结果目录"));

    auto* filePage = new QWidget(tabs);
    auto* fileLayout = new QVBoxLayout(filePage);

    m_fileTable = new QTableWidget(filePage);
    m_fileTable->setColumnCount(4);
    m_fileTable->setHorizontalHeaderLabels(
        {QStringLiteral("显示名"), QStringLiteral("文件名"), QStringLiteral("格式"), QStringLiteral("说明")});
    m_fileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTable->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* fileButtonRow = new QHBoxLayout;
    auto* addFileBtn = new QPushButton(QStringLiteral("添加"), filePage);
    auto* removeFileBtn = new QPushButton(QStringLiteral("删除"), filePage);
    fileButtonRow->addWidget(addFileBtn);
    fileButtonRow->addWidget(removeFileBtn);
    fileButtonRow->addStretch();

    fileLayout->addWidget(m_fileTable);
    fileLayout->addLayout(fileButtonRow);

    tabs->addTab(filePage, QStringLiteral("结果文件"));

    root->addWidget(tabs);

    auto* bottomRow = new QHBoxLayout;
    auto* resetBtn = new QPushButton(QStringLiteral("恢复默认"), this);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);

    bottomRow->addWidget(resetBtn);
    bottomRow->addStretch();
    bottomRow->addWidget(saveBtn);
    bottomRow->addWidget(closeBtn);

    root->addLayout(bottomRow);

    connect(addDirBtn, &QPushButton::clicked, this, &ResultRuleConfigDialog::addDirectoryRule);
    connect(removeDirBtn, &QPushButton::clicked, this, &ResultRuleConfigDialog::removeDirectoryRule);
    connect(addFileBtn, &QPushButton::clicked, this, &ResultRuleConfigDialog::addFileRule);
    connect(removeFileBtn, &QPushButton::clicked, this, &ResultRuleConfigDialog::removeFileRule);

    connect(saveBtn, &QPushButton::clicked, this,
            [this]()
            {
                saveToManager();
                accept();
            });

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(resetBtn, &QPushButton::clicked, this,
            [this]()
            {
                const int ret = QMessageBox::question(
                    this,
                    QStringLiteral("恢复默认"),
                    QStringLiteral("确定恢复默认结果目录和文件规则吗？"));

                if (ret != QMessageBox::Yes)
                    return;

                ResultRuleManager::instance().resetToDefault();
                loadFromManager();
            });
}

void ResultRuleConfigDialog::loadFromManager()
{
    ResultRuleManager::instance().ensureLoaded();

    const QVector<ResultDirectoryRule> dirs = ResultRuleManager::instance().directoryRules();
    m_dirTable->setRowCount(0);

    for (const ResultDirectoryRule& rule : dirs)
    {
        const int row = m_dirTable->rowCount();
        m_dirTable->insertRow(row);

        setCellText(m_dirTable, row, 0, rule.displayName);
        setCellText(m_dirTable, row, 1, rule.dirName);
        setCellText(m_dirTable, row, 2, rule.description);
    }

    const QVector<ResultFileRule> files = ResultRuleManager::instance().fileRules();
    m_fileTable->setRowCount(0);

    for (const ResultFileRule& rule : files)
    {
        const int row = m_fileTable->rowCount();
        m_fileTable->insertRow(row);

        setCellText(m_fileTable, row, 0, rule.displayName);
        setCellText(m_fileTable, row, 1, rule.fileName);
        setCellText(m_fileTable, row, 2, rule.format);
        setCellText(m_fileTable, row, 3, rule.description);
    }
}

void ResultRuleConfigDialog::saveToManager()
{
    ResultRuleManager::instance().setDirectoryRules(collectDirectoryRules());
    ResultRuleManager::instance().setFileRules(collectFileRules());
    ResultRuleManager::instance().save();
}

void ResultRuleConfigDialog::addDirectoryRule()
{
    ResultDirectoryRule rule;

    if (!editDirectoryRule(rule, this))
        return;

    const int row = m_dirTable->rowCount();
    m_dirTable->insertRow(row);

    setCellText(m_dirTable, row, 0, rule.displayName);
    setCellText(m_dirTable, row, 1, rule.dirName);
    setCellText(m_dirTable, row, 2, rule.description);

    m_dirTable->selectRow(row);
}

void ResultRuleConfigDialog::removeDirectoryRule()
{
    const int row = m_dirTable->currentRow();

    if (row >= 0)
        m_dirTable->removeRow(row);
}

void ResultRuleConfigDialog::addFileRule()
{
    ResultFileRule rule;
    rule.format = QStringLiteral("tecplot");

    if (!editFileRule(rule, this))
        return;

    const int row = m_fileTable->rowCount();
    m_fileTable->insertRow(row);

    setCellText(m_fileTable, row, 0, rule.displayName);
    setCellText(m_fileTable, row, 1, rule.fileName);
    setCellText(m_fileTable, row, 2, rule.format);
    setCellText(m_fileTable, row, 3, rule.description);

    m_fileTable->selectRow(row);
}

void ResultRuleConfigDialog::removeFileRule()
{
    const int row = m_fileTable->currentRow();

    if (row >= 0)
        m_fileTable->removeRow(row);
}

QVector<ResultDirectoryRule> ResultRuleConfigDialog::collectDirectoryRules() const
{
    QVector<ResultDirectoryRule> rules;

    for (int row = 0; row < m_dirTable->rowCount(); ++row)
    {
        ResultDirectoryRule rule;
        rule.displayName = cellText(m_dirTable, row, 0);
        rule.dirName = cellText(m_dirTable, row, 1);
        rule.description = cellText(m_dirTable, row, 2);

        if (rule.dirName.isEmpty())
            continue;

        if (rule.displayName.isEmpty())
            rule.displayName = rule.dirName;

        rules.push_back(rule);
    }

    return rules;
}

QVector<ResultFileRule> ResultRuleConfigDialog::collectFileRules() const
{
    QVector<ResultFileRule> rules;

    for (int row = 0; row < m_fileTable->rowCount(); ++row)
    {
        ResultFileRule rule;
        rule.displayName = cellText(m_fileTable, row, 0);
        rule.fileName = cellText(m_fileTable, row, 1);
        rule.format = cellText(m_fileTable, row, 2).toLower();
        rule.description = cellText(m_fileTable, row, 3);

        if (rule.fileName.isEmpty())
            continue;

        if (rule.displayName.isEmpty())
            rule.displayName = rule.fileName;

        if (rule.format.isEmpty())
            rule.format = rule.fileName.endsWith(QStringLiteral(".vtu"), Qt::CaseInsensitive)
                              ? QStringLiteral("vtu")
                              : QStringLiteral("tecplot");

        rules.push_back(rule);
    }

    return rules;
}
