#include "VectorSettingWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkArrowSource.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkFloatArray.h>
#include <vtkGlyph3D.h>
#include <vtkLookupTable.h>
#include <vtkMaskPoints.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTecplotReader.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalBlocker>

#include "PostResultAutoPrepare.h"
#include "PostTecplotUtils.h"
#include "ResultDisplayNames.h"
#include "PostVisualUtils.h"
#include "ui_VectorSettingWidget.h"

#include <stdio.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <cmath>
#include <limits>

VectorSettingWidget::VectorSettingWidget(VisualWidgetService* postProcessingWidget, QWidget* parent)
    : ui(new Ui::VectorSettingWidget), BasePage(PublicConstants::kPagePostProcessVector)
{
    ui->setupUi(this);

    // 槽函数连接
    connect(ui->confirmPushButton, &QPushButton::clicked, this,
            &VectorSettingWidget::on_confirmPushButton_clicked);
    connect(ui->scaleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VectorSettingWidget::on_scaleSpinBox_valueChanged);
    connect(ui->thicknessSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &VectorSettingWidget::on_thicknessSpinBox_valueChanged);
    connect(ui->resultDirComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &VectorSettingWidget::on_resultDirComboBox_currentIndexChanged);

    // 采样间隔使用数值框，避免非法文本和空值。
    ui->maskSizeSpinBox->setRange(1, 1000000);

    // 长度和粗细使用可直接输入的数值控件，避免滑块无法看出当前倍率。
    m_scaleFactor = ui->scaleSpinBox->value();
    m_thicknessFactor = ui->thicknessSpinBox->value();

    m_visualService = postProcessingWidget;
}
VectorSettingWidget::~VectorSettingWidget()
{
    delete ui;
}

double VectorSettingWidget::computeAutoBaseScale(vtkUnstructuredGrid* grid)
{
    if (!grid)
        return 1.0;
    double b[6];
    grid->GetBounds(b);
    const double dx = b[1] - b[0];
    const double dy = b[3] - b[2];
    const double dz = b[5] - b[4];
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (diag <= 0.0 || !std::isfinite(diag))
        return 1.0;
    return diag * 0.06; // 默认箭头基准长度为模型包围盒对角线的 6%
}

void VectorSettingWidget::beforeLoad()
{
    showPostProcessingOnly(m_visualService, false);
    const QString proj =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    refreshResultGroups();
    if (m_dataPath.isEmpty())
        m_dataPath = PostResultAutoPrepare::firstResultDirectory(proj);

    if (ui->stepComboBox->count() == 0)
    {
        // 首次进入：加载文件列表并默认读第一个
        loadFileName(m_dataPath);
    }
    else
    {
        // 非首次：恢复之前的显示（如果有箭头 actor，就挂回去）
        if (m_visualService && m_vectorActor)
        {
            addActorByService(m_visualService, m_vectorActor);
            m_visualService->showScalarBar(true, false);
            if (auto* scalarBar = m_visualService->scalarBarActor())
                scalarBar->SetTitle(QStringLiteral("|V|").toUtf8().constData());
            requestRender();
        }
    }
    m_loadedOnce = true;
    applyPendingState();
}

QJsonObject VectorSettingWidget::saveJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("resultDir"), ui->resultDirComboBox->currentData().toString());
    object.insert(QStringLiteral("stepFile"), ui->stepComboBox->currentData().toString());
    object.insert(QStringLiteral("xField"), ui->xComboBox->currentData().toString());
    object.insert(QStringLiteral("yField"), ui->yComboBox->currentData().toString());
    object.insert(QStringLiteral("zField"), ui->zComboBox->currentData().toString());
    object.insert(QStringLiteral("maskRatio"), ui->maskSizeSpinBox->value());
    object.insert(QStringLiteral("scale"), ui->scaleSpinBox->value());
    object.insert(QStringLiteral("thickness"), ui->thicknessSpinBox->value());
    return object;
}

