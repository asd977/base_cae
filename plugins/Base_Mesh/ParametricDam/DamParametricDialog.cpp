#include "DamParametricDialog.h"

#include "DamParametricCalculator.h"
#include "DamParametricRunner.h"
#include "MsgJzDamIO.h"
#include "ui_DamParametricDialog.h"

#include <QDir>
#include <QMessageBox>

DamParametricDialog::DamParametricDialog(const DamParametricContext& ctx,
                                         const MsgJzDamParams& params, QWidget* parent)
    : QDialog(parent), ui(new Ui::DamParametricDialog), m_ctx(ctx), m_params(params)
{
    ui->setupUi(this);
    setWindowTitle(damDialogTitle(m_ctx.iobject, m_ctx.ndimModel));

    if (FillPrepIO::ensureExists(m_ctx.projectDir))
        FillPrepIO::read(m_ctx.projectDir, m_fillPrep);

    applyContextToUi();
    applyParamsToUi();
    applyFillPrepToUi();
    updateObjectLabels();

    connect(ui->btnConvert, &QPushButton::clicked, this, &DamParametricDialog::onConvertClicked);
    connect(ui->btnGenerate, &QPushButton::clicked, this, &DamParametricDialog::onGenerateClicked);
    connect(ui->btnCancel, &QPushButton::clicked, this, &DamParametricDialog::reject);
}

DamParametricDialog::~DamParametricDialog()
{
    delete ui;
}

void DamParametricDialog::applyContextToUi()
{
    ui->editSolverRoot->setText(m_ctx.solverRoot);
    ui->spinDim->setValue(m_ctx.ndimModel);
    ui->labelDamType->setText(damObjectName(m_ctx.iobject));
}

void DamParametricDialog::applyParamsToUi()
{
    ui->editP003->setText(m_params.P(3));
    ui->editP004->setText(m_params.P(4));
    ui->editP005->setText(m_params.P(5));
    ui->editP006->setText(m_params.P(6));
    ui->editP009->setText(m_params.P(9));
    ui->editP010->setText(m_params.P(10));
    ui->editP011->setText(m_params.P(11));
    ui->editP012->setText(m_params.P(12));
    ui->editP013->setText(m_params.P(13));
    ui->editP015->setText(m_params.P(15));
    ui->editP016->setText(m_params.P(16));
    ui->editP025->setText(m_params.P(25));
    ui->editP027->setText(m_params.P(27));
    ui->editP029->setText(m_params.P(29));
    ui->editP031->setText(m_params.P(31));
    ui->editP034->setText(m_params.P(34));

    ui->editP001->setText(m_params.P(1));
    ui->editP002->setText(m_params.P(2));
    ui->editP007->setText(m_params.P(7));
    ui->editP008->setText(m_params.P(8));
    ui->editP019->setText(m_params.P(19));
    ui->editP020->setText(m_params.P(20));
    ui->editP021->setText(m_params.P(21));
    ui->editP022->setText(m_params.P(22));
    ui->editP023->setText(m_params.P(23));
}

bool DamParametricDialog::collectParamsFromUi(QString* error)
{
    m_ctx.solverRoot = ui->editSolverRoot->text().trimmed();
    m_ctx.ndimModel = ui->spinDim->value();
    if (m_ctx.solverRoot.isEmpty())
    {
        if (error)
            *error = QString("求解器根目录不能为空");
        return false;
    }

    m_params.P(3) = ui->editP003->text().trimmed();
    m_params.P(4) = ui->editP004->text().trimmed();
    m_params.P(5) = ui->editP005->text().trimmed();
    m_params.P(6) = ui->editP006->text().trimmed();
    m_params.P(9) = ui->editP009->text().trimmed();
    m_params.P(10) = ui->editP010->text().trimmed();
    m_params.P(11) = ui->editP011->text().trimmed();
    m_params.P(12) = ui->editP012->text().trimmed();
    m_params.P(13) = ui->editP013->text().trimmed();
    m_params.P(15) = ui->editP015->text().trimmed();
    m_params.P(16) = ui->editP016->text().trimmed();
    m_params.P(25) = ui->editP025->text().trimmed();
    m_params.P(27) = ui->editP027->text().trimmed();
    m_params.P(29) = ui->editP029->text().trimmed();
    m_params.P(31) = ui->editP031->text().trimmed();
    m_params.P(34) = ui->editP034->text().trimmed();
    m_params.P(35) = QString::number(m_ctx.ndimModel);

    applyDamObjectLabels(m_ctx.iobject, m_params);
    return true;
}

