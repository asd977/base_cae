#include "AnimationAviWidget.h"

#include "PostComboBoxPerformance.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
#include "ResultDisplayNames.h"
#include "ui_AnimationAviWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkDataSetMapper.h>
#include <vtkIdList.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{
QString sequenceScalarKey(const QString& sequenceKey, const QString& scalar)
{
    return sequenceKey + QLatin1Char('|') + scalar;
}

bool validRange(const double range[2])
{
    return range && std::isfinite(range[0]) && std::isfinite(range[1]) &&
           range[0] <= range[1];
}

QString cacheKeyForPath(const QString& filePath)
{
    return QFileInfo(filePath).absoluteFilePath().toCaseFolded();
}

void interpolateDataAttributes(vtkDataSetAttributes* first,
                               vtkDataSetAttributes* second,
                               vtkDataSetAttributes* output, double factor)
{
    if (!first || !second || !output)
        return;

    for (int arrayIndex = 0; arrayIndex < first->GetNumberOfArrays(); ++arrayIndex)
    {
        vtkDataArray* firstArray = first->GetArray(arrayIndex);
        if (!firstArray || !firstArray->GetName())
            continue;

        if (ResultDisplayNames::isInternalField(QString::fromUtf8(firstArray->GetName())))
            continue;

        vtkDataArray* secondArray = second->GetArray(firstArray->GetName());
        vtkDataArray* outputArray = output->GetArray(firstArray->GetName());
        if (!secondArray || !outputArray ||
            firstArray->GetNumberOfTuples() != secondArray->GetNumberOfTuples() ||
            firstArray->GetNumberOfComponents() !=
                secondArray->GetNumberOfComponents())
            continue;

        const vtkIdType tupleCount = firstArray->GetNumberOfTuples();
        const int componentCount = firstArray->GetNumberOfComponents();
        for (vtkIdType tuple = 0; tuple < tupleCount; ++tuple)
        {
            for (int component = 0; component < componentCount; ++component)
            {
                const double firstValue = firstArray->GetComponent(tuple, component);
                const double secondValue = secondArray->GetComponent(tuple, component);
                outputArray->SetComponent(
                    tuple, component,
                    firstValue + (secondValue - firstValue) * factor);
            }
        }
        outputArray->Modified();
    }
}
} // namespace

AnimationAviWidget::AnimationAviWidget(VisualWidgetService* postProcessingWidget,
                                       QWidget* parent)
    : BasePage(PublicConstants::kPagePostProcessAnimation),
      ui(new Ui::AnimationAviWidget),
      m_visualService(postProcessingWidget)
{
    ui->setupUi(this);

    configurePostResultCombo(ui->analysisComboBox);
    configurePostResultCombo(ui->rangeComboBox);
    configurePostResultCombo(ui->resultComboBox);
    configurePostResultCombo(ui->scalarComboBox);

    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(false);

    connect(&m_timer, &QTimer::timeout, this, &AnimationAviWidget::onTick);

    // setupUi() 已通过 QMetaObject::connectSlotsByName() 自动连接所有
    // on_<objectName>_<signal>() 槽。这里不能再次手动连接，否则一次点击会
    // 连续执行“播放”和“暂停”，下拉框、滑块等控件也会被重复处理。
    connect(ui->interpCheckBox, &QCheckBox::toggled, this,
            [this](bool)
    {
        m_subStep = 0;
        updateControls();
    });
    connect(ui->interpCountSpin,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { m_subStep = 0; });

    EventManager::instance().registerHandler(
        postDisplaySettingsChangedEvent(), this,
        [this](const QVariantMap& data)
        {
            if (!property("postDisplayActive").toBool() || !m_grid)
                return;
            if (data.value(QStringLiteral("reason")).toString() == QStringLiteral("mesh-filter"))
                m_rangeCache.clear();
            applyScalar(m_grid, currentScalarName());
            renderNow();
        });

    setStatusText(QStringLiteral("等待载入动画结果"));
    updateControls();
}

AnimationAviWidget::~AnimationAviWidget()
{
    stopTimer();
    delete ui;
}

void AnimationAviWidget::beforeLoad()
{
    setProperty("postDisplayActive", true);
    showPostProcessingOnly(m_visualService, false);
    if (!m_visualService)
        m_visualService =
            ServiceRegistry::instance().getService<VisualWidgetService>();

    refreshModel();
    if (m_actor && m_grid)
        attachActor();
    renderNow();
}

void AnimationAviWidget::beforeUnload()
{
    setPostViewActive(false);
    setProperty("postDisplayActive", false);
    clearPostDisplayContext();
    stopTimer();
    detachActor();
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    renderNow();
}

