#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include "qdebug.h"

class PointPickingDialog : public QDialog
{
	Q_OBJECT
public:
	explicit PointPickingDialog(QWidget* parent = nullptr) : QDialog(parent) 
	{
		// 设置对话框标题
		setWindowTitle("位置测量");
		setWindowIcon(QIcon(":/resources/icon.svg"));
		setWindowModality(Qt::WindowModality::NonModal);
		setMinimumSize(300, 50);
		// 创建布局和控件
		QHBoxLayout* layout = new QHBoxLayout(this);
		toggleButton = new QPushButton(this);
		statusLabel = new QLabel("单击按钮启用点拾取", this);
		layout->addWidget(toggleButton);
		layout->addWidget(statusLabel);
		setLayout(layout);
		toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
		toggleButton->setFixedSize(50, 50);
		toggleButton->setIconSize(toggleButton->size());
		toggleButton->setStyleSheet("QPushButton { border: none; padding: 0px; }");
		connect(toggleButton, &QPushButton::clicked, this, &PointPickingDialog::togglePicking);
		connect(this, &QDialog::rejected, this, &PointPickingDialog::onDialogClosed);
	}
	~PointPickingDialog()
	{
		qDebug() << "PointPickingDialog destroyed";
	}

signals:
	// 信号：点选模式切换（true 表示启用，false 表示禁用）
	void pickingToggled(bool enabled);

private slots:
	void togglePicking() {
		pickingEnabled = !pickingEnabled;
		if (pickingEnabled) {
			// 启用状态：显示启用图标
			toggleButton->setIcon(QIcon(":resources/icon/switch-on.png"));
			statusLabel->setText("启用点拾取,在3D视图中单击点");
		}
		else {
			// 禁用状态：显示禁用图标
			toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			statusLabel->setText("点拾取已禁用");
		}
		emit pickingToggled(pickingEnabled);
	}
	void onDialogClosed() {
		// 对话框关闭时禁用点选模式
		if (pickingEnabled) {
			pickingEnabled = false;
			toggleButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			toggleButton->setIconSize(toggleButton->size());
			statusLabel->setText("点拾取已禁用");
			emit pickingToggled(false);
		}
	}

private:
	QPushButton* toggleButton;
	QLabel* statusLabel;
	bool pickingEnabled = false;
};