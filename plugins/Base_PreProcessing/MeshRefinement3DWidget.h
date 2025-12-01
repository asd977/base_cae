#ifndef MESHREFINEMENT3DWIDGET_H
#define MESHREFINEMENT3DWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QCheckBox>
#include "BasePage.h"

class MeshRefinement3DWidget : public BasePage
{
	Q_OBJECT

public:
	explicit MeshRefinement3DWidget(QWidget *parent = nullptr);
	~MeshRefinement3DWidget();

	
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
	bool getFixShape() const;

	// 设置当前值
	void setSelectedMethod(const QString value) const;
	void setGridType(const QString value) const;
	void setOrderType(const QString value) const;
	void setSizeFactor(const double value) const;
	void setMinUnitSize(const double value) const;
	void setMaxUnitSize(const double value) const;
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
	QCheckBox *m_shapeCheckBox;
	void setupUi();
};

#endif // MESHREFINEMENT3DWIDGET_H