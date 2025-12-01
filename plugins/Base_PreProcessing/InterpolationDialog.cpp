#include "interpolationdialog.h"
#include <QDoubleValidator>
#include <QMessageBox>
#include "qDebug.h"
#include <PublicVariables.h>
#include <PublicConstants.h>

InterpolationDialog::InterpolationDialog(QWidget* parent) : BasePage(PublicConstants::PAGE_PREPROCESS_INTER)
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	// 点输入区域
	pointsLayout = new QVBoxLayout();
	mainLayout->addLayout(pointsLayout);

	// 添加初始两个点
	for (int i = 0; i < 2; ++i) {
		QLabel* pointLabel = new QLabel(QString("点%1 (X, Y)：").arg(i), this);
		QLineEdit* xEdit = new QLineEdit(this);
		QLineEdit* yEdit = new QLineEdit(this);
		xEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
		yEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
		QHBoxLayout* pointLayout = new QHBoxLayout();
		pointLayout->addWidget(pointLabel);
		pointLayout->addWidget(xEdit);
		pointLayout->addWidget(yEdit);
		pointsLayout->addLayout(pointLayout);
		xEdits.append(xEdit);
		yEdits.append(yEdit);
		// 设置默认值
		xEdit->setText(QString::number(i * 1.0));
		yEdit->setText(QString::number(i % 2));
	}

	// 添加和删除按钮
	addButton = new QPushButton("添加点", this);
	removeButton = new QPushButton("删除点", this);
	QHBoxLayout* actionLayout = new QHBoxLayout();
	actionLayout->addWidget(addButton);
	actionLayout->addWidget(removeButton);
	mainLayout->addLayout(actionLayout);

	// 创建按钮
	QPushButton *okButton = new QPushButton("生成几何体", this);
	okButton->setIcon(QIcon(":/resources/icon/confirm.png")); // 确认图标
	addButton->setIcon(QIcon(":/resources/icon/InnerAdd.png")); 
	removeButton->setIcon(QIcon(":/resources/icon/InnerSub.png"));

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
	addButton->setStyleSheet(buttonStyle);
	removeButton->setStyleSheet(buttonStyle);
	actionLayout->addWidget(okButton);
	mainLayout->addStretch();

	// 连接信号和槽
	connect(okButton, &QPushButton::clicked, this, &InterpolationDialog::validateInput);

	// 连接信号和槽
	connect(addButton, &QPushButton::clicked, this, &InterpolationDialog::addPoint);
	connect(removeButton, &QPushButton::clicked, this, &InterpolationDialog::removePoint);
	connect(okButton, &QPushButton::clicked, this, &InterpolationDialog::validateInput);

}

std::vector<gp_Pnt2d> InterpolationDialog::getPoints() const
{
	return points;
}

QJsonObject InterpolationDialog::saveJson()
{
	QJsonObject state;
	state["size"] = xEdits.size();
	for (int i = 0; i < xEdits.size(); ++i) 
	{
		double x = xEdits[i]->text().toDouble();
		double y = yEdits[i]->text().toDouble();
		state[QString("x%1").arg(i)] = x;
		state[QString("y%1").arg(i)] = y;
	}
	return state;
}

void InterpolationDialog::loadJson(const QJsonObject & state)
{
	int	size = 0;
	if (state.contains("size"))
	{
		size = state["size"].toInt();
	}
	if (size <= 0) return;

	for (size_t i = 0; i < size; i++)
	{
		double x = state[QString("x%1").arg(i)].toDouble();
		double y = state[QString("y%1").arg(i)].toDouble();

		if (i == 0)
		{
			xEdits[0]->setText(QString::number(x));
			yEdits[0]->setText(QString::number(y));
			continue;
		}
		if (i == 1)
		{
			xEdits[1]->setText(QString::number(x));
			yEdits[1]->setText(QString::number(y));
			continue;
		}
		QLabel* pointLabel = new QLabel(QString("点%1 (X, Y)：").arg(i), this);
		QLineEdit* xEdit = new QLineEdit(this);
		QLineEdit* yEdit = new QLineEdit(this);
		xEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
		yEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
		QHBoxLayout* pointLayout = new QHBoxLayout();
		pointLayout->addWidget(pointLabel);
		pointLayout->addWidget(xEdit);
		pointLayout->addWidget(yEdit);
		pointsLayout->addLayout(pointLayout);
		xEdits.append(xEdit);
		yEdits.append(yEdit);
		// 设置默认值
		xEdit->setText(QString::number(x));
		yEdit->setText(QString::number(y));

		// 更新布局
		pointsLayout->update();
		adjustSize();
	}
}

