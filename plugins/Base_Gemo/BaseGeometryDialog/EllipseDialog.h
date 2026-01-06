#pragma once

#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QJsonObject>

#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

#include "BasePage.h"

class EllipseDialog : public BasePage
{
    Q_OBJECT
public:
    explicit EllipseDialog(QWidget* parent = nullptr);

    double getMajorRadius() const;
    double getMinorRadius() const;
    gp_Pnt getCenter() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() {};
    void beforeUnload() {};

private slots:
    void validateInput();
    void onPlaneChanged(int);

private:
    enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };

    PlaneType currentPlane() const;
    gp_Dir planeNormal(PlaneType p) const;
    void updatePlaneUi();

private:
    QLineEdit* majorRadiusEdit = nullptr;
    QLineEdit* minorRadiusEdit = nullptr;

    QLabel* centerXLabel = nullptr;
    QLabel* centerYLabel = nullptr;
    QLabel* centerZLabel = nullptr;

    QLineEdit* centerXEdit = nullptr;
    QLineEdit* centerYEdit = nullptr;
    QLineEdit* centerZEdit = nullptr;

    QLabel*    planeLabel = nullptr;
    QComboBox* planeCombo = nullptr;

    double majorRadius = 2.0;
    double minorRadius = 1.0;
    gp_Pnt center;
};
