#include "lxqttaskgroup.h"
#include <QVBoxLayout>
#include <QDebug>
#include "../panel/ilxqtpanel.h"
#include <QMimeData>
#include "../panel/ilxqtpanelplugin.h"
#include <QDialog>
#include <KF5/KWindowSystem/KWindowSystem>
#include <QFocusEvent>
#include "lxqttaskbar.h"
#include <QTimer>
#include <QDragLeaveEvent>
#include <QMenu>
#include <XdgIcon>
#include "lxqttaskpopup.h"

LxQtTaskGroup::LxQtTaskGroup(const QString &groupName,QIcon icon,ILxQtPanelPlugin * plugin, LxQtTaskBar *parent):
    LxQtTaskButton(0,parent,parent),
    mGroupName(groupName),
    mFrame(new LxQtLooseFocusFrame(mButtonHash,this)),
    mLayout(new QVBoxLayout(mFrame)),
    mPlugin(plugin),
    mTimer(new QTimer(this)),
    mShowTimer(new QTimer(this))
{
    Q_ASSERT(parent);

    setText(groupName);
    setIcon(icon);

    mFrame->setLayout(mLayout);
    mFrame->setHidden(true);

    mLayout->setSpacing(5);
    mLayout->setMargin(5);

    connect(this,SIGNAL(clicked(bool)),this,SLOT(onClicked(bool)));
    connect(KWindowSystem::self(),SIGNAL(activeWindowChanged(WId)),this,SLOT(onActiveWindowChanged(WId)));
    connect(KWindowSystem::self(),SIGNAL(windowRemoved(WId)),this,SLOT(onWindowRemoved(WId)));
    connect(KWindowSystem::self(),SIGNAL(currentDesktopChanged(int)),this,SLOT(onDesktopChanged(int)));
    connect(KWindowSystem::self(), SIGNAL(windowChanged(WId, NET::Properties, NET::Properties2)),
            SLOT(windowChanged(WId, NET::Properties, NET::Properties2)));

    connect(mFrame,SIGNAL(mouseLeft(bool)),this,SLOT(mouseFrameChanged(bool)));

    mTimer->setSingleShot(true);
    mTimer->setInterval(200);
    connect(mTimer,SIGNAL(timeout()),this,SLOT(timeoutClose()));

    mShowTimer->setSingleShot(true);
    mShowTimer->setInterval(400);
    //connect(mShowTimer,SIGNAL(timeout()),this,SLOT(timeoutRaise()));

    setObjectName(groupName);
}

void LxQtTaskGroup::contextMenuEvent(QContextMenuEvent *event)
{
    if (windowId())
    {
        LxQtTaskButton::contextMenuEvent(event);
        return;
    }

    QMenu menu(tr("Group"));
    menu.addAction(XdgIcon::fromTheme("process-stop"), tr("Close group"),this,SLOT(closeGroup()));
    menu.exec(mapToGlobal(event->pos()));
}

void LxQtTaskGroup::closeGroup()
{
    foreach (LxQtTaskButton * button, mButtonHash)
    {
        button->closeApplication();
    }
}

void LxQtTaskGroup::timeoutRaise()
{
    if (toolButtonStyle() == Qt::ToolButtonIconOnly)
    {
        raisePopup(true);
    }
    else
    {
        if (!windowId())
            raisePopup(true);
    }
}

/************************************************

 ************************************************/
LxQtTaskButton * LxQtTaskGroup::createButton(WId id)
{
    if (mButtonHash.contains(id))
        return mButtonHash.value(id);

    LxQtTaskButton * btn = new LxQtTaskButton(id,parentTaskBar(),mFrame);

    if (btn->isApplicationActive())
    {
        btn->setChecked(true);
        setChecked(true);
    }

    btn->setParentGroup(this);

    mButtonHash.insert(id,btn);
    mLayout->addWidget(btn);

    connect(btn,SIGNAL(clicked()),this,SLOT(onChildButtonClicked()));
    connect(btn,SIGNAL(dropped(QPoint,QDropEvent*)),mFrame,SLOT(buttonDropped(QPoint,QDropEvent*)));

    refreshVisibility();
    regroup();

    return btn;
}

