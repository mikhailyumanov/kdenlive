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


#include "timelinemodel.hpp"
#include "trackmodel.hpp"
#include "clipmodel.hpp"
#include "groupsmodel.hpp"
#include "snapmodel.hpp"

#include "doc/docundostack.hpp"

#include <klocalizedstring.h>
#include <QDebug>
#include <QModelIndex>
#include <mlt++/MltTractor.h>
#include <mlt++/MltProfile.h>
#include <queue>

int TimelineModel::next_id = 0;

#define PUSH_UNDO(undo, redo, text)                                     \
    if (auto ptr = m_undoStack.lock()) {                                \
        ptr->push(new FunctionalUndoCommand(undo, redo, text));         \
    } else {                                                            \
        qDebug() << "ERROR : unable to access undo stack";              \
        Q_ASSERT(false);                                                \
    }


TimelineModel::TimelineModel(Mlt::Profile *profile, std::weak_ptr<DocUndoStack> undo_stack) :
    m_tractor(new Mlt::Tractor(*profile)),
    m_snaps(new SnapModel()),
    m_undoStack(undo_stack),
    m_profile(profile),
    m_blackClip(new Mlt::Producer(*profile,"color:black"))
{
    // Create black background track
    m_blackClip->set("id", "black_track");
    m_blackClip->set("mlt_type", "producer");
    m_blackClip->set("aspect_ratio", 1);
    m_blackClip->set("set.test_audio", 0);
    m_tractor->insert_track(*m_blackClip, 0);
}

TimelineModel::~TimelineModel()
{
    //Remove black background
    //m_tractor->remove_track(0);
    std::vector<int> all_ids;
    for(auto tracks : m_iteratorTable) {
        all_ids.push_back(tracks.first);
    }
    for(auto tracks : all_ids) {
        deregisterTrack_lambda(tracks, false)();
    }
}

int TimelineModel::getTracksCount() const
{
    int count = m_tractor->count();
    Q_ASSERT(count >= 0);
    // don't count the black background track
    Q_ASSERT(count -1 == static_cast<int>(m_allTracks.size()));
    return count - 1;
}

int TimelineModel::getClipsCount() const
{
    return static_cast<int>(m_allClips.size());
}


int TimelineModel::getClipTrackId(int cid) const
{
    Q_ASSERT(m_allClips.count(cid) > 0);
    const auto clip = m_allClips.at(cid);
    return clip->getCurrentTrackId();
}

int TimelineModel::getClipPosition(int cid) const
{
    Q_ASSERT(m_allClips.count(cid) > 0);
    const auto clip = m_allClips.at(cid);
    return clip->getPosition();
}

int TimelineModel::getClipPlaytime(int cid) const
{
    Q_ASSERT(m_allClips.count(cid) > 0);
    const auto clip = m_allClips.at(cid);
    return clip->getPlaytime();
}

int TimelineModel::getTrackClipsCount(int tid) const
{
    return getTrackById_const(tid)->getClipsCount();
}

int TimelineModel::getTrackPosition(int tid) const
{
    Q_ASSERT(m_iteratorTable.count(tid) > 0);
    auto it = m_allTracks.begin();
    return (int)std::distance(it, (decltype(it))m_iteratorTable.at(tid));
}

