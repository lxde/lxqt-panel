#ifndef COLORBUTTON_H
#define COLORBUTTON_H

#include <QToolButton>


class QColor;
class QPaintEvent;


class ColorButton : public QToolButton
{
    public:

        void paintEvent(QPaintEvent* event);
        void setColor(QColor color);

    private:

        QColor color;
};

#endif // COLORBUTTON_H