QJsonObject AnimationAviWidget::saveJson()
{
    QJsonObject object;
    object.insert(QStringLiteral("analysisKey"),
                  ui->analysisComboBox->currentData().toString());
    object.insert(QStringLiteral("rangeKey"),
                  ui->rangeComboBox->currentData().toString());
    object.insert(QStringLiteral("sequenceKey"),
                  ui->resultComboBox->currentData().toString());
    object.insert(QStringLiteral("scalar"), currentScalarName());
    object.insert(QStringLiteral("frame"), m_currentFrame);
    object.insert(QStringLiteral("fps"), ui->fpsSpin->value());
    object.insert(QStringLiteral("loop"), ui->loopCheckBox->isChecked());
    object.insert(QStringLiteral("fixedRange"),
                  ui->fixRangeCheckBox->isChecked());
    object.insert(QStringLiteral("showExcavatedRegion"),
                  ui->showExcavatedCheckBox->isChecked());
    object.insert(QStringLiteral("interpolate"),
                  ui->interpCheckBox->isChecked());
    object.insert(QStringLiteral("interpolationFrames"),
                  ui->interpCountSpin->value());
    return object;
}

void AnimationAviWidget::loadJson(const QJsonObject& object)
{
    m_pendingJson = object;
    m_pendingAnalysisKey =
        object.value(QStringLiteral("analysisKey")).toString();
    m_pendingRangeKey = object.value(QStringLiteral("rangeKey")).toString();
    m_pendingSequenceKey =
        object.value(QStringLiteral("sequenceKey")).toString();
    m_pendingScalar = object.value(QStringLiteral("scalar")).toString();
    m_pendingShowExcavated = object.contains(QStringLiteral("showExcavatedRegion"))
                                 ? object.value(QStringLiteral("showExcavatedRegion")).toBool(false)
                                 : m_pendingSequenceKey.contains(
                                       QStringLiteral("含已开挖体"), Qt::CaseInsensitive);
    {
        const QSignalBlocker blocker(ui->showExcavatedCheckBox);
        ui->showExcavatedCheckBox->setChecked(m_pendingShowExcavated);
    }

    ui->fpsSpin->setValue(
        object.value(QStringLiteral("fps")).toDouble(5.0));
    ui->loopCheckBox->setChecked(
        object.value(QStringLiteral("loop")).toBool(true));
    ui->fixRangeCheckBox->setChecked(
        object.value(QStringLiteral("fixedRange")).toBool(true));
    ui->interpCheckBox->setChecked(
        object.value(QStringLiteral("interpolate")).toBool(false));
    ui->interpCountSpin->setValue(
        object.value(QStringLiteral("interpolationFrames")).toInt(2));

    if (!m_model.sequences().isEmpty())
        refreshModel();
}

void AnimationAviWidget::refreshModel()
{
    stopTimer();
    clearGridCache();
    m_rangeCache.clear();

    const QString projectDir = PublicVariables::instance()
                                   .get(PublicConstants::kProjectDirectoryKey)
                                   .toString();
    m_model.rebuild(projectDir);

    QString preferredSequence = !m_pendingSequenceKey.isEmpty()
                                    ? m_pendingSequenceKey
                                    : ui->resultComboBox->currentData().toString();
    QString preferredAnalysis = !m_pendingAnalysisKey.isEmpty()
                                    ? m_pendingAnalysisKey
                                    : ui->analysisComboBox->currentData().toString();
    QString preferredRange = !m_pendingRangeKey.isEmpty()
                                 ? m_pendingRangeKey
                                 : ui->rangeComboBox->currentData().toString();
    const bool preferredShowExcavated = !m_pendingJson.isEmpty()
                                             ? m_pendingShowExcavated
                                             : ui->showExcavatedCheckBox->isChecked();

    const int preferredIndex = m_model.sequenceIndex(preferredSequence);
    if (const auto* sequence = m_model.sequence(preferredIndex))
    {
        preferredSequence = sequence->key;
        preferredAnalysis = sequence->analysisKey;
        preferredRange = sequence->rangeKey;
    }

    populateAnalysisCombo(preferredAnalysis);
    populateRangeCombo(preferredRange);
    populateResultCombo(preferredSequence);
    {
        const QSignalBlocker blocker(ui->showExcavatedCheckBox);
        ui->showExcavatedCheckBox->setChecked(preferredShowExcavated);
    }
    activateSelectedSequence();

    if (!m_pendingScalar.isEmpty())
    {
        const int scalarIndex =
            ResultDisplayNames::findInternalName(ui->scalarComboBox,
                                                 m_pendingScalar);
        if (scalarIndex >= 0)
            ui->scalarComboBox->setCurrentIndex(scalarIndex);
    }

    if (!m_pendingJson.isEmpty() && currentSequence())
    {
        const int frame =
            m_pendingJson.value(QStringLiteral("frame")).toInt(0);
        loadFrame(qBound(0, frame, currentSequence()->frames.size() - 1));
    }

    m_pendingAnalysisKey.clear();
    m_pendingRangeKey.clear();
    m_pendingSequenceKey.clear();
    m_pendingScalar.clear();
    m_pendingShowExcavated = false;
    m_pendingJson = QJsonObject();
    updateControls();
}

