#include "MeshRefinementWidget.h"
#include <QHBoxLayout>
#include <QValidator>
#include <QDebug>
#include <QSpacerItem>
#include <PublicVariables.h>
#include <PublicConstants.h>

MeshRefinementWidget::MeshRefinementWidget(QWidget *parent) : BasePage(PublicConstants::PAGE_PREPROCESS_MESH2D)
{
	setupUi();
}

MeshRefinementWidget::~MeshRefinementWidget()
{
}

//QVariantMap MeshRefinementWidget::saveState() const
//{
//	QVariantMap state;
//	state["SelectedMethod"] = getSelectedMethod();
//	state["SizeFactor"] = getSizeFactor();
//	state["MinUnitSize"] = getMinUnitSize();
//	state["MaxUnitSize"] = getMaxUnitSize();
//	state["SmoothingIterations"] = getSmoothingIterations();
//	state["GridType"] = getGridType();
//	state["OrderType"] = getOrderType();
//	state["fixShape"] = getFixShape();
//	return state;
//}
//
//void MeshRefinementWidget::loadState(const QVariantMap & state)
//{
//	if (state.contains("SelectedMethod"))
//	{
//		QString value = state["SelectedMethod"].toString();
//		setSelectedMethod(value);
//	}
//	if (state.contains("SizeFactor"))
//	{
//		double value = state["SizeFactor"].toDouble();
//		setSizeFactor(value);
//	}
//	if (state.contains("MinUnitSize"))
//	{
//		double value = state["MinUnitSize"].toDouble();
//		setMinUnitSize(value);
//	}
//	if (state.contains("MaxUnitSize"))
//	{
//		double value = state["MaxUnitSize"].toDouble();
//		setMaxUnitSize(value);
//	}
//	if (state.contains("SmoothingIterations"))
//	{
//		int value = state["SmoothingIterations"].toInt();
//		setSmoothingIterations(value);
//	}
//	if (state.contains("GridType"))
//	{
//		QString value = state["GridType"].toString();
//		setGridType(value);
//	}
//	if (state.contains("OrderType"))
//	{
//		QString value = state["OrderType"].toString();
//		setOrderType(value);
//	}
//	if (state.contains("fixShape"))
//	{
//		bool value = state["fixShape"].toBool();
//		setFixShape(value);
//	}
//	return;
//}

