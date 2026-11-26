#include "CaveBlockStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleValidator>
#include <QFile>
#include <QFileInfo>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextStream>
#include <QVector3D>
#include <cmath>

#include "CaveBlockPlanePage.h"
#include "PickManager.h"
#include "ui_CaveBlockStabilityWidget.h"

#include <vtkCell.h>
#include <vtkUnstructuredGrid.h>

#include "SolverUiLayoutHelper.h"
CaveBlockStabilityWidget::CaveBlockStabilityWidget(QWidget* parent)
    : BasePage("CaveBlockStabilityAnalysis"), ui(new Ui::CaveBlockStabilityWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);

    // 滑块搜索范围是块体识别的一部分，直接保留在主界面。
    // 摘要用于解释当前限制；±1.0E6 表示该方向基本不限制。
    m_rangeSummaryLabel = new QLabel(this);
    m_rangeSummaryLabel->setWordWrap(true);
    m_rangeSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const int rangeIndex = ui->contentLayout->indexOf(ui->rangeGroupBox);
    const int insertIndex = rangeIndex >= 0 ? rangeIndex : ui->contentLayout->count();
    ui->contentLayout->insertWidget(insertIndex, m_rangeSummaryLabel);
    ui->rangeGroupBox->setVisible(true);

    m_forceInfoLabel = new QLabel(this);
    m_forceInfoLabel->setWordWrap(true);
    m_forceInfoLabel->setText(QStringLiteral(
        "作用对象：整个候选滑块。这里输入的是等效支护合力，不是某一条滑面上的力；正负方向按全局坐标轴。"));
    const int forceIndex = ui->contentLayout->indexOf(ui->forceGroupBox);
    if (forceIndex >= 0)
        ui->contentLayout->insertWidget(forceIndex + 1, m_forceInfoLabel);

    // GUI 固定采用自动判定滑移方向。原求解器仍保留 IFIX=1/2/3 的底层能力，
    // 但普通块体稳定分析不再要求用户手工输入方向。
    refreshDimensionUi();
    connect(ui->comboIfDir, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateDirectionEditors(); });
    updateDirectionEditors();

    setAttribute(Qt::WA_StyledBackground, true);
    ui->icutFileEdit->setText(QStringLiteral("ICUT.DAT"));
    ui->safeFileEdit->setText(QStringLiteral("SAFE_CAVEBLK.DAT"));
    ui->icutFileEdit->hide();
    ui->safeFileEdit->hide();
    ui->labelIcut->hide();
    ui->labelSafe->hide();
    // ui->fileHiddenWidget->hide();
    ui->computeBtn->setText(QStringLiteral("打开计算程序"));

    for (QDoubleSpinBox* spin : {ui->weightEdit, ui->xMinEdit, ui->xMaxEdit,
                                  ui->yMinEdit, ui->yMaxEdit, ui->zMinEdit, ui->zMaxEdit,
                                  ui->fxEdit, ui->fyEdit, ui->fzEdit})
    {
        spin->setRange(-1.0e9, 1.0e9);
        spin->setDecimals(6);
        spin->setSingleStep(1.0);
        spin->setKeyboardTracking(false);
    }
    ui->weightEdit->setMinimum(0.000001);


    for (QDoubleSpinBox* edit : {ui->xMinEdit, ui->xMaxEdit, ui->yMinEdit, ui->yMaxEdit,
                                  ui->zMinEdit, ui->zMaxEdit})
    {
        connect(edit, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this](double)
                {
                    updateRangeSummary();
                    syncPreviewSearchBounds();
                });
    }
    updateRangeSummary();

    connect(ui->computeBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (ui->computeBtn->property("computing").toBool())
                {
                    emit stopRequested();
                    return;
                }

                QString error;
                if (!validateInputs(&error))
                {
                    QMessageBox::warning(this, QStringLiteral("请完善块体稳定设置"), error);
                    return;
                }
                emit computeRequested();
            });
}

CaveBlockStabilityWidget::~CaveBlockStabilityWidget()
{
    if (!m_blockGroupId.isEmpty())
        CaveBlockPlanePage::clearGroupPreview(m_blockGroupId, false);
    delete ui;
}

void CaveBlockStabilityWidget::setBlockGroupId(const QString& groupId)
{
    const QString normalized = groupId.trimmed();
    if (m_blockGroupId == normalized)
    {
        syncPreviewSearchBounds();
        return;
    }

    const QString oldGroupId = m_blockGroupId;
    m_blockGroupId = normalized;

    // 设置页只负责给同组滑面提供搜索范围。切换工程/分析节点时先清理旧组预览，
    // 再把当前范围同步给新组，避免预览仍引用上一个分析节点。
    if (!oldGroupId.isEmpty())
        CaveBlockPlanePage::clearGroupPreview(oldGroupId, false);

    updateRangeSummary();
    syncPreviewSearchBounds();
}

