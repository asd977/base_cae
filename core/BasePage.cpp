#include <BasePage.h>

BasePage::BasePage(QString pageType)
    : m_uuid(QUuid::createUuid().toString()), m_displayName(m_uuid) {
  m_type = pageType;
}

QString BasePage::getPageType() { return m_type; }
