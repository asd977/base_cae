#pragma once

#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include "qdebug.h"

class CentroidPickingDialog : public QDialog
{
	Q_OBJECT
public:
	explicit CentroidPickingDialog(QWidget* parent = nullptr) : QDialog(parent)
	{
		// 设置对话框标题
		setWindowTitle("质心测量");
		setWindowIcon(QIcon(":/resources/icon.svg"));
		setWindowModality(Qt::WindowModality::NonModal);
		setMinimumSize(300, 50);
		// 创建布局和控件
		QVBoxLayout* v_layout = new QVBoxLayout(this);
		QHBoxLayout* layout = new QHBoxLayout(this);
		QHBoxLayout* layout2 = new QHBoxLayout(this);
		toggleButton = new QPushButton(this);
		calculateButton = new QPushButton(this);
		describeLabel = new QLabel("单击按钮计算当前选中点集的质心", this);
		statusLabel = new QLabel("单击按钮启用点拾取", this);
		layout->addWidget(toggleButton);
		layout->addWidget(statusLabel);
		layout2->addWidget(calculateButton);
		layout2->addWidget(describeLabel);
		v_layout->addLayout(layout);
		v_layout->addLayout(layout2);
		setLayout(v_layout);
		toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
		toggleButton->setFixedSize(50, 50);
		toggleButton->setIconSize(toggleButton->size());
		calculateButton->setEnabled(false);
		calculateButton->setIcon(QIcon(":resources/icon/centroid.png"));
		calculateButton->setFixedSize(50, 50);
		calculateButton->setIconSize(calculateButton->size());
		connect(toggleButton, &QPushButton::clicked, this, &CentroidPickingDialog::togglePicking);
		connect(this, &QDialog::rejected, this, &CentroidPickingDialog::onDialogClosed);
		connect(calculateButton, &QPushButton::clicked, this, &CentroidPickingDialog::onCalculateCentroid);
	}
	~CentroidPickingDialog()
	{
		qDebug() << "CentroidPickingDialog destroyed";
	}

signals:
	// 信号：点选模式切换（true 表示启用，false 表示禁用）
	void pickingToggled(bool enabled);
	// 请求计算质心
	void calculateCentroidRequested();

private slots:
	void togglePicking() {
		pickingEnabled = !pickingEnabled;
		if (pickingEnabled) 
		{
			// 启用状态：显示启用图标
			toggleButton->setIcon(QIcon(":resources/icon/switch-on.png"));
			statusLabel->setText("启用点拾取,在3D视图中单击点");
			calculateButton->setEnabled(true);
		}
		else {
			// 禁用状态：显示禁用图标
			toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			statusLabel->setText("点拾取已禁用");
			calculateButton->setEnabled(false);
		}
		emit pickingToggled(pickingEnabled);
	}
	void onCalculateCentroid() 
	{
		emit calculateCentroidRequested();
	}
	void onDialogClosed() {
		// 对话框关闭时禁用点选模式
		if (pickingEnabled) {
			pickingEnabled = false;
			toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			toggleButton->setIconSize(toggleButton->size());
			statusLabel->setText("点拾取已禁用");
			calculateButton->setEnabled(false);
			emit pickingToggled(false);
		}
	}

private:
	QPushButton* toggleButton;
	QLabel* statusLabel;
	QLabel* describeLabel;
	bool pickingEnabled = false;
	QPushButton* calculateButton;
};