LxQtTaskButton * LxQtTaskGroup::checkedButton() const
{
    foreach (LxQtTaskButton* button, mButtonHash)
    {
        if (button->isChecked())
        {
            return button;
        }
    }

    return NULL;
}

bool LxQtTaskGroup::checkNextPrevChild(bool next,bool modulo)
{
    int idx = mLayout->indexOf(checkedButton());
    int inc;
    if (next)
        inc = 1;
    else
        inc = -1;

    idx += inc;

    if (!checkedButton())
    {
        if (next)
        {
            idx = 0;
        }
        else
        {
            for(int i = mLayout->count() - 1; i >= 0; i--)
                if(mLayout->itemAt(i)->widget()->isVisibleTo(mFrame))
                {
                    idx = i;
                    break;
                }
        }
    }

    while(true)
    {
        if (!modulo)
        {
            if (mLayout->count() <= idx || idx < 0)
                return false;
        }
        else
        {
            idx = (idx + mButtonHash.count()) % mButtonHash.count();
        }

        QWidget * w = mLayout->itemAt(idx)->widget();
        LxQtTaskButton * button = qobject_cast<LxQtTaskButton*>(w);
        if (button->isVisibleTo(mFrame))
        {
            button->raiseApplication();
            return true;
        }
        idx += inc;
    }
    return false;
}

/************************************************

 ************************************************/
