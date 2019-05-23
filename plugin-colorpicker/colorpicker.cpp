/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2019 LXQt team
 * Authors:
 *   micrococo <micrococo@gmx.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */


#include "colorbutton.h"
#include "colorpicker.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopWidget>
#include <QBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>
#include <XdgIcon>


const QString ColorPickerWidget::svgIcon {
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\">"
    "  <path"
    "     style=\"fill:%1;fill-opacity:1;stroke:none\""
    "     d=\"M 8,2 A 6,6 0 0 0 2,8 6,6 0 0 0 8,14 6,6 0 0 0 14,8 6,6 0 0 0 8,2 M 8,3 A 5,5 0 0 1 13,8 5,5 0 0 1 8,13 5,5 0 0 1 3,8 5,5 0 0 1 8,3\""
    "     id=\"path2\" />"
    "  <path"
    "     style=\"fill:%2;stroke-width:0.03125\""
    "     d=\"M 7.3252447,12.939477 C 4.22976,12.490771 2.2993447,9.4012904 3.2660519,6.4430523 3.7960078,4.8213248 5.176118,3.5502752 6.8340753,3.1569853 8.2327213,2.8252076 9.7084288,3.1079402 10.87908,3.9319726 c 0.351893,0.2477004 0.938414,0.8342222 1.186115,1.1861147 0.392347,0.5573834 0.693227,1.2608044 0.828674,1.9373428 0.09455,0.4722872 0.101172,1.3507353 0.01371,1.8196175 -0.12871,0.6899891 -0.443837,1.4407734 -0.842381,2.0069604 -0.247701,0.351892 -0.834222,0.938414 -1.186115,1.186115 -0.549288,0.386648 -1.2303943,0.679168 -1.9132103,0.82168 -0.4104575,0.08567 -1.2226672,0.110259 -1.640625,0.04967 z\""
    "     id=\"path32\" />"
    "</svg>"
};


ColorPicker::ColorPicker(const ILXQtPanelPluginStartupInfo &startupInfo) :
    QObject(),
    ILXQtPanelPlugin(startupInfo)
{
    mWidget = new ColorPickerWidget();

    connect(mWidget, &ColorPickerWidget::showMenuRequested, this, [&](QMenu *menu)
    {
        willShowWindow(menu);
        menu->popup(calculatePopupWindowPos(menu->sizeHint()).topLeft());
    });

    realign();
}


ColorPicker::~ColorPicker()
{
    delete mWidget;
}


void ColorPicker::realign()
{
    mWidget->update(panel()->isHorizontal());
}


ColorPickerWidget::ColorPickerWidget(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    mSeparator = new QFrame();
    mSeparator->setFrameShape(QFrame::VLine);
    mSeparator->setFrameShadow(QFrame::Sunken);
    mSeparator->setLineWidth(1);
    mSeparator->setFixedHeight(16);

    mPickerButton = new QToolButton();
    mPickerButton->setObjectName("ColorPickerPickerButton");
    mPickerButton->setAccessibleName("ColorPickerPickerButton");
    mPickerButton->setAutoRaise(true);
    mPickerButton->setIcon(XdgIcon::fromTheme("color-select-symbolic", "color-select", "color-picker", "kcolorchooser"));

    mColorButton = new ColorButton();
    mColorButton->setObjectName("ColorPickerColorButton");
    mColorButton->setAccessibleName("ColorPickerColorButton");
    mColorButton->setAutoRaise(true);
    mColorButton->setStyleSheet("::menu-indicator{ image: none; }");

    QBoxLayout *layout = new QBoxLayout(QBoxLayout::LeftToRight);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mPickerButton);
    layout->addWidget(mSeparator);
    layout->addWidget(mColorButton);
    setLayout(layout);

    mClearListAction = new QAction(XdgIcon::fromTheme("edit-clear-all", "edit-clear"), tr("Clear list"));
    mClearListAction->setObjectName("ColorPickerClearAction");

    connect(mPickerButton, &QToolButton::clicked, this, &ColorPickerWidget::captureMouse);

    connect(mColorButton, &QToolButton::clicked, this, [&]()
    {
       buildMenu();
       emit showMenuRequested(mColorsMenu);
    });
}


