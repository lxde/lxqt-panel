/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXDE-Qt - a lightweight, Qt based, desktop toolset
 * http://razor-qt.org
 *
 * Copyright: 2011 Razor team
 * Authors:
 *   Petr Vanek <petr@scribus.info>
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

#include <QButtonGroup>
#include <QToolButton>
#include <QWheelEvent>
#include <QtDebug>
#include <QSignalMapper>
#include <lxqt-globalkeys.h>
#include <LXQt/GridLayout>
#include <KWindowSystem/KWindowSystem>
#include <KWindowSystem/NETWM>
#include <QX11Info>
#include <cmath>

#include "desktopswitch.h"
#include "desktopswitchbutton.h"
#include "desktopswitchconfiguration.h"

DesktopSwitch::DesktopSwitch(const ILxQtPanelPluginStartupInfo &startupInfo) :
    QObject(),
    ILxQtPanelPlugin(startupInfo),
    m_pSignalMapper(new QSignalMapper(this)),
    m_desktopCount(KWindowSystem::numberOfDesktops()),
    mRows(1),
    mDesktops(new NETRootInfo(QX11Info::connection(), NET::NumberOfDesktops | NET::CurrentDesktop | NET::DesktopNames, NET::WM2DesktopLayout)),
    mLabelType(DesktopSwitchButton::LABEL_TYPE_NUMBER)
{
    m_buttons = new QButtonGroup(this);
    connect (m_pSignalMapper, SIGNAL(mapped(int)), this, SLOT(setDesktop(int)));

    mLayout = new LxQt::GridLayout(&mWidget);
    mWidget.setLayout(mLayout);
    settingsChanged(); // also calls setup

    connect(KWindowSystem::self(), SIGNAL(numberOfDesktopsChanged(int)), SLOT(onNumberOfDesktopsChanged(int)));
    connect(KWindowSystem::self(), SIGNAL(currentDesktopChanged(int)), SLOT(onCurrentDesktopChanged(int)));
    connect(KWindowSystem::self(), SIGNAL(desktopNamesChanged()), SLOT(onDesktopNamesChanged()));
}

void DesktopSwitch::setup()
{
    // clear current state
    foreach (QAbstractButton * b, m_buttons->buttons())
    {
        // TODO/FIXME: maybe it has to be removed from layout too?
        m_pSignalMapper->removeMappings(b);
        m_buttons->removeButton(b);
//        dynamic_cast<DesktopSwitchButton*>(b)->unregisterShortcut();
        delete b;
    }

    // create new desktop layout
    for (int i = 0; i < m_desktopCount; ++i)
    {
        QString path = QString("/panel/%1/desktop_%2").arg(settings()->group()).arg(i + 1);
        QString shortcut = QString("Control+F%1").arg(i + 1);

        DesktopSwitchButton * m = new DesktopSwitchButton(&mWidget, i, path, shortcut, mLabelType,
                                                          KWindowSystem::desktopName(i + 1).isEmpty() ?
                                                          tr("Desktop %1").arg(i + 1) :
                                                          KWindowSystem::desktopName(i + 1));
        m_pSignalMapper->setMapping(m, i);
        connect(m, SIGNAL(activated()), m_pSignalMapper, SLOT(map())) ;
        mWidget.layout()->addWidget(m);
        m_buttons->addButton(m, i);
    }

    int activeDesk = KWindowSystem::currentDesktop();
    QAbstractButton * button = m_buttons->button(activeDesk - 1);
    if (button)
        button->setChecked(true);

    connect(m_buttons, SIGNAL(buttonClicked(int)),
            this, SLOT(setDesktop(int)));

}

DesktopSwitch::~DesktopSwitch()
{
}

void DesktopSwitch::setDesktop(int desktop)
{
    KWindowSystem::setCurrentDesktop(desktop + 1);
}

void DesktopSwitch::onNumberOfDesktopsChanged(int count)
{
    if (m_desktopCount != count)
    {
        qDebug() << "Desktop count changed from" << m_desktopCount << "to" << count;
        m_desktopCount = count;
        onDesktopNamesChanged();
        setup();
    }
}

void DesktopSwitch::onCurrentDesktopChanged(int current)
{
    QAbstractButton *button = m_buttons->button(current - 1);
    if (button)
        button->setChecked(true);
}

void DesktopSwitch::onDesktopNamesChanged()
{
    QStringList names;
    const int count = KWindowSystem::numberOfDesktops();
    for (int i = 0; i < count; ++i)
        names << KWindowSystem::desktopName(i + 1);
    m_desktopNames = names;
    setup();
}

void DesktopSwitch::settingsChanged()
{
    mRows = settings()->value("rows", 1).toInt();
    mLabelType = static_cast<DesktopSwitchButton::LabelType>(settings()->value("labelType", 0).toInt());
    setup();
}

void DesktopSwitch::realign()
{
    int columns = static_cast<int>(ceil(static_cast<float>(m_desktopCount) / mRows));
    mLayout->setEnabled(false);
    if (panel()->isHorizontal())
    {
        mLayout->setRowCount(mRows);
        mLayout->setColumnCount(0);
        mDesktops->setDesktopLayout(NET::OrientationHorizontal, columns, mRows, NET::DesktopLayoutCornerTopLeft);
    }
    else
    {
        mLayout->setColumnCount(mRows);
        mLayout->setRowCount(0);
        mDesktops->setDesktopLayout(NET::OrientationHorizontal, mRows, columns, NET::DesktopLayoutCornerTopLeft);
    }
    mLayout->setEnabled(true);
}

QDialog *DesktopSwitch::configureDialog()
{
    return new DesktopSwitchConfiguration(settings(), &mWidget);
}

DesktopSwitchWidget::DesktopSwitchWidget():
    QFrame(),
    m_mouseWheelThresholdCounter(0)
{
}

void DesktopSwitchWidget::wheelEvent(QWheelEvent *e)
{
    // Without some sort of threshold which has to be passed, scrolling is too sensitive
    m_mouseWheelThresholdCounter -= e->delta();
    // If the user hasn't scrolled far enough in one direction (positive or negative): do nothing
    if(abs(m_mouseWheelThresholdCounter) < 100)
        return;
    
    int max = KWindowSystem::numberOfDesktops();
    int delta = e->delta() < 0 ? 1 : -1;
    int current = KWindowSystem::currentDesktop() + delta;

    if (current > max){
        current = 1;
    }
    else if (current < 1)
        current = max;

    m_mouseWheelThresholdCounter = 0;
    KWindowSystem::setCurrentDesktop(current);
}