void InterpolationDialog::addPoint()
{
	int index = xEdits.size();
	if (index >= 100) {
		QMessageBox::warning(this, "输入错误", "点的数量不能超过100个。");
		return;
	}

	QLabel* pointLabel = new QLabel(QString("点%1 (X, Y)：").arg(index), this);
	QLineEdit* xEdit = new QLineEdit(this);
	QLineEdit* yEdit = new QLineEdit(this);
	xEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	yEdit->setValidator(new QDoubleValidator(-1e6, 1e6, 6, this));
	QHBoxLayout* pointLayout = new QHBoxLayout();
	pointLayout->addWidget(pointLabel);
	pointLayout->addWidget(xEdit);
	pointLayout->addWidget(yEdit);
	pointsLayout->addLayout(pointLayout);
	xEdits.append(xEdit);
	yEdits.append(yEdit);
	// 设置默认值
	xEdit->setText(QString::number(index * 1.0));
	yEdit->setText(QString::number(index % 2));

	// 更新布局
	pointsLayout->update();
	adjustSize();
}

void InterpolationDialog::removePoint()
{
	if (xEdits.size() <= 2) {
		QMessageBox::warning(this, "输入错误", "插值曲线至少需要2个点。");
		return;
	}

	// 删除最后一个点的控件和布局
	QLayoutItem* item = pointsLayout->takeAt(pointsLayout->count() - 1);
	if (!item) {
		qDebug() << "错误：无法从pointsLayout获取布局项";
		return;
	}

	// 移除xEdit和yEdit
	xEdits.takeLast();
	yEdits.takeLast();

	// 清理QHBoxLayout中的控件
	if (QLayout* pointLayout = item->layout()) {
		while (QLayoutItem* subItem = pointLayout->takeAt(0)) {
			if (QWidget* widget = subItem->widget()) {
				widget->setParent(nullptr); // 移除父对象，避免重复删除
				delete widget; // 删除QLabel或QLineEdit
			}
			delete subItem; // 删除子布局项
		}
		pointLayout->setParent(nullptr); // 移除父对象
		delete pointLayout; // 删除QHBoxLayout
	}
	else {
		qDebug() << "错误：item不是QHBoxLayout";
	}

	//delete item; // 删除布局项

	// 更新布局
	pointsLayout->update();
	adjustSize();

	// 调试输出
	qDebug() << "剩余点数：" << xEdits.size();
}

void InterpolationDialog::validateInput()
{
	points.clear();
	bool ok;

	for (int i = 0; i < xEdits.size(); ++i) {
		double x = xEdits[i]->text().toDouble(&ok);
		if (!ok) {
			QMessageBox::warning(this, "输入错误", QString("点%1的X坐标必须为有效数字。").arg(i));
			return;
		}
		double y = yEdits[i]->text().toDouble(&ok);
		if (!ok) {
			QMessageBox::warning(this, "输入错误", QString("点%1的Y坐标必须为有效数字。").arg(i));
			return;
		}
		points.emplace_back(x, y);
	}

	if (points.size() < 2) {
		QMessageBox::warning(this, "输入错误", "插值曲线至少需要2个点。");
		return;
	}
	emit parametersSubmitted(m_uuid, points);

}