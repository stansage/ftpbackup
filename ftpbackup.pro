# -------------------------------------------------
# Project created by QtCreator 2010-05-30T13:54:34
# -------------------------------------------------
QT -= core \
    gui
TEMPLATE = app
SOURCES += main.cpp \
    data.cpp \
    backuptask.cpp \
    ftpclient.cpp \
    singleton.cpp
INCLUDEPATH += /usr/include/mysql
CONFIG(debug, debug|release):LIBS += -lPocoFoundationd \
    -lPocoUtild \
    -lPocoNetd \
    -lPocoDatad \
    -lPocoMySQLd
else:LIBS += -lPocoFoundation \
    -lPocoUtil \
    -lPocoNet \
    -lPocoData \
    -lPocoMySQL
HEADERS += data.h \
    backuptask.h \
    main.h \
    ftpclient.h \
    singleton.h
OTHER_FILES += README \
    config.properties
