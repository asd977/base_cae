#pragma once

#include <BasePage.h>

#include "PostAnimationSequenceModel.h"

#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <vtkActor.h>
#include <vtkDataSetMapper.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

class VisualWidgetService;
class vtkDataArray;

namespace Ui
{
class AnimationAviWidget;
}

class AnimationAviWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit AnimationAviWidget(VisualWidgetService* postProcessingWidget,
                                QWidget* parent = nullptr);
    ~AnimationAviWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_rangeComboBox_currentIndexChanged(int index);
    void on_resultComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_playPauseButton_clicked();
    void on_prevButton_clicked();
    void on_nextButton_clicked();
    void on_frameSlider_valueChanged(int value);
    void on_fpsSpin_valueChanged(double value);
    void on_fixRangeCheckBox_toggled(bool checked);
    void on_showExcavatedCheckBox_toggled(bool checked);
    void onTick();

  private:
    void refreshModel();
    void populateAnalysisCombo(const QString& preferredKey);
    void populateRangeCombo(const QString& preferredKey);
    void populateResultCombo(const QString& preferredSequenceKey);
    void activateSelectedSequence();

    const PostAnimationSequenceModel::Sequence* currentSequence() const;
    QString frameFilePath(const PostAnimationSequenceModel::Frame& frame) const;
    bool currentSequenceHasExcavatedVariant() const;
    bool loadFrame(int index, bool updateScalarList = false);
    vtkSmartPointer<vtkUnstructuredGrid> readGrid(const QString& filePath);
    void clearGridCache();
    void rememberGrid(const QString& filePath,
                      const vtkSmartPointer<vtkUnstructuredGrid>& grid);
    void preloadNeighbourFrame();

    void fillScalarCombo(vtkUnstructuredGrid* grid, const QString& preferredScalar);
    QString currentScalarName() const;
    vtkDataArray* scalarArray(vtkUnstructuredGrid* grid, const QString& name,
                              bool* pointData = nullptr) const;
    bool computeGlobalRange(const QString& scalarName, double range[2]);
    void applyScalar(vtkUnstructuredGrid* grid, const QString& scalarName);

    void buildPipelineIfNeeded();
    void attachActor();
    void detachActor();
    void updateSequenceSummary();
    void updateFrameLabel();
    void updateControls();
    void setStatusText(const QString& text, bool error = false);
    void renderNow();

    bool startTimer();
    void stopTimer();
    int nextFrameIndex(int direction) const;
    bool advanceFrame();

    bool canInterpolate(vtkUnstructuredGrid* first,
                        vtkUnstructuredGrid* second) const;
    vtkSmartPointer<vtkUnstructuredGrid> interpolate(
        vtkUnstructuredGrid* first, vtkUnstructuredGrid* second,
        double factor) const;
    bool showInterpolatedFrame(int nextIndex, double factor);

    Ui::AnimationAviWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    PostAnimationSequenceModel m_model;
    int m_sequenceIndex{-1};
    int m_currentFrame{-1};
    int m_subStep{0};
    bool m_playing{false};
    bool m_actorAttached{false};
    bool m_loadingState{false};

    QString m_pendingAnalysisKey;
    QString m_pendingRangeKey;
    QString m_pendingSequenceKey;
    QString m_pendingScalar;
    bool m_pendingShowExcavated{false};
    QJsonObject m_pendingJson;

    QTimer m_timer;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    QHash<QString, QPair<double, double>> m_rangeCache;
    QHash<QString, vtkSmartPointer<vtkUnstructuredGrid>> m_gridCache;
    QStringList m_gridCacheOrder;
    int m_gridCacheLimit{4};
};