void VectorSettingWidget::loadJson(const QJsonObject& object)
{
    m_pendingState.clear();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_pendingState.insert(it.key(), it.value().toVariant());
    m_hasPendingState = true;
    if (m_loadedOnce)
        applyPendingState();
}

void VectorSettingWidget::applyPendingState()
{
    if (!m_hasPendingState)
        return;

    const QVariantMap state = m_pendingState;
    m_pendingState.clear();
    m_hasPendingState = false;

    const QString resultDir = state.value(QStringLiteral("resultDir")).toString();
    if (!resultDir.isEmpty())
    {
        const int index = ui->resultDirComboBox->findData(resultDir);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->resultDirComboBox);
            ui->resultDirComboBox->setCurrentIndex(index);
            m_dataPath = resultDir;
        }
    }
    loadFileName(m_dataPath);

    const QString stepFile = state.value(QStringLiteral("stepFile")).toString();
    if (!stepFile.isEmpty())
    {
        const int index = ui->stepComboBox->findData(stepFile);
        if (index >= 0)
        {
            const QSignalBlocker blocker(ui->stepComboBox);
            ui->stepComboBox->setCurrentIndex(index);
            on_stepComboBox_currentIndexChanged(index);
        }
    }

    auto restoreCombo = [](QComboBox* combo, const QString& name)
    {
        if (!combo || name.isEmpty())
            return;
        const int index = combo->findData(name);
        if (index >= 0)
            combo->setCurrentIndex(index);
    };
    restoreCombo(ui->xComboBox, state.value(QStringLiteral("xField")).toString());
    restoreCombo(ui->yComboBox, state.value(QStringLiteral("yField")).toString());
    restoreCombo(ui->zComboBox, state.value(QStringLiteral("zField")).toString());
    ui->maskSizeSpinBox->setValue(
        std::max(1, state.value(QStringLiteral("maskRatio"), 1).toInt()));
    {
        double scale = state.value(QStringLiteral("scale"), 1.0).toDouble();
        // 兼容旧工程中滑块按 25～400 保存的整数值。
        if (scale > 10.0)
            scale /= 100.0;
        const QSignalBlocker blocker(ui->scaleSpinBox);
        ui->scaleSpinBox->setValue(qBound(0.1, scale, 10.0));
        m_scaleFactor = ui->scaleSpinBox->value();
    }
    {
        const QSignalBlocker blocker(ui->thicknessSpinBox);
        ui->thicknessSpinBox->setValue(
            qBound(0.25, state.value(QStringLiteral("thickness"), 1.0).toDouble(), 4.0));
        m_thicknessFactor = ui->thicknessSpinBox->value();
    }
}

void VectorSettingWidget::refreshResultGroups()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString previousPath = m_dataPath;
    const QVector<PostResultAutoPrepare::ResultGroup> groups =
        PostResultAutoPrepare::resultGroups(projectDir);

    ui->resultDirComboBox->blockSignals(true);
    ui->resultDirComboBox->clear();
    for (const PostResultAutoPrepare::ResultGroup& group : groups)
        ui->resultDirComboBox->addItem(group.displayName, group.sourcePath);

    int index = previousPath.isEmpty() ? -1 : ui->resultDirComboBox->findData(previousPath);
    if (index < 0 && ui->resultDirComboBox->count() > 0)
        index = 0;
    ui->resultDirComboBox->setCurrentIndex(index);
    m_dataPath = index >= 0 ? ui->resultDirComboBox->itemData(index).toString() : QString();
    ui->resultDirComboBox->blockSignals(false);
}

void VectorSettingWidget::on_resultDirComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    m_dataPath = ui->resultDirComboBox->itemData(index).toString();
    loadFileName(m_dataPath);
}

