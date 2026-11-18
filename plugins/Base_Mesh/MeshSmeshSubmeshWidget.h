#pragma once
#include <BasePage.h>
#include <SmeshWorkflowData.h>
#include <functional>
#include <QSet>
class QListWidget;
class QLineEdit;
class QDoubleSpinBox;
class QComboBox;
class QSpinBox;

// Adapted to this application's CAD service from TMSR's scoped Tet settings.
class MeshSmeshSubmeshWidget final : public BasePage {
    Q_OBJECT
public:
    explicit MeshSmeshSubmeshWidget(int dimension, QWidget* parent = nullptr);
    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject&) override;
    SmeshSubmeshSettings settings() const;
    void setDisplayName(const QString& name);
    void setAvailableEntities(std::function<QStringList(int)> provider) { m_available=std::move(provider); }
    // A solid claimed by a sweep is not an ordinary volume-meshing scope.
    void setReservedVolumeIds(std::function<QSet<int>()> provider) { m_reservedVolumes=std::move(provider); }
    void refreshEntities();
signals:
    void configurationChanged();
    void computeRequested(const QString& uuid);
private:
    void updateSelection();
    int m_dimension;
    QVector<int> m_selected;
    std::function<QStringList(int)> m_available;
    std::function<QSet<int>()> m_reservedVolumes;
    QListWidget* m_entities;
    QLineEdit* m_name;
    QDoubleSpinBox* m_max;
    QDoubleSpinBox* m_min;
    QComboBox* m_minMode;
    QComboBox* m_fineness = nullptr;
    QComboBox* m_optimize = nullptr;
    QComboBox* m_curvature = nullptr;
    QComboBox* m_scope = nullptr;
    QSpinBox* m_segments = nullptr;
    QDoubleSpinBox* m_bias = nullptr;
    QSpinBox* m_faceCurvature = nullptr;
};