bool TimelineModel::requestClipMove(int cid, int tid, int position, bool updateView, Fun &undo, Fun &redo)
{
    Q_ASSERT(isClip(cid));
    std::function<bool (void)> local_undo = [](){return true;};
    std::function<bool (void)> local_redo = [](){return true;};
    bool ok = true;
    int old_tid = getClipTrackId(cid);
    int old_clip_index = -1;
    if (old_tid != -1) {
        old_clip_index = getTrackById(old_tid)->getRowfromClip(cid);
        ok = getTrackById(old_tid)->requestClipDeletion(cid, updateView, local_undo, local_redo);
        if (!ok) {
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    ok = getTrackById(tid)->requestClipInsertion(cid, position, updateView, local_undo, local_redo);
    if (!ok) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        return false;
    }
    int new_clip_index = getTrackById(tid)->getRowfromClip(cid);
    auto operation = [cid, tid, old_tid, old_clip_index, new_clip_index, updateView, this]() {
        /*
        qDebug()<<"Clip move : updateView"<<updateView<<". We are in track #"<<getTrackPosition(tid);
        if (updateView) {
            if (tid == old_tid) {
                qDebug()<<"NOTIFY CHANGE";
                QModelIndex modelIndex = makeClipIndexFromID(cid);
                notifyChange(modelIndex, modelIndex, true, false);
            } else {
                if (old_tid != -1){
                    _beginRemoveRows(makeTrackIndexFromID(old_tid), old_clip_index, old_clip_index);
                    _endRemoveRows();
                }
                qDebug()<<"INSERT ROW";
                _beginInsertRows(makeTrackIndexFromID(tid), new_clip_index, new_clip_index);
                _endInsertRows();
            }
        }
        */
        return true;
    };
    auto reverse = []() {return true;};
    //push the operation and its reverse in the undo/redo
    UPDATE_UNDO_REDO(operation, reverse, local_undo, local_redo);
    //We have to expend the local undo with the data modification signals. They have to be sent after the real operations occured in the model so that the GUI can query the correct new values
    local_undo = [local_undo, tid, old_tid, cid, old_clip_index, new_clip_index, updateView, this]() {
        bool v = local_undo();
        /*
        if (updateView) {
            if (tid == old_tid) {
                QModelIndex modelIndex = makeClipIndexFromID(cid);
                notifyChange(modelIndex, modelIndex, true, false);
            } else {
                _beginRemoveRows(makeTrackIndexFromID(tid), new_clip_index, new_clip_index);
                _endRemoveRows();
                if (old_tid != -1) {
                    _beginInsertRows(makeTrackIndexFromID(old_tid), old_clip_index, old_clip_index);
                    _endInsertRows();
                }
            }
        }
        */
        return v;
    };
    bool result = operation();
    if (result) {
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    }
    return result;
}

bool TimelineModel::requestClipMove(int cid, int tid, int position,  bool updateView, bool logUndo)
{
    Q_ASSERT(m_allClips.count(cid) > 0);
    if (m_allClips[cid]->getPosition() == position && getClipTrackId(cid) == tid) {
        return true;
    }
    if (m_groups->isInGroup(cid)) {
        //element is in a group.
        int gid = m_groups->getRootId(cid);
        int current_tid = getClipTrackId(cid);
        int track_pos1 = getTrackPosition(tid);
        int track_pos2 = getTrackPosition(current_tid);
        int delta_track = track_pos1 - track_pos2;
        int delta_pos = position - m_allClips[cid]->getPosition();
        return requestGroupMove(cid, gid, delta_track, delta_pos, updateView, logUndo);
    }
    std::function<bool (void)> undo = [](){return true;};
    std::function<bool (void)> redo = [](){return true;};
    bool res = requestClipMove(cid, tid, position, updateView, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move clip"));
    }
    return res;
}

int TimelineModel::suggestClipMove(int cid, int tid, int position)
{
    Q_ASSERT(isClip(cid));
    Q_ASSERT(isTrack(tid));
    int currentPos = getClipPosition(cid);
    int currentTrack = getClipTrackId(cid);
    if (currentPos == position || currentTrack != tid) {
        return position;
    }
    qDebug() << "Starting suggestion "<<cid << position << currentPos;
    //we check if move is possible
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool possible = requestClipMove(cid, tid, position, false, undo, redo);
    qDebug() << "Original move success" << possible;
    if (possible) {
        undo();
        return position;
    }
    bool after = position > currentPos;
    int blank_length = getTrackById(tid)->getBlankSizeNearClip(cid, after);
    qDebug() << "Found blank" << blank_length;
    if (blank_length < INT_MAX) {
        if (after) {
            return currentPos + blank_length;
        } else {
            return currentPos - blank_length;
        }
    }
    return position;
}

bool TimelineModel::requestClipInsertion(std::shared_ptr<Mlt::Producer> prod, int trackId, int position, int &id)
{
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestClipInsertion(prod, trackId, position, id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Insert Clip"));
    }
    return result;
}

bool TimelineModel::requestClipInsertion(std::shared_ptr<Mlt::Producer> prod, int trackId, int position, int &id, Fun& undo, Fun& redo)
{
    int clipId = TimelineModel::getNextId();
    id = clipId;
    Fun local_undo = deregisterClip_lambda(clipId);
    ClipModel::construct(shared_from_this(), prod, clipId);
    auto clip = m_allClips[clipId];
    Fun local_redo = [clip, this](){
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is sufficient to register it.
        registerClip(clip);
        return true;
    };
    bool res = requestClipMove(clipId, trackId, position, true, local_undo, local_redo);
    if (!res) {
        Q_ASSERT(undo());
        id = -1;
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestClipDeletion(int cid)
{
    Q_ASSERT(isClip(cid));
    if (m_groups->isInGroup(cid)) {
        return requestGroupDeletion(cid);
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool res = requestClipDeletion(cid, undo, redo);
    if (res) {
        PUSH_UNDO(undo, redo, i18n("Delete Clip"));
    }
    return res;
}

bool TimelineModel::requestClipDeletion(int cid, Fun& undo, Fun& redo)
{
    qDebug() << "deleting clip "<<cid;
    int tid = getClipTrackId(cid);
    if (tid != -1) {
        bool res = getTrackById(tid)->requestClipDeletion(cid, true, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    auto operation = deregisterClip_lambda(cid);
    auto clip = m_allClips[cid];
    auto reverse = [this, clip]() {
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is sufficient to register it.
        registerClip(clip);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        return true;
    }
    undo();
    return false;
}

bool TimelineModel::requestGroupMove(int cid, int gid, int delta_track, int delta_pos, bool updateView, bool logUndo)
{
    std::function<bool (void)> undo = [](){return true;};
    std::function<bool (void)> redo = [](){return true;};
    Q_ASSERT(m_allGroups.count(gid) > 0);
    bool ok = true;
    auto all_clips = m_groups->getLeaves(gid);
    std::vector<int> sorted_clips(all_clips.begin(), all_clips.end());
    //we have to sort clip in an order that allows to do the move without self conflicts
    //If we move up, we move first the clips on the upper tracks (and conversely).
    //If we move left, we move first the leftmost clips (and conversely).
    std::sort(sorted_clips.begin(), sorted_clips.end(), [delta_track, delta_pos, this](int cid1, int cid2){
            int tid1 = getClipTrackId(cid1);
            int tid2 = getClipTrackId(cid2);
            int track_pos1 = getTrackPosition(tid1);
            int track_pos2 = getTrackPosition(tid2);
            if (tid1 == tid2) {
                int p1 = m_allClips[cid1]->getPosition();
                int p2 = m_allClips[cid2]->getPosition();
                return !(p1 <= p2) == !(delta_pos <= 0);
            }
            return !(track_pos1 <= track_pos2) == !(delta_track <= 0);
        });
    for (int clip : sorted_clips) {
        int current_track_id = getClipTrackId(clip);
        int current_track_position = getTrackPosition(current_track_id);
        int target_track_position = current_track_position + delta_track;
        if (target_track_position >= 0 && target_track_position < getTracksCount()) {
            auto it = m_allTracks.cbegin();
            std::advance(it, target_track_position);
            int target_track = (*it)->getId();
            int target_position = m_allClips[clip]->getPosition() + delta_pos;
            ok = requestClipMove(clip, target_track, target_position, updateView || (clip != cid), undo, redo);
        } else {
            ok = false;
        }
        if (!ok) {
            bool undone = undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    if (logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move group"));
    }
    return true;
}

bool TimelineModel::requestGroupDeletion(int cid)
{
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    // we do a breadth first exploration of the group tree, ungroup (delete) every inner node, and then delete all the leaves.
    std::queue<int> group_queue;
    group_queue.push(m_groups->getRootId(cid));
    std::unordered_set<int> all_clips;
    while (!group_queue.empty()) {
        int current_group = group_queue.front();
        group_queue.pop();
        Q_ASSERT(isGroup(current_group));
        auto children = m_groups->getDirectChildren(current_group);
        int one_child = -1; //we need the id on any of the indices of the elements of the group
        for(int c : children) {
            if (isClip(c)) {
                all_clips.insert(c);
                one_child = c;
            } else {
                Q_ASSERT(isGroup(c));
                one_child = c;
                group_queue.push(c);
            }
        }
        if (one_child != -1) {
            bool res = m_groups->ungroupItem(one_child, undo, redo);
            if (!res) {
                undo();
                return false;
            }
        }
    }
    for(int clipId : all_clips) {
        bool res = requestClipDeletion(clipId, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    PUSH_UNDO(undo, redo, i18n("Remove group"));
    return true;
}


bool TimelineModel::requestClipResize(int cid, int size, bool right, bool logUndo, bool snapping)
{
    Q_ASSERT(isClip(cid));
    if (snapping) {
        Fun temp_undo = [](){return true;};
        Fun temp_redo = [](){return true;};
        int in = getClipPosition(cid);
        int out = in + getClipPlaytime(cid) - 1;
        m_snaps->ignore({in,out});
        int proposed_size = -1;
        if (right) {
            int target_pos = in + size - 1;
            int snapped_pos = m_snaps->getClosestPoint(target_pos);
            //TODO Make the number of frames adjustable
            if (snapped_pos != -1 && qAbs(target_pos - snapped_pos) <= 10) {
                proposed_size = snapped_pos - in;
            }
        } else {
            int target_pos = out + 1 - size;
            int snapped_pos = m_snaps->getClosestPoint(target_pos);
            //TODO Make the number of frames adjustable
            if (snapped_pos != -1 && qAbs(target_pos - snapped_pos) <= 10) {
                proposed_size = out + 2 - snapped_pos;
            }
        }
        m_snaps->unIgnore();
        if (proposed_size != -1) {
            if (m_allClips[cid]->requestResize(proposed_size, right, temp_undo, temp_redo)) {
                temp_undo(); //undo temp move
                size = proposed_size;
            }
        }
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    Fun update_model = [cid, right, logUndo, this]() {
        if (getClipTrackId(cid) != -1) {
            QModelIndex modelIndex = makeClipIndexFromID(cid);
            if (right) {
                notifyChange(modelIndex, modelIndex, false, true, logUndo);
            } else {
                notifyChange(modelIndex, modelIndex, true, true, logUndo);
            }
        }
        return true;
    };
    bool result = m_allClips[cid]->requestResize(size, right, undo, redo);
    if (result) {
        PUSH_LAMBDA(update_model, undo);
        PUSH_LAMBDA(update_model, redo);
        update_model();
        if (logUndo) {
            PUSH_UNDO(undo, redo, i18n("Resize clip"));
        }
    }
    return result;
}

bool TimelineModel::requestClipTrim(int cid, int delta, bool right, bool ripple, bool logUndo)
{
    return requestClipResize(cid, m_allClips[cid]->getPlaytime() - delta, right, logUndo);
}

bool TimelineModel::requestClipsGroup(const std::unordered_set<int>& ids)
{
    for (int id : ids) {
        if (isClip(id)) {
            if (getClipTrackId(id) == -1) {
                return false;
            }
        } else if (!isGroup(id)) {
            return false;
        }
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    int gid = m_groups->groupItems(ids, undo, redo);
    if (gid != -1) {
        PUSH_UNDO(undo, redo, i18n("Group clips"));
    }
    return (gid != -1);
}

bool TimelineModel::requestClipUngroup(int id)
{
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestClipUngroup(id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Ungroup clips"));
    }
    return result;
}

bool TimelineModel::requestClipUngroup(int id, Fun& undo, Fun& redo)
{
    return m_groups->ungroupItem(id, undo, redo);
}

bool TimelineModel::requestTrackInsertion(int position, int &id)
{
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestTrackInsertion(position, id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Insert Track"));
    }
    return result;
}

bool TimelineModel::requestTrackInsertion(int position, int &id, Fun& undo, Fun& redo)
{
    if (position == -1) {
        position = (int)(m_allTracks.size());
    }
    if (position < 0 || position > (int)m_allTracks.size()) {
        return false;
    }
    int trackId = TimelineModel::getNextId();
    id = trackId;
    Fun local_undo = deregisterTrack_lambda(trackId);
    TrackModel::construct(shared_from_this(), trackId, position);
    auto track = getTrackById(trackId);
    Fun local_redo = [track, position, this](){
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is sufficient to register it.
        registerTrack(track, position);
        return true;
    };
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestTrackDeletion(int tid)
{
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestTrackDeletion(tid, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Delete Track"));
    }
    return result;
}

bool TimelineModel::requestTrackDeletion(int tid, Fun& undo, Fun& redo)
{
    Q_ASSERT(isTrack(tid));
    std::vector<int> clips_to_delete;
    for(const auto& it : getTrackById(tid)->m_allClips) {
        clips_to_delete.push_back(it.first);
    }
    qDebug() << "Deleting track "<<tid<<"with "<<clips_to_delete.size()<<" to delete";
    Fun local_undo = [](){return true;};
    Fun local_redo = [](){return true;};
    for(int clip : clips_to_delete) {
        bool res = true;
        while (res && m_groups->isInGroup(clip)) {
            res = requestClipUngroup(clip, local_undo, local_redo);
        }
        if (res) {
            res = requestClipDeletion(clip, local_undo, local_redo);
        }
        if (!res) {
            bool u = local_undo();
            Q_ASSERT(u);
            return false;
        }
    }
    int old_position = getTrackPosition(tid);
    auto operation = deregisterTrack_lambda(tid, true);
    std::shared_ptr<TrackModel> track = getTrackById(tid);
    auto reverse = [this, track, old_position]() {
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is sufficient to register it.
        registerTrack(track, old_position);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, local_undo, local_redo);
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
        return true;
    }
    local_undo();
    return false;

}

void TimelineModel::registerTrack(std::shared_ptr<TrackModel> track, int pos)
{
    int id = track->getId();
    if (pos == -1) {
        pos = static_cast<int>(m_allTracks.size());
    }
    Q_ASSERT(pos >= 0);
    Q_ASSERT(pos <= static_cast<int>(m_allTracks.size()));

    //effective insertion (MLT operation), add 1 to account for black background track
    int error = m_tractor->insert_track(*track ,pos + 1);
    Q_ASSERT(error == 0); //we might need better error handling...

    // we now insert in the list
    auto posIt = m_allTracks.begin();
    std::advance(posIt, pos);
    auto it = m_allTracks.insert(posIt, std::move(track));
    //it now contains the iterator to the inserted element, we store it
    Q_ASSERT(m_iteratorTable.count(id) == 0); //check that id is not used (shouldn't happen)
    m_iteratorTable[id] = it;
    qDebug()<<"Resetting model";
    _resetView();
}

void TimelineModel::registerClip(std::shared_ptr<ClipModel> clip)
{
    int id = clip->getId();
    Q_ASSERT(m_allClips.count(id) == 0);
    m_allClips[id] = clip;
    m_groups->createGroupItem(id);
}

void TimelineModel::registerGroup(int groupId)
{
    Q_ASSERT(m_allGroups.count(groupId) == 0);
    m_allGroups.insert(groupId);
}

Fun TimelineModel::deregisterTrack_lambda(int id, bool updateView)
{
    return [this, id, updateView]() {
        auto it = m_iteratorTable[id]; //iterator to the element
        int index = getTrackPosition(id); //compute index in list
        if (updateView) {
            QModelIndex root;
            qDebug() << "Reseting view";
            _resetView();
        }
        m_tractor->remove_track(static_cast<int>(index + 1)); //melt operation, add 1 to account for black background track
        //send update to the model
        m_allTracks.erase(it);  //actual deletion of object
        m_iteratorTable.erase(id);  //clean table
        return true;
    };
}

Fun TimelineModel::deregisterClip_lambda(int cid)
{
    return [this, cid]() {
        Q_ASSERT(m_allClips.count(cid) > 0);
        Q_ASSERT(getClipTrackId(cid) == -1); //clip must be deleted from its track at this point
        Q_ASSERT(!m_groups->isInGroup(cid)); //clip must be ungrouped at this point
        m_allClips.erase(cid);
        m_groups->destructGroupItem(cid);
        return true;
    };
}

void TimelineModel::deregisterGroup(int id)
{
    Q_ASSERT(m_allGroups.count(id) > 0);
    m_allGroups.erase(id);
}

std::shared_ptr<TrackModel> TimelineModel::getTrackById(int tid)
{
    Q_ASSERT(m_iteratorTable.count(tid) > 0);
    return *m_iteratorTable[tid];
}

const std::shared_ptr<TrackModel> TimelineModel::getTrackById_const(int tid) const
{
    Q_ASSERT(m_iteratorTable.count(tid) > 0);
    return *m_iteratorTable.at(tid);
}

std::shared_ptr<ClipModel> TimelineModel::getClipPtr(int cid) const
{
    Q_ASSERT(m_allClips.count(cid) > 0);
    return m_allClips.at(cid);
}

int TimelineModel::getNextId()
{
    return TimelineModel::next_id++;
}

bool TimelineModel::isClip(int id) const
{
    return m_allClips.count(id) > 0;
}

bool TimelineModel::isTrack(int id) const
{
    return m_iteratorTable.count(id) > 0;
}

bool TimelineModel::isGroup(int id) const
{
    return m_allGroups.count(id) > 0;
}

int TimelineModel::duration() const
{
    return m_tractor->get_playtime();
}


std::unordered_set<int> TimelineModel::getGroupElements(int cid)
{
    int gid = m_groups->getRootId(cid);
    return m_groups->getLeaves(gid);
}

Mlt::Profile *TimelineModel::getProfile()
{
    return m_profile;
}

bool TimelineModel::requestReset(Fun& undo, Fun& redo)
{
    std::vector<int> all_ids;
    for (const auto& track : m_iteratorTable) {
        all_ids.push_back(track.first);
    }
    qDebug() << "RESET : "<<all_ids.size();
    bool ok = true;
    for (int tid : all_ids) {
        ok = ok && requestTrackDeletion(tid, undo, redo);
    }
    TimelineModel::next_id = 0;
    return ok;
}

void TimelineModel::setUndoStack(std::weak_ptr<DocUndoStack> undo_stack)
{
    m_undoStack = undo_stack;
}

int TimelineModel::requestBestSnapPos(int pos, int length)
{
    int snapped_start = m_snaps->getClosestPoint(pos);
    int snapped_end = m_snaps->getClosestPoint(pos + length);
    int startDiff = qAbs(pos - snapped_start);
    int endDiff = qAbs(pos + length - snapped_end);
    if (startDiff < endDiff && snapped_start >= 0) {
        // snap to start
        if (startDiff < 10) {
            return snapped_start;
        }
    } else {
        // snap to end
        if (endDiff < 10 && snapped_end >= 0) {
            return snapped_end - length;
        }
    }
    return -1;
}

int TimelineModel::requestNextSnapPos(int pos)
{
    return m_snaps->getNextPoint(pos);
}

int TimelineModel::requestPreviousSnapPos(int pos)
{
    return m_snaps->getPreviousPoint(pos);
}
