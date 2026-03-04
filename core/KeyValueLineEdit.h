#ifndef KEYVALUELINEEDIT_H
#define KEYVALUELINEEDIT_H

#include <core_global.h>

#include <QLineEdit>
#include <QVariant>

class CORE_EXPORT KeyValueLineEdit : public QLineEdit
{
    Q_OBJECT
    // 作为“默认文本/建议键名”的属性；设置后会把文本设为该值
    Q_PROPERTY(QString paramKey READ paramKey WRITE setParamKey NOTIFY paramKeyChanged)

  public:
    explicit KeyValueLineEdit(QWidget* parent = nullptr);

    QString paramKey() const
    {
        return m_key;
    }
    void setParamKey(const QString& key);

    // 解析当前文本：若为已注册键则返回其值；否则返回用户输入的字面量（QString）
    QVariant resolvedValue(bool* fromKey = nullptr) const;

    // 便捷：当前文本是否为已注册键
    bool keyExists() const;

    // 便捷：返回当前文本（去空格）
    QString currentKeyText() const;

  signals:
    void paramKeyChanged(const QString& key);

  protected:
    void showEvent(QShowEvent* e) override;

  private slots:
    void onTextEdited(const QString& text);

  private:
    void updateTooltip();

  private:
    QString m_key; // 仅作为初始化显示用途；实际以 text() 为准
};

#endif // KEYVALUELINEEDIT_H
