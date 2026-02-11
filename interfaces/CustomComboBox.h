#ifndef CUSTOMCOMBOBOX_H
#define CUSTOMCOMBOBOX_H

#include <QMouseEvent>
#include <QDebug>

#include <qcombobox.h>
#include <qlistview.h>
#include <core_global.h>

class CORE_EXPORT CustomComboBox : public QComboBox
{
    Q_OBJECT

public:
    CustomComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        // 设置自定义的视图
        setView(new QListView(this));
        connect(view(), &QListView::entered, this, &CustomComboBox::onItemHovered);
    }

signals:
    void itemHovered(const int &itemText); // 定义信号
    void dropdownOpened();  // 下拉框打开信号
    void dropdownClosed();  // 下拉框关闭信号

protected:
    void showPopup() override {
        emit dropdownOpened(); // 发射打开信号
        QComboBox::showPopup(); // 调用基类的 showPopup 方法
    }

    void hidePopup() override {
        emit dropdownClosed(); // 发射关闭信号
        QComboBox::hidePopup(); // 调用基类的 hidePopup 方法
    }

private slots:
    void onItemHovered(const QModelIndex &index) {
        if (index.isValid()) {
            emit itemHovered(index.row()); // 发射信号
        }
    }
};

#endif // CUSTOMCOMBOBOX_H
