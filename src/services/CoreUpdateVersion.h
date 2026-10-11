#pragma once

#include <QString>

namespace CoreUpdateVersion {

QString normalizeTag(QString value);
bool isPrerelease(const QString& tag);
bool isNewerThan(const QString& candidate, const QString& current);

} // namespace CoreUpdateVersion