void MeshRefinementWidget::setupUi()
{
	//setWindowIcon(QIcon(":/resources/icon.svg"));
	// 垂直布局
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// 方法下拉框
	QHBoxLayout *methodLayout = new QHBoxLayout();
	methodLayout->addWidget(new QLabel("方法:"));
	m_methodComboBox = new QComboBox(this);
	m_methodComboBox->addItems({ "Automatic", "Delaunay", "Frontal","Mesh Adapt","Delaunay for quad","Packing for parallelograms" });
	methodLayout->addWidget(m_methodComboBox);
	mainLayout->addLayout(methodLayout);

	// 网格类型下拉框
	QHBoxLayout *gridLayout = new QHBoxLayout();
	gridLayout->addWidget(new QLabel("网格类型:"));
	m_gridComboBox = new QComboBox(this);
	m_gridComboBox->addItems({ "三角形", "四边形" });
	gridLayout->addWidget(m_gridComboBox);
	mainLayout->addLayout(gridLayout);

	// 阶数下拉框
	QHBoxLayout *orderLayout = new QHBoxLayout();
	orderLayout->addWidget(new QLabel("阶数:"));
	m_orderComboBox = new QComboBox(this);
	m_orderComboBox->addItems({ "一阶", "二阶"});
	orderLayout->addWidget(m_orderComboBox);
	mainLayout->addLayout(orderLayout);

	// 尺寸因子输入
	QHBoxLayout *sizeFactorLayout = new QHBoxLayout();
	sizeFactorLayout->addWidget(new QLabel("尺寸因子:"));
	m_sizeFactorEdit = new QLineEdit("1.0", this);
	m_sizeFactorEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this)); // 非负数
	connect(m_sizeFactorEdit, &QLineEdit::textChanged, this, &MeshRefinementWidget::validateInput);
	sizeFactorLayout->addWidget(m_sizeFactorEdit);
	mainLayout->addLayout(sizeFactorLayout);

	// 最小单元尺寸输入
	QHBoxLayout *minUnitSizeLayout = new QHBoxLayout();
	minUnitSizeLayout->addWidget(new QLabel("最小单元尺寸:"));
	m_minUnitSizeEdit = new QLineEdit("1.0", this);
	m_minUnitSizeEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this)); // 非负数
	connect(m_minUnitSizeEdit, &QLineEdit::textChanged, this, &MeshRefinementWidget::validateInput);
	minUnitSizeLayout->addWidget(m_minUnitSizeEdit);
	mainLayout->addLayout(minUnitSizeLayout);

	// 最大单元尺寸输入
	QHBoxLayout *maxUnitSizeLayout = new QHBoxLayout();
	maxUnitSizeLayout->addWidget(new QLabel("最大单元尺寸:"));
	m_maxUnitSizeEdit = new QLineEdit("10.0", this);
	m_maxUnitSizeEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this)); // 非负数
	connect(m_maxUnitSizeEdit, &QLineEdit::textChanged, this, &MeshRefinementWidget::validateInput);
	maxUnitSizeLayout->addWidget(m_maxUnitSizeEdit);
	mainLayout->addLayout(maxUnitSizeLayout);

	// 光滑迭代次数输入
	QHBoxLayout *smoothingLayout = new QHBoxLayout();
	smoothingLayout->addWidget(new QLabel("光滑迭代次数:"));
	m_smoothingIterationsEdit = new QLineEdit("5", this);
	m_smoothingIterationsEdit->setValidator(new QIntValidator(0, 1000, this)); // 非负整数
	connect(m_smoothingIterationsEdit, &QLineEdit::textChanged, this, &MeshRefinementWidget::validateInput);
	smoothingLayout->addWidget(m_smoothingIterationsEdit);
	mainLayout->addLayout(smoothingLayout);
	
	// 启用几何修复
	QHBoxLayout *fixShapeLayout = new QHBoxLayout();
	fixShapeLayout->addWidget(new QLabel("启用几何修复:"));
	m_shapeCheckBox = new QCheckBox(this);
	m_shapeCheckBox->setChecked(true);
	fixShapeLayout->addWidget(m_shapeCheckBox);
	mainLayout->addLayout(fixShapeLayout);
	mainLayout->addStretch();
	// 设置布局
	setLayout(mainLayout);
}

void MeshRefinementWidget::validateInput()
{
	// 验证输入，确保非负数
	bool ok;
	double sizeFactor = m_sizeFactorEdit->text().toDouble(&ok);
	if (!ok || sizeFactor < 0) {
		m_sizeFactorEdit->setText("0.0");
	}

	double minUnitSize = m_minUnitSizeEdit->text().toDouble(&ok);
	if (!ok || minUnitSize < 0) {
		m_minUnitSizeEdit->setText("0.0");
	}

	double maxUnitSize = m_maxUnitSizeEdit->text().toDouble(&ok);
	if (!ok || maxUnitSize < 0) {
		m_maxUnitSizeEdit->setText("0.0");
	}

	int smoothingIterations = m_smoothingIterationsEdit->text().toInt(&ok);
	if (!ok || smoothingIterations < 0) {
		m_smoothingIterationsEdit->setText("0");
	}

	// 确保最大单元尺寸大于最小单元尺寸
	if (maxUnitSize < minUnitSize && maxUnitSize >= 0 && minUnitSize >= 0) {
		m_maxUnitSizeEdit->setText(m_minUnitSizeEdit->text());
	}
}

