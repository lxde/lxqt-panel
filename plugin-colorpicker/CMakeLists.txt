set(PLUGIN "colorpicker")

set(HEADERS
    colorpicker.h
    colorbutton.h
)

set(SOURCES
    colorpicker.cpp
    colorbutton.cpp
)

set(UIS "")

set(LIBRARIES
    lxqt
)

BUILD_LXQT_PLUGIN(${PLUGIN})
