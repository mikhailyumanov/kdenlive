/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/
#include "snapmodel.hpp"
#include <limits.h>
#include <QDebug>


SnapModel::SnapModel()
{
}

void SnapModel::addPoint(int position)
{
    if (m_snaps.count(position) == 0) {
        m_snaps[position] = 1;
    } else {
        m_snaps[position]++;
    }
}

void SnapModel::removePoint(int position)
{
    Q_ASSERT(m_snaps.count(position) > 0);
    if (m_snaps[position] == 1) {
        m_snaps.erase(position);
    } else {
        m_snaps[position]--;
    }
}

int SnapModel::getClosestPoint(int position)
{
    if (m_snaps.size() == 0) {
        return -1;
    }
    auto it = m_snaps.lower_bound(position);
    long long int prev = INT_MIN, next = INT_MAX;
    if (it != m_snaps.end()) {
        next = (*it).first;
    }
    if (it != m_snaps.begin()) {
        --it;
        prev = (*it).first;
    }
    if (std::abs((long long)position - prev) < std::abs((long long)position - next)) {
        return (int)prev;
    }
    return (int)next;
}

int SnapModel::getNextPoint(int position)
{
    if (m_snaps.size() == 0) {
        return position;
    }
    auto it = m_snaps.lower_bound(position + 1);
    long long int next = position;
    if (it != m_snaps.end()) {
        next = (*it).first;
    }
    return (int)next;
}

int SnapModel::getPreviousPoint(int position)
{
    if (m_snaps.size() == 0) {
        return 0;
    }
    auto it = m_snaps.lower_bound(position);
    long long int prev = 0;
    if (it != m_snaps.begin()) {
        --it;
        prev = (*it).first;
    }
    return (int)prev;
}

void SnapModel::ignore(const std::vector<int>& pts)
{
    for (int pt : pts) {
        auto it = m_snaps.find(pt);
        if (it != m_snaps.end()) {
            m_ignore.push_back(*it);
            m_snaps.erase(it);
        }
    }
}

void SnapModel::unIgnore()
{
    for (const auto& pt : m_ignore) {
        if (m_snaps.count(pt.first) == 0) {
            m_snaps[pt.first] = 0;
        }
        m_snaps[pt.first] += pt.second;
    }
    m_ignore.clear();
}
