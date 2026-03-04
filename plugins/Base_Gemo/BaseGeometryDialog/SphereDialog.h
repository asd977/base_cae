#ifndef SPHEREDIALOG_H
#define SPHEREDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <gp_Pnt.hxx>

#include "BasePage.h"

class SphereDialog : public BasePage
{
    Q_OBJECT

  public:
    SphereDialog(QWidget* parent = nullptr);
    ~SphereDialog();

    void beforeLoad() {};
    void beforeUnload() {};
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
  private slots:
    void onOkClicked();

  private:
    QLineEdit* radiusEdit;
    QLineEdit* xEdit;
    QLineEdit* yEdit;
    QLineEdit* zEdit;
    void initUI();
};

#endif // SPHEREDIALOG_H
