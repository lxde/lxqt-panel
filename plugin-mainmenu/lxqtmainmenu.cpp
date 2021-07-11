/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2010-2011 Razor team
 * Authors:
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
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

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */


#include "lxqtmainmenu.h"
#include "lxqtmainmenuconfiguration.h"
#include "../panel/lxqtpanel.h"
#include "actionview.h"
#include <QAction>
#include <QTimer>
#include <QMessageBox>
#include <QEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWidgetAction>
#include <QLineEdit>
#include <lxqt-globalkeys.h>
#include <algorithm> // for find_if()
#include <QApplication>

#include <XdgMenuWidget>
#include <XdgIcon>

#ifdef HAVE_MENU_CACHE
    #include "xdgcachedmenu.h"
#else
    #include <QStandardPaths>
    #include <QClipboard>
    #include <QMimeData>
    #include <XdgAction>
#endif

#define DEFAULT_SHORTCUT "Alt+F1"

LXQtMainMenu::LXQtMainMenu(const ILXQtPanelPluginStartupInfo &startupInfo):
    QObject(),
    ILXQtPanelPlugin(startupInfo),
    mMenu(nullptr),
    mShortcut(nullptr),
    mSearchEditAction{new QWidgetAction{this}},
    mSearchViewAction{new QWidgetAction{this}},
    mMakeDirtyAction{new QAction{this}},
    mFilterMenu(true),
    mFilterShow(true),
    mFilterClear(false),
    mFilterShowHideMenu(true),
    mHeavyMenuChanges(false)
{
#ifdef HAVE_MENU_CACHE
    mMenuCache = nullptr;
    mMenuCacheNotify = nullptr;
#endif

    mDelayedPopup.setSingleShot(true);
    mDelayedPopup.setInterval(200);
    connect(&mDelayedPopup, &QTimer::timeout, this, &LXQtMainMenu::showHideMenu);
    mHideTimer.setSingleShot(true);
    mHideTimer.setInterval(250);

    mSearchTimer.setSingleShot(true);
    connect(&mSearchTimer, &QTimer::timeout, this, &LXQtMainMenu::searchMenu);
    mSearchTimer.setInterval(350); // typing speed (not very fast)

    mButton.setAutoRaise(true);
    mButton.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    //Notes:
    //1. installing event filter to parent widget to avoid infinite loop
    //   (while setting icon we also need to set the style)
    //2. delaying of installEventFilter because in c-tor mButton has no parent widget
    //   (parent is assigned in panel's logic after widget() call)
    QTimer::singleShot(0, mButton.parentWidget(), [this] {
        Q_ASSERT(mButton.parentWidget());
        mButton.parentWidget()->installEventFilter(this);
    });

    connect(&mButton, &QToolButton::clicked, this, &LXQtMainMenu::showHideMenu);

    mSearchView = new ActionView;
    mSearchView->setVisible(false);
    mSearchView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mSearchView, &QAbstractItemView::activated, this, &LXQtMainMenu::showHideMenu);
    connect(mSearchView, &ActionView::requestShowHideMenu, this, &LXQtMainMenu::showHideMenu);
    connect(mSearchView, &QWidget::customContextMenuRequested, this, &LXQtMainMenu::onRequestingCustomMenu);
    mSearchViewAction->setDefaultWidget(mSearchView);
    mSearchEdit = new QLineEdit;
    mSearchEdit->setClearButtonEnabled(true);
    mSearchEdit->setPlaceholderText(LXQtMainMenu::tr("Search..."));
    connect(mSearchEdit, &QLineEdit::textChanged, this, [this] (QString const &) {
        mSearchTimer.start();
    });
    connect(mSearchEdit, &QLineEdit::returnPressed, mSearchView, &ActionView::activateCurrent);
    mSearchEditAction->setDefaultWidget(mSearchEdit);
    QTimer::singleShot(0, this, [this] {
        settingsChanged();
    });

    mShortcut = GlobalKeyShortcut::Client::instance()->addAction(QString{}, QStringLiteral("/panel/%1/show_hide").arg(settings()->group()), LXQtMainMenu::tr("Show/hide main menu"), this);
    if (mShortcut)
    {
        connect(mShortcut, &GlobalKeyShortcut::Action::registrationFinished, this, [this] {
            if (mShortcut->shortcut().isEmpty())
                mShortcut->changeShortcut(QStringLiteral(DEFAULT_SHORTCUT));
        });
        connect(mShortcut, &GlobalKeyShortcut::Action::activated, this, [this] {
            if (!mHideTimer.isActive())
                // Delay this a little -- if we don't do this, search field
                // won't be able to capture focus
                // See <https://github.com/lxqt/lxqt-panel/pull/131> and
                // <https://github.com/lxqt/lxqt-panel/pull/312>
                mDelayedPopup.start();
        });
    }
}


