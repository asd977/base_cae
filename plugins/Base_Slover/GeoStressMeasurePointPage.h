#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class GeoStressMeasurePointPage : public BasePage
{
    Q_OBJECT
  public:
    struct MeasurePoint
    {
        QString x;
        QString y;
        QString z;
        QString sgx;
        QString sgy;
        QString sgz;
        QString txy;
        QString tyz;
        QString tzx;
    };

    explicit GeoStressMeasurePointPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    MeasurePoint point() const;

  private slots:
    void onClearClicked();
    void onImportClicked();
    void onNodePicked(const QString& domain, const QString& activeUuid, GridKind kind,
                      vtkIdType pid, int nodeId, double x, double y, double z,
                      const QString& prevUuid);

  private:
    int currentDimension() const;
    void applyDimensionUi();
    bool applyImportedValues(const QStringList& values);
    void setCoordinateValues(double x, double y, double z);

  private:
    QLineEdit* m_pointIdEdit = nullptr;
    QLineEdit* m_xEdit = nullptr;
    QLineEdit* m_yEdit = nullptr;
    QLineEdit* m_zEdit = nullptr;
    QLineEdit* m_sgxEdit = nullptr;
    QLineEdit* m_sgyEdit = nullptr;
    QLineEdit* m_sgzEdit = nullptr;
    QLineEdit* m_txyEdit = nullptr;
    QLineEdit* m_tyzEdit = nullptr;
    QLineEdit* m_tzxEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    QCheckBox* m_pickEnableCheckBox = nullptr;
    QLabel* m_zLabel = nullptr;
    QLabel* m_sgzLabel = nullptr;
    QLabel* m_tyzLabel = nullptr;
    QLabel* m_tzxLabel = nullptr;
};