void VectorSettingWidget::beforeUnload()
{
    // 页面被切换：仅隐藏当前向量箭头，保留对象以便回来后快速恢复。
    if (m_visualService && m_vectorActor)
        removeActorByService(m_visualService, m_vectorActor, false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    requestRender();
    // 不要置空 m_vectorActor/m_glyph/m_grid —— 保持状态以用于恢复
}

void VectorSettingWidget::loadFileName(QString dataPath)
{
    ui->stepComboBox->blockSignals(true);
    ui->stepComboBox->clear();

    QDir directory(dataPath);
    if (!directory.exists())
    {
        qWarning() << "Directory not found:" << dataPath;
        ui->stepComboBox->blockSignals(false);
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QVector<PostResultAutoPrepare::ResultFile> files =
        PostResultAutoPrepare::resultFiles(projectDir, dataPath, true);

    for (const PostResultAutoPrepare::ResultFile& file : files)
    {
        const QString itemName = file.displayName.trimmed().isEmpty()
                                     ? QStringLiteral("结果数据")
                                     : file.displayName.trimmed();
        ui->stepComboBox->addItem(itemName, file.filePath);
    }

    ui->stepComboBox->blockSignals(false);

    if (ui->stepComboBox->count() > 0)
    {
        ui->stepComboBox->setCurrentIndex(0);
        on_stepComboBox_currentIndexChanged(0);
    }
    else
    {
        qWarning() << "No configured result files found in" << dataPath;
    }
}

void VectorSettingWidget::on_stepComboBox_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const QString filePath = ui->stepComboBox->currentData().toString();
    if (filePath.isEmpty())
    {
        qWarning() << "[Vector] No data file mapped for selection.";
        return;
    }
    setFileName(filePath);

    // 切文件时，先把旧的箭头从场景移除，避免看到旧结果
    if (m_visualService && m_vectorActor)
    {
        removeActorByService(m_visualService, m_vectorActor);
        // 不置空指针，等用户点“确认”后再重建新 glyph/actor
    }

    // 重新加载新数据（只读，不立即建箭头；等用户点“确认”）
    ensureDataLoaded();

    // 可选：如果希望切换后自动重建箭头，放开下面一行
    // rebuildPipeline();
}

void VectorSettingWidget::setFileName(const QString& file)
{
    if (m_fileName == file)
        return;
    m_fileName = file;
    m_grid = nullptr; // 强制下次重建
}

bool VectorSettingWidget::ensureDataLoaded()
{
    if (m_grid)
        return true;
    if (m_fileName.isEmpty())
        return false;

    QFileInfo info(m_fileName);
    QString ext = info.suffix().toLower();

    vtkSmartPointer<vtkUnstructuredGrid> grid;

    if (ext == "txt" || ext == "dat" || ext == "tec")
    {
        grid = PostTecplotUtils::readGrid(m_fileName);
    }
    else if (ext == "vtu")
    {
        // ---- 新增的 vtu ----
        grid = readVtu(m_fileName);
    }
    else
    {
        qWarning() << "[Vector] Unsupported file:" << m_fileName;
        return false;
    }

    if (!grid || grid->GetNumberOfPoints() == 0)
        return false;

    m_grid = grid;
    populateComponentCombos();
    return true;
}

void VectorSettingWidget::populateComponentCombos()
{
    if (!m_grid)
        return;

    QStringList names;
    vtkPointData* pd = m_grid->GetPointData();
    const int nArr = pd->GetNumberOfArrays();
    for (int i = 0; i < nArr; ++i)
    {
        vtkDataArray* arr = pd->GetArray(i);
        if (!arr || !arr->GetName())
            continue;
        if (arr->GetNumberOfComponents() == 1 || arr->GetNumberOfComponents() == 3)
        {
            names << QString::fromUtf8(arr->GetName());
        }
    }
    names.removeDuplicates();
    names.sort();

    auto displayName = [](const QString& name)
    {
        return ResultDisplayNames::displayName(name);
    };
    auto setCombo = [&](QComboBox* cb)
    {
        cb->clear();
        for (const QString& name : names)
            cb->addItem(displayName(name), name);
    };
    setCombo(ui->xComboBox);
    setCombo(ui->yComboBox);
    setCombo(ui->zComboBox);

    auto trySelect = [&](QComboBox* cb, const QStringList& prefixes)
    {
        for (int i = 0; i < cb->count(); ++i)
        {
            const QString name = cb->itemData(i).toString();
            for (const QString& prefix : prefixes)
            {
                if (name.startsWith(prefix, Qt::CaseInsensitive))
                {
                    cb->setCurrentIndex(i);
                    return;
                }
            }
        }
    };
    trySelect(ui->xComboBox, {QStringLiteral("UX"), QStringLiteral("VX"), QStringLiteral("SX")});
    trySelect(ui->yComboBox, {QStringLiteral("UY"), QStringLiteral("VY"), QStringLiteral("SY")});
    trySelect(ui->zComboBox, {QStringLiteral("UZ"), QStringLiteral("VZ"), QStringLiteral("SZ")});

    if (ui->xComboBox->currentIndex() < 0 && ui->xComboBox->count() > 0)
        ui->xComboBox->setCurrentIndex(0);
    if (ui->yComboBox->currentIndex() < 0 && ui->yComboBox->count() > 1)
        ui->yComboBox->setCurrentIndex(1);
    bool hasZComponent = false;
    for (int i = 0; i < ui->zComboBox->count(); ++i)
    {
        const QString name = ui->zComboBox->itemData(i).toString();
        if (name.startsWith(QStringLiteral("UZ"), Qt::CaseInsensitive) ||
            name.startsWith(QStringLiteral("VZ"), Qt::CaseInsensitive) ||
            name.startsWith(QStringLiteral("SZ"), Qt::CaseInsensitive))
        {
            hasZComponent = true;
            break;
        }
    }
    if (!hasZComponent)
    {
        ui->zComboBox->clear();
        ui->zComboBox->addItem(QStringLiteral("二维结果，Z 分量为零"), QString());
        ui->zComboBox->setEnabled(false);
    }
    else
    {
        ui->zComboBox->setEnabled(true);
    }
}

vtkDataArray* VectorSettingWidget::arrayByName(vtkUnstructuredGrid* grid, const QString& name)
{
    if (!grid || name.isEmpty())
        return nullptr;
    return grid->GetPointData()->GetArray(name.toUtf8().constData());
}

bool VectorSettingWidget::buildVectorArraysFromSelections(vtkUnstructuredGrid* grid, double& magMin,
                                                          double& magMax)
{
    if (!grid)
        return false;

    const QString xName = ui->xComboBox->currentData().toString();
    const QString yName = ui->yComboBox->currentData().toString();
    const QString zName = ui->zComboBox->currentData().toString();

    vtkDataArray* arrX = arrayByName(grid, xName);
    vtkDataArray* arrY = arrayByName(grid, yName);
    vtkDataArray* arrZ = arrayByName(grid, zName);

    if (!arrX || !arrY)
    {
        qWarning() << "[Vector] 所选分量数组不存在。";
        return false;
    }

    const vtkIdType nPts = grid->GetNumberOfPoints();
    if (arrX->GetNumberOfTuples() < nPts || arrY->GetNumberOfTuples() < nPts ||
        (arrZ && arrZ->GetNumberOfTuples() < nPts))
    {
        qWarning() << "[Vector] 分量数组长度与点数不一致。";
        return false;
    }

    const bool packed3 = arrZ && xName == yName && xName == zName &&
                         arrX->GetNumberOfComponents() == 3;

    auto vec = vtkSmartPointer<vtkFloatArray>::New();
    vec->SetName("Vector");
    vec->SetNumberOfComponents(3);
    vec->SetNumberOfTuples(nPts);

    auto mag = vtkSmartPointer<vtkFloatArray>::New();
    mag->SetName("VectorMag");
    mag->SetNumberOfComponents(1);
    mag->SetNumberOfTuples(nPts);

    magMin = std::numeric_limits<double>::infinity();
    magMax = -std::numeric_limits<double>::infinity();

    if (packed3)
    {
        double t[3];
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            arrX->GetTuple(i, t);
            if (!std::isfinite(t[0]) || !std::isfinite(t[1]) || !std::isfinite(t[2]))
                t[0] = t[1] = t[2] = 0.0;
            vec->SetTuple3(i, static_cast<float>(t[0]), static_cast<float>(t[1]),
                           static_cast<float>(t[2]));
            const double m = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            mag->SetTuple1(i, static_cast<float>(m));
            if (m < magMin)
                magMin = m;
            if (m > magMax)
                magMax = m;
        }
    }
    else
    {
        for (vtkIdType i = 0; i < nPts; ++i)
        {
            double x = arrX->GetComponent(i, 0);
            double y = arrY->GetComponent(i, 0);
            double z = arrZ ? arrZ->GetComponent(i, 0) : 0.0;
            if (!std::isfinite(x))
                x = 0.0;
            if (!std::isfinite(y))
                y = 0.0;
            if (!std::isfinite(z))
                z = 0.0;
            vec->SetTuple3(i, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            const double m = std::sqrt(x * x + y * y + z * z);
            mag->SetTuple1(i, static_cast<float>(m));
            if (m < magMin)
                magMin = m;
            if (m > magMax)
                magMax = m;
        }
    }

    auto* pd = grid->GetPointData();
    pd->SetVectors(vec);               // 朝向/长度基于 Vector
    pd->AddArray(mag);                 // 上色使用 VectorMag
    pd->SetActiveScalars("VectorMag"); // 不是必须，但有助于调试/检查

    return true;
}

void VectorSettingWidget::rebuildPipeline()
{
    if (!ensureDataLoaded())
        return;

    // 抽样间隔
    m_maskOnRatio = ui->maskSizeSpinBox->value();
    if (m_maskOnRatio < 1)
        m_maskOnRatio = 1;

    // 生成 Vector / VectorMag，并得到其范围
    double magMin = 0.0, magMax = 1.0;
    if (!buildVectorArraysFromSelections(m_grid, magMin, magMax))
        return;

    auto nearlyEqual = [](double a, double b)
    {
        const double abs = 1e-15, rel = 1e-12;
        return std::fabs(a - b) <= std::max(abs, rel * std::max(std::fabs(a), std::fabs(b)));
    };

    // 移除旧 actor
    if (m_vectorActor)
    {
        if (m_visualService)
            removeActorByService(m_visualService, m_vectorActor);
        m_vectorActor = nullptr;
    }

    // 抽样
    auto mask = vtkSmartPointer<vtkMaskPoints>::New();
    mask->SetInputData(m_grid);
    mask->SetOnRatio(m_maskOnRatio); // 每隔 N 个取 1 个
    mask->RandomModeOff();
    mask->Update();

    // 箭头源
    m_arrowSource = vtkSmartPointer<vtkArrowSource>::New();
    m_arrowSource->SetTipLength(0.35);
    m_arrowSource->SetTipRadius(0.08 * m_thicknessFactor);
    m_arrowSource->SetShaftRadius(0.02 * m_thicknessFactor);

    // Glyph：向量用于朝向；上色用 VectorMag
    m_glyph = vtkSmartPointer<vtkGlyph3D>::New();
#if VTK_MAJOR_VERSION >= 9
    m_glyph->SetInputConnection(mask->GetOutputPort());
    m_glyph->SetSourceConnection(m_arrowSource->GetOutputPort());
#else
    m_glyph->SetInputConnection(0, mask->GetOutputPort());
    m_glyph->SetSourceConnection(0, m_arrowSource->GetOutputPort());
#endif
    m_glyph->OrientOn();
    m_glyph->SetVectorModeToUseVector();
    m_glyph->SetScaleModeToDataScalingOff(); // 等长
    // 指定向量与标量数组
    m_glyph->SetInputArrayToProcess(1, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    "Vector"); // vectors
    m_glyph->SetInputArrayToProcess(0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS,
                                    "VectorMag"); // scalars

    m_glyph->SetScaleModeToDataScalingOff(); // 等长
    {
        const double base = computeAutoBaseScale(m_grid);
        m_glyph->SetScaleFactor(base * m_scaleFactor); // 自适应尺度 × 滑条倍率
    }
    m_glyph->Update();

    // Mapper：显式按 VectorMag 上色
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(m_glyph->GetOutputPort());
    mapper->ScalarVisibilityOn();
    mapper->SetScalarModeToUsePointFieldData();
    mapper->SelectColorArray("VectorMag");
    mapper->SetColorModeToMapScalars();

    // 同步色标 LUT（与 VectorMag 一致）
    if (!(magMax > magMin) || nearlyEqual(magMax, magMin))
    {
        // 常量场兜底，使 LUT 有一个有效范围
        magMin = 0.0;
        magMax = 1.0;
    }

    if (vtkSmartPointer<vtkScalarBarActor> scalarBar =
            m_visualService ? m_visualService->scalarBarActor() : nullptr)
    {
        vtkLookupTable* lut = vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable());
        if (!lut)
        {
            auto l = vtkSmartPointer<vtkLookupTable>::New();
            l->Build();
            lut = l;
            scalarBar->SetLookupTable(lut);
        }
        lut->SetRange(magMin, magMax);
        mapper->SetLookupTable(lut);
        mapper->UseLookupTableScalarRangeOn();

        scalarBar->SetTitle(QStringLiteral("|V|").toUtf8().constData());
        scalarBar->Modified();
        m_visualService->showScalarBar(true, false);
    }
    else
    {
        mapper->SetScalarRange(magMin, magMax);
    }

    // Actor
    m_vectorActor = vtkSmartPointer<vtkActor>::New();
    m_vectorActor->SetMapper(mapper);
    m_vectorActor->GetProperty()->SetRepresentationToSurface();

    if (m_visualService)
        addActorByService(m_visualService, m_vectorActor);

    requestRender();
}

