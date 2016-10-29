/***************************************************************************
 *   Copyright (C) 2016 by Nicolas Carion                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA          *
 ***************************************************************************/


#include "draggablelabel.h"
#include <QMouseEvent>
#include <QApplication>
#include "klocalizedstring.h"


DraggableLabel::DraggableLabel(const QString &text, QWidget *parent):
    QLabel(text, parent)
    , m_dragStarted(false)
{
    setContextMenuPolicy(Qt::NoContextMenu);
    setToolTip(i18n("Click to copy data to clipboard"));
}

void DraggableLabel::mousePressEvent(QMouseEvent *ev)
{
    QLabel::mousePressEvent(ev);
    if (ev->button() == Qt::LeftButton) {
        m_clickStart = ev->pos();
        m_dragStarted = false;
    }
}

void DraggableLabel::mouseReleaseEvent(QMouseEvent *ev)
{
    // Don't call mouserelease in cas of drag because label might be deleted by a drop
    if (!m_dragStarted)
        QLabel::mouseReleaseEvent(ev);
    else
        ev->ignore();
    m_clickStart = QPoint();
}

void DraggableLabel::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_dragStarted) {
        ev->ignore();
        return;
    }
    QLabel::mouseMoveEvent(ev);
    if (!m_clickStart.isNull() && (m_clickStart - ev->pos()).manhattanLength() >= QApplication::startDragDistance()) {
        emit startDrag(objectName());
        m_dragStarted = true;
        m_clickStart = QPoint();
    }
}

