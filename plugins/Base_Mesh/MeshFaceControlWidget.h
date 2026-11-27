#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>
#include <services/MeshModuleControllerService.h>
#include "core/MeshControlSelectionPresentation.h"

#include <QMap>
#include <QStringList>
#include <QVector>
#include <QEvent>
#include <QVariant>

class vtkActor;

class QPushButton;
class QLabel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QStackedWidget;

namespace Ui
{
class MeshFaceControlWidget;
}

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit MeshFaceControlWidget(QWidget* parent = nullptr);
    ~MeshFaceControlWidget() override;
    void beforeLoad() override;
    void beforeUnload() override;
    void setOwnerTask(const QString& kind, const QString& uuid, const QString& displayName);

  protected:
    bool event(QEvent* ev) override;

  private slots:
    void onAddControl();
    void onDeleteControl();
    void onResetForm();
    void onSelectionChanged();
    void onParameterEdited();

    void onAppendSelectedEntity();
    bool appendCadFace(int id);

  private:
    struct FaceControlItem
    {
        QString faceIds;
        QString partitionFaceRefs;
        double localSizeFactor = 0.5; // A new face rule refines only the picked face.
        bool curvatureRefine = false; // Curvature is explicit opt-in, never silently adds 20 segments.
        int curvatureElementsPer2Pi = 20;
        int minimumSizeMode = 0; // Native automatic face minimum; independent of the body's manual minimum.
        double manualMinimumSize = 0.0;
    };

    void setupUiState();
    bool validateInput(QString& err, int ignoreRow = -1) const;
    FaceControlItem collectFormData() const;
    void fillFormData(const FaceControlItem& item);
    void appendOrUpdateTableRow(int row, const FaceControlItem& item);
    void refreshButtons();
    void updateEffectiveSizeHint();
    double parentEffectiveMaxSize() const;
    void syncAbsoluteFaceSize();

    void rebuildFaceCandidates();
    void clearHighlight();
    void updateEntityUiState();
    void syncRulesToService();
    void loadRulesFromService();
    void updateBackendSpecificUi();
    QObject* geometryBridgeObject() const;
    QVariantList partitionPlanes() const;
    void beginPartitionFacePick();
    void finishPartitionFacePick();
    void clearPartitionFaceRefs();
    void showPartitionFaceSelection();
    void updatePartitionRefLabel();
    void togglePartitionFaceRef(int region, int face);
    static QString normalizedPartitionRefs(const QString& text);

  private:
    Ui::MeshFaceControlWidget* ui;
    QVector<FaceControlItem> m_items;
    QStringList m_entityIds;
    QString m_highlightPropId;
    MeshControlSelectionPresentation m_selectionPresentation;
    QString m_ownerTaskKind;
    QString m_ownerTaskUuid;
    QString m_ownerTaskName;
    bool m_smeshBackend = true;
    enum PartitionPickStage { PickPartitionNone = 0, PickCadFace, PickPartitionRegion, PickPartitionFace };
    PartitionPickStage m_partitionPickStage = PickPartitionNone;
    int m_partitionPickRegion = -1;
    QLabel* m_partitionPickCaption = nullptr;
    QPushButton* m_pickPartitionFace = nullptr;
    QPushButton* m_clearPartitionFaces = nullptr;
    QLabel* m_partitionRefLabel = nullptr;
    QComboBox* m_minimumMode = nullptr;
    QDoubleSpinBox* m_manualMinimum = nullptr;
    QLabel* m_minimumLabel = nullptr;
    QLabel* m_manualMinimumLabel = nullptr;
    QLabel* m_effectiveSizeHint = nullptr;
    QComboBox* m_sizePreset = nullptr;
    QLabel* m_sizePresetLabel = nullptr;
    QDoubleSpinBox* m_absoluteFaceSize = nullptr;
    QStackedWidget* m_sizeModeStack = nullptr;
    QCheckBox* m_advancedFaceOptions = nullptr;
};

#endif // MESHFACECONTROLWIDGET_H
