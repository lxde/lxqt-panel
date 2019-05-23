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


#ifndef LXQT_COLORPICKER_H
#define LXQT_COLORPICKER_H

#include "../panel/ilxqtpanelplugin.h"
#include <QFrame>
#include <QToolButton>


class QMenu;
class ColorButton;


class ColorPickerWidget : public QWidget
{
    Q_OBJECT

public:

    ColorPickerWidget(QWidget* parent = nullptr);
    ~ColorPickerWidget();

    QMenu*       popupMenu() { return mColorsMenu; }
    QToolButton* pickerButton() { return mPickerButton; }
    ColorButton* colorButton() { return mColorButton; }
    void         update(bool isHorizontal);

signals:

    void showMenuRequested(QMenu* menu);

protected:

    void mouseReleaseEvent(QMouseEvent *event);

private slots:

    void captureMouse();

private:

    static const QString svgIcon;

    QMenu         *mColorsMenu      { nullptr };
    QToolButton   *mPickerButton    { nullptr };
    ColorButton   *mColorButton     { nullptr };
    QAction       *mClearListAction { nullptr };
    QFrame        *mSeparator       { nullptr };
    bool           mCapturing       { false };
    QList <QColor> mColorsList      {};

    QIcon colorIcon(QColor color);
    void  buildMenu();
    void  paste(const QString color) const;
};


class ColorPicker : public QObject, public ILXQtPanelPlugin
{
    Q_OBJECT

public:

    ColorPicker(const ILXQtPanelPluginStartupInfo &startupInfo);
    ~ColorPicker();

    virtual QWidget *widget() { return mWidget; }
    virtual QString themeId() const { return "ColorPicker"; }

    bool isSeparate() const { return true; }
    void realign();

private:

    ColorPickerWidget *mWidget;
};


class ColorPickerPlugin: public QObject, public ILXQtPanelPluginLibrary
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "lxqt.org/Panel/PluginInterface/3.0")
    Q_INTERFACES(ILXQtPanelPluginLibrary)

public:

    ILXQtPanelPlugin *instance(const ILXQtPanelPluginStartupInfo &startupInfo) const
    {
        return new ColorPicker(startupInfo);
    }
};


#endif
