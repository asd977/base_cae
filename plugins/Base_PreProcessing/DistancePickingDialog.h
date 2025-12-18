#pragma once
#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QComboBox>
#include "qdebug.h"

class DistancePickingDialog : public QDialog
{
	Q_OBJECT
public:
	explicit DistancePickingDialog(QWidget* parent = nullptr) : QDialog(parent)
	{
		// 设置对话框标题
		setWindowTitle("距离测量");
		setWindowIcon(QIcon(":/resources/icon.svg"));
		setWindowModality(Qt::WindowModality::NonModal);
		setMinimumSize(450, 70);

		// 创建布局和控件
		QVBoxLayout* v_layout = new QVBoxLayout(this);
		QHBoxLayout* layout1 = new QHBoxLayout(this);
		QHBoxLayout* layout2 = new QHBoxLayout(this);
		QHBoxLayout* layout3 = new QHBoxLayout(this);
		QHBoxLayout* layout4 = new QHBoxLayout(this);

		selectPointButton = new QPushButton(this);
		selectPointOrLineButton = new QPushButton(this);
		calculateDistanceButton = new QPushButton(this);
		comboBox = new QComboBox(this);
		statusLabel = new QLabel("请选择测量模式", this);
		describeLabel = new QLabel("选择点或点/线以测量距离", this);
		distanceDescriptionLabel = new QLabel("点击计算距离以获取结果", this);

		// 添加控件到布局
		layout1->addWidget(selectPointButton);
		layout1->addWidget(statusLabel);
		layout2->addWidget(selectPointOrLineButton);
		layout2->addWidget(comboBox);
		layout3->addWidget(calculateDistanceButton);
		layout3->addWidget(distanceDescriptionLabel);
		layout4->addWidget(describeLabel);
		v_layout->addLayout(layout1);
		v_layout->addLayout(layout2);
		v_layout->addLayout(layout3);
		v_layout->addLayout(layout4);
		setLayout(v_layout);

		// 配置按钮和下拉框
		selectPointButton->setIcon(QIcon(":resources/icon/switch-off.png"));
		selectPointButton->setFixedSize(50, 50);
		selectPointButton->setIconSize(selectPointButton->size());

		selectPointOrLineButton->setIcon(QIcon(":resources/icon/switch-off.png"));
		selectPointOrLineButton->setFixedSize(50, 50);
		selectPointOrLineButton->setIconSize(selectPointOrLineButton->size());

		calculateDistanceButton->setIcon(QIcon(":resources/icon/ruler.png"));
		calculateDistanceButton->setFixedSize(50, 50);
		calculateDistanceButton->setIconSize(calculateDistanceButton->size());
		calculateDistanceButton->setEnabled(false);

		comboBox->addItems(QStringList() << "点" << "线");
		comboBox->setEnabled(true);

		// 初始状态：仅启用第一个按钮
		selectPointOrLineButton->setChecked(false);
		selectPointButton->setChecked(false);
		// 信号与槽连接
		connect(selectPointButton, &QPushButton::clicked, this, &DistancePickingDialog::togglePointSelection);
		connect(selectPointOrLineButton, &QPushButton::clicked, this, &DistancePickingDialog::togglePointOrLineSelection);
		connect(this, &QDialog::rejected, this, &DistancePickingDialog::onDialogClosed);
		connect(calculateDistanceButton, &QPushButton::clicked, this, &DistancePickingDialog::onCalculateDistance);
		connect(comboBox, &QComboBox::currentTextChanged, this, &DistancePickingDialog::onComboBoxChanged);
	}

