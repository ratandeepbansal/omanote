#pragma once

#include <QRegularExpression>

// Inline tag syntax shared by the notes model (extraction) and the highlighter
// (colouring): a '#' not glued to a word, followed by letters, digits, '_', '-'
// or '/'. Headings ("# Title") never match because a heading has a space after
// the hashes. Pure numbers ("#123") are excluded so issue references stay plain.
inline const QRegularExpression &tagPattern() {
    static const QRegularExpression re(
        QStringLiteral("(?<![\\p{L}\\p{N}_#&/])#(?=[\\p{L}\\p{N}_/-]*[\\p{L}_])([\\p{L}\\p{N}_][\\p{L}\\p{N}_/-]*)"),
        QRegularExpression::UseUnicodePropertiesOption);
    return re;
}