void VectorSettingWidget::requestRender() const
{
    if (!m_visualService)
        return;
    if (auto* renderWindow = m_visualService->renderWindow())
        renderWindow->Render();
}

void VectorSettingWidget::on_confirmPushButton_clicked()
{
    rebuildPipeline();
}

void VectorSettingWidget::on_scaleSpinBox_valueChanged(double value)
{
    m_scaleFactor = qBound(0.1, value, 10.0);
    if (m_glyph)
    {
        const double base = computeAutoBaseScale(m_grid);
        m_glyph->SetScaleFactor(base * m_scaleFactor);
        m_glyph->Update();
        requestRender();
    }
}

void VectorSettingWidget::on_thicknessSpinBox_valueChanged(double value)
{
    m_thicknessFactor = qBound(0.25, value, 4.0);
    if (m_arrowSource)
    {
        m_arrowSource->SetTipRadius(0.08 * m_thicknessFactor);
        m_arrowSource->SetShaftRadius(0.02 * m_thicknessFactor);
        m_arrowSource->Modified();
        if (m_glyph)
            m_glyph->Update();
        requestRender();
    }
}

vtkSmartPointer<vtkUnstructuredGrid> VectorSettingWidget::readVtu(const QString& fileName)
{
    auto reader = vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    reader->SetFileName(fileName.toUtf8().constData());
    reader->Update();

    vtkUnstructuredGrid* ug = reader->GetOutput();
    if (!ug || ug->GetNumberOfPoints() == 0)
    {
        qWarning() << "[Vector] Failed to read VTU file:" << fileName;
        return nullptr;
    }

    auto result = vtkSmartPointer<vtkUnstructuredGrid>::New();
    result->ShallowCopy(ug);
    return result;
}
