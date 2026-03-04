#ifndef MATERIALLIBRARYWIDGET_H
#define MATERIALLIBRARYWIDGET_H

#include <QDialog>
#include <QTableWidgetItem>

#include "MaterialLibraryData.h"

class MaterialManager;

namespace Ui {
class MaterialLibraryWidget;
}

class MaterialLibraryWidget : public QDialog {
  Q_OBJECT

 public:
  explicit MaterialLibraryWidget(MaterialManager *manager,
                                 QWidget *parent = nullptr);
  ~MaterialLibraryWidget();

  void addMaterialAndFocus();
  void refreshModels();
  void setCurrentModelKey(const QString &key);

 protected:
  bool event(QEvent *event) override;

 private slots:
  void on_modelComboBox_currentIndexChanged(int index);
  void on_addButton_clicked();
  void on_removeButton_clicked();
  void on_closeButton_clicked();
  void onItemChanged(QTableWidgetItem *item);

 private:
  void loadModel(const QString &key);
  void populateTable(MaterialModel *model);
  void syncRowToModel(MaterialModel *model, int row, int column,
                      const QString &value);

 private:
  Ui::MaterialLibraryWidget *ui;
  MaterialManager *m_materialManager;
  bool m_isUpdating;
  QString m_currentKey;
};

#endif  // MATERIALLIBRARYWIDGET_H
