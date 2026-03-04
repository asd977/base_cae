#pragma once

#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class CircleDialog : public BasePage {
  Q_OBJECT
 public:
  explicit CircleDialog(QWidget* parent = nullptr);

  double getRadius() const;
  gp_Pnt getCenter() const;

  QJsonObject saveJson() override;
  void loadJson(const QJsonObject& state) override;
  void beforeLoad();
  void beforeUnload();

 private slots:
  void validateInput();
  void onPlaneChanged(int);

 private:
  enum class PlaneType { XY = 0, XZ = 1, YZ = 2 };

  PlaneType currentPlane() const;
  void updatePlaneUi();
  gp_Dir planeNormal(PlaneType p) const;

 private:
  QLineEdit* radiusEdit = nullptr;

  QLabel* centerXLabel = nullptr;
  QLabel* centerYLabel = nullptr;
  QLabel* centerZLabel = nullptr;

  QLineEdit* centerXEdit = nullptr;
  QLineEdit* centerYEdit = nullptr;
  QLineEdit* centerZEdit = nullptr;

  QLabel* planeLabel = nullptr;
  QComboBox* planeCombo = nullptr;

  double radius = 1.0;
  gp_Pnt center;
};
