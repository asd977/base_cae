#pragma once
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QWidget>

inline QJsonObject saveLoadPageState(QWidget* page,const QStringList& names)
{
    QJsonObject result;
    for(const auto& name:names)
    {
        auto* object=page->findChild<QWidget*>(name);
        if(auto* field=qobject_cast<QDoubleSpinBox*>(object))result[name]=field->value();
        else if(auto* field=qobject_cast<QSpinBox*>(object))result[name]=field->value();
        else if(auto* field=qobject_cast<QLineEdit*>(object))result[name]=field->text();
        else if(auto* field=qobject_cast<QComboBox*>(object))result[name]=field->currentIndex();
        else if(auto* field=qobject_cast<QAbstractButton*>(object))result[name]=field->isChecked();
    }
    return result;
}
inline void loadLoadPageState(QWidget* page,const QJsonObject& data,const QStringList& names)
{
    for(const auto& name:names)
    {
        if(!data.contains(name))continue;
        auto* object=page->findChild<QWidget*>(name);if(!object)continue;
        QSignalBlocker blocker(object);const auto value=data[name];
        if(auto* field=qobject_cast<QDoubleSpinBox*>(object))field->setValue(value.toDouble());
        else if(auto* field=qobject_cast<QSpinBox*>(object))field->setValue(value.toInt());
        else if(auto* field=qobject_cast<QLineEdit*>(object))field->setText(value.toString());
        else if(auto* field=qobject_cast<QComboBox*>(object))field->setCurrentIndex(value.toInt());
        else if(auto* field=qobject_cast<QAbstractButton*>(object))field->setChecked(value.toBool());
    }
}

#define CAE_LOAD_PAGE_STATE(...) \
    QJsonObject saveJson() override {return saveLoadPageState(this, {__VA_ARGS__});} \
    void loadJson(const QJsonObject& data) override {loadLoadPageState(this,data,{__VA_ARGS__});}