	~DistancePickingDialog()
	{
		qDebug() << "DistancePickingDialog destroyed";
	}

signals:
	// 信号：选择点模式切换（true 表示启用，false 表示禁用）
	void pointSelectionToggled(bool enabled);
	// 信号：选择点或线模式切换（true 表示启用，false 表示禁用，带类型）
	void pointOrLineSelectionToggled(bool enabled, QString type);
	// 信号：请求计算距离
	void calculateDistanceRequested();

private slots:
	void togglePointSelection() {
		if (pointSelectionEnabled) 
		{
			pointSelectionEnabled = false;
			pointOrLineSelectionEnabled = false;
			selectPointButton->setChecked(false);
			selectPointButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			selectPointOrLineButton->setChecked(false);
			comboBox->setEnabled(false);
			calculateDistanceButton->setEnabled(false);
			statusLabel->setText("测量模式已关闭");
			describeLabel->setText("选择点或点/线以测量距离");
			emit pointSelectionToggled(false);
			//emit pointOrLineSelectionToggled(false, "");
		}
		else {
			pointSelectionEnabled = true;
			pointOrLineSelectionEnabled = false;
			selectPointButton->setChecked(true);
			selectPointButton->setIcon(QIcon(":resources/icon/switch-on.png"));
			calculateDistanceButton->setEnabled(true);
			selectPointOrLineButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			selectPointOrLineButton->setChecked(false);
			comboBox->setEnabled(false);
			statusLabel->setText("启用点选择模式，在3D视图中选择第一个顶点");
			emit pointOrLineSelectionToggled(false, "");
			emit pointSelectionToggled(true);
		}
	}

	void togglePointOrLineSelection() {
		if (pointOrLineSelectionEnabled) 
		{
			pointSelectionEnabled = false;
			pointOrLineSelectionEnabled = false;
			selectPointButton->setChecked(false);
			selectPointOrLineButton->setChecked(false);
			selectPointOrLineButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			comboBox->setEnabled(true);
			calculateDistanceButton->setEnabled(false);
			statusLabel->setText("测量模式已关闭");
			describeLabel->setText("选择点或点/线以测量距离");
			describeLabel->setText("当前测量点" + comboBox->currentText() + "之间的距离");
			//emit pointSelectionToggled(false);
			emit pointOrLineSelectionToggled(false, "");
		}
		else {
			pointSelectionEnabled = false;
			pointOrLineSelectionEnabled = true;
			selectPointButton->setChecked(false);
			selectPointOrLineButton->setChecked(true);
			calculateDistanceButton->setEnabled(true);
			selectPointOrLineButton->setIcon(QIcon(":resources/icon/switch-on.png"));
			selectPointButton->setIcon(QIcon(":resources/icon/switch-off.png"));
			comboBox->setEnabled(true);
			statusLabel->setText("启用点/线选择模式，在3D视图中选择第二个顶点/线");
			describeLabel->setText("当前测量点" + comboBox->currentText() + "之间的距离");
			emit pointSelectionToggled(false);
			emit pointOrLineSelectionToggled(true, comboBox->currentText());
		}
	}
	void onComboBoxChanged()
	{
		pointOrLineSelectionEnabled = false;
		togglePointOrLineSelection();
		//describeLabel->setText("当前测量点" + comboBox->currentText() + "之间的距离");
	}
	void onCalculateDistance() 
	{
		if (pointSelectionEnabled || pointOrLineSelectionEnabled) 
		{
			pointSelectionEnabled = true;
			togglePointSelection();
			pointOrLineSelectionEnabled = true;
			togglePointOrLineSelection();
			emit calculateDistanceRequested();
		}
	}
	void onDialogClosed() {
		// 对话框关闭时禁用所有选择模式
		if (pointSelectionEnabled || pointOrLineSelectionEnabled) {
			pointSelectionEnabled = false;
			pointOrLineSelectionEnabled = false;
			selectPointButton->setChecked(false);
			selectPointOrLineButton->setChecked(false);
			calculateDistanceButton->setEnabled(false);
			comboBox->setEnabled(false);
			statusLabel->setText("测量模式已关闭");
			describeLabel->setText("选择点或点/线以测量距离");
			emit pointSelectionToggled(false);
			emit pointOrLineSelectionToggled(false, "");
		}
	}

private:
	QPushButton* selectPointButton;
	QPushButton* selectPointOrLineButton;
	QComboBox* comboBox;
	QLabel* statusLabel;
	QLabel* describeLabel;
	QPushButton* calculateDistanceButton;
	QLabel* distanceDescriptionLabel;
	bool pointSelectionEnabled = false;
	bool pointOrLineSelectionEnabled = false;
};
