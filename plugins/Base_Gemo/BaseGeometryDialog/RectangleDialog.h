#pragma once

#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class RectangleDialog : public BasePage {
  Q_OBJECT
 public:
  explicit RectangleDialog(QWidget* parent = nullptr);

  double getLength() const;
  double getWidth() const;
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
  QLineEdit* lengthEdit = nullptr;
  QLineEdit* widthEdit = nullptr;

  QLabel* centerXLabel = nullptr;
  QLabel* centerYLabel = nullptr;
  QLabel* centerZLabel = nullptr;

  QLineEdit* centerXEdit = nullptr;
  QLineEdit* centerYEdit = nullptr;
  QLineEdit* centerZEdit = nullptr;

  QLabel* planeLabel = nullptr;
  QComboBox* planeCombo = nullptr;

  double length = 2.0;
  double width = 1.0;
  gp_Pnt center;
};