QJsonObject MeshRefinementWidget::saveJson()
{
	QJsonObject state;
	state["SelectedMethod"] = getSelectedMethod();
	state["SizeFactor"] = getSizeFactor();
	state["MinUnitSize"] = getMinUnitSize();
	state["MaxUnitSize"] = getMaxUnitSize();
	state["SmoothingIterations"] = getSmoothingIterations();
	state["GridType"] = getGridType();
	state["OrderType"] = getOrderType();
	state["fixShape"] = getFixShape();
	return state;
}

void MeshRefinementWidget::loadJson(const QJsonObject & state)
{
	if (state.contains("SelectedMethod"))
	{
		QString value = state["SelectedMethod"].toString();
		setSelectedMethod(value);
	}
	if (state.contains("SizeFactor"))
	{
		double value = state["SizeFactor"].toDouble();
		setSizeFactor(value);
	}
	if (state.contains("MinUnitSize"))
	{
		double value = state["MinUnitSize"].toDouble();
		setMinUnitSize(value);
	}
	if (state.contains("MaxUnitSize"))
	{
		double value = state["MaxUnitSize"].toDouble();
		setMaxUnitSize(value);
	}
	if (state.contains("SmoothingIterations"))
	{
		int value = state["SmoothingIterations"].toInt();
		setSmoothingIterations(value);
	}
	if (state.contains("GridType"))
	{
		QString value = state["GridType"].toString();
		setGridType(value);
	}
	if (state.contains("OrderType"))
	{
		QString value = state["OrderType"].toString();
		setOrderType(value);
	}
	if (state.contains("fixShape"))
	{
		bool value = state["fixShape"].toBool();
		setFixShape(value);
	}
	return;
}

QString MeshRefinementWidget::getSelectedMethod() const
{
	return m_methodComboBox->currentText();
}

QString MeshRefinementWidget::getGridType() const
{
	return m_gridComboBox->currentText();
}

QString MeshRefinementWidget::getOrderType() const
{
	return m_orderComboBox->currentText();
}

double MeshRefinementWidget::getSizeFactor() const
{
	bool ok;
	double value = m_sizeFactorEdit->text().toDouble(&ok);
	return ok ? value : 1.0; // 默认值 1.0
}

double MeshRefinementWidget::getMinUnitSize() const
{
	bool ok;
	double value = m_minUnitSizeEdit->text().toDouble(&ok);
	return ok ? value : 1; // 默认值 1
}

double MeshRefinementWidget::getMaxUnitSize() const
{
	bool ok;
	double value = m_maxUnitSizeEdit->text().toDouble(&ok);
	return ok ? value : 10; // 默认值 10
}

int MeshRefinementWidget::getSmoothingIterations() const
{
	bool ok;
	int value = m_smoothingIterationsEdit->text().toInt(&ok);
	return ok ? value : 10; // 默认值 5
}

bool MeshRefinementWidget::getFixShape() const
{
	bool flag = m_shapeCheckBox->isChecked();
	return flag;
}

void MeshRefinementWidget::setSelectedMethod(const QString value) const
{
	m_methodComboBox->setCurrentText(value);
}

void MeshRefinementWidget::setGridType(const QString value) const
{
	m_gridComboBox->setCurrentText(value);
}

void MeshRefinementWidget::setOrderType(const QString value) const
{
	m_orderComboBox->setCurrentText(value);
}

void MeshRefinementWidget::setSizeFactor(const double value) const
{
	m_sizeFactorEdit->setText(QString::number(value));
}

void MeshRefinementWidget::setMinUnitSize(const double value) const
{
	m_minUnitSizeEdit->setText(QString::number(value));
}

void MeshRefinementWidget::setMaxUnitSize(const double value) const
{
	m_maxUnitSizeEdit->setText(QString::number(value));
}

void MeshRefinementWidget::setSmoothingIterations(const int value) const
{
	m_smoothingIterationsEdit->setText(QString::number(value));
}

void MeshRefinementWidget::setFixShape(const bool value) const
{
	m_shapeCheckBox->setChecked(value);
}
