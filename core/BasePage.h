#pragma once
#include <QVariant>
#include <QUuid>
#include <QWidget>
#include <core_global.h>
#include <QJsonObject>

/**
* @brief 所有设置页面的基类 继承此类必须实现基类的纯虚函数
* @author tongle
* @date 2025.07.30
*/
class CORE_EXPORT BasePage: public QWidget
{
    Q_OBJECT
public:
    BasePage(QString pageType);
	virtual ~BasePage() {}
	/**
	* @brief 获取当前模块页面
	*/
    virtual QWidget* getWidget() {return this;};
	/**
	* @brief 获取页面的唯一标识符
	*/
    QString getUuid() const { return m_uuid; }
	/**
	* @brief 返回页面类型
	*/
    virtual QString getPageType();
    /**
    * @brief 页面前处理
    */
    virtual void beforeLoad() = 0;
    /**
    * @brief 页面后处理
    */
    virtual void beforeUnload() = 0;

    /**
    * @brief 保存页面参数
    */
    virtual QJsonObject saveJson() { return {}; }

    /**
    * @brief 加载页面参数
    */
    virtual void loadJson(const QJsonObject& obj) { Q_UNUSED(obj); }

protected:
    QString m_uuid;
    QString m_type;
};

