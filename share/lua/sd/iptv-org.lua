--[[
 Copyright © 2026 VideoLAN and AUTHORS
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
--]]

local json = nil

function json_init()
    if json ~= nil then return false end
    vlc.msg.dbg("JSON parser lazy initialization")
    json = require("dkjson")
    json["parse_url"] = function(url)
        local stream = vlc.stream(url)
        local data = ""
        local line = ""
        while true do
            line = stream:read(8192)
            if not line then break end
            data = data .. line
        end
        return json.decode(data)
    end
end

function descriptor()
    return {
        title = "Prog tv",
        description = "IPTV channels",
        author = "Me",
        capabilities = {"search"}
    }
end

function main()
    json_init()

    vlc.msg.dbg("[iptv] Loading streams")
    local streams   = json.parse_url("https://iptv-org.github.io/api/streams.json")
    vlc.msg.dbg("[iptv] Loading channels")
    local channels  = json.parse_url("https://iptv-org.github.io/api/channels.json")
    vlc.msg.dbg("[iptv] Loading countries")
    local countries = json.parse_url("https://iptv-org.github.io/api/countries.json")

    if not streams or not channels or not countries then
        vlc.msg.err("[iptv] Loading error")
        return
    end

    -- Channels
    local channel_index = {}
    for _, ch in ipairs(channels) do
        if ch.id and ch.id ~= "" then
            channel_index[ch.id] = ch
        end
    end

    -- Countries names
    local country_names = {}
    for _, country in ipairs(countries) do
        if country.code and country.name then
            country_names[country.code] = country.name
        end
    end

    -- Qualities priority
    local quality_rank = {
        ["1080p"] = 6,
        ["720p"]  = 5,
        ["480p"]  = 4,
        ["360p"]  = 3,
        ["240p"]  = 2,
        ["sd"]    = 1,
        [""]      = 0
    }

    local function get_rank(q)
        return quality_rank[q or ""] or 0
    end

    -- Keep only best streams
    local best_streams = {}

    for _, s in ipairs(streams) do
        local ch_id = s.channel
        if ch_id and ch_id ~= "" then
            local ch = channel_index[ch_id]
            if ch and s.url and s.url ~= "" then
                local cc = ch.country
                if country_names[cc] then
                    if not best_streams[cc] then
                        best_streams[cc] = {}
                    end
                    local current  = best_streams[cc][ch_id]
                    local new_rank = get_rank(s.quality)
                    if not current or new_rank > get_rank(current.quality) then
                        best_streams[cc][ch_id] = {
                            url        = s.url,
                            title      = s.title or ch.name or "Untitled",
                            quality    = s.quality or "",
                            referrer   = s.referrer,
                            user_agent = s.user_agent
                        }
                    end
                end
            end
        end
    end

    -- Create VLC nodes only for countries with streams
    for cc, chans in pairs(best_streams) do
        local node = vlc.sd.add_node({
            title  = country_names[cc],
            arturl = "https://flagcdn.io/flags/4x3/" .. string.lower(cc) .. ".svg"
        })
        for _, best in pairs(chans) do
            local titre = best.title
            if best.quality and best.quality ~= "" then
                titre = titre .. " [" .. best.quality .. "]"
            end

            local options = {}
            if best.referrer and best.referrer ~= "" then
                options["http-referrer"] = best.referrer
            end
            if best.user_agent and best.user_agent ~= "" then
                options["http-user-agent"] = best.user_agent
            end

            node:add_subitem({
                path     = best.url,
                title    = titre,
                duration = -1,
                options  = options
            })
        end
    end
end

function deactivate() end

function activate()
    main()
end