--[[
Resolve Twitch channel and video URLs to the actual stream URL

 Copyright © 2017 the VideoLAN team

 Author: Marvin Scholz <epirat07 at gmail dot com>

 2026: rewritten for the GQL PlaybackAccessToken API (Helix/Kraken/api.* are gone)
 2026: server-side preroll skipping
 Author: mmrry <sl2007 at yandex dot com>

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

local CLIENT_ID = "kimne78kx3ncx6brgo4mv6wki5h1ko"
local GQL_URL   = "https://gql.twitch.tv/gql"
local USHER_URL = "https://usher.ttvnw.net"
local IS_WIN    = package.config:sub(1, 1) == "\\"
local OPTIONS   = { ":adaptive-logic=highest" }
local MAX_HEIGHT = 0

-- Preroll handling
local SKIP_PREROLL  = true
local LIVE_DELAY_MS = 4000
local LIVE_MARGIN_S = 1
local AD_CUT_S      = 1
local RESUME_MAX_S  = 30
local PREROLL_MAX_S = 90

local TOKEN_ARGS   = "$platform: String!, $backend: String!, $player: String!"
local TOKEN_PARAMS = "{platform: $platform, playerBackend: $backend, playerType: $player}"

local LIVE_QUERY = [[query($login: String!, ]] .. TOKEN_ARGS .. [[) {
  streamPlaybackAccessToken(channelName: $login, params: ]] .. TOKEN_PARAMS .. [[) {
    value signature authorization { isForbidden forbiddenReasonCode }
  }
  user(login: $login) {
    displayName stream { id } broadcastSettings { title game { displayName } }
  }
}]]

local VOD_QUERY = [[query($id: ID!, ]] .. TOKEN_ARGS .. [[) {
  videoPlaybackAccessToken(id: $id, params: ]] .. TOKEN_PARAMS .. [[) { value signature }
  video(id: $id) { title description owner { displayName } game { displayName } }
}]]

function probe()
    if vlc.access ~= "http" and vlc.access ~= "https" then
        return false
    end
    local host = vlc.path:match("^([%w%.]-)twitch%.tv/[%w_]")
    return host == "" or host == "www." or host == "m." or host == "go."
end

local function fail(msg)
    vlc.msg.err("Twitch: " .. msg)
    return {}
end

-- Copy of the shared OPTIONS plus per-item extras
local function item_options(extra)
    local opts = {}
    for _, v in ipairs(OPTIONS) do opts[#opts + 1] = v end
    for _, v in ipairs(extra or {}) do opts[#opts + 1] = v end
    return opts
end

-- Quote a single shell argument (cmd.exe on Windows, sh elsewhere)
local function quote(s)
    if IS_WIN then
        return '"' .. s:gsub('"', '\\"') .. '"'
    end
    return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function gql(query, variables)
    local json = require("dkjson")
    variables.platform, variables.backend, variables.player = "web", "mediaplayer", "site"
    local body = json.encode({ query = query:gsub("%s+", " "), variables = variables })
    local cmd  = table.concat({
        "curl -sS --max-time 10",
        "-H", quote("Client-ID: " .. CLIENT_ID),
        "-H", quote("Content-Type: text/plain; charset=UTF-8"),
        "--data-binary", quote(body),
        GQL_URL,
    }, " ")

    local pipe = io.popen(cmd)
    if not pipe then
        return nil, "failed to run curl"
    end
    local resp = pipe:read("*a")
    pipe:close()

    local obj, _, err = json.decode(resp or "")
    if not obj then
        return nil, "bad GQL response: " .. (err or "empty (is curl installed?)")
    end
    if obj.errors then
        return nil, "GQL error: " .. tostring(obj.errors[1].message)
    end
    return obj.data or {}, nil
end

local function read_all(s)
    local parts = {}
    while true do
        local chunk = s:read(65536)
        if not chunk or chunk == "" then break end
        parts[#parts + 1] = chunk
    end
    return table.concat(parts)
end

local function http_get(url)
    local ok, s = pcall(vlc.stream, url)
    if not ok or not s then return nil end
    local body = read_all(s)

    if body:sub(1, 2) == "\31\139" then
        ok, s = pcall(vlc.stream, url)
        if not ok or not s or not s.addfilter then return nil end
        s:addfilter("inflate")
        body = read_all(s)
    end
    if body == "" then return nil end
    return body
end

local function decode_token(token)
    local ok, t = pcall(function() return require("dkjson").decode(token.value) end)
    if ok and type(t) == "table" then
        return t
    end
    return nil
end

local function log_token_flags(t)
    if not t then return end
    local function s(v) return tostring(v) end
    vlc.msg.dbg("Twitch: token server_ads=" .. s(t.server_ads)
        .. " show_ads=" .. s(t.show_ads) .. " hide_ads=" .. s(t.hide_ads)
        .. " turbo=" .. s(t.turbo) .. " subscriber=" .. s(t.subscriber))
end

local function usher_url(path, token)
    return USHER_URL .. path .. "?" .. table.concat({
        "sig=" .. token.signature,
        "token=" .. vlc.strings.encode_uri_component(token.value),
        "allow_source=true",
        "playlist_include_framerate=true",
        "player_backend=mediaplayer",
        "p=" .. math.random(9999999),
    }, "&")
end

local function scan_playlist(pl)
    local segs, has_live, ad_listed = {}, false, 0
    for dur, title in pl:gmatch("#EXTINF:([%d%.]+),([^\r\n]*)") do
        local d, live = tonumber(dur) or 0, (title == "live")
        has_live = has_live or live
        if not live then ad_listed = ad_listed + d end
        segs[#segs + 1] = { dur = d, live = live }
    end

    local segdur = 0
    for _, s in ipairs(segs) do
        if s.dur > segdur then segdur = s.dur end
    end
    if segdur == 0 then segdur = 2 end

    local tail, all_live = 0, true
    for i = #segs, 1, -1 do
        if not segs[i].live then
            all_live = false
            break
        end
        tail = tail + segs[i].dur
    end

    local ad_total, first_ad, seen, marked = 0, 0, {}, false
    for attrs in pl:gmatch("#EXT%-X%-DATERANGE:([^\r\n]*)") do
        local id = attrs:match('ID="([^"]*)"') or attrs
        if attrs:find('CLASS="twitch%-stitched%-ad"') or id:find("^stitched%-ad") then
            marked = true
            if not seen[id] then
                seen[id] = true
                local d = tonumber(attrs:match("DURATION=([%d%.]+)")) or 0
                if first_ad == 0 then first_ad = d end
                ad_total = ad_total + d
            end
        end
    end

    return {
        segdur    = segdur,
        has_live  = has_live,
        all_live  = all_live,
        tail      = tail,
        ad_listed = ad_listed,
        ad_total  = ad_total,
        first_ad  = first_ad,
        marked    = marked,
    }
end

local function live_needed(s)
    return math.max(LIVE_DELAY_MS / 1000, 3 * s.segdur) + s.segdur + LIVE_MARGIN_S
end

local function wait_live(variant, max_s)
    local started = os.time()
    local polls   = 0
    while true do
        local pl = http_get(variant)
        polls = polls + 1
        if not pl then
            vlc.msg.warn("Twitch: media playlist unreadable")
            return false
        end
        local s = scan_playlist(pl)
        if s.all_live or s.tail >= live_needed(s) or (not s.has_live and not s.marked) then
            vlc.msg.dbg(string.format("Twitch: live content ready after %d poll(s), %ds",
                polls, os.time() - started))
            return true
        end
        if os.time() - started >= max_s then
            vlc.msg.warn("Twitch: live wait timed out, playback may stall at the ad boundary")
            return false
        end
    end
end

local function with_meta(meta, item)
    for k, v in pairs(meta) do
        if item[k] == nil then item[k] = v end
    end
    return item
end

local function pick_variant(master)
    local best, best_key, fallback
    for attrs, url in master:gmatch("#EXT%-X%-STREAM%-INF:([^\r\n]*)[\r\n]+([^\r\n#][^\r\n]*)") do
        fallback = fallback or url
        local h   = tonumber(attrs:match("RESOLUTION=%d+x(%d+)")) or 0
        local fps = tonumber(attrs:match("FRAME%-RATE=([%d%.]+)")) or 0
        local bw  = tonumber(attrs:match("[^%-]BANDWIDTH=(%d+)") or attrs:match("^BANDWIDTH=(%d+)")) or 0
        local audio_only = attrs:find('VIDEO="audio_only"') or (h == 0 and attrs:find("mp4a") and not attrs:find("avc"))
        if not audio_only and (MAX_HEIGHT == 0 or h <= MAX_HEIGHT) then
            local key = h * 1e12 + fps * 1e9 + bw
            if not best_key or key > best_key then
                best, best_key = url, key
            end
        end
    end
    if best then
        vlc.msg.dbg(string.format("Twitch: variant %dp chosen", math.floor(best_key / 1e12)))
    end
    return best or fallback
end

local function preroll_items(master_url, channel, meta)
    local master = http_get(master_url)
    local variant = master and pick_variant(master)
    if not variant then
        vlc.msg.warn("Twitch: master playlist unreadable, preroll not handled")
        return nil
    end

    local pl = http_get(variant)
    if not pl then
        vlc.msg.warn("Twitch: media playlist unreadable, preroll not handled")
        return nil
    end

    local s = scan_playlist(pl)
    if not s.has_live and not s.marked then
        return nil
    end
    if s.ad_listed == 0 and not s.marked then
        return nil
    end

    local live_opts = { ":adaptive-livedelay=" .. LIVE_DELAY_MS }

    if s.first_ad <= AD_CUT_S then

        vlc.msg.info("Twitch: server-side preroll of unknown length, waiting for live")
        wait_live(variant, PREROLL_MAX_S)
        return { with_meta(meta, { path = variant, options = item_options(live_opts) }) }
    end


    local run = s.first_ad - AD_CUT_S
    vlc.msg.info(string.format(
        "Twitch: server-side preroll (%.0fs announced), playing it first", s.ad_total))
    local ad_item = with_meta(meta, {
        path    = variant,
        name    = meta.name .. " [ad]",
        options = item_options({
            ":adaptive-livedelay=60000",
            string.format(":run-time=%.2f", run),
        }),
    })

    local resume = "https://www.twitch.tv/" .. channel:lower()
        .. "?vlcresume=" .. vlc.strings.encode_uri_component(variant)
        .. "&vlcname=" .. vlc.strings.encode_uri_component(meta.name)
    local live_item = with_meta(meta, { path = resume })

    return { ad_item, live_item }
end

local function parse_resume(variant, name)
    vlc.msg.info("Twitch: preroll done, switching to live")
    wait_live(variant, RESUME_MAX_S)
    return { {
        path    = variant,
        name    = name,
        artist  = name and name:gsub("^Twitch: ", ""),
        options = item_options({ ":adaptive-livedelay=" .. LIVE_DELAY_MS }),
    } }
end

local function parse_timestamp(t)
    if not t or t == "" then return nil end
    if t:match("^%d+$") then return tonumber(t) end
    if not t:match("^[%dhms]+$") then return nil end
    local mult  = { h = 3600, m = 60, s = 1 }
    local total, consumed = 0, 0
    for num, unit in t:gmatch("(%d+)([hms])") do
        total    = total + tonumber(num) * mult[unit]
        consumed = consumed + #num + 1
    end
    if consumed ~= #t or total == 0 then return nil end
    return total
end

local function parse_video(video_id)
    vlc.msg.dbg("Twitch: Loading video url for " .. video_id)

    local data, err = gql(VOD_QUERY, { id = video_id })
    if not data then
        return fail(err)
    end

    local token = data.videoPlaybackAccessToken
    if not token then
        return fail("no access token for video " .. video_id)
    end
    log_token_flags(decode_token(token))

    local extra = {}
    local start = parse_timestamp(vlc.path:match("[?&]t=([%w]+)"))
    if start then
        vlc.msg.dbg("Twitch: starting VOD at " .. start .. "s")
        extra[#extra + 1] = ":start-time=" .. start
    end

    local item = {
        path    = usher_url("/vod/" .. video_id .. ".m3u8", token),
        options = item_options(extra),
        name = "Twitch: " .. video_id,
        url  = vlc.path,
    }

    local video = data.video
    if video then
        item.name        = "Twitch: " .. video.title
        item.artist      = video.owner and video.owner.displayName
        item.description = video.description
        item.genre       = video.game and video.game.displayName
    end

    return { item }
end

local function parse_stream(channel)
    vlc.msg.dbg("Twitch: Loading stream url for " .. channel)

    local data, err = gql(LIVE_QUERY, { login = channel })
    if not data then
        return fail(err)
    end

    local user, token = data.user, data.streamPlaybackAccessToken
    if not user or not token then
        return fail("channel not found: " .. channel)
    end
    if not user.stream then
        return fail(user.displayName .. " is offline")
    end
    if token.authorization and token.authorization.isForbidden then
        return fail("playback forbidden: " .. tostring(token.authorization.forbiddenReasonCode))
    end

    local claims = decode_token(token)
    log_token_flags(claims)

    local path = usher_url("/api/channel/hls/" .. channel:lower() .. ".m3u8", token)
    local bs   = user.broadcastSettings or {}
    local game = bs.game and bs.game.displayName
    local meta = {
        name        = "Twitch: " .. user.displayName,
        artist      = user.displayName,
        nowplaying  = game and (user.displayName .. " playing " .. game),
        genre       = game,
        description = bs.title,
        url         = vlc.path,
    }

    if SKIP_PREROLL and claims and claims.server_ads == true then
        local items = preroll_items(path, channel, meta)
        if items then
            return items
        end
    end

    return { with_meta(meta, { path = path, options = item_options() }) }
end

function parse()
    local resume = vlc.path:match("[?&]vlcresume=([^&]+)")
    if resume then
        local name = vlc.path:match("[?&]vlcname=([^&]+)")
        return parse_resume(vlc.strings.decode_uri(resume),
                            name and vlc.strings.decode_uri(name) or "Twitch")
    end

    local video_id = vlc.path:match("/videos/(%d+)")
    if video_id then
        return parse_video(video_id)
    end

    local channel = vlc.path:match("twitch%.tv/([%w_]+)")
    if not channel then
        return fail("failed to parse channel name from url")
    end
    return parse_stream(channel)
end