/************************************************

 ************************************************/
LXQtMainMenu::~LXQtMainMenu()
{
    mButton.parentWidget()->removeEventFilter(this);
    if (mMenu)
    {
        mMenu->removeAction(mSearchEditAction);
        mMenu->removeAction(mSearchViewAction);
        delete mMenu;
    }
#ifdef HAVE_MENU_CACHE
    if(mMenuCache)
    {
        menu_cache_remove_reload_notify(mMenuCache, mMenuCacheNotify);
        menu_cache_unref(mMenuCache);
    }
#endif
}


/************************************************

 ************************************************/
void LXQtMainMenu::showHideMenu()
{
    if (mMenu && mMenu->isVisible())
        mMenu->hide();
    else
        showMenu();
}

/************************************************

 ************************************************/
void LXQtMainMenu::showMenu()
{
    if (!mMenu)
        return;

    willShowWindow(mMenu);
    // Just using Qt`s activateWindow() won't work on some WMs like Kwin.
    // Solution is to execute menu 1ms later using timer
    mMenu->popup(calculatePopupWindowPos(mMenu->sizeHint()).topLeft());
    if (mFilterMenu || mFilterShow)
    {
        //Note: part of the workadound for https://bugreports.qt.io/browse/QTBUG-52021
        mSearchEdit->setReadOnly(false);
        //the setReadOnly also changes the cursor, override it back to normal
        mSearchEdit->unsetCursor();
        mSearchEdit->setFocus();
    }
}

#ifdef HAVE_MENU_CACHE
// static
void LXQtMainMenu::menuCacheReloadNotify(MenuCache* cache, gpointer user_data)
{
    reinterpret_cast<LXQtMainMenu*>(user_data)->buildMenu();
}
#endif

/************************************************

 ************************************************/
