#pragma once

#include <BasePage.h>
#include <QJsonObject>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>


namespace Ui
{
class BoxDialog;
class SphereDialog;
class ConeDialog;
class CylinderDialog;
class TorusDialog;
class CircleDialog;
class EllipseDialog;
class RectangleDialog;
class PolygonDialog;
}

class BoxDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit BoxDialog(QWidget* parent = nullptr);
    ~BoxDialog() override;
    void beforeLoad() override {}
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    gp_Pnt getPosition() const;
  private slots:
    void onOkClicked();
  private:
    Ui::BoxDialog* ui = nullptr;
};

class SphereDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit SphereDialog(QWidget* parent = nullptr);
    ~SphereDialog() override;
    void beforeLoad() override {}
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
  private slots:
    void onOkClicked();
  private:
    Ui::SphereDialog* ui = nullptr;
};

class ConeDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit ConeDialog(QWidget* parent = nullptr);
    ~ConeDialog() override;
    void beforeLoad() override {}
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    double getBottomRadius() const;
    double getTopRadius() const;
    double getHeight() const;
    gp_Pnt getBaseCenter() const;
  private slots:
    void onOkClicked();
    void onCancelClicked();
  private:
    Ui::ConeDialog* ui = nullptr;
};

class CylinderDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit CylinderDialog(QWidget* parent = nullptr);
    ~CylinderDialog() override;
    void beforeLoad() override {}
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    double getRadius() const;
    double getHeight() const;
    gp_Pnt getBaseCenter() const;
  signals:
    void parametersSubmitted(QString uuid, double radius, double height, const gp_Pnt& position);
  private slots:
    void onOkClicked();
    void onCancelClicked();
  private:
    Ui::CylinderDialog* ui = nullptr;
};

class TorusDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit TorusDialog(QWidget* parent = nullptr);
    ~TorusDialog() override;
    void beforeLoad() override {}
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    double getMajorRadius() const;
    double getMinorRadius() const;
    gp_Pnt getCenter() const;
  signals:
    void parametersSubmitted(QString uuid, double majorRadius, double minorRadius,
                             const gp_Pnt& position);
  private slots:
    void onOkClicked();
  private:
    Ui::TorusDialog* ui = nullptr;
};

class CircleDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit CircleDialog(QWidget* parent = nullptr);
    ~CircleDialog() override;
    double getRadius() const;
    gp_Pnt getCenter() const;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override;
    void beforeUnload() override;
  private slots:
    void validateInput();
    void onPlaneChanged(int);
  private:
    enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };
    PlaneType currentPlane() const;
    void updatePlaneUi();
    gp_Dir planeNormal(PlaneType p) const;
    Ui::CircleDialog* ui = nullptr;
    double radius = 1.0;
    gp_Pnt center;
};

class EllipseDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit EllipseDialog(QWidget* parent = nullptr);
    ~EllipseDialog() override;
    double getMajorRadius() const;
    double getMinorRadius() const;
    gp_Pnt getCenter() const;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void validateInput();
    void onPlaneChanged(int);
  private:
    enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };
    PlaneType currentPlane() const;
    gp_Dir planeNormal(PlaneType p) const;
    void updatePlaneUi();
    Ui::EllipseDialog* ui = nullptr;
    double majorRadius = 2.0;
    double minorRadius = 1.0;
    gp_Pnt center;
};

class RectangleDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit RectangleDialog(QWidget* parent = nullptr);
    ~RectangleDialog() override;
    double getLength() const;
    double getWidth() const;
    gp_Pnt getCenter() const;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void validateInput();
    void onPlaneChanged(int);
  private:
    enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };
    PlaneType currentPlane() const;
    gp_Dir planeNormal(PlaneType p) const;
    void updatePlaneUi();
    Ui::RectangleDialog* ui = nullptr;
    double length = 2.0;
    double width = 1.0;
    gp_Pnt center;
};

class PolygonDialog : public BasePage
{
    Q_OBJECT
  public:
    explicit PolygonDialog(QWidget* parent = nullptr);
    ~PolygonDialog() override;
    double getRadius() const;
    int getSides() const;
    gp_Pnt getCenter() const;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override {}
    void beforeUnload() override {}
  private slots:
    void validateInput();
    void onPlaneChanged(int);
  private:
    enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };
    PlaneType currentPlane() const;
    gp_Dir planeNormal(PlaneType p) const;
    void updatePlaneUi();
    Ui::PolygonDialog* ui = nullptr;
    double radius = 1.0;
    int sides = 5;
    gp_Pnt center;
};
