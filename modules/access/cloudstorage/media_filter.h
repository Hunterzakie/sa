/*****************************************************************************
 * media_filter.h: Media file filtering for cloudstorage module
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs and VideoLAN
 *
 * Authors: Ashish Vardhan Karanam <ashishkaranam06@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_CLOUDSTORAGE_MEDIA_FILTER_H
#define VLC_CLOUDSTORAGE_MEDIA_FILTER_H

#include <string>
#include <unordered_set>
#include <cctype>
#include <cloudstorage/IItem.h>

namespace vlc {
namespace cloudstorage {

static inline bool IsMediaFile(const std::string& filename);

static inline bool IsSupportedMediaFile(const ::cloudstorage::IItem::Pointer& item) {
    if (item->type() == ::cloudstorage::IItem::FileType::Directory)
        return true;

    if (IsMediaFile(item->filename()))
        return true;

    if (item->type() == ::cloudstorage::IItem::FileType::Video ||
        item->type() == ::cloudstorage::IItem::FileType::Audio)
        return true;

    return false;
}

static inline bool IsMediaFile(const std::string& filename) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos)
        return false;

    std::string ext = filename.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    static const std::unordered_set<std::string> media_extensions = {
        // Video formats
        ".mp4", ".m4v", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
        ".vob", ".ogv", ".ts", ".m2ts", ".mts", ".mpg", ".mpeg", ".m2v",
        ".3gp", ".3g2", ".divx", ".f4v", ".rm", ".rmvb", ".asf", ".iso",
        ".wtv", ".drc", ".mng", ".yuv",

        // Audio formats
        ".mp3", ".aac", ".m4a", ".flac", ".wav", ".wma", ".ogg", ".oga",
        ".opus", ".aiff", ".aif", ".alac", ".ac3", ".dts", ".mid", ".midi",
        ".ape", ".mpc", ".mka", ".ra", ".voc", ".m4b", ".m4p", ".caf",
        ".mka", ".xm", ".it", ".s3m", ".mod",

        // Playlists
        ".m3u", ".m3u8", ".pls", ".xspf", ".asx", ".cue",

        // Subtitles
        ".srt", ".vtt", ".sub", ".idx", ".ass", ".ssa", ".smi"
    };

    return media_extensions.find(ext) != media_extensions.end();
}

} 
} 

#endif
