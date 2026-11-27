#pragma once

#include <BasePage.h>
#include <QString>
#include <QVariantMap>
#include "core/MeshControlSelectionPresentation.h"

class QListWidget;
class QLineEdit;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QSpinBox;

// A user-managed selection of Tet volumes in the SMESH operation sequence.
// The computed mesh belongs to the shared parent, not to this UI node.
class MeshSmeshTetWidget final : public BasePage
{
    Q_OBJECT
public:
    explicit MeshSmeshTetWidget(QWidget* parent = nullptr);
    ~MeshSmeshTetWidget() override;
    void setOperation(const QString& planUuid, const QString& taskUuid);
    void beforeLoad() override;
    void beforeUnload() override;
    bool commitSettings();
private:
    void reload();
    bool save(bool interactive = true);
    void queueCommit();
    QVariantMap occupiedDomains() const;
    void previewSelected();
    void generateSelected();
    void selectUnassigned();
    void selectVisible(bool checked);
    void updateSurfaceSizingHint();
    void syncSelectionHighlight();
    MeshControlSelectionPresentation m_selectionPresentation;
    QListWidget* m_list = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_surfaceSizingHint = nullptr;
    QComboBox* m_scopeMode = nullptr;
    QComboBox* m_method = nullptr;
    QComboBox* m_density = nullptr;
    QComboBox* m_sizingScope = nullptr;
    QDoubleSpinBox* m_targetSize = nullptr;
    QComboBox* m_fineness = nullptr;
    QCheckBox* m_showAdvanced = nullptr;
    QCheckBox* m_curvature = nullptr;
    QComboBox* m_chordalMode = nullptr;
    QDoubleSpinBox* m_chordalError = nullptr;
    QLabel* m_chordalHint = nullptr;
    QCheckBox* m_optimize = nullptr;
    QComboBox* m_minMode = nullptr;
    QDoubleSpinBox* m_minSize = nullptr;
    QDoubleSpinBox* m_growthRate = nullptr;
    QDoubleSpinBox* m_segPerEdge = nullptr;
    QDoubleSpinBox* m_segPerRadius = nullptr;
    QSpinBox* m_surfaceLimit = nullptr;
    double m_presetBaseSize = 5.0; // Baseline of THIS SubMesh, not global Mesh.
    bool m_reloading = false;
    bool m_commitQueued = false;
    QString m_planUuid;
    QString m_taskUuid;
};
