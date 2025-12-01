#include "conedialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include <PublicVariables.h>
#include <PublicConstants.h>

ConeDialog::ConeDialog(QWidget *parent) : BasePage(PublicConstants::PAGE_PREPROCESS_CONE)
{
	// 创建输入框和标签
	QLabel *bottomRadiusLabel = new QLabel("底面半径:", this);
	bottomRadiusEdit = new QLineEdit(this);
	bottomRadiusEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this));

	QLabel *topRadiusLabel = new QLabel("顶面半径:", this);
	topRadiusEdit = new QLineEdit(this);
	topRadiusEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this));

	QLabel *heightLabel = new QLabel("高度:", this);
	heightEdit = new QLineEdit(this);
	heightEdit->setValidator(new QDoubleValidator(0.1, 1000.0, 2, this));

	QLabel *xLabel = new QLabel("底面中心X:", this);
	xEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *yLabel = new QLabel("底面中心Y:", this);
	yEdit = new QLineEdit(this);
	yEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	QLabel *zLabel = new QLabel("底面中心Z:", this);
	zEdit = new QLineEdit(this);
	zEdit->setValidator(new QDoubleValidator(-1000.0, 1000.0, 2, this));

	// 设置默认值
	bottomRadiusEdit->setText("30.0");
	topRadiusEdit->setText("10.0");
	heightEdit->setText("50.0");
	xEdit->setText("-50.0");
	yEdit->setText("-50.0");
	zEdit->setText("0.0");

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标

	// 使用 QSS 去除按钮边框并优化视觉融合
	QString buttonStyle = R"(
        QPushButton {
            border: none; /* 去除边框 */
            background: transparent; /* 透明背景 */
            padding: 2px; /* 减少内边距，文字和图标更紧凑 */
            color: #333; /* 文字颜色，深灰色 */
        }
        QPushButton:hover {
            background: #e0e0e0; /* 鼠标悬停时浅灰背景 */
            border-radius: 4px; /* 圆角效果 */
        }
        QPushButton:pressed {
            background: #c0c0c0; /* 点击时更深的背景 */
        }
    )";
	okButton->setStyleSheet(buttonStyle);

	// 布局
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *bottomRadiusLayout = new QHBoxLayout;
	QHBoxLayout *topRadiusLayout = new QHBoxLayout;
	QHBoxLayout *heightLayout = new QHBoxLayout;
	QHBoxLayout *xLayout = new QHBoxLayout;
	QHBoxLayout *yLayout = new QHBoxLayout;
	QHBoxLayout *zLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

	bottomRadiusLayout->addWidget(bottomRadiusLabel);
	bottomRadiusLayout->addWidget(bottomRadiusEdit);
	topRadiusLayout->addWidget(topRadiusLabel);
	topRadiusLayout->addWidget(topRadiusEdit);
	heightLayout->addWidget(heightLabel);
	heightLayout->addWidget(heightEdit);
	xLayout->addWidget(xLabel);
	xLayout->addWidget(xEdit);
	yLayout->addWidget(yLabel);
	yLayout->addWidget(yEdit);
	zLayout->addWidget(zLabel);
	zLayout->addWidget(zEdit);
	buttonLayout->addStretch();
	buttonLayout->addWidget(okButton);


	mainLayout->addLayout(bottomRadiusLayout);
	mainLayout->addLayout(topRadiusLayout);
	mainLayout->addLayout(heightLayout);
	mainLayout->addLayout(xLayout);
	mainLayout->addLayout(yLayout);
	mainLayout->addLayout(zLayout);
	mainLayout->addLayout(buttonLayout);
	mainLayout->addStretch();
	setLayout(mainLayout);

	// 连接按钮信号
	connect(okButton, &QPushButton::clicked, this, &ConeDialog::onOkClicked);
}

ConeDialog::~ConeDialog()
{
}

QJsonObject ConeDialog::saveJson()
{
	QJsonObject state;
	state["bottomRadius"] = getBottomRadius();
	state["topRadius"] = getTopRadius();
	state["height"] = getHeight();
	state["X"] = xEdit->text().toDouble();
	state["Y"] = yEdit->text().toDouble();
	state["Z"] = zEdit->text().toDouble();
	return state;
}

void ConeDialog::loadJson(const QJsonObject & state)
{
	if (state.contains("bottomRadius"))
	{
		QString value = QString::number(state["bottomRadius"].toDouble());
		bottomRadiusEdit->setText(value);
	}
	if (state.contains("topRadius"))
	{
		QString value = QString::number(state["topRadius"].toDouble());
		topRadiusEdit->setText(value);
	}
	if (state.contains("height"))
	{
		QString value = QString::number(state["height"].toDouble());
		heightEdit->setText(value);
	}
	if (state.contains("X"))
	{
		QString value = QString::number(state["X"].toDouble());
		xEdit->setText(value);
	}
	if (state.contains("Y"))
	{
		QString value = QString::number(state["Y"].toDouble());
		yEdit->setText(value);
	}
	if (state.contains("Z"))
	{
		QString value = QString::number(state["Z"].toDouble());
		zEdit->setText(value);
	}
}

double ConeDialog::getBottomRadius() const
{
	return bottomRadiusEdit->text().toDouble();
}

double ConeDialog::getTopRadius() const
{
	return topRadiusEdit->text().toDouble();
}

double ConeDialog::getHeight() const
{
	return heightEdit->text().toDouble();
}

gp_Pnt ConeDialog::getBaseCenter() const
{
	return gp_Pnt(xEdit->text().toDouble(), yEdit->text().toDouble(), zEdit->text().toDouble());
}

void ConeDialog::onOkClicked()
{
	bool ok;
	double bottomRadius = bottomRadiusEdit->text().toDouble(&ok);
	if (!ok || bottomRadius < 0) {
		QMessageBox::warning(this, "无效输入", "底面半径必须为非负数！");
		return;
	}
	double topRadius = topRadiusEdit->text().toDouble(&ok);
	if (!ok || topRadius < 0) {
		QMessageBox::warning(this, "无效输入", "顶面半径必须为非负数！");
		return;
	}
	double height = heightEdit->text().toDouble(&ok);
	if (!ok || height <= 0) {
		QMessageBox::warning(this, "无效输入", "高度必须为正数！");
		return;
	}
	emit parametersSubmitted(m_uuid, bottomRadius, topRadius, height, getBaseCenter());
}

void ConeDialog::onCancelClicked()
{
}
