#include "KeyValueLineEdit.h"

#include "PublicVariables.h"

KeyValueLineEdit::KeyValueLineEdit(QWidget* parent) : QLineEdit(parent)
{
    // 编辑时不写入全局，只更新提示
    connect(this, &QLineEdit::textEdited, this, &KeyValueLineEdit::onTextEdited);
}

void KeyValueLineEdit::setParamKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key.trimmed();
    emit paramKeyChanged(m_key);

    // 作为初始内容显示（保持“始终显示键/字面量”的语义）
    setText(m_key);
    updateTooltip();
}

void KeyValueLineEdit::showEvent(QShowEvent* e)
{
    QLineEdit::showEvent(e);
    updateTooltip();
}

QString KeyValueLineEdit::currentKeyText() const
{
    return text().trimmed();
}

bool KeyValueLineEdit::keyExists() const
{
    const QString k = currentKeyText();
    if (k.isEmpty())
        return false;
    return !PublicVariables::instance().ownerOf(k).isEmpty();
}

QVariant KeyValueLineEdit::resolvedValue(bool* fromKey) const
{
    if (fromKey)
        *fromKey = false;

    const QString k = currentKeyText();
    if (k.isEmpty())
        return QVariant();

    const QString ownerUuid = PublicVariables::instance().ownerOf(k);
    if (!ownerUuid.isEmpty())
    {
        if (fromKey)
            *fromKey = true;
        return PublicVariables::instance().get(k); // 返回键对应的值
    }

    // 未注册键：按字面量原样返回（QString）
    return k;
}

void KeyValueLineEdit::onTextEdited(const QString& /*text*/)
{
    // 不写 PublicVariables；仅更新提示
    updateTooltip();
}

void KeyValueLineEdit::updateTooltip()
{
    const QString k = currentKeyText();
    if (k.isEmpty())
    {
        setToolTip(tr("输入一个已定义的键，或输入字面量（将按原值使用）"));
        return;
    }

    const QString ownerUuid = PublicVariables::instance().ownerOf(k);
    if (!ownerUuid.isEmpty())
    {
        const QVariant v = PublicVariables::instance().get(k);
        // 只做提示，不改变显示文本
        setToolTip(tr("参数键名：%1（所属页面标识：%2）\n当前值：%3")
                       .arg(k, ownerUuid, v.toString()));
    }
    else
    {
        setToolTip(tr("参数键名：%1（未定义，将按输入值使用）").arg(k));
    }
}