void AnimationAviWidget::populateAnalysisCombo(const QString& preferredKey)
{
    const QSignalBlocker blocker(ui->analysisComboBox);
    ui->analysisComboBox->clear();
    for (const auto& item : m_model.analyses())
        ui->analysisComboBox->addItem(item.name, item.key);

    int index = preferredKey.isEmpty()
                    ? -1
                    : ui->analysisComboBox->findData(preferredKey);
    if (index < 0 && ui->analysisComboBox->count() > 0)
        index = 0;
    ui->analysisComboBox->setCurrentIndex(index);
}

void AnimationAviWidget::populateRangeCombo(const QString& preferredKey)
{
    const QString analysisKey =
        ui->analysisComboBox->currentData().toString();
    const QSignalBlocker blocker(ui->rangeComboBox);
    ui->rangeComboBox->clear();
    for (const auto& item : m_model.ranges(analysisKey))
        ui->rangeComboBox->addItem(item.name, item.key);

    int index = preferredKey.isEmpty()
                    ? -1
                    : ui->rangeComboBox->findData(preferredKey);
    if (index < 0 && ui->rangeComboBox->count() > 0)
        index = 0;
    ui->rangeComboBox->setCurrentIndex(index);
}

void AnimationAviWidget::populateResultCombo(
    const QString& preferredSequenceKey)
{
    const QString analysisKey =
        ui->analysisComboBox->currentData().toString();
    const QString rangeKey = ui->rangeComboBox->currentData().toString();
    const QSignalBlocker blocker(ui->resultComboBox);
    ui->resultComboBox->clear();
    for (const auto& item : m_model.results(analysisKey, rangeKey))
        ui->resultComboBox->addItem(item.name, item.key);

    int index = preferredSequenceKey.isEmpty()
                    ? -1
                    : ui->resultComboBox->findData(preferredSequenceKey);
    if (index < 0 && ui->resultComboBox->count() > 0)
        index = 0;
    ui->resultComboBox->setCurrentIndex(index);
}

void AnimationAviWidget::activateSelectedSequence()
{
    stopTimer();
    clearGridCache();
    m_rangeCache.clear();
    m_sequenceIndex = m_model.sequenceIndex(
        ui->resultComboBox->currentData().toString());
    m_currentFrame = -1;
    m_subStep = 0;

    const auto* sequence = currentSequence();
    if (!sequence)
    {
        m_grid = nullptr;
        ui->scalarComboBox->clear();
        ui->frameSlider->setRange(0, 0);
        ui->frameSlider->setValue(0);
        ui->frameLabel->setText(QStringLiteral("—"));
        ui->sequenceSummaryLabel->setText(
            QStringLiteral("暂无可播放的序列结果"));
        setStatusText(QStringLiteral("没有找到至少包含两帧的结果序列"), true);
        detachActor();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        updateControls();
        renderNow();
        return;
    }

    const bool hasExcavatedVariant = currentSequenceHasExcavatedVariant();
    {
        const QSignalBlocker blocker(ui->showExcavatedCheckBox);
        if (!hasExcavatedVariant)
            ui->showExcavatedCheckBox->setChecked(false);
    }

    {
        const QSignalBlocker blocker(ui->frameSlider);
        ui->frameSlider->setRange(0, sequence->frames.size() - 1);
        ui->frameSlider->setValue(0);
    }

    updateSequenceSummary();
    if (!loadFrame(0, true))
    {
        setStatusText(QStringLiteral("首帧读取失败，无法播放该结果序列"), true);
        updateControls();
        return;
    }

    if (m_visualService && m_visualService->renderer())
        m_visualService->renderer()->ResetCamera();
    setStatusText(QStringLiteral("动画序列已就绪"));
    updateControls();
    renderNow();
}

const PostAnimationSequenceModel::Sequence*
AnimationAviWidget::currentSequence() const
{
    return m_model.sequence(m_sequenceIndex);
}

QString AnimationAviWidget::frameFilePath(
    const PostAnimationSequenceModel::Frame& frame) const
{
    if (ui->showExcavatedCheckBox->isChecked() &&
        !frame.excavatedFilePath.trimmed().isEmpty())
        return frame.excavatedFilePath;
    if (!frame.filePath.trimmed().isEmpty())
        return frame.filePath;
    return frame.excavatedFilePath;
}

bool AnimationAviWidget::currentSequenceHasExcavatedVariant() const
{
    const auto* sequence = currentSequence();
    if (!sequence)
        return false;
    return std::any_of(sequence->frames.cbegin(), sequence->frames.cend(),
                       [](const PostAnimationSequenceModel::Frame& frame)
    {
        return !frame.excavatedFilePath.trimmed().isEmpty();
    });
}

