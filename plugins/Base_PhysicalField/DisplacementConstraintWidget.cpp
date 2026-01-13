#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QList>
#include <algorithm>

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget *parent)
    : BasePage("DisplacementConstraintWidget")
    , ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
    connectSignals();
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
    delete ui;
}

void DisplacementConstraintWidget::beforeLoad()
{
    refreshMeshInfo();
    applyConstraintVisibility();
}

void DisplacementConstraintWidget::beforeUnload()
{
    applyConstraintVisibility();
}

void DisplacementConstraintWidget::connectSignals()
{
    connect(ui->showUxCheck, &QCheckBox::toggled, this, &DisplacementConstraintWidget::applyConstraintVisibility);
    connect(ui->showUyCheck, &QCheckBox::toggled, this, &DisplacementConstraintWidget::applyConstraintVisibility);
    connect(ui->showUzCheck, &QCheckBox::toggled, this, &DisplacementConstraintWidget::applyConstraintVisibility);

    connect(ui->applyNodeButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), QStringLiteral("网格服务不可用"));
            return;
        }
        QString err;
        if (!service->setNodeConstraint(ui->nodeIdSpin->value(), currentDofValues(), &err)) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), err.isEmpty() ? QStringLiteral("节点约束设置失败") : err);
            return;
        }
    });

    connect(ui->applyPlaneButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), QStringLiteral("网格服务不可用"));
            return;
        }
        if (!service->setConstraintsForPlane(ui->aSpin->value(),
                                             ui->bSpin->value(),
                                             ui->cSpin->value(),
                                             ui->dSpin->value(),
                                             ui->tolSpin->value(),
                                             currentDofValues())) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), QStringLiteral("方程约束设置失败"));
            return;
        }
    });

    connect(ui->applyMatypeButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), QStringLiteral("网格服务不可用"));
            return;
        }
        const int matype = ui->matypeCombo->currentData().toInt();
        if (!service->setConstraintsForMatype(matype, currentDofValues())) {
            QMessageBox::warning(this, QStringLiteral("约束设置"), QStringLiteral("材料集约束设置失败"));
            return;
        }
    });

    connect(ui->loadConstraintButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("约束加载"), QStringLiteral("网格服务不可用"));
            return;
        }
        const QString fileName = QFileDialog::getOpenFileName(this,
                                                              QStringLiteral("加载 IIID.DAT"),
                                                              defaultConstraintFilePath(QStringLiteral("IIID.dat")),
                                                              QStringLiteral("Constraint File (*.dat *.DAT)"));
        if (fileName.isEmpty()) {
            return;
        }
        QString err;
        if (!service->loadConstraintFile(fileName, &err)) {
            QMessageBox::warning(this, QStringLiteral("约束加载"), err.isEmpty() ? QStringLiteral("约束文件加载失败") : err);
            return;
        }
    });

    connect(ui->saveConstraintButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("约束保存"), QStringLiteral("网格服务不可用"));
            return;
        }
        const QString fileName = QFileDialog::getSaveFileName(this,
                                                              QStringLiteral("保存 IIID.DAT"),
                                                              defaultConstraintFilePath(QStringLiteral("IIID.dat")),
                                                              QStringLiteral("Constraint File (*.dat *.DAT)"));
        if (fileName.isEmpty()) {
            return;
        }
        QString err;
        if (!service->saveConstraintFile(fileName, &err)) {
            QMessageBox::warning(this, QStringLiteral("约束保存"), err.isEmpty() ? QStringLiteral("约束文件保存失败") : err);
            return;
        }
    });

    connect(ui->generateTestButton, &QPushButton::clicked, this, [this]() {
        auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        if (!service) {
            QMessageBox::warning(this, QStringLiteral("生成约束"), QStringLiteral("网格服务不可用"));
            return;
        }
        const QString fileName = defaultConstraintFilePath(QStringLiteral("IIID.dat"));
        QString err;
        if (!service->generateDefaultConstraintFile(fileName, &err)) {
            QMessageBox::warning(this, QStringLiteral("生成约束"), err.isEmpty() ? QStringLiteral("生成测试约束文件失败") : err);
            return;
        }
        QMessageBox::information(this,
                                 QStringLiteral("生成约束"),
                                 QStringLiteral("已生成测试约束文件: %1").arg(fileName));
    });
}

void DisplacementConstraintWidget::refreshMeshInfo()
{
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        ui->meshFileLabel->setText(QStringLiteral("网格文件: 未加载"));
        return;
    }
    QString err;
    service->ensureConstraintModel(&err);
    const QString modelFile = service->currentModelFileName();
    const QString fileName = modelFile.isEmpty() ? QStringLiteral("未命名 IIIN.DAT") : modelFile;
    ui->meshFileLabel->setText(QStringLiteral("网格文件: %1").arg(fileName));
    updateMatypeList();
}

void DisplacementConstraintWidget::updateMatypeList()
{
    ui->matypeCombo->clear();
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        return;
    }
    const QSet<int> matypes = service->getAvailableMatypes();
    QList<int> values = matypes.values();
    std::sort(values.begin(), values.end());
    for (int matype : values) {
        ui->matypeCombo->addItem(QString::number(matype), matype);
    }
    if (ui->matypeCombo->count() == 0) {
        ui->matypeCombo->addItem(QStringLiteral("无材料集"), -1);
    }
}

QVector<int> DisplacementConstraintWidget::currentDofValues() const
{
    return {ui->uxSpin->value(), ui->uySpin->value(), ui->uzSpin->value()};
}

QString DisplacementConstraintWidget::defaultConstraintFilePath(const QString &fallbackName) const
{
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        return fallbackName;
    }
    const QString modelFile = service->currentModelFileName();
    if (modelFile.isEmpty()) {
        return fallbackName;
    }
    const QFileInfo info(modelFile);
    return info.dir().filePath(QStringLiteral("IIID.DAT"));
}

void DisplacementConstraintWidget::applyConstraintVisibility()
{
    auto *service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!service) {
        return;
    }
    service->setConstraintVisibility(ui->showUxCheck->isChecked(),
                                     ui->showUyCheck->isChecked(),
                                     ui->showUzCheck->isChecked());
}
