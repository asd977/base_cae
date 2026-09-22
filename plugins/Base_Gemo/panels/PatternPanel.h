#pragma once

#include <BasePage.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <CadTypes.h>

namespace Ui
{
class PatternDialog;
}

class QGroupBox;
class QWidget;

class PatternDialog : public BasePage
{
    Q_OBJECT

public:
    explicit PatternDialog(QWidget* parent = nullptr);
    ~PatternDialog() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialSource(const QString& uuid);

private slots:
    void onApplyClicked();
    void onCancelClicked();

private:
    void refreshSources();
    void updateStatus(const QString& text, bool ok);
    void syncSourceHighlight();
    void reverseDirection(int directionIndex);
    void reversePolarDirection();
    void updateDirectionUi(int directionIndex);
    void updatePatternModeUi();
    void updatePolarAxisUi();
    void updatePreview();
    void clearPreview(bool renderNow = true);
    bool validateDirections(PatternDirection directions[3], qint64* totalCount = nullptr);
    bool validatePolar(PolarPatternParameters& parameters) const;
    PatternDirection directionValue(int directionIndex) const;
    void setDirectionValue(int directionIndex, const PatternDirection& direction);
    PolarPatternParameters polarValue() const;
    void setPolarValue(const PolarPatternParameters& parameters, int axisMode);
    bool isPolarMode() const;
    QGroupBox* directionGroup(int index) const;
    QComboBox* axisCombo(int index) const;
    QSpinBox* countSpin(int index) const;
    QDoubleSpinBox* spacingSpin(int index) const;
    QDoubleSpinBox* stepSpin(int directionIndex, int component) const;
    QWidget* customRow(int index) const;
    QPushButton* reverseButton(int index) const;
    QDoubleSpinBox* polarOriginSpin(int component) const;
    QDoubleSpinBox* polarAxisSpin(int component) const;

private:
    Ui::PatternDialog* ui = nullptr;
    QString m_sourceUuid;
    QJsonObject m_committedState;
};