vtkSmartPointer<vtkUnstructuredGrid>
AnimationAviWidget::readGrid(const QString& filePath)
{
    const QString key = cacheKeyForPath(filePath);
    const auto cached = m_gridCache.constFind(key);
    if (cached != m_gridCache.constEnd())
        return cached.value();

    QString error;
    vtkSmartPointer<vtkUnstructuredGrid> grid =
        PostTecplotUtils::readGrid(filePath, &error);
    if (!grid || grid->GetNumberOfPoints() <= 0)
    {
        qWarning().noquote() << error;
        return nullptr;
    }

    rememberGrid(filePath, grid);
    return grid;
}

void AnimationAviWidget::clearGridCache()
{
    m_gridCache.clear();
    m_gridCacheOrder.clear();
}

void AnimationAviWidget::rememberGrid(
    const QString& filePath,
    const vtkSmartPointer<vtkUnstructuredGrid>& grid)
{
    if (!grid)
        return;

    const QString key = cacheKeyForPath(filePath);
    m_gridCache.insert(key, grid);
    m_gridCacheOrder.removeAll(key);
    m_gridCacheOrder.push_back(key);

    while (m_gridCacheOrder.size() > m_gridCacheLimit)
    {
        const QString oldest = m_gridCacheOrder.takeFirst();
        m_gridCache.remove(oldest);
    }
}

void AnimationAviWidget::preloadNeighbourFrame()
{
    const auto* sequence = currentSequence();
    const int next = nextFrameIndex(1);
    if (!sequence || next < 0 || next >= sequence->frames.size())
        return;

    const QString sequenceKey = sequence->key;
    const QString filePath = frameFilePath(sequence->frames.at(next));
    if (m_gridCache.contains(cacheKeyForPath(filePath)))
        return;

    QTimer::singleShot(0, this, [this, sequenceKey, filePath]()
    {
        const auto* active = currentSequence();
        if (!active || active->key != sequenceKey || m_playing)
            return;
        readGrid(filePath);
    });
}

bool AnimationAviWidget::loadFrame(int index, bool updateScalarList)
{
    const auto* sequence = currentSequence();
    if (!sequence || index < 0 || index >= sequence->frames.size())
        return false;

    QScopedValueRollback<bool> loadingGuard(m_loadingState, true);
    const QString filePath = frameFilePath(sequence->frames.at(index));
    vtkSmartPointer<vtkUnstructuredGrid> grid = readGrid(filePath);
    if (!grid)
        return false;

    const QString preferredScalar = currentScalarName();
    m_grid = grid;
    m_currentFrame = index;
    m_subStep = 0;

    if (updateScalarList || !scalarArray(m_grid, preferredScalar))
        fillScalarCombo(m_grid, preferredScalar);
    applyScalar(m_grid, currentScalarName());

    {
        const QSignalBlocker blocker(ui->frameSlider);
        ui->frameSlider->setValue(index);
    }
    updateFrameLabel();
    attachActor();
    renderNow();
    preloadNeighbourFrame();
    return true;
}

void AnimationAviWidget::fillScalarCombo(vtkUnstructuredGrid* grid,
                                          const QString& preferredScalar)
{
    QStringList arrays;
    if (grid && grid->GetPointData())
    {
        for (int index = 0;
             index < grid->GetPointData()->GetNumberOfArrays(); ++index)
        {
            const char* name = grid->GetPointData()->GetArrayName(index);
            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName))
                    arrays.push_back(qName);
            }
        }
    }
    if (grid && grid->GetCellData())
    {
        for (int index = 0;
             index < grid->GetCellData()->GetNumberOfArrays(); ++index)
        {
            const char* name = grid->GetCellData()->GetArrayName(index);
            if (name && *name)
            {
                const QString qName = QString::fromUtf8(name);
                if (!ResultDisplayNames::isInternalField(qName) && !arrays.contains(qName))
                    arrays.push_back(qName);
            }
        }
    }

    const QSignalBlocker blocker(ui->scalarComboBox);
    ResultDisplayNames::populateCombo(ui->scalarComboBox, arrays,
                                     preferredScalar);
}

QString AnimationAviWidget::currentScalarName() const
{
    return ResultDisplayNames::internalName(ui->scalarComboBox);
}

vtkDataArray* AnimationAviWidget::scalarArray(vtkUnstructuredGrid* grid,
                                               const QString& name,
                                               bool* pointData) const
{
    if (!grid || name.isEmpty())
        return nullptr;

    if (vtkDataArray* array =
            grid->GetPointData()->GetArray(name.toUtf8().constData()))
    {
        if (pointData)
            *pointData = true;
        return array;
    }
    if (vtkDataArray* array =
            grid->GetCellData()->GetArray(name.toUtf8().constData()))
    {
        if (pointData)
            *pointData = false;
        return array;
    }
    return nullptr;
}