void LxQtTaskGroup::onActiveWindowChanged(WId window)
{
    bool contains = mButtonHash.contains(window);
    foreach(LxQtTaskButton * btn, mButtonHash)
    {
        btn->setChecked(false);
    }

    if (contains)
    {
        LxQtTaskButton * btn = mButtonHash.value(window);
        btn->setChecked(true);
        if (btn->hasUrgencyHint())
            btn->setUrgencyHint(false);
    }
    setChecked(contains);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::onDesktopChanged(int number)
{
    refreshVisibility();
    regroup();
}

/************************************************

 ************************************************/
void LxQtTaskGroup::onWindowRemoved(WId window)
{
    removeButton(window);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::onChildButtonClicked()
{
    raisePopup(false);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::removeButton(WId window)
{
    if (mButtonHash.contains(window))
    {
        LxQtTaskButton * button = mButtonHash.value(window);
        mButtonHash.remove(window);
        mLayout->removeWidget(button);
        //button->setParentGroup(NULL);
        //disconnect(button,SIGNAL(clicked()),this,SLOT(onChildButtonClicked()));

        delete button;

        if (mButtonHash.count())
        {
            regroup();
        }
        else
        {
            if (isVisible())
                emit visibilityChanged(false);
            hide();
            groupBecomeEmpty(groupName());

        }
    }
}

void LxQtTaskGroup::setToolButtonsStyle(Qt::ToolButtonStyle style)
{
    /*
    foreach (LxQtTaskButton* button, mButtonHash)
    {
        button->setToolButtonStyle(style);
    }
    */

    setToolButtonStyle(style);
}

/************************************************

 ************************************************/
int LxQtTaskGroup::buttonsCount() const
{
    return mButtonHash.count();
}

/************************************************

 ************************************************/
int LxQtTaskGroup::visibleButtonsCount(LxQtTaskButton ** first) const
{
    int i = 0;
    if (first)
        *first = NULL;

    foreach(LxQtTaskButton * btn, mButtonHash.values())
    {

        if (btn->isVisibleTo(mFrame))
        {
            i++;
            if (first && !*first)
                *first = btn;
        }
    }

    return i;
}

void LxQtTaskGroup::draggingTimerTimeout()
{
    if (!windowId())
    {
        raisePopup(true);
    }
    else
    {
        raiseApplication();
    }
}

/************************************************

 ************************************************/
void LxQtTaskGroup::onClicked(bool checked)
{
    if (visibleButtonsCount() > 1)
    {
        setChecked(true);
        if (mFrame->isVisible()  )
        {
            raisePopup(false);
            if (!mButtonHash.contains(KWindowSystem::activeWindow()))
                setChecked(false);
            return;
        }
        raisePopup(true);
        timerEnable(false);
    }
}

/************************************************

 ************************************************/
void LxQtTaskGroup::regroup()
{
    LxQtTaskButton * btn;
    int cont = visibleButtonsCount(&btn);

    recalculateFrameIfVisible();

    if (cont == 1)
    {
        setText(btn->text());
        setWindowId(btn->windowId());
    }
    else
    {
        setText(mGroupName + QString(" - %1 times").arg(cont));
        setWindowId(0);
    }
}

void LxQtTaskGroup::showOnAllDesktopSettingChanged()
{

    refreshVisibility();
    regroup();

    recalculateFrameIfVisible();
}

void LxQtTaskGroup::recalculateFrameIfVisible()
{
    if (mFrame->isVisible())
    {
        recalculateFrameHeight();
        if (mPlugin->panel()->position() == ILxQtPanel::PositionBottom)
            recalculateFramePosition();
    }
}

void LxQtTaskGroup::setAutoRotation(bool value, ILxQtPanel::Position position)
{
    /*
    foreach (LxQtTaskButton * button, mButtonHash)
    {
        //button->setAutoRotation(value,position);
    }
    */
    LxQtTaskButton::setAutoRotation(value,position);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::refreshVisibility()
{
    if (parentTaskBar()->settings().showOnlyCurrentDesktopTasks)
    {
        foreach(LxQtTaskButton * btn, mButtonHash)
        {
            btn->setVisible(btn->desktopNum() == KWindowSystem::currentDesktop());
        }
    }
    else
    {
        foreach(LxQtTaskButton * btn, mButtonHash)
        {
            btn->setVisible(true);
        }
    }

    bool is = isVisible();
    bool will = visibleButtonsCount();
    setVisible(will);

    if (is != will)
        emit visibilityChanged(will);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::arbitraryMimeData(QMimeData *mimedata)
{
    QByteArray byteArray;
    QDataStream stream(&byteArray, QIODevice::WriteOnly);
    qDebug() << QString("Dragging group button: %1").arg(groupName());
    stream << groupName();
    mimedata->setData("lxqt/lxqttaskgroup", byteArray);

    mMimeData = mimedata;
}

/************************************************

 ************************************************/
void LxQtTaskGroup::raisePopup(bool raise)
{
    if (raise)
    {
        //setup geometry
        recalculateFrameHeight();

        int h = height();
        if (mPlugin->panel()->isHorizontal())
            h = width();

        mFrame->setMaximumWidth(parentTaskBar()->settings().buttonWidth);
        mFrame->setMinimumWidth(h);
        mFrame->resize(parentTaskBar()->settings().buttonWidth,mFrame->height());

        recalculateFramePosition();
    }

    //qDebug() << "now " << groupName() << "rlkae" << raise;
    mFrame->setVisible(raise);
}

void LxQtTaskGroup::refreshIconsGeometry()
{
    foreach(LxQtTaskButton * but, mButtonHash)
    {
        but->refreshIconGeometry(mPlugin->panel()->iconSize());
    }

    //group icons are set automatically by panel
    //refreshIconGeometry(parentTaskBar());
}

/************************************************

 ************************************************/
void LxQtTaskGroup::recalculateFrameHeight()
{
    QRect geometry = mPlugin->panel()->globalGometry();
    bool horizontal = mPlugin->panel()->isHorizontal();

    int h = geometry.width() ;
    if (horizontal)
        h = geometry.height();

    if (!horizontal && !parentTaskBar()->settings().autoRotate)
        h = height();

    h /= mPlugin->panel()->lineCount();

    int cont = visibleButtonsCount();
    mFrame->setMaximumHeight(cont * h + (cont +1) * mLayout->spacing());
    mFrame->setMinimumHeight(mFrame->maximumHeight());
    mFrame->resize(mFrame->width(),mFrame->maximumHeight());
    //mFrame->resizeEyeCandy(mFrame->width(),mFrame->maximumHeight());
}

void LxQtTaskGroup::recalculateFramePosition()
{
    //set position
    int x_offset = 0, y_offset = 0;
    switch (mPlugin->panel()->position())
    {
    case ILxQtPanel::PositionBottom:
        y_offset = -mFrame->height() - 5 ; break;
    case ILxQtPanel::PositionTop:
        y_offset = mPlugin->panel()->globalGometry().height() + 5; break;
    case ILxQtPanel::PositionLeft:
        x_offset = mPlugin->panel()->globalGometry().width() + 5; break;
    case ILxQtPanel::PositionRight:
        x_offset = -mFrame->width() - 5;
        break;
    }

    int x, y;
    x = parentWidget()->mapToGlobal(pos()).x() + x_offset ;
    y =    parentWidget()->mapToGlobal(pos()).y() + y_offset;

    mFrame->moveEyeCandy(QPoint(x,y));
}

/************************************************

 ************************************************/
void LxQtTaskGroup::leaveEvent(QEvent *event)
{
    timerEnable(true);
    mShowTimer->stop();
}

/************************************************

 ************************************************/
void LxQtTaskGroup::enterEvent(QEvent *event)
{
    timerEnable(false);
    mShowTimer->start();
}

/************************************************

 ************************************************/
void LxQtTaskGroup::dragEnterEvent(QDragEnterEvent *event)
{
    timerEnable(false);

    bool vis = parentTaskBar()->property("groupvisible").toBool();

    if (!(event->mimeData() == mMimeData))
    {
        raisePopup(vis);
    }
    else if (vis)
    {
        raisePopup(true);
    }


    LxQtTaskButton::dragEnterEvent(event);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::dragLeaveEvent(QDragLeaveEvent * event)
{
    parentTaskBar()->setProperty("groupvisible",mFrame->isVisible());
    raisePopup(false);
    LxQtTaskButton::dragLeaveEvent(event);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::mouseFrameChanged(bool left)
{
    timerEnable(left);
}

/************************************************

 ************************************************/
void LxQtTaskGroup::timerEnable(bool enable)
{
    if (enable)
    {
        mTimer->start();
    }
    else
    {
        mTimer->stop();
    }
}

/************************************************

 ************************************************/
void LxQtTaskGroup::timeoutClose()
{
    raisePopup(false);
    parentTaskBar()->setProperty("groupvisible",false);

    if (!mButtonHash.contains(KWindowSystem::activeWindow()))
    {
        setChecked(false);
    }
}

/************************************************

 ************************************************/
void LxQtTaskGroup::windowChanged(WId window, NET::Properties prop, NET::Properties2 prop2)
{
    LxQtTaskButton* button = mButtonHash.value(window);
    if (!button)
        return;

    // window changed virtual desktop
    if (prop.testFlag(NET::WMDesktop))
    {
        if (parentTaskBar()->settings().showOnlyCurrentDesktopTasks)
        {
            int desktop = button->desktopNum();
            button->setHidden(desktop != NET::OnAllDesktops && desktop != KWindowSystem::currentDesktop());
            refreshVisibility();
            regroup();
        }
    }

    if (prop.testFlag(NET::WMVisibleName) || prop.testFlag(NET::WMName))
        button->updateText();

    // FIXME: NET::WMIconGeometry is causing high CPU and memory usage
    if (prop.testFlag(NET::WMIcon) /*|| prop.testFlag(NET::WMIconGeometry)*/)
        button->updateIcon();

    if (prop.testFlag(NET::WMState))
        button->setUrgencyHint(KWindowInfo(window, NET::WMState).hasState(NET::DemandsAttention));
}

/************************************************

 ************************************************/





