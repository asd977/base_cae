#include "MeshRefinement3DWidget.h"
#include <QHBoxLayout>
#include <QValidator>
#include <QDebug>
#include <QSpacerItem>
#include <PublicVariables.h>
#include <PublicConstants.h>

MeshRefinement3DWidget::MeshRefinement3DWidget(QWidget *parent) : BasePage(PublicConstants::kPagePreprocessMesh3d)
{
	setupUi();
}

MeshRefinement3DWidget::~MeshRefinement3DWidget()
{
}

void MeshRefinement3DWidget::setupUi()
{
	//setWindowIcon(QIcon(":/resources/icon.svg"));
	// 垂直布局
	QVBoxLayout *mainLayout = new QVBoxLayout(this);

	// 方法下拉框
	QHBoxLayout *methodLayout = new QHBoxLayout();
	methodLayout->addWidget(new QLabel("方法:"));
	m_methodComboBox = new QComboBox(this);
	m_methodComboBox->addItems({"Delaunay","New Delaunay","Frontal","Frontal Delaunay","R-tree","MMG3D","HXT"});
	methodLayout->addWidget(m_methodComboBox);
	mainLayout->addLayout(methodLayout);

	// 网格类型下拉框
	QHBoxLayout *gridLayout = new QHBoxLayout();
	gridLayout->addWidget(new QLabel("网格类型:"));
	m_gridComboBox = new QComboBox(this);
	m_gridComboBox->addItems({ "四面体", "六面体" });
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
	connect(m_sizeFactorEdit, &QLineEdit::textChanged, this, &MeshRefinement3DWidget::validateInput);
	sizeFactorLayout->addWidget(m_sizeFactorEdit);
	mainLayout->addLayout(sizeFactorLayout);

	// 最小单元尺寸输入
	QHBoxLayout *minUnitSizeLayout = new QHBoxLayout();
	minUnitSizeLayout->addWidget(new QLabel("最小单元尺寸:"));
	m_minUnitSizeEdit = new QLineEdit("1.0", this);
	m_minUnitSizeEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this)); // 非负数
	connect(m_minUnitSizeEdit, &QLineEdit::textChanged, this, &MeshRefinement3DWidget::validateInput);
	minUnitSizeLayout->addWidget(m_minUnitSizeEdit);
	mainLayout->addLayout(minUnitSizeLayout);

	// 最大单元尺寸输入
	QHBoxLayout *maxUnitSizeLayout = new QHBoxLayout();
	maxUnitSizeLayout->addWidget(new QLabel("最大单元尺寸:"));
	m_maxUnitSizeEdit = new QLineEdit("10.0", this);
	m_maxUnitSizeEdit->setValidator(new QDoubleValidator(0.0, 1000.0, 2, this)); // 非负数
	connect(m_maxUnitSizeEdit, &QLineEdit::textChanged, this, &MeshRefinement3DWidget::validateInput);
	maxUnitSizeLayout->addWidget(m_maxUnitSizeEdit);
	mainLayout->addLayout(maxUnitSizeLayout);

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

void MeshRefinement3DWidget::validateInput()
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

	// 确保最大单元尺寸大于最小单元尺寸
	if (maxUnitSize < minUnitSize && maxUnitSize >= 0 && minUnitSize >= 0) {
		m_maxUnitSizeEdit->setText(m_minUnitSizeEdit->text());
	}
}


QJsonObject MeshRefinement3DWidget::saveJson()
{
	QJsonObject state;
	state["SelectedMethod"] = getSelectedMethod();
	state["SizeFactor"] = getSizeFactor();
	state["MinUnitSize"] = getMinUnitSize();
	state["MaxUnitSize"] = getMaxUnitSize();
	state["GridType"] = getGridType();
	state["OrderType"] = getOrderType();
	state["fixShape"] = getFixShape();
	return state;
}

void MeshRefinement3DWidget::loadJson(const QJsonObject & state)
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

QString MeshRefinement3DWidget::getSelectedMethod() const
{
	return m_methodComboBox->currentText();
}

QString MeshRefinement3DWidget::getGridType() const
{
	return m_gridComboBox->currentText();
}

QString MeshRefinement3DWidget::getOrderType() const
{
	return m_orderComboBox->currentText();
}

double MeshRefinement3DWidget::getSizeFactor() const
{
	bool ok;
	double value = m_sizeFactorEdit->text().toDouble(&ok);
	return ok ? value : 1.0; // 默认值 1.0
}

double MeshRefinement3DWidget::getMinUnitSize() const
{
	bool ok;
	double value = m_minUnitSizeEdit->text().toDouble(&ok);
	return ok ? value : 1; // 默认值 0.1
}

double MeshRefinement3DWidget::getMaxUnitSize() const
{
	bool ok;
	double value = m_maxUnitSizeEdit->text().toDouble(&ok);
	return ok ? value : 10; // 默认值 1.0
}

bool MeshRefinement3DWidget::getFixShape() const
{
	bool flag = m_shapeCheckBox->isChecked();
	return flag;
}

void MeshRefinement3DWidget::setSelectedMethod(const QString value) const
{
	m_methodComboBox->setCurrentText(value);
}

void MeshRefinement3DWidget::setGridType(const QString value) const
{
	m_gridComboBox->setCurrentText(value);
}

void MeshRefinement3DWidget::setOrderType(const QString value) const
{
	m_orderComboBox->setCurrentText(value);
}

void MeshRefinement3DWidget::setSizeFactor(const double value) const
{
	m_sizeFactorEdit->setText(QString::number(value));
}

void MeshRefinement3DWidget::setMinUnitSize(const double value) const
{
	m_minUnitSizeEdit->setText(QString::number(value));
}

void MeshRefinement3DWidget::setMaxUnitSize(const double value) const
{
	m_maxUnitSizeEdit->setText(QString::number(value));
}

void MeshRefinement3DWidget::setFixShape(const bool value) const
{
	m_shapeCheckBox->setChecked(value);
}

