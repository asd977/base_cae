#pragma once

#include <BasePage.h>
#include <PickManager.h>

namespace Ui
{
class GeoStressSideFacePage;
}

class QTimer;

class GeoStressSideFacePage : public BasePage
{
    Q_OBJECT
  public:
    struct FaceAttr
    {
        QString idir;
        QString height;
        QString gama;
        QString pgrout;
        QString ii;
    };

    explicit GeoStressSideFacePage(QWidget* parent = nullptr);
    ~GeoStressSideFacePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString faceFile() const;
    FaceAttr attr() const;
    int selectionCount() const;
    bool validatePressure(int verticalAxis, QString* errorMessage = nullptr) const;
    bool exportDatFile() const;
    void setFaceIndex(int index);

  private slots:
    void onClearClicked();
    void onBatchAddClicked();
    void onGroupHovered(int row);

  private:
    QString pickDomain() const;
    PickMode currentPickMode() const;
    void refreshGroupCombo();
    void schedulePressurePreview();
    void updatePressurePreview();
    void clearPressurePreview(bool renderNow = true);
    void restoreSelectedBoundaryHighlight();
    void updateHeightSliderRange();
    void syncHeightSliderFromSpinBox();
    void setPreviewStatus(const QString& text, bool warning = false);

  private:
    Ui::GeoStressSideFacePage* ui = nullptr;
    int m_faceIndex = 1;
    QTimer* m_pressurePreviewTimer = nullptr;
    bool m_pressurePreviewActive = false;
    bool m_updatingHeightSlider = false;
    double m_heightSliderMinimum = 0.0;
    double m_heightSliderMaximum = 1.0;
};