bool AnimationAviWidget::computeGlobalRange(const QString& scalarName,
                                             double range[2])
{
    const auto* sequence = currentSequence();
    if (!sequence || scalarName.isEmpty())
        return false;

    const QString modeKey = sequence->key +
        (ui->showExcavatedCheckBox->isChecked()
             ? QStringLiteral("|with-excavated")
             : QStringLiteral("|current-stage"));
    const QString cacheKey = sequenceScalarKey(modeKey, scalarName);
    const auto cached = m_rangeCache.constFind(cacheKey);
    if (cached != m_rangeCache.constEnd())
    {
        range[0] = cached.value().first;
        range[1] = cached.value().second;
        return true;
    }

    // 计算整段序列范围时会临时读取多帧，不能让最后一帧覆盖当前界面的
    // 网格筛选状态提示。
    const PostMeshFilterReport currentFilterReport = postMeshFilterReport();

    bool initialized = false;
    double minimum = 0.0;
    double maximum = 1.0;
    for (const auto& frame : sequence->frames)
    {
        vtkSmartPointer<vtkUnstructuredGrid> grid = readGrid(frameFilePath(frame));
        vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(grid);
        vtkDataArray* array = scalarArray(displayGrid, scalarName);
        if (!array)
            continue;

        double localRange[2];
        array->GetRange(localRange);
        if (!validRange(localRange))
            continue;

        if (!initialized)
        {
            minimum = localRange[0];
            maximum = localRange[1];
            initialized = true;
        }
        else
        {
            minimum = std::min(minimum, localRange[0]);
            maximum = std::max(maximum, localRange[1]);
        }
    }

    postMeshFilterReport() = currentFilterReport;
    if (!initialized)
        return false;
    if (std::abs(maximum - minimum) < 1.0e-14)
        maximum = minimum + 1.0;

    range[0] = minimum;
    range[1] = maximum;
    m_rangeCache.insert(cacheKey, qMakePair(minimum, maximum));
    return true;
}

void AnimationAviWidget::applyScalar(vtkUnstructuredGrid* grid,
                                      const QString& scalarName)
{
    buildPipelineIfNeeded();
    if (!grid || scalarName.isEmpty() || !m_visualService)
    {
        if (m_mapper)
            m_mapper->ScalarVisibilityOff();
        if (m_visualService)
            m_visualService->showScalarBar(false, false);
        return;
    }

    vtkSmartPointer<vtkUnstructuredGrid> displayGrid = postFilteredResultGrid(grid);
    if (!displayGrid || displayGrid->GetNumberOfCells() <= 0)
    {
        clearPostDisplayContext();
        m_mapper->SetInputData(displayGrid);
        m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        return;
    }

    bool pointData = true;
    vtkDataArray* array = scalarArray(displayGrid, scalarName, &pointData);
    if (!array || array->GetNumberOfTuples() <= 0)
    {
        clearPostDisplayContext();
        m_mapper->ScalarVisibilityOff();
        m_visualService->showScalarBar(false, false);
        return;
    }

    double range[2];
    array->GetRange(range);
    if (!validRange(range))
        return;

    // “统一色标范围”属于动画播放自身的显示方式：开启时，自动范围采用整段序列范围；
    // 公共“结果显示”中的自定义范围仍可覆盖它。
    if (ui->fixRangeCheckBox->isChecked())
        computeGlobalRange(scalarName, range);

    if (property("postDisplayActive").toBool())
        publishPostDisplayContext(scalarName, range[0], range[1], false, false, true);

    double displayMin = range[0];
    double displayMax = range[1];
    resolvePostDisplayRange(scalarName, range[0], range[1], displayMin, displayMax);
    if (std::abs(displayMax - displayMin) < 1.0e-14)
    {
        const double delta = std::max(std::abs(displayMin) * 1.0e-6, 1.0e-9);
        displayMin -= delta;
        displayMax += delta;
    }

    vtkScalarBarActor* scalarBar = m_visualService->scalarBarActor();
    vtkLookupTable* lookupTable =
        scalarBar ? vtkLookupTable::SafeDownCast(scalarBar->GetLookupTable())
                  : nullptr;
    vtkSmartPointer<vtkLookupTable> ownedLookup;
    if (!lookupTable)
    {
        ownedLookup = vtkSmartPointer<vtkLookupTable>::New();
        lookupTable = ownedLookup;
        if (scalarBar)
            scalarBar->SetLookupTable(lookupTable);
    }

    applyStandardPostLookupTable(lookupTable, displayMin, displayMax);

    m_mapper->SetInputData(displayGrid);
    m_mapper->SetLookupTable(lookupTable);
    m_mapper->SetScalarRange(displayMin, displayMax);
    if (pointData)
        m_mapper->SetScalarModeToUsePointFieldData();
    else
        m_mapper->SetScalarModeToUseCellFieldData();
    m_mapper->SelectColorArray(scalarName.toUtf8().constData());
    m_mapper->ScalarVisibilityOn();

    if (scalarBar)
    {
        applyStandardPostScalarBar(
            m_visualService, ResultDisplayNames::scalarBarName(scalarName));
        m_visualService->showScalarBar(postScalarBarShouldBeVisible(), false);
    }
}

