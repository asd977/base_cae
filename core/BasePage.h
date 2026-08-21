#pragma once
#include <core_global.h>

#include <QJsonObject>
#include <QUuid>
#include <QVariant>
#include <QWidget>

/**
 * @brief 所有设置页面的基类 继承此类必须实现基类的纯虚函数
 * @author tongle
 * @date 2025.07.30
 */
class CORE_EXPORT BasePage : public QWidget
{
    friend class ProjectNode;
    Q_OBJECT
  public:
    BasePage(QString pageType);
    virtual ~BasePage()
    {
    }
    /**
     * @brief 获取当前模块页面
     */
    virtual QWidget* getWidget()
    {
        return this;
    };
    /**
     * @brief 获取页面的唯一标识符
     */
    QString getUuid() const
    {
        return m_uuid;
    }
    QString getDisplayName() const
    {
        return m_displayName;
    }
    void setDisplayName(const QString& name)
    {
        m_displayName = name;
    }
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
    virtual QJsonObject saveJson()
    {
        return {};
    }

    /**
     * @brief 加载页面参数
     */
    virtual void loadJson(const QJsonObject& obj)
    {
        Q_UNUSED(obj);
    }

    // 大型页面（尤其后处理结果页）可延迟恢复状态，避免打开工程时立即
    // 扫描结果目录、读取 Tecplot/VTU 并构造显示网格。首次真正进入页面时
    // 由 ProjectTreeWidget 在 beforeLoad() 前统一应用。
    void deferLoadJson(const QJsonObject& obj)
    {
        m_deferredJson = obj;
        m_hasDeferredJson = true;
    }

    bool hasDeferredLoadJson() const
    {
        return m_hasDeferredJson;
    }

    void applyDeferredLoadJson()
    {
        if (!m_hasDeferredJson)
            return;

        const QJsonObject obj = m_deferredJson;
        // 必须先清标记，再调用虚函数；loadJson 内部若触发页面切换，不会重复恢复。
        m_deferredJson = QJsonObject{};
        m_hasDeferredJson = false;
        loadJson(obj);
    }

    // 页面尚未真正打开时，保存工程仍然要保留原始状态，不能把默认 UI 值覆盖掉。
    QJsonObject saveJsonForPersistence()
    {
        return m_hasDeferredJson ? m_deferredJson : saveJson();
    }

  protected:
    QString m_uuid;
    QString m_type;
    QString m_displayName;

  private:
    QJsonObject m_deferredJson;
    bool m_hasDeferredJson{false};
};
