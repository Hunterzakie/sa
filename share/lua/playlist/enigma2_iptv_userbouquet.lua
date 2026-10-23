--[[

 Copyright © 2026 VideoLAN and AUTHORS

 Authors: Michael Mess <michael@michaelmess.de>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
--]]

-- Probe function. Checks if the file can be handled by this code.
function probe()
    -- Check if the file starts with "#NAME " as every Enigma2 userbouquet.*.tv file
    local header_name = vlc.peek( 6 )
    if not header_name or not header_name == "#NAME " then
        return false
    end
    local header_rest_and_first_service_line = vlc.peek( 1000 )
    if not header_rest_and_first_service_line then
        return false
    end
    return string.match( header_rest_and_first_service_line, ".*[\r\n]+#SERVICE%s+[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:.*" )
end

-- Parse function. Converts the IPTV entries containing http(s) streams into a playlist
function parse()
    local playlist = {}
    local current_item = nil

    while true do
        local line = vlc.readline()
        if not line then break end -- End of file reached, exit.

        -- Parse one #SERVICE line containing the stream URL
        if string.match( line, "^#SERVICE" ) then
            -- Cut out the URL part (it is not yet sure the SERVICE contains one)
            local url_part = string.match( line, "#SERVICE%s+[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:[^:]+:(.*)" )
            -- If it contains an URL with http(s)
            if url_part and string.match( url_part, "^http" ) then
                -- Remove any optional data following URL, separated with ":"
                url_part = string.gsub( url_part, ":.*$", "" )
                -- Decode Hex-Codes for column delimiter only
                url_part = string.gsub( url_part, "%%3a", ":" )
                url_part = string.gsub( url_part, "%%3A", ":" )
                -- Remove any whitespace
                url_part = string.gsub( url_part, "%s+$", "" )
                current_item = { path = url_part, name = "NameNotYetSetFromDescription" }
            else
                current_item = nil
            end

        -- Parse following #DESCRIPTION line containing the displayed station name
        elseif string.match( line, "^#DESCRIPTION" ) and current_item then
            local name = string.match( line, "^#DESCRIPTION%s+(.*)" )
            if name then
                current_item.name = string.gsub( name, "%s+$", "" )
                -- Add to VLC-playlist
                table.insert( playlist, current_item )
                current_item = nil
            end
        end
    end

    return playlist
end