void AnimationAviWidget::buildPipelineIfNeeded()
{
    if (m_mapper)
        return;

    m_mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(m_mapper);
    applyPostActorRepresentation(m_actor);
}

void AnimationAviWidget::attachActor()
{
    buildPipelineIfNeeded();
    if (!m_actorAttached && m_visualService && m_actor)
    {
        addActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("animation")),
                          m_actor, QStringLiteral("动画结果"));
        m_actorAttached = true;
    }
}

void AnimationAviWidget::detachActor()
{
    if (m_actorAttached && m_visualService && m_actor)
    {
        removeActorByService(m_visualService, postActorId(getUuid(), QStringLiteral("animation")));
        m_actorAttached = false;
    }
}

void AnimationAviWidget::updateSequenceSummary()
{
    const auto* sequence = currentSequence();
    if (!sequence || sequence->frames.isEmpty())
    {
        ui->sequenceSummaryLabel->setText(
            QStringLiteral("暂无可播放的序列结果"));
        return;
    }

    const QString axisName =
        sequence->axis == PostAnimationSequenceModel::Axis::Time
            ? QStringLiteral("时间序列")
            : QStringLiteral("阶段序列");
    const QString first = sequence->frames.constFirst().label;
    const QString last = sequence->frames.constLast().label;
    ui->sequenceSummaryLabel->setText(
        QStringLiteral("%1，共 %2 帧：%3 ～ %4")
            .arg(axisName)
            .arg(sequence->frames.size())
            .arg(first, last));
}

void AnimationAviWidget::updateFrameLabel()
{
    const auto* sequence = currentSequence();
    if (!sequence || m_currentFrame < 0 ||
        m_currentFrame >= sequence->frames.size())
    {
        ui->frameLabel->setText(QStringLiteral("—"));
        return;
    }

    QString text = QStringLiteral("%1 / %2　%3")
                       .arg(m_currentFrame + 1)
                       .arg(sequence->frames.size())
                       .arg(sequence->frames.at(m_currentFrame).label);
    if (m_subStep > 0)
    {
        text += QStringLiteral("　过渡 %1/%2")
                    .arg(m_subStep)
                    .arg(ui->interpCountSpin->value());
    }
    ui->frameLabel->setText(text);
}

void AnimationAviWidget::updateControls()
{
    const auto* sequence = currentSequence();
    const bool hasAnalyses = ui->analysisComboBox->count() > 0;
    const bool hasRanges = ui->rangeComboBox->count() > 0;
    const bool hasResults = ui->resultComboBox->count() > 0;
    const bool ready = sequence && sequence->frames.size() >= 2 && m_grid;

    ui->analysisComboBox->setEnabled(hasAnalyses);
    ui->rangeComboBox->setEnabled(hasRanges);
    ui->resultComboBox->setEnabled(hasResults);
    ui->scalarComboBox->setEnabled(ready &&
                                   ui->scalarComboBox->count() > 0);
    ui->frameSlider->setEnabled(ready);
    ui->playPauseButton->setEnabled(ready);
    ui->prevButton->setEnabled(ready);
    ui->nextButton->setEnabled(ready);
    ui->fpsSpin->setEnabled(ready);
    ui->loopCheckBox->setEnabled(ready);
    ui->fixRangeCheckBox->setEnabled(ready);

    const bool hasExcavatedVariant = currentSequenceHasExcavatedVariant();
    ui->showExcavatedCheckBox->setVisible(hasExcavatedVariant);
    ui->showExcavatedCheckBox->setEnabled(ready && hasExcavatedVariant);
    ui->showExcavatedCheckBox->setToolTip(
        hasExcavatedVariant
            ? QStringLiteral("显示当前阶段已经开挖掉的区域")
            : QString());

    const bool interpolationAvailable =
        ready && sequence->supportsInterpolation &&
        sequence->axis == PostAnimationSequenceModel::Axis::Time;
    ui->interpCheckBox->setEnabled(interpolationAvailable);
    ui->interpCheckBox->setToolTip(
        interpolationAvailable ? QString()
                               : QStringLiteral("当前结果不支持平滑过渡"));
    ui->interpCountSpin->setEnabled(interpolationAvailable &&
                                    ui->interpCheckBox->isChecked());
}

void AnimationAviWidget::setStatusText(const QString& text, bool error)
{
    Q_UNUSED(error);
    ui->statusLabel->setText(text);
}

void AnimationAviWidget::renderNow()
{
    if (m_visualService && m_visualService->renderWindow())
        m_visualService->renderWindow()->Render();
}

bool AnimationAviWidget::startTimer()
{
    const auto* sequence = currentSequence();
    if (!sequence || sequence->frames.size() < 2 || !m_grid)
    {
        setStatusText(QStringLiteral("当前没有可播放的帧"), true);
        return false;
    }

    if (m_currentFrame < 0 && !loadFrame(0))
    {
        setStatusText(QStringLiteral("当前帧读取失败"), true);
        return false;
    }

    if (!ui->loopCheckBox->isChecked() &&
        m_currentFrame >= sequence->frames.size() - 1)
    {
        if (!loadFrame(0))
            return false;
    }

    const int interval =
        qMax(16, qRound(1000.0 / qMax(0.5, ui->fpsSpin->value())));
    m_timer.start(interval);
    m_playing = true;
    ui->playPauseButton->setText(QStringLiteral("暂停"));
    setStatusText(QStringLiteral("正在播放"));
    return true;
}

void AnimationAviWidget::stopTimer()
{
    m_timer.stop();
    m_playing = false;
    if (ui)
        ui->playPauseButton->setText(QStringLiteral("播放"));
}

int AnimationAviWidget::nextFrameIndex(int direction) const
{
    const auto* sequence = currentSequence();
    if (!sequence || sequence->frames.isEmpty())
        return -1;

    int index = m_currentFrame + direction;
    if (index >= 0 && index < sequence->frames.size())
        return index;
    if (!ui->loopCheckBox->isChecked())
        return -1;
    return direction >= 0 ? 0 : sequence->frames.size() - 1;
}

bool AnimationAviWidget::advanceFrame()
{
    const auto* sequence = currentSequence();
    const int next = nextFrameIndex(1);
    if (!sequence || next < 0)
        return false;

    const bool wraps = next == 0 && m_currentFrame > 0;
    const int interpolationCount =
        sequence->supportsInterpolation &&
                sequence->axis == PostAnimationSequenceModel::Axis::Time &&
                ui->interpCheckBox->isChecked()
            ? ui->interpCountSpin->value()
            : 0;

    if (!wraps && interpolationCount > 0 &&
        m_subStep < interpolationCount)
    {
        const double factor =
            static_cast<double>(m_subStep + 1) /
            static_cast<double>(interpolationCount + 1);
        ++m_subStep;
        if (showInterpolatedFrame(next, factor))
            return true;
        m_subStep = interpolationCount;
    }

    return loadFrame(next);
}

bool AnimationAviWidget::canInterpolate(vtkUnstructuredGrid* first,
                                         vtkUnstructuredGrid* second) const
{
    if (!first || !second ||
        first->GetNumberOfPoints() != second->GetNumberOfPoints() ||
        first->GetNumberOfCells() != second->GetNumberOfCells())
        return false;

    for (vtkIdType cellId = 0; cellId < first->GetNumberOfCells(); ++cellId)
    {
        vtkCell* firstCell = first->GetCell(cellId);
        vtkCell* secondCell = second->GetCell(cellId);
        if (!firstCell || !secondCell ||
            firstCell->GetCellType() != secondCell->GetCellType() ||
            firstCell->GetNumberOfPoints() != secondCell->GetNumberOfPoints())
            return false;

        vtkIdList* firstIds = firstCell->GetPointIds();
        vtkIdList* secondIds = secondCell->GetPointIds();
        for (vtkIdType point = 0; point < firstIds->GetNumberOfIds(); ++point)
        {
            if (firstIds->GetId(point) != secondIds->GetId(point))
                return false;
        }
    }
    return true;
}

vtkSmartPointer<vtkUnstructuredGrid> AnimationAviWidget::interpolate(
    vtkUnstructuredGrid* first, vtkUnstructuredGrid* second,
    double factor) const
{
    if (!canInterpolate(first, second))
        return nullptr;

    vtkSmartPointer<vtkUnstructuredGrid> output =
        vtkSmartPointer<vtkUnstructuredGrid>::New();
    output->DeepCopy(first);

    vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(first->GetNumberOfPoints());
    double firstPoint[3];
    double secondPoint[3];
    double point[3];
    for (vtkIdType index = 0; index < first->GetNumberOfPoints(); ++index)
    {
        first->GetPoint(index, firstPoint);
        second->GetPoint(index, secondPoint);
        for (int component = 0; component < 3; ++component)
        {
            point[component] =
                firstPoint[component] +
                (secondPoint[component] - firstPoint[component]) * factor;
        }
        points->SetPoint(index, point);
    }
    output->SetPoints(points);

    interpolateDataAttributes(first->GetPointData(), second->GetPointData(),
                              output->GetPointData(), factor);
    interpolateDataAttributes(first->GetCellData(), second->GetCellData(),
                              output->GetCellData(), factor);
    return output;
}

