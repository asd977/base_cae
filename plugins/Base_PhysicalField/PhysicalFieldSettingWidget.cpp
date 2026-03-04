#include "PhysicalFieldSettingWidget.h"

#include <ServiceRegistry.h>
#include <services/PhysicalFieldService.h>

#include "ui_PhysicalFieldSettingWidget.h"

PhysicalFieldSettingWidget::PhysicalFieldSettingWidget(QWidget *parent)
    : ui(new Ui::PhysicalFieldSettingWidget),
      BasePage("PhysicalFieldSettingWidget") {
  ui->setupUi(this);
  on_pushButton_2_clicked();
}

PhysicalFieldSettingWidget::~PhysicalFieldSettingWidget() { delete ui; }

void PhysicalFieldSettingWidget::beforeLoad() {}

void PhysicalFieldSettingWidget::beforeUnload() {}

void PhysicalFieldSettingWidget::on_testPushButton_clicked() {
  PhysicalFieldService *service =
      ServiceRegistry::instance().getService<PhysicalFieldService>();
  service->generateIIIDFile();
}

void PhysicalFieldSettingWidget::on_pushButton_clicked() {
  PhysicalFieldService *service =
      ServiceRegistry::instance().getService<PhysicalFieldService>();
  service->generateIIWLFile();
}

void PhysicalFieldSettingWidget::on_pushButton_2_clicked() {
  // --- 1. 坐标定义 (科学计数法) ---
  // 点 1
  double x1 = 0.0, y1 = 25.0, z1 = 77.0;
  // 点 2
  double x2 = 0.0, y2 = 26.7502, z2 = 78.2;
  // 点 3
  double x3 = 0.0, y3 = 26.6981, z3 = 78.4858;
  // 点 4
  double x4 = 0.0, y4 = 25.0, z4 = 78.2;

  // --- 2. 距离计算函数 (Lambda 表达式，方便复用) ---
  auto getDist = [](double ax, double ay, double az, double bx, double by,
                    double bz) {
    double dx = bx - ax;
    double dy = by - ay;
    double dz = bz - az;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };

  // --- 3. 执行要求的计算 ---

  // 计算 点2 到 点1 的距离
  double dist2_1 = getDist(x2, y2, z2, x1, y1, z1);

  // 计算 点3 到 点1 的距离
  double dist3_1 = getDist(x3, y3, z3, x1, y1, z1);

  // 计算 点3 到 点4 的距离
  double dist3_4 = getDist(x3, y3, z3, x4, y4, z4);

  // 计算 点3 到 点4 的距离
  double dist2_4 = getDist(x2, y2, z2, x4, y4, z4);

  // --- 4. 打印输出 ---
  qDebug() << "========================================";
  qDebug() << "Distance (Point 2 to 1):" << dist2_1;  // 结果约为 2.119
  qDebug() << "Distance (Point 3 to 1):" << dist3_1;  // 结果约为 2.476
  qDebug() << "Distance (Point 3 to 4):" << dist3_4;  // 结果约为 1.722
  qDebug() << "Distance (Point 2 to 4):" << dist2_4;
  qDebug() << "========================================";
}
