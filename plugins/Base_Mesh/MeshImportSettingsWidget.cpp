#include "MeshImportSettingsWidget.h"

#include <QFileInfo>

#include "ui_MeshImportSettingsWidget.h"

MeshImportSettingsWidget::MeshImportSettingsWidget(QWidget* parent)
    : BasePage("MeshImportSettings"), ui(new Ui::MeshImportSettingsWidget)
{
    ui->setupUi(this);

    ui->comboVerticalAxis->setItemData(0, 1);
    ui->comboVerticalAxis->setItemData(1, 2);
    ui->comboVerticalAxis->setItemData(2, 3);
    ui->comboVerticalAxis->setCurrentIndex(1);

    connect(ui->buttonImport, &QPushButton::clicked, this,
            &MeshImportSettingsWidget::importRequested);
    connect(ui->comboVerticalAxis,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int)
            {
                emit verticalAxisChanged(verticalAxis());
            });
}

MeshImportSettingsWidget::~MeshImportSettingsWidget()
{
    delete ui;
}

int MeshImportSettingsWidget::verticalAxis() const
{
    return ui->comboVerticalAxis->currentData().toInt();
}

void MeshImportSettingsWidget::setVerticalAxis(int axis)
{
    const int idx = ui->comboVerticalAxis->findData(axis);
    if (idx >= 0)
        ui->comboVerticalAxis->setCurrentIndex(idx);
}

void MeshImportSettingsWidget::setSourceFileName(const QString& fileName)
{
    if (fileName.trimmed().isEmpty())
    {
        ui->labelSourceFile->setText(QStringLiteral("未导入网格"));
        ui->labelSourceFile->setToolTip(QString());
        return;
    }

    const QFileInfo info(fileName);
    ui->labelSourceFile->setText(QStringLiteral("当前网格：%1").arg(info.fileName()));
    ui->labelSourceFile->setToolTip(fileName);
}

void MeshImportSettingsWidget::beforeLoad()
{
}

void MeshImportSettingsWidget::beforeUnload()
{
}
