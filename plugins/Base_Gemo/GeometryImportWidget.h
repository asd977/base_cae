#pragma once
#ifndef GEOMETRYIMPORTWIDGET_H
#define GEOMETRYIMPORTWIDGET_H

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

#include "BasePage.h"

class GeometryImportWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit GeometryImportWidget(QWidget* parent = nullptr);

    void beforeLoad() {};
    void beforeUnload() {};
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString getFilePath() const; // 获取当前路径
    void setFilePath(const QString& path);
  signals:
    void fileImported(const QString& filePath,
                      const QString& uuid); // 信号，携带文件路径
  private slots:
    void onBrowseButtonClicked(); // 浏览按钮槽函数
    void onImportButtonClicked(); // 导入按钮槽函数

  private:
    QLabel* pathLabel;
    QLineEdit* pathLineEdit;
    QPushButton* browseButton;
    QPushButton* importButton;
};

#endif // GEOMETRYIMPORTWIDGET_H