ColorPickerWidget::~ColorPickerWidget()
{
    if (mColorsMenu)
    {
        delete mColorsMenu;
    }
}


void ColorPickerWidget::update(bool isHorizontal)
{
    QLayout *layout = this->layout();

    if (isHorizontal)
    {
        qobject_cast<QBoxLayout*>(layout)->setDirection(QBoxLayout::LeftToRight);
        mSeparator->setFrameShape(QFrame::VLine);
    }
    else
    {
        qobject_cast<QBoxLayout*>(layout)->setDirection(QBoxLayout::TopToBottom);
        mSeparator->setFrameShape(QFrame::HLine);
    }

    mColorButton->setColor(mColorsList.isEmpty() ?
                                palette().color(QPalette::Window) :
                                mColorsList.at(0));
}


void ColorPickerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!mCapturing)
    {
        return;
    }

    WId id = QApplication::desktop()->winId();
    QPixmap pixmap = qApp->primaryScreen()->grabWindow(id, event->globalX(), event->globalY(), 1, 1);

    QImage img = pixmap.toImage();
    QColor col = QColor(img.pixel(0,0));

    mColorButton->setColor(col);
    paste(col.name());

    if (mColorsList.contains(col))
    {
        mColorsList.move(mColorsList.indexOf(col), 0);
    }
    else
    {
        mColorsList.prepend(col);
    }

    if (mColorsList.size() > 10)
    {
        mColorsList.removeLast();
    }

    mCapturing = false;
    releaseMouse();

    if (!mPickerButton->contentsRect().contains(mapFromGlobal(QCursor::pos())))
    {
        QApplication::sendEvent(mPickerButton, new QEvent(QEvent::Leave));
    }
}


void ColorPickerWidget::captureMouse()
{
    grabMouse(Qt::CrossCursor);
    mCapturing = true;
}


QIcon ColorPickerWidget::colorIcon(QColor color)
{
    QString data = svgIcon.arg(palette().color(QPalette::Text).name()).arg(color.name());
    QPixmap pixmap(mColorButton->iconSize());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QSvgRenderer renderer { data.toLocal8Bit() };
    renderer.render(&painter);
    QIcon icon(pixmap);

    return icon;
}


void ColorPickerWidget::buildMenu()
{
    if (mColorsMenu == nullptr)
    {
        mColorsMenu = new QMenu();
        mColorsMenu->setObjectName("ColorPickerMenu");
        mColorsMenu->setAccessibleName("ColorPickerMenu");
        mColorButton->setMenu(mColorsMenu);
        mClearListAction = new QAction(XdgIcon::fromTheme("edit-clear-all", "edit-clear"), tr("Clear list"));
        mClearListAction->setObjectName("ColorPickerClearAction");

        connect(mColorsMenu, &QMenu::triggered, this, [&](QAction *action)
        {
            if (action == mClearListAction)
            {
                return;
            }

            paste(action->text());

            QColor color(action->text());
            mColorsList.move(mColorsList.indexOf(color), 0);
            mColorButton->setColor(color);
        });
    }

    for (QAction *action : mColorsMenu->actions())
    {
        mColorsMenu->removeAction(action);

        if (action != mClearListAction)
        {
            delete action;
        }
    }

    if (mColorsList.isEmpty())
    {
        QAction* empty = new QAction(tr("empty"), mColorsMenu);
        empty->setEnabled(false);
        mColorsMenu->addAction(empty);
        mColorButton->setColor(palette().color(QPalette::Window));
    }
    else
    {
        for (QColor color : mColorsList)
        {
            mColorsMenu->addAction(new QAction(colorIcon(color), color.name(), mColorsMenu));
        }

        mColorsMenu->addAction(mClearListAction);

        connect(mClearListAction, &QAction::triggered, this, [&]()
        {
           mColorsList.clear();
           mColorButton->setColor(palette().color(QPalette::Window));
        });
    }
}


void ColorPickerWidget::paste(const QString color) const
{
    QClipboard *clip = QApplication::clipboard();

    if (clip)
    {
        clip->setText(color);
    }
}
