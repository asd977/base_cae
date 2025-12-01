#ifndef MESHREFINEMENTWIDGET_H
#define MESHREFINEMENTWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QCheckBox>
#include "BasePage.h"

class MeshRefinementWidget : public BasePage
{
	Q_OBJECT
		 
public:
	explicit MeshRefinementWidget(QWidget *parent = nullptr);
	~MeshRefinementWidget();

	
	void beforeLoad() {};
	void beforeUnload() {};
	QJsonObject saveJson() override;
	void loadJson(const QJsonObject &obj) override;

	// 获取当前设置的值
	QString getSelectedMethod() const;
	QString getGridType() const;
	QString getOrderType() const;
	double getSizeFactor() const;
	double getMinUnitSize() const;
	double getMaxUnitSize() const;
	int getSmoothingIterations() const;
	bool getFixShape() const;

	// 设置当前值
	void setSelectedMethod(const QString value) const;
	void setGridType(const QString value) const;
	void setOrderType(const QString value) const;
	void setSizeFactor(const double value) const;
	void setMinUnitSize(const double value) const;
	void setMaxUnitSize(const double value) const;
	void setSmoothingIterations(const int value) const;
	void setFixShape(const bool value)const;

private slots:
	void validateInput();

private:
	QComboBox *m_methodComboBox;
	QComboBox * m_gridComboBox;
	QComboBox * m_orderComboBox;
	QLineEdit *m_sizeFactorEdit;
	QLineEdit *m_minUnitSizeEdit;
	QLineEdit *m_maxUnitSizeEdit;
	QLineEdit *m_smoothingIterationsEdit;
	QCheckBox *m_shapeCheckBox;

	void setupUi();
};

#endif // MESHREFINEMENTWIDGET_H