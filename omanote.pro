QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus network

CONFIG += c++17 release
TARGET = omanote
TEMPLATE = app

HEADERS += \
    src/tagpattern.h \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemtheme.h \
    src/notesmodel.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp \
    src/notesmodel.cpp

RESOURCES += src/resources.qrc
