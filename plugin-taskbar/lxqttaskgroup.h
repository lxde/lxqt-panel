#ifndef LXQTTASKGROUP_H
#define LXQTTASKGROUP_H

#include <QDialog>
#include "lxqttaskbutton.h"
#include <KF5/KWindowSystem/kwindowsystem.h>

class QVBoxLayout;
class ILxQtPanelPlugin;

class LxQtLooseFocusFrame;

class LxQtTaskGroup: public LxQtTaskButton
{
    Q_OBJECT
public:
    LxQtTaskGroup(const QString & groupName, QIcon icon ,ILxQtPanelPlugin * plugin, LxQtTaskBar * parent);

    void removeButton(WId window);
    int buttonsCount() const;
    int visibleButtonsCount(LxQtTaskButton ** first = NULL) const;


    LxQtTaskButton * createButton(WId id);
    LxQtTaskButton * checkedButton() const;
    bool checkNextPrevChild(bool next, bool modulo);

    QString groupName() const {return mGroupName;}

    void refreshIconsGeometry();
    void showOnAllDesktopSettingChanged();
    void setAutoRotation(bool value, ILxQtPanel::Position position);
    void setToolButtonsStyle(Qt::ToolButtonStyle style);
    void hidePopup(void) {raisePopup(false);}
    void showPopup(void) {raisePopup(true);}


protected:
    void arbitraryMimeData(QMimeData * mime);

    void leaveEvent(QEvent * event);
    void enterEvent(QEvent * event);
    void dragEnterEvent(QDragEnterEvent * event);
    void dragLeaveEvent(QDragLeaveEvent * event);
    void contextMenuEvent(QContextMenuEvent * event);

    QString acceptMimeData() const {return QString("lxqt/lxqttaskgroup");}
    void draggingTimerTimeout();

private slots:
    void onClicked(bool checked);
    void onChildButtonClicked();
    void onActiveWindowChanged(WId window);
    void onWindowRemoved(WId window);
    void onDesktopChanged(int number);
    void windowChanged(WId window, NET::Properties prop, NET::Properties2 prop2);

    void mouseFrameChanged(bool left);
    void timeoutClose(void);
    void timeoutRaise(void);
    void closeGroup(void);

signals:
    void groupBecomeEmpty(QString name);
    void visibilityChanged(bool visible);

private:


    QString mGroupName;
    LxQtLooseFocusFrame * mFrame;
    LxQtTaskButtonHash mButtonHash;
    QVBoxLayout * mLayout;
    ILxQtPanelPlugin * mPlugin;
    QTimer * mTimer;
    QTimer * mShowTimer;
    const QMimeData * mMimeData;


    void raisePopup(bool raise);
    void recalculateFrameHeight();
    void recalculateFramePosition();
    void recalculateFrameIfVisible();
    void refreshVisibility();
    void regroup(void);
    void timerEnable(bool enable);
};


#endif // LXQTTASKGROUP_H
