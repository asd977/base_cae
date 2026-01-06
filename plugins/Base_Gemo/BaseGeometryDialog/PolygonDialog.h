#pragma once

#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QJsonObject>

#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

#include "BasePage.h"

class PolygonDialog : public BasePage
{
    Q_OBJECT
public:
    explicit PolygonDialog(QWidget* parent = nullptr);

    double getRadius() const;
    int    getSides() const;
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
    QLabel*    planeLabel = nullptr;
    QComboBox* planeCombo = nullptr;

    QLineEdit* radiusEdit = nullptr;
    QLineEdit* sidesEdit  = nullptr;

    QLineEdit* rotateDegEdit = nullptr; // 旋转角(°)

    QLabel*    centerXLabel = nullptr;
    QLabel*    centerYLabel = nullptr;
    QLabel*    centerZLabel = nullptr;
    QLineEdit* centerXEdit  = nullptr;
    QLineEdit* centerYEdit  = nullptr;
    QLineEdit* centerZEdit  = nullptr;

    double radius = 1.0;
    int    sides  = 5;
    gp_Pnt center;
};
