#include "SlopeStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QJsonArray>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

SlopeStabilityWidget::SlopeStabilityWidget(QWidget* parent)
    : BasePage("SlopeStabilityAnalysis")
{
    initDefaultItems();

    auto* layout = new QVBoxLayout(this);

    m_summaryLabel = new QLabel(
        "请在下表设置 ISLOP.DAT 的键值参数（共 20 行），点击“导出计算文件”生成。", this);
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList() << "行号" << "键" << "值" << "说明");
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    refreshTable();
    layout->addWidget(m_table);

    m_computeButton = new QPushButton("导出计算文件", this);
    layout->addWidget(m_computeButton);
    layout->addStretch();

    connect(m_computeButton, &QPushButton::clicked, this, &SlopeStabilityWidget::computeRequested);
}

void SlopeStabilityWidget::beforeLoad()
{
}

void SlopeStabilityWidget::beforeUnload()
{
}

bool SlopeStabilityWidget::exportIslopDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("ISLOP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QVector<QString> values;
    values.reserve(m_items.size());
    for (int row = 0; row < m_items.size(); ++row)
    {
        const QTableWidgetItem* item = m_table->item(row, 2);
        values.push_back(item ? item->text() : m_items[row].value);
    }

    QTextStream os(&file);
    for (const auto& value : values)
        os << "'" << value << "'\n";

    return true;
}

QJsonObject SlopeStabilityWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray values;
    for (int row = 0; row < m_items.size(); ++row)
    {
        const QTableWidgetItem* item = m_table->item(row, 2);
        values.append(item ? item->text() : m_items[row].value);
    }
    obj["values"] = values;
    return obj;
}

void SlopeStabilityWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray values = obj.value("values").toArray();
    for (int i = 0; i < m_items.size(); ++i)
    {
        if (i < values.size() && values.at(i).isString())
            m_items[i].value = values.at(i).toString(m_items[i].value);
    }
    refreshTable();
}

void SlopeStabilityWidget::initDefaultItems()
{
    m_items = {
        {1, "计算默认文件目录", "F:\\FEMCLASS\\Z08_SLOP3D\\PREP\\", "计算默认文件目录"},
        {2, "强度折减材料集文件", "MACB.DAT", "进行强度折减的材料集文件"},
        {3, "求解方法", "3", "0增量法；1降C；2降F；3同时降C&F；4同时降E/C/F"},
        {4, "重力加速度", "0.0   -9.81   0.0", "三方向重力加速度"},
        {5, "弹塑性迭代方法", "1", "1初应变法；2初应力法；3变刚度法"},
        {6, "最大迭代次数", "500", "最大迭代次数"},
        {7, "迭代误差阈值", "0.0001", "迭代误差阈值"},
        {8, "施加荷载方式", "1", "1仅重力；2按 IIWL.DAT"},
        {9, "荷载文件名", "IIWL.DAT", "按荷载文件施加时使用"},
        {10, "备用10", " -------------- ", "备用，暂无定义"},
        {11, "备用11", " -------------- ", "备用，暂无定义"},
        {12, "特征节点集文件", "JDTRACE.DAT", "特征节点集文件"},
        {13, "特征单元集文件", "KETRACE.DAT", "特征单元集文件"},
        {14, "特征时刻文件", "TDTRACE.DAT", "特征时刻文件"},
        {15, "阻尼计算方式", "0", "0系统阻尼；1分材料阻尼"},
        {16, "瑞雷阻尼开关", "0", "0非瑞雷；1瑞雷"},
        {17, "阻尼参数1", "0.05", "非瑞雷阻尼比或瑞雷质量阻尼 fm"},
        {18, "阻尼参数2", "0.0", "瑞雷刚度阻尼 fk"},
        {19, "计算类型", "1", "1静力；2动力；3静力+动力"},
        {20, "备用20", "------------", "备用，暂未定义"},
    };
}

void SlopeStabilityWidget::refreshTable()
{
    if (!m_table)
        return;

    m_table->setRowCount(m_items.size());
    for (int row = 0; row < m_items.size(); ++row)
    {
        const DatItem& d = m_items[row];
        auto* lineItem = new QTableWidgetItem(QString::number(d.lineNo));
        lineItem->setFlags(lineItem->flags() & ~Qt::ItemIsEditable);
        auto* keyItem = new QTableWidgetItem(d.key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        auto* valueItem = new QTableWidgetItem(d.value);
        auto* descItem = new QTableWidgetItem(d.description);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);

        m_table->setItem(row, 0, lineItem);
        m_table->setItem(row, 1, keyItem);
        m_table->setItem(row, 2, valueItem);
        m_table->setItem(row, 3, descItem);
    }
}