void CaveBlockStabilityWidget::setComputing(bool computing)
{
    ui->computeBtn->setProperty("computing", computing);
    ui->computeBtn->setText(computing ? QStringLiteral("关闭计算程序")
                                      : QStringLiteral("打开计算程序"));
}

void CaveBlockStabilityWidget::beforeLoad()
{
    m_pageActive = true;
    refreshDimensionUi();
    updateRangeSummary();
    syncPreviewSearchBounds();
}

void CaveBlockStabilityWidget::beforeUnload()
{
    m_pageActive = false;
    if (!m_blockGroupId.isEmpty())
        CaveBlockPlanePage::clearGroupPreview(m_blockGroupId, false);
}

QJsonObject CaveBlockStabilityWidget::saveJson()
{
    QJsonObject obj;
    obj["icut_file"] = ui->icutFileEdit->text().trimmed();
    obj["safe_file"] = ui->safeFileEdit->text().trimmed();
    obj["weight"] = QString::number(ui->weightEdit->value(), 'g', 15);
    obj["x_min"] = QString::number(ui->xMinEdit->value(), 'g', 15);
    obj["x_max"] = QString::number(ui->xMaxEdit->value(), 'g', 15);
    obj["y_min"] = QString::number(ui->yMinEdit->value(), 'g', 15);
    obj["y_max"] = QString::number(ui->yMaxEdit->value(), 'g', 15);
    obj["z_min"] = QString::number(ui->zMinEdit->value(), 'g', 15);
    obj["z_max"] = QString::number(ui->zMaxEdit->value(), 'g', 15);
    obj["fx"] = QString::number(ui->fxEdit->value(), 'g', 15);
    obj["fy"] = QString::number(ui->fyEdit->value(), 'g', 15);
    obj["fz"] = QString::number(ui->fzEdit->value(), 'g', 15);
    obj["if_dir"] = QStringLiteral("0");
    obj["direction_mode_version"] = 3;
    obj["ax"] = ui->axEdit->text().trimmed();
    obj["ay"] = ui->ayEdit->text().trimmed();
    obj["az"] = ui->azEdit->text().trimmed();
    obj["bx"] = ui->bxEdit->text().trimmed();
    obj["by"] = ui->byEdit->text().trimmed();
    obj["bz"] = ui->bzEdit->text().trimmed();
    obj["im1"] = ui->im1Edit->text().trimmed();
    obj["im2"] = ui->im2Edit->text().trimmed();
    return obj;
}

void CaveBlockStabilityWidget::loadJson(const QJsonObject& obj)
{
    auto loadNumber = [&obj](const char* key, double fallback)
    {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (v.isDouble())
            return v.toDouble(fallback);
        bool ok = false;
        const double parsed = v.toString().trimmed().toDouble(&ok);
        return ok ? parsed : fallback;
    };
    ui->icutFileEdit->setText(obj.value("icut_file").toString(ui->icutFileEdit->text()));
    ui->safeFileEdit->setText(obj.value("safe_file").toString(ui->safeFileEdit->text()));
    ui->weightEdit->setValue(loadNumber("weight", ui->weightEdit->value()));
    ui->xMinEdit->setValue(loadNumber("x_min", ui->xMinEdit->value()));
    ui->xMaxEdit->setValue(loadNumber("x_max", ui->xMaxEdit->value()));
    ui->yMinEdit->setValue(loadNumber("y_min", ui->yMinEdit->value()));
    ui->yMaxEdit->setValue(loadNumber("y_max", ui->yMaxEdit->value()));
    ui->zMinEdit->setValue(loadNumber("z_min", ui->zMinEdit->value()));
    ui->zMaxEdit->setValue(loadNumber("z_max", ui->zMaxEdit->value()));
    ui->fxEdit->setValue(loadNumber("fx", ui->fxEdit->value()));
    ui->fyEdit->setValue(loadNumber("fy", ui->fyEdit->value()));
    ui->fzEdit->setValue(loadNumber("fz", ui->fzEdit->value()));
    refreshDimensionUi();
    // 当前 GUI 统一采用自动判定。旧工程中的 if_dir/方向点仍可读取保存，
    // 但不再作为本次计算的方向控制参数。
    ui->comboIfDir->setCurrentIndex(0);
    ui->axEdit->setText(obj.value("ax").toString(ui->axEdit->text()));
    ui->ayEdit->setText(obj.value("ay").toString(ui->ayEdit->text()));
    ui->azEdit->setText(obj.value("az").toString(ui->azEdit->text()));
    ui->bxEdit->setText(obj.value("bx").toString(ui->bxEdit->text()));
    ui->byEdit->setText(obj.value("by").toString(ui->byEdit->text()));
    ui->bzEdit->setText(obj.value("bz").toString(ui->bzEdit->text()));
    ui->im1Edit->setText(obj.value("im1").toString(ui->im1Edit->text()));
    ui->im2Edit->setText(obj.value("im2").toString(ui->im2Edit->text()));
    updateDirectionEditors();
    updateRangeSummary();
    syncPreviewSearchBounds();
}