void LXQtMainMenu::settingsChanged()
{
    setButtonIcon();
    if (settings()->value(QStringLiteral("showText"), false).toBool())
    {
        mButton.setText(settings()->value(QStringLiteral("text"), QStringLiteral("Start")).toString());
        mButton.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    else
    {
        mButton.setText(QLatin1String(""));
        mButton.setToolButtonStyle(Qt::ToolButtonIconOnly);
    }

    mLogDir = settings()->value(QStringLiteral("log_dir"), QString()).toString();

    QString menu_file = settings()->value(QStringLiteral("menu_file"), QString()).toString();
    if (menu_file.isEmpty())
        menu_file = XdgMenu::getMenuFileName();

    if (mMenuFile != menu_file)
    {
        mMenuFile = menu_file;
#ifdef HAVE_MENU_CACHE
        menu_cache_init(0);
        if(mMenuCache)
        {
            menu_cache_remove_reload_notify(mMenuCache, mMenuCacheNotify);
            menu_cache_unref(mMenuCache);
        }
        mMenuCache = menu_cache_lookup(mMenuFile.toLocal8Bit().constData());
        if (MenuCacheDir * root = menu_cache_dup_root_dir(mMenuCache))
        {
            menu_cache_item_unref(MENU_CACHE_ITEM(root));
            buildMenu();
        }
        mMenuCacheNotify = menu_cache_add_reload_notify(mMenuCache, (MenuCacheReloadNotify)menuCacheReloadNotify, this);
#else
        mXdgMenu.setEnvironments(QStringList() << QStringLiteral("X-LXQT") << QStringLiteral("LXQt"));
        mXdgMenu.setLogDir(mLogDir);

        bool res = mXdgMenu.read(mMenuFile);
        connect(&mXdgMenu, &XdgMenu::changed, this, &LXQtMainMenu::buildMenu);
        if (res)
        {
            QTimer::singleShot(1000, this, &LXQtMainMenu::buildMenu);
        }
        else
        {
            QMessageBox::warning(nullptr, QStringLiteral("Parse error"), mXdgMenu.errorString());
            return;
        }
#endif
    }

    setMenuFontSize();

    //clear the search to not leaving the menu in wrong state
    mSearchEdit->setText(QString{});
    mFilterMenu = settings()->value(QStringLiteral("filterMenu"), true).toBool();
    mFilterShow = settings()->value(QStringLiteral("filterShow"), true).toBool();
    mFilterClear = settings()->value(QStringLiteral("filterClear"), true).toBool();
    mFilterShowHideMenu = settings()->value(QStringLiteral("filterShowHideMenu"), true).toBool();
    if (mMenu)
    {
        mSearchEdit->setVisible(mFilterMenu || mFilterShow);
        mSearchEditAction->setVisible(mFilterMenu || mFilterShow);
        if (mFilterClear && !mMenu->isVisible())
            mSearchEdit->clear();
    }
    mSearchView->setMaxItemsToShow(settings()->value(QStringLiteral("filterShowMaxItems"), 10).toInt());
    mSearchView->setMaxItemWidth(settings()->value(QStringLiteral("filterShowMaxWidth"), 300).toInt());

    realign();
}

static bool filterMenu(QMenu * menu, QString const & filter)
{
    bool has_visible = false;
    const auto actions = menu->actions();
    for (auto const & action : actions)
    {
        if (QMenu * sub_menu = action->menu())
        {
            action->setVisible(filterMenu(sub_menu, filter)/*recursion*/);
            has_visible |= action->isVisible();
        } else if (nullptr != qobject_cast<QWidgetAction *>(action))
        {
            //our searching widget
            has_visible = true;
        } else if (!action->isSeparator())
        {
            //real menu action -> app
            bool visible(filter.isEmpty() || action->text().contains(filter, Qt::CaseInsensitive) || action->toolTip().contains(filter, Qt::CaseInsensitive));
#ifndef HAVE_MENU_CACHE
            if(!visible)
            {
                if (XdgAction * xdgAction = qobject_cast<XdgAction *>(action))
                {
                    const XdgDesktopFile& df = xdgAction->desktopFile();
                    QStringList list = df.expandExecString();
                    if (!list.isEmpty())
                    {
                        if (list.at(0).contains(filter, Qt::CaseInsensitive))
                            visible = true;
                    }
                }
            }
#endif
            action->setVisible(visible);
            has_visible |= action->isVisible();
        }
    }
    return has_visible;
}

static void showHideMenuEntries(QMenu * menu, bool show)
{
    //show/hide the top menu entries
    const auto actions = menu->actions();
    for (auto const & action : actions)
    {
        if (nullptr == qobject_cast<QWidgetAction *>(action))
        {
            action->setVisible(show);
        }
    }
}

static void setTranslucentMenus(QMenu * menu)
{
    menu->setAttribute(Qt::WA_TranslucentBackground);
    const auto actions = menu->actions();
    for (auto const & action : actions)
    {
        if (QMenu * sub_menu = action->menu())
        {
            setTranslucentMenus(sub_menu);
        }
    }
}

/************************************************

 ************************************************/
void LXQtMainMenu::searchMenu()
{
    const QString text = mSearchEdit->text();
    if (mFilterShow)
    {
        mHeavyMenuChanges = true;
        const bool shown = !text.isEmpty();
        if (mFilterShowHideMenu)
            showHideMenuEntries(mMenu, !shown);
        if (shown)
            mSearchView->setFilter(text);
        mSearchView->setVisible(shown);
        mSearchViewAction->setVisible(shown);
        //TODO: how to force the menu to recalculate it's size in a more elegant way?
        mMenu->addAction(mMakeDirtyAction);
        mMenu->removeAction(mMakeDirtyAction);
        mHeavyMenuChanges = false;
    }
    if (mFilterMenu && !(mFilterShow && mFilterShowHideMenu))
        filterMenu(mMenu, text);

}

/************************************************

 ************************************************/
void LXQtMainMenu::setSearchFocus(QAction *action)
{
    if (mFilterMenu || mFilterShow)
    {
        if(action == mSearchEditAction)
            mSearchEdit->setFocus();
        else
            mSearchEdit->clearFocus();
    }
}

static void menuInstallEventFilter(QMenu * menu, QObject * watcher)
{
    for (auto const & action : const_cast<QList<QAction *> const &&>(menu->actions()))
    {
        if (action->menu())
            menuInstallEventFilter(action->menu(), watcher); // recursion
    }
    menu->installEventFilter(watcher);
}

/************************************************

 ************************************************/
void LXQtMainMenu::buildMenu()
{
    if(mMenu)
    {
        mMenu->removeAction(mSearchEditAction);
        mMenu->removeAction(mSearchViewAction);
        delete mMenu;
    }
#ifdef HAVE_MENU_CACHE
    mMenu = new XdgCachedMenu(mMenuCache, &mButton);
#else
    mMenu = new XdgMenuWidget(mXdgMenu, QLatin1String(""), &mButton);
    addContextMenu(mMenu);
#endif
    mMenu->setObjectName(QStringLiteral("TopLevelMainMenu"));
    setTranslucentMenus(mMenu);
    // Note: the QWidget::ensurePolished() workarounds problem with transparent
    // QLineEdit (mSearchEditAction) in menu with Breeze style
    // https://bugs.kde.org/show_bug.cgi?id=368048
    mMenu->ensurePolished();
    mMenu->setStyle(&mTopMenuStyle);

    mMenu->addSeparator();

    menuInstallEventFilter(mMenu, this);
    connect(mMenu, &QMenu::aboutToHide, &mHideTimer, QOverload<>::of(&QTimer::start));
    connect(mMenu, &QMenu::aboutToShow, &mHideTimer, &QTimer::stop);

    mMenu->addSeparator();
    mMenu->addAction(mSearchViewAction);
    mMenu->addAction(mSearchEditAction);
    connect(mMenu, &QMenu::hovered, this, &LXQtMainMenu::setSearchFocus);
    //Note: setting readOnly to true to avoid wake-ups upon the Qt's internal "blink" cursor timer
    //(if the readOnly is not set, the "blink" timer is active also in case the menu is not shown ->
    //QWidgetLineControl::updateNeeded is performed w/o any need)
    //https://bugreports.qt.io/browse/QTBUG-52021
    connect(mMenu, &QMenu::aboutToHide, mSearchEdit, [this] {
        mSearchEdit->setReadOnly(true);
        if (mFilterClear)
            mSearchEdit->clear();
    });
    mSearchEdit->setVisible(mFilterMenu || mFilterShow);
    mSearchEditAction->setVisible(mFilterMenu || mFilterShow);
    mSearchView->fillActions(mMenu);

    searchMenu();
    setMenuFontSize();
}

/************************************************

 ************************************************/
void LXQtMainMenu::addContextMenu(QMenu *menu)
{
    const auto actions = menu->actions();
    for (auto const & action : actions)
    {
        if (action->menu())
        {
            action->menu()->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(action->menu(), &QWidget::customContextMenuRequested, this, &LXQtMainMenu::onRequestingCustomMenu);
            addContextMenu(action->menu());
        }
    }
}

void LXQtMainMenu::onRequestingCustomMenu(const QPoint& p)
{
#ifdef HAVE_MENU_CACHE
    Q_UNUSED(p)
    return;
#else
    QMenu *parentMenu = qobject_cast<QMenu*>(QObject::sender());
    ActionView *parentView = qobject_cast<ActionView*>(QObject::sender());
    QAction *action;
    QPoint globalPos;
    if (parentView != nullptr) {
        action = parentView->indexAt(p).data(ActionView::ActionRole).value<QAction*>();
        if (action == nullptr)
            return;
        globalPos = parentView->mapToGlobal(p);
    }
    else if (parentMenu != nullptr) {
        action = parentMenu->actionAt(p);
        if (action == nullptr || action->menu() != nullptr || action->isSeparator())
            return;
        globalPos = parentMenu->mapToGlobal(p);
    }
    else {
        return;
    }
    XdgAction *xdgAction = qobject_cast<XdgAction *>(action);
    if (xdgAction == nullptr)
        return;
    const XdgDesktopFile& df = xdgAction->desktopFile();
    QString file = df.fileName();

    QMenu menu;
    QAction *a;

    if (df.actions().count() > 0 && df.type() == XdgDesktopFile::Type::ApplicationType)
    {
        for (int i = 0; i < df.actions().count(); ++i)
        {
            QString actionString(df.actions().at(i));
            a = menu.addAction(df.actionIcon(actionString), df.actionName(actionString));
            connect(a, &QAction::triggered, this, [this, df, actionString] {
                df.actionActivate(actionString, QStringList());
                mMenu->hide();
            });
        }
        menu.addSeparator();
    }

    a = menu.addAction(XdgIcon::fromTheme(QLatin1String("desktop")), tr("Add to desktop"));
    connect(a, &QAction::triggered, [file] {
        QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        QString desktopFile = desktop + QStringLiteral("/") + file.section(QStringLiteral("/"), -1);
        if (QFile::exists(desktopFile))
        {
            QMessageBox::StandardButton btn =
            QMessageBox::question(nullptr,
                                  tr("Question"),
                                  tr("A file with the same name already exists.\nDo you want to overwrite it?"));
            if (btn == QMessageBox::No)
                return;
            if (!QFile::remove(desktopFile))
            {
                QMessageBox::warning(nullptr,
                                     tr("Warning"),
                                     tr("The file cannot be overwritten."));
                return;
            }
        }
        QFile::copy(file, desktopFile);
    });
    a = menu.addAction(XdgIcon::fromTheme(QLatin1String("edit-copy")), tr("Copy"));
    connect(a, &QAction::triggered, this, [file] {
        QClipboard* clipboard = QApplication::clipboard();
        QMimeData* data = new QMimeData();
        data->setData(QStringLiteral("text/uri-list"), QUrl::fromLocalFile(file).toEncoded()
                                                       + QByteArray("\r\n"));
        clipboard->setMimeData(data);
    });
    menu.exec(globalPos);
#endif
}

/************************************************

 ************************************************/
void LXQtMainMenu::setMenuFontSize()
{
    if (!mMenu)
        return;

    QFont menuFont = mButton.font();
    bool customFont = settings()->value(QStringLiteral("customFont"), false).toBool();

    if(customFont)
    {
        menuFont = mMenu->font();
        menuFont.setPointSize(settings()->value(QStringLiteral("customFontSize")).toInt());
    }

    if (mMenu->font() != menuFont)
    {
        mMenu->setFont(menuFont);
        const QList<QMenu*> subMenuList = mMenu->findChildren<QMenu*>();
        for (QMenu* const subMenu : subMenuList)
        {
            subMenu->setFont(menuFont);
        }
        mSearchEdit->setFont(menuFont);
        mSearchView->setFont(menuFont);
    }

    // icon size the same as the font height if a custom font is selected,
    // otherwise use the default size
    int icon_size = (customFont ? QFontMetrics(menuFont).height()
                                : MenuStyle::DEFAULT_ICON_SIZE);
    mTopMenuStyle.setIconSize(icon_size);

    // get the size back from the style (this will resolve DEFAULT_ICON_SIZE
    // to an actual pixel size if necessary)
    icon_size = mTopMenuStyle.pixelMetric(QStyle::PM_SmallIconSize);
    mSearchView->setIconSize(QSize{icon_size, icon_size});
}


/************************************************

 ************************************************/
void LXQtMainMenu::setButtonIcon()
{
    if (settings()->value(QStringLiteral("ownIcon"), false).toBool())
    {
        mButton.setStyleSheet(QStringLiteral("#MainMenu { qproperty-icon: url(%1); }")
                .arg(settings()->value(QLatin1String("icon"), QLatin1String(LXQT_GRAPHICS_DIR"/helix.svg")).toString()));
    } else
    {
        mButton.setStyleSheet(QString());
    }
}


/************************************************

 ************************************************/
QDialog *LXQtMainMenu::configureDialog()
{
    return new LXQtMainMenuConfiguration(settings(), mShortcut, QStringLiteral(DEFAULT_SHORTCUT));
}
/************************************************

 ************************************************/

// functor used to match a QAction by prefix
struct MatchAction
{
    MatchAction(QString key):key_(key) {}
    bool operator()(QAction* action) { return action->text().startsWith(key_, Qt::CaseInsensitive); }
    QString key_;
};

bool LXQtMainMenu::eventFilter(QObject *obj, QEvent *event)
{
    if(obj == mButton.parentWidget())
    {
        // the application is given a new QStyle
        if(event->type() == QEvent::StyleChange)
        {
            setMenuFontSize();
            setButtonIcon();
        }
    }
    else if(QMenu* menu = qobject_cast<QMenu*>(obj))
    {
        if(event->type() == QEvent::KeyPress)
        {
            // if our shortcut key is pressed while the menu is open, close the menu
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier))
            {
                mHideTimer.start();
                mMenu->hide(); // close the app menu
                return true;
            }
            else // go to the menu item which starts with the pressed key if there is an active action.
            {
                QString key = keyEvent->text();
                if(key.isEmpty())
                    return false;
                QAction* action = menu->activeAction();
                if(action !=nullptr) {
                    QList<QAction*> actions = menu->actions();
                    QList<QAction*>::iterator it = std::find(actions.begin(), actions.end(), action);
                    it = std::find_if(it + 1, actions.end(), MatchAction(key));
                    if(it == actions.end())
                        it = std::find_if(actions.begin(), it, MatchAction(key));
                    if(it != actions.end())
                        menu->setActiveAction(*it);
                }
            }
        }

        if (obj == mMenu)
        {
            if (event->type() == QEvent::Resize)
            {
                QResizeEvent * e = dynamic_cast<QResizeEvent *>(event);
                if (e->oldSize().isValid() && e->oldSize() != e->size())
                {
                    mMenu->move(calculatePopupWindowPos(e->size()).topLeft());
                }
            } else if (event->type() == QEvent::KeyPress)
            {
                QKeyEvent * e = dynamic_cast<QKeyEvent*>(event);
                if (Qt::Key_Escape == e->key())
                {
                    if (!mSearchEdit->text().isEmpty())
                    {
                        mSearchEdit->setText(QString{});
                        //filter out this to not close the menu
                        return true;
                    }
                }
            } else if (QEvent::ActionChanged == event->type()
                    || QEvent::ActionAdded == event->type())
            {
                //filter this if we are performing heavy changes to reduce flicker
                if (mHeavyMenuChanges)
                    return true;
            }
        }
    }
    return false;
}

#undef DEFAULT_SHORTCUT