bool AnimationAviWidget::showInterpolatedFrame(int nextIndex, double factor)
{
    const auto* sequence = currentSequence();
    if (!m_grid || !sequence || nextIndex < 0 ||
        nextIndex >= sequence->frames.size())
        return false;

    vtkSmartPointer<vtkUnstructuredGrid> nextGrid =
        readGrid(frameFilePath(sequence->frames.at(nextIndex)));
    vtkSmartPointer<vtkUnstructuredGrid> interpolatedGrid =
        interpolate(m_grid, nextGrid, factor);
    if (!interpolatedGrid)
        return false;

    applyScalar(interpolatedGrid, currentScalarName());
    attachActor();
    updateFrameLabel();
    renderNow();
    return true;
}

void AnimationAviWidget::on_showExcavatedCheckBox_toggled(bool checked)
{
    if (m_loadingState)
        return;

    const auto* sequence = currentSequence();
    if (!sequence || !currentSequenceHasExcavatedVariant())
    {
        const QSignalBlocker blocker(ui->showExcavatedCheckBox);
        ui->showExcavatedCheckBox->setChecked(false);
        return;
    }

    stopTimer();
    m_subStep = 0;
    m_rangeCache.clear();

    const int frame = qBound(0, m_currentFrame, sequence->frames.size() - 1);
    if (!loadFrame(frame, false))
    {
        setStatusText(QStringLiteral("显示范围切换失败"), true);
        return;
    }

    setStatusText(checked ? QStringLiteral("已显示已开挖区域")
                          : QStringLiteral("已切换为当前开挖状态"));
    updateControls();
}

void AnimationAviWidget::onTick()
{
    if (!m_playing)
        return;

    if (!advanceFrame())
    {
        stopTimer();
        setStatusText(QStringLiteral("播放结束"));
    }
}

void AnimationAviWidget::on_analysisComboBox_currentIndexChanged(int)
{
    if (m_loadingState)
        return;
    stopTimer();
    populateRangeCombo(QString());
    populateResultCombo(QString());
    activateSelectedSequence();
}

void AnimationAviWidget::on_rangeComboBox_currentIndexChanged(int)
{
    if (m_loadingState)
        return;
    stopTimer();
    populateResultCombo(QString());
    activateSelectedSequence();
}

void AnimationAviWidget::on_resultComboBox_currentIndexChanged(int)
{
    if (m_loadingState)
        return;
    activateSelectedSequence();
}

void AnimationAviWidget::on_scalarComboBox_currentIndexChanged(int)
{
    if (!m_grid || m_loadingState)
        return;

    m_subStep = 0;
    applyScalar(m_grid, currentScalarName());
    renderNow();
}

void AnimationAviWidget::on_playPauseButton_clicked()
{
    if (m_playing)
    {
        stopTimer();
        setStatusText(QStringLiteral("已暂停"));
        return;
    }

    if (!startTimer())
        return;

    // 先让按钮和状态文字完成刷新，再读取下一帧。这样即使结果文件较大，
    // 用户点击后也会立即看到“正在播放”，不会感觉按钮没有响应。
    m_timer.stop();
    QTimer::singleShot(0, this, [this]()
    {
        if (!m_playing)
            return;
        if (!advanceFrame())
        {
            stopTimer();
            setStatusText(QStringLiteral("无法读取下一帧"), true);
            return;
        }
        const int interval =
            qMax(16, qRound(1000.0 / qMax(0.5, ui->fpsSpin->value())));
        m_timer.start(interval);
    });
}

void AnimationAviWidget::on_prevButton_clicked()
{
    stopTimer();
    m_subStep = 0;
    const int previous = nextFrameIndex(-1);
    if (previous < 0)
        setStatusText(QStringLiteral("已经是第一帧"));
    else if (!loadFrame(previous))
        setStatusText(QStringLiteral("上一帧读取失败"), true);
    else
        setStatusText(QStringLiteral("已切换到上一帧"));
}

void AnimationAviWidget::on_nextButton_clicked()
{
    stopTimer();
    m_subStep = 0;
    const int next = nextFrameIndex(1);
    if (next < 0)
        setStatusText(QStringLiteral("已经是最后一帧"));
    else if (!loadFrame(next))
        setStatusText(QStringLiteral("下一帧读取失败"), true);
    else
        setStatusText(QStringLiteral("已切换到下一帧"));
}

void AnimationAviWidget::on_frameSlider_valueChanged(int value)
{
    if (m_loadingState)
        return;
    stopTimer();
    m_subStep = 0;
    if (!loadFrame(value))
        setStatusText(QStringLiteral("所选帧读取失败"), true);
    else
        setStatusText(QStringLiteral("已定位到所选帧"));
}

void AnimationAviWidget::on_fpsSpin_valueChanged(double)
{
    if (m_playing)
        startTimer();
}

void AnimationAviWidget::on_fixRangeCheckBox_toggled(bool)
{
    if (m_grid)
    {
        applyScalar(m_grid, currentScalarName());
        renderNow();
    }
}