bool CaveBlockStabilityWidget::exportDatFiles(QVector<CaveBlockPlanePage*>& planePages)
{
    if (planePages.isEmpty())
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());

    if (!wd.exists("SliderCase") && !wd.mkpath("SliderCase"))
        return false;

    QDir sliderDir(wd.filePath("SliderCase"));

    QFile icutFile(sliderDir.filePath(valueOrDefault(ui->icutFileEdit, "ICUT.DAT")));
    if (!icutFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream icut(&icutFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    icut.setCodec("UTF-8");
#endif
    const int meshDim = planePages.first()->spatialDimension();
    icut << QString("%1 1\n").arg(planePages.size());
    if (meshDim == 2)
        icut << "X1 Y1 X2 Y2 II\n";
    else
        icut << "X1 Y1 Z1 X2 Y2 Z2 X3 Y3 Z3 II\n";
    for (int i = 0; i < planePages.size(); ++i)
        icut << planePages[i]->icutLine(i + 1) << "\n";

    QFile paraFile(sliderDir.filePath("PARA.DAT"));
    if (!paraFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream para(&paraFile);
 #if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    para.setCodec("UTF-8");
#endif
    para << QString("%1\n").arg(planePages.size());
    para << "FP C(KPA) RT(KPA) II\n";
    for (int i = 0; i < planePages.size(); ++i)
        para << planePages[i]->paraLine(i + 1) << "\n";

    QFile mesg(sliderDir.filePath("MESG227.DAT"));
    if (!mesg.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&mesg);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    os.setCodec("UTF-8");
#endif
    os << quoteValue("---") << "\n";
    os << quoteValue(valueOrDefault(ui->icutFileEdit, "ICUT.DAT")) << "\n";
    os << quoteValue(valueOrDefault(ui->safeFileEdit, "SAFE_CAVEBLK.DAT")) << "\n";
    os << quoteValue(valueOrDefault(ui->weightEdit, "23.52")) << "\n";
    // SOLVER TUT(5..8) are CHARACTER*30; format compactly so joined strings never exceed 30 bytes.
    auto compactNum = [this](auto* edit, const char* fallback) -> QString
    {
        const QString text = valueOrDefault(edit, QString::fromLatin1(fallback));
        bool ok = false;
        const double value = text.toDouble(&ok);
        return ok ? QString::number(value, 'g', 7) : text;
    };
    os << quoteValue(QString("%1 %2")
                         .arg(compactNum(ui->xMinEdit, "-1.0E6"), compactNum(ui->xMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(compactNum(ui->yMinEdit, "-1.0E6"), compactNum(ui->yMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(compactNum(ui->zMinEdit, "-1.0E6"), compactNum(ui->zMaxEdit, "1.0E6")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(compactNum(ui->fxEdit, "-50.0"),
                              compactNum(ui->fyEdit, "0.0"),
                              compactNum(ui->fzEdit, "0.0")))
       << "\n";
    os << quoteValue(QStringLiteral("0")) << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(compactNum(ui->axEdit, "0.0"),
                              compactNum(ui->ayEdit, "0.0"),
                              compactNum(ui->azEdit, "0.0")))
       << "\n";
    os << quoteValue(QString("%1 %2 %3")
                         .arg(compactNum(ui->bxEdit, "-1.0"),
                              compactNum(ui->byEdit, "-1.0"),
                              compactNum(ui->bzEdit, "-1.0")))
       << "\n";
    os << quoteValue(QString("%1 %2")
                         .arg(valueOrDefault(ui->im1Edit, "1"))
                         .arg(valueOrDefault(ui->im2Edit, "2")))
       << "\n";
    const QStringList lines = {
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",
        "---",

        "                              ",
        "                              ",

        "MESG227.DAT",
        QDir::toNativeSeparators(sliderDir.absolutePath() + QDir::separator()),

        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------",
        "-------"
    };

    for (const QString& line : lines)
    {
        os << quoteValue(line) << "\n";
    }

    // ICUTPLANE.DAT、JME_CUT.DAT 和 SAFE_CAVEBLK.DAT 必须由原始
    // A0_FEMANALYSIS 的块体流程根据 IIIN.DAT + ICUT.DAT 重新生成。
    // 这里不再用整个模型体积/固定 Z 方向伪造这些中间结果，且清掉旧结果，
    // 避免修改滑块面后误用上一次计算数据。
    for (const QString& staleName : {QStringLiteral("ICUTPLANE.DAT"),
                                     QStringLiteral("JME_CUT.DAT"),
                                     QStringLiteral("SAFE_CAVEBLK.DAT")})
    {
        const QString stalePath = sliderDir.filePath(staleName);
        if (QFileInfo::exists(stalePath))
            QFile::remove(stalePath);
    }

    return true;
}

bool CaveBlockStabilityWidget::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    auto value = [](const QDoubleSpinBox* edit)
    {
        return edit ? edit->value() : 0.0;
    };

    if (value(ui->weightEdit) <= 0.0)
        return fail(QStringLiteral("块体容重必须为正数。"));

    auto validateRange = [&](QDoubleSpinBox* minEdit, QDoubleSpinBox* maxEdit, const QString& axis)
    {
        const double minValue = value(minEdit);
        const double maxValue = value(maxEdit);
        if (maxValue <= minValue)
            return fail(QStringLiteral("%1 方向的滑块搜索范围上限必须大于下限。").arg(axis));
        return true;
    };

    if (!validateRange(ui->xMinEdit, ui->xMaxEdit, QStringLiteral("X")) ||
        !validateRange(ui->yMinEdit, ui->yMaxEdit, QStringLiteral("Y")))
        return false;
    const bool is2D = spatialDimension() == 2;
    if (!is2D && !validateRange(ui->zMinEdit, ui->zMaxEdit, QStringLiteral("Z")))
        return false;

    // 滑移方向固定由求解器自动判断（IFIX=0），无需校验手工方向参数。

    return true;
}

int CaveBlockStabilityWidget::spatialDimension() const
{
    const int publicDim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (publicDim == 2 || publicDim == 3)
        return publicDim;

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (grid && grid->GetNumberOfCells() > 0)
    {
        for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
        {
            vtkCell* cell = grid->GetCell(cellId);
            if (cell)
                return cell->GetCellDimension() == 2 ? 2 : 3;
        }
    }
    return 3;
}

void CaveBlockStabilityWidget::refreshDimensionUi()
{
    const bool is2D = spatialDimension() == 2;

    {
        const QSignalBlocker blocker(ui->comboIfDir);
        ui->comboIfDir->clear();
        ui->comboIfDir->addItem(QStringLiteral("自动判断"), QStringLiteral("0"));
        ui->comboIfDir->setCurrentIndex(0);
    }

    ui->labelWeight->setText(QStringLiteral("容重 γ（kN/m³）"));
    ui->rangeGroupBox->setTitle(QStringLiteral("滑块搜索范围"));
    ui->forceGroupBox->setTitle(QStringLiteral("滑块支护力（合力，kN）"));
    ui->labelFx->setText(QStringLiteral("全局 X 分量"));
    ui->labelFy->setText(QStringLiteral("全局 Y 分量"));
    ui->labelFz->setText(QStringLiteral("全局 Z 分量"));

    ui->weightEdit->setToolTip(QStringLiteral("岩体容重，用于计算滑块自重。单位：kN/m³。"));
    ui->rangeGroupBox->setToolTip(
        QStringLiteral("用于确认需要验算的候选滑块区域，不改变滑面位置。±1.0E6 可视为该方向基本不限制。"));
    ui->forceGroupBox->setToolTip(
        QStringLiteral("作用在整个候选滑块上的等效支护合力，不是某一滑面上的力。支护前工况不计该力，支护后工况计入该力。"));
    ui->directionGroupBox->setToolTip(QStringLiteral("滑移方向由求解器根据受力状态自动判断。"));
    ui->directionGroupBox->setVisible(false);

    // 二维求解只使用 X/Y。Z 范围和 Z 支护力继续保留在工程数据中以兼容旧工程，
    // 但不在二维界面中暴露。
    for (QWidget* w : {static_cast<QWidget*>(ui->labelZMin), static_cast<QWidget*>(ui->zMinEdit),
                       static_cast<QWidget*>(ui->labelZMax), static_cast<QWidget*>(ui->zMaxEdit),
                       static_cast<QWidget*>(ui->labelFz), static_cast<QWidget*>(ui->fzEdit)})
    {
        if (w)
            w->setVisible(!is2D);
    }

    updateDirectionEditors();
    updateRangeSummary();
}

void CaveBlockStabilityWidget::updateDirectionEditors()
{
    // 当前产品统一采用求解器自动判定滑移方向（IFIX=0）。
    // 保留 .ui 中旧控件仅用于工程兼容，不在界面中暴露。
    ui->directionGroupBox->setVisible(false);
}

void CaveBlockStabilityWidget::updateRangeSummary()
{
    if (!m_rangeSummaryLabel)
        return;

    auto parse = [](const QDoubleSpinBox* edit, double fallback)
    {
        return edit ? edit->value() : fallback;
    };
    auto fmtRange = [](const QString& axis, double lo, double hi)
    {
        constexpr double kUnbounded = 9.0e5;
        const bool loInf = lo <= -kUnbounded;
        const bool hiInf = hi >=  kUnbounded;
        if (loInf && hiInf)
            return QStringLiteral("%1不限").arg(axis);
        if (loInf)
            return QStringLiteral("%1 ≤ %2").arg(axis).arg(hi, 0, 'g', 8);
        if (hiInf)
            return QStringLiteral("%1 ≥ %2").arg(axis).arg(lo, 0, 'g', 8);
        return QStringLiteral("%1：%2 ～ %3")
            .arg(axis).arg(lo, 0, 'g', 8).arg(hi, 0, 'g', 8);
    };

    const double xmin = parse(ui->xMinEdit, -1.0e6);
    const double xmax = parse(ui->xMaxEdit,  1.0e6);
    const double ymin = parse(ui->yMinEdit, -1.0e6);
    const double ymax = parse(ui->yMaxEdit,  1.0e6);
    QStringList limits;
    limits << fmtRange(QStringLiteral("X"), xmin, xmax)
           << fmtRange(QStringLiteral("Y"), ymin, ymax);
    if (spatialDimension() != 2)
    {
        const double zmin = parse(ui->zMinEdit, -1.0e6);
        const double zmax = parse(ui->zMaxEdit,  1.0e6);
        limits << fmtRange(QStringLiteral("Z"), zmin, zmax);
    }

    const bool is2D = spatialDimension() == 2;
    m_rangeSummaryLabel->setText(
        is2D
            ? QStringLiteral("候选滑块：由各滑面“点1 → 点2”的右侧共同确定；附加范围：%1。")
                  .arg(limits.join(QStringLiteral("，")))
            : QStringLiteral("候选滑块：由各滑面朝向块体内部的一侧共同确定；附加范围：%1。")
                  .arg(limits.join(QStringLiteral("，"))));
    m_rangeSummaryLabel->setToolTip(
        is2D
            ? QStringLiteral("二维滑面的方向会决定块体在哪一侧。若半透明候选滑块不对，请到对应滑块面点击“翻转块体侧”。搜索范围通常无需修改。")
            : QStringLiteral("搜索范围用于排除不属于目标块体的候选区域，通常无需修改。"));
}

void CaveBlockStabilityWidget::syncPreviewSearchBounds()
{
    if (m_blockGroupId.isEmpty())
        return;

    auto parse = [](const QDoubleSpinBox* edit, double fallback)
    {
        return edit ? edit->value() : fallback;
    };
    double bounds[6] = {
        parse(ui->xMinEdit, -1.0e6), parse(ui->xMaxEdit, 1.0e6),
        parse(ui->yMinEdit, -1.0e6), parse(ui->yMaxEdit, 1.0e6),
        parse(ui->zMinEdit, -1.0e6), parse(ui->zMaxEdit, 1.0e6)};

    CaveBlockPlanePage::setGroupSearchBounds(m_blockGroupId, bounds);
    if (m_pageActive)
        CaveBlockPlanePage::refreshGroupPreview(m_blockGroupId, true);
}

QString CaveBlockStabilityWidget::quoteValue(QString value)
{
    return "'" + value.trimmed() + "'";
}

QString CaveBlockStabilityWidget::valueOrDefault(QLineEdit* edit, QString def)
{
    if (!edit)
        return def;
    const QString v = edit->text().trimmed();
    return v.isEmpty() ? def : v;
}

QString CaveBlockStabilityWidget::valueOrDefault(QDoubleSpinBox* edit, QString def)
{
    return edit ? QString::number(edit->value(), 'g', 15) : def;
}
