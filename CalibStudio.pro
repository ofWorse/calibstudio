QT       += core gui widgets network

CONFIG += c++17

win32 {
    # OpenCV from the same prefix as Qt, e.g. MSYS2 UCRT64
    # (pacman -S mingw-w64-ucrt-x86_64-opencv).
    # OpenCV 5 installs its headers into include/opencv5, OpenCV 4 into include/opencv4.
    QT_PREFIX = $$[QT_INSTALL_PREFIX]
    OPENCV_INCLUDE =
    exists($$QT_PREFIX/include/opencv5/opencv2/core.hpp): OPENCV_INCLUDE = $$QT_PREFIX/include/opencv5
    else: exists($$QT_PREFIX/include/opencv4/opencv2/core.hpp): OPENCV_INCLUDE = $$QT_PREFIX/include/opencv4

    isEmpty(OPENCV_INCLUDE) {
        error("OpenCV not found in $$QT_PREFIX. Install it with the same toolchain as Qt, e.g. pacman -S mingw-w64-ucrt-x86_64-opencv")
    }
    message("OpenCV: $$OPENCV_INCLUDE")
    INCLUDEPATH += $$OPENCV_INCLUDE

    LIBS += -L$$QT_PREFIX/lib \
            -lopencv_core \
            -lopencv_imgproc \
            -lopencv_imgcodecs \
            -lopencv_videoio

    LIBS += -lstrmiids -lole32 -loleaut32 -luuid
    DEFINES += NOMINMAX
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4
}





INCLUDEPATH += $$PWD

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    camera/camerainfo.cpp \
    camera/cameraprovider.cpp \
    camera/cameradevice.cpp \
    camera/capturethread.cpp \
    camera/gvcp.cpp \
    camera/gigevisionprovider.cpp \
    ui/videowidget.cpp

HEADERS += \
    mainwindow.h \
    camera/camerainfo.h \
    camera/cameraprovider.h \
    camera/cameradevice.h \
    camera/capturethread.h \
    camera/gvcp.h \
    camera/gigevisionprovider.h \
    ui/videowidget.h

win32 {
    SOURCES += camera/directshowprovider.cpp
    HEADERS += camera/directshowprovider.h
} else {
    SOURCES += camera/opencvprobeprovider.cpp
    HEADERS += camera/opencvprobeprovider.h
}

TRANSLATIONS += \
    CalibStudio_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations



qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
