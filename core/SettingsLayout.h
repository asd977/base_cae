#pragma once

#include <core_global.h>
#include <QStackedWidget>

namespace CaeSettingsLayout
{
// Font/style based content floor; the surrounding scroll area remains shrinkable.
CORE_EXPORT int readableWidth(const QWidget* widget);
CORE_EXPORT void prepare(QWidget* page);

// Inactive pages must not determine the current settings dock's requirements.
class CORE_EXPORT ActivePageStack final : public QStackedWidget
{
public:
    explicit ActivePageStack(QWidget* parent=nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
};
}