void DamParametricDialog::applyFillPrepToUi()
{
    ui->editXmid->setText(QString::number(m_fillPrep.xmid, 'f', 4));
    ui->editWidth->setText(QString::number(m_fillPrep.width, 'f', 4));
    ui->editYa->setText(QString::number(m_fillPrep.ya, 'f', 4));
    ui->editYb->setText(QString::number(m_fillPrep.yb, 'f', 4));
    ui->editTol2->setText(QString::number(m_fillPrep.tol2, 'f', 4));
}

bool DamParametricDialog::collectFillPrepFromUi(QString* error)
{
    bool ok = false;
    m_fillPrep.xmid = ui->editXmid->text().trimmed().toDouble(&ok);
    if (!ok)
    {
        if (error)
            *error = QString("XMID 不是合法数字");
        return false;
    }

    m_fillPrep.width = ui->editWidth->text().trimmed().toDouble(&ok);
    if (!ok)
    {
        if (error)
            *error = QString("WIDTH 不是合法数字");
        return false;
    }

    m_fillPrep.ya = ui->editYa->text().trimmed().toDouble(&ok);
    if (!ok)
    {
        if (error)
            *error = QString("YA 不是合法数字");
        return false;
    }

    m_fillPrep.yb = ui->editYb->text().trimmed().toDouble(&ok);
    if (!ok)
    {
        if (error)
            *error = QString("YB 不是合法数字");
        return false;
    }

    m_fillPrep.tol2 = ui->editTol2->text().trimmed().toDouble(&ok);
    if (!ok)
    {
        if (error)
            *error = QString("TOL2 不是合法数字");
        return false;
    }

    m_fillPrep.xa = 0.0;
    m_fillPrep.xb = 0.0;
    return true;
}

void DamParametricDialog::updateObjectLabels()
{
    ui->labelW4->setText(m_params.P(24));
    ui->labelW5->setText(m_params.P(26));
    ui->labelT1->setText(m_params.P(28));
    ui->labelT2->setText(m_params.P(30));
    ui->labelP034->setText(m_params.P(33));
}

void DamParametricDialog::onConvertClicked()
{
    QString error;
    if (!collectParamsFromUi(&error))
    {
        QMessageBox::warning(this, QString("参数化建模"), error);
        return;
    }

    if (!DamParametricCalculator::calculate(m_ctx.iobject, m_params, &error))
    {
        QMessageBox::warning(this, QString("转换计算失败"), error);
        return;
    }

    if (!MsgJzDamIO::write(m_ctx.projectDir, m_params, &error))
    {
        QMessageBox::warning(this, QString("写入 MSGJZDAM.DAT 失败"), error);
        return;
    }

    applyParamsToUi();
    QMessageBox::information(this, QString("转换计算"), QString("参数转换完成，MSGJZDAM.DAT 已刷新。"));
}

void DamParametricDialog::onGenerateClicked()
{
    QString error;
    if (!collectParamsFromUi(&error))
    {
        QMessageBox::warning(this, QString("参数化建模"), error);
        return;
    }

    if (!DamParametricCalculator::calculate(m_ctx.iobject, m_params, &error))
    {
        QMessageBox::warning(this, QString("参数化建模"), error);
        return;
    }

    if (!MsgJzDamIO::write(m_ctx.projectDir, m_params, &error))
    {
        QMessageBox::warning(this, QString("参数化建模"), error);
        return;
    }

    if (m_ctx.iobject == 3 || m_ctx.iobject == 4 || m_ctx.iobject == 5 || m_ctx.iobject == 8)
    {
        if (!collectFillPrepFromUi(&error))
        {
            QMessageBox::warning(this, QString("参数化建模"), error);
            return;
        }

        if (!FillPrepIO::write(m_ctx.projectDir, m_fillPrep, &error))
        {
            QMessageBox::warning(this, QString("参数化建模"), error);
            return;
        }
    }

    if (!DamParametricRunner::run(m_ctx, &error))
    {
        QMessageBox::warning(this, QString("参数化建模"), error);
        return;
    }

    const QString iiinPath = QDir(m_ctx.projectDir).filePath("IIIN.DAT");
    emit iiinGenerated(iiinPath);
    accept();
}
