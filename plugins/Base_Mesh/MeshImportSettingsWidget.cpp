#include "MeshImportSettingsWidget.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MeshImportSettingsWidget::MeshImportSettingsWidget(QWidget* parent)
    : BasePage("MeshImportSettings"), m_comboVerticalAxis(new QComboBox(this)),
      m_labelSourceFile(new QLabel(QStringLiteral("未导入网格"), this)),
      m_buttonImport(new QPushButton(QStringLiteral("选择网格文件"), this))
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("网格导入与计算轴向"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    rootLayout->addWidget(title);

    auto* hint = new QLabel(QStringLiteral("请选择网格文件，并设置 IIIN.DAT 中用于计算的竖向轴。"), this);
    hint->setWordWrap(true);
    rootLayout->addWidget(hint);

    auto* fileGroup = new QGroupBox(QStringLiteral("网格文件"), this);
    auto* fileLayout = new QHBoxLayout(fileGroup);
    m_labelSourceFile->setWordWrap(true);
    fileLayout->addWidget(m_labelSourceFile, 1);
    fileLayout->addWidget(m_buttonImport);
    rootLayout->addWidget(fileGroup);

    auto* axisGroup = new QGroupBox(QStringLiteral("轴向设置"), this);
    auto* axisLayout = new QFormLayout(axisGroup);
    m_comboVerticalAxis->addItem(QStringLiteral("X 轴"), 1);
    m_comboVerticalAxis->addItem(QStringLiteral("Y 轴"), 2);
    m_comboVerticalAxis->addItem(QStringLiteral("Z 轴"), 3);
    m_comboVerticalAxis->setCurrentIndex(1);
    axisLayout->addRow(QStringLiteral("竖向轴"), m_comboVerticalAxis);
    rootLayout->addWidget(axisGroup);

    rootLayout->addStretch(1);

    connect(m_buttonImport, &QPushButton::clicked, this, &MeshImportSettingsWidget::importRequested);
    connect(m_comboVerticalAxis,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int)
            {
                emit verticalAxisChanged(verticalAxis());
            });
}

int MeshImportSettingsWidget::verticalAxis() const
{
    return m_comboVerticalAxis->currentData().toInt();
}

void MeshImportSettingsWidget::setVerticalAxis(int axis)
{
    const int idx = m_comboVerticalAxis->findData(axis);
    if (idx >= 0)
        m_comboVerticalAxis->setCurrentIndex(idx);
}

void MeshImportSettingsWidget::setSourceFileName(const QString& fileName)
{
    if (fileName.trimmed().isEmpty())
    {
        m_labelSourceFile->setText(QStringLiteral("未导入网格"));
        return;
    }

    const QFileInfo info(fileName);
    m_labelSourceFile->setText(QStringLiteral("当前网格：%1").arg(info.fileName()));
    m_labelSourceFile->setToolTip(fileName);
}

void MeshImportSettingsWidget::beforeLoad()
{
}

void MeshImportSettingsWidget::beforeUnload()
{
}
