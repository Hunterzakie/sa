// Copyright (C) 2024 VLC authors and VideoLAN
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// chapter_command_matroska_js.cpp : MatroskaJS implementation for Matroska Chapter Codecs
// Author: Khalid Masum <khalid.masum.92@gmail.com>


#include "chapter_command_matroska_js.hpp"
#include "virtual_segment.hpp"

#include "duktape/duktape.h"

duk_bool_t duk_check_timeout(void *udata) {
    if (udata == nullptr) return false;
    auto data = static_cast<mkv::matroska_js_interpreter_c*>(udata);
    return data->timed_out;
}

namespace mkv {

constexpr vlc_tick_t INTERPRETER_TIMEOUT = VLC_TICK_FROM_SEC(3);

//MatroskaJS
const char* matroska_js_interpreter_c::CMD_MS_GOTO_AND_PLAY = "GotoAndPlay";
const char* matroska_js_interpreter_c::CMD_MS_LOG_MSG = "LogMsg";

static matroska_js_interpreter_c* receive_interpreter_object(duk_context *ctx)
{
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1,  DUK_HIDDEN_SYMBOL("interpreter_obj"));
    matroska_js_interpreter_c *interpretor = static_cast<matroska_js_interpreter_c*>( duk_get_pointer(ctx, -1));

    return interpretor;
}

bool matroska_js_interpreter_c:: gotoChapter(chapter_uid i_chapter_uid)
{
    virtual_segment_c *p_vsegment;
    virtual_chapter_c *p_vchapter = vm.FindVChapter( i_chapter_uid, p_vsegment );

    if ( p_vchapter == NULL )
        vlc_debug( l, "Chapter %" PRId64 " not found", i_chapter_uid);
    else
    {
        if ( !p_vchapter->EnterAndLeave( vm.GetCurrentVSegment()->CurrentChapter(), false ) )
        {
            vm.JumpTo( *p_vsegment, *p_vchapter );
        }
        return true;
    }

    return false;
}


/**
 * execute_GotoAndPlay:
 * Handles Matroska Script GotoAndPlay command by jumping to the
 * designated chapter.
 *
 * Takes an arguement string containing,
 * GotoAndPlay Command Parameter:
 *
 * chapter_uid      string: The goto chapter uid.
 *
 */
bool matroska_js_interpreter_c::execute_GotoAndPlay(const std::string &arg)
{
    chapter_uid i_chapter_uid = atoll( arg.c_str() );
    bool b_result = gotoChapter(i_chapter_uid);

    return b_result;
}

duk_ret_t matroska_js_interpreter_c::js_execute_GotoAndPlay(duk_context *ctx)
{
    if (!duk_is_string(ctx, 0))
        return DUK_RET_TYPE_ERROR;

    const char* arg = duk_to_string(ctx, 0);

    auto interpretor = receive_interpreter_object(ctx);
    interpretor->execute_GotoAndPlay(arg);

    return 0;

}

/**
 * execute_LogMsg:
 * Handles Matroska Script LogMsg command by logging the passed
 * parameter.
 *
 * Takes an arguement string containing,
 * LogMsg Command Parameter:
 *
 * arg      string: The string to be logged.
 *
 */
bool matroska_js_interpreter_c::execute_LogMsg(const std::string &arg)
{
    vlc_debug(l, "%s", arg.c_str());
    return true;
}

duk_ret_t matroska_js_interpreter_c::js_execute_LogMsg(duk_context *ctx)
{
//    int n = duk_get_top(ctx); //read total number of args
    if (!duk_is_string(ctx, 0))
        return DUK_RET_TYPE_ERROR;

    const char* arg = duk_to_string(ctx, 0);

    auto interpretor = receive_interpreter_object(ctx);
    interpretor->execute_LogMsg(arg);

    return 0;

}

void matroska_js_interpreter_c::on_timeout()
{
    vlc_error(l,"Script taking too long (%" PRId64 ") to execute, stopping", SEC_FROM_VLC_TICK(INTERPRETER_TIMEOUT));
    timed_out = true;
}

void matroska_js_interpreter_c::fatal_error_handler(void *udata, const char* msg)
{
    auto metadata_obj = static_cast<matroska_js_interpreter_c*> (udata);
    vlc_error( metadata_obj->l, "DUKTAPE: FATAL ERROR OCCURED DURING JS EXECUTION:\n %s", msg );
    throw std::runtime_error("DUKTAPE FATAL");
}


duk_context* matroska_js_interpreter_c::ms_setup()
{
    duk_context *ctx = duk_create_heap(NULL, NULL, NULL, this, fatal_error_handler);

    duk_push_c_function(ctx, js_execute_GotoAndPlay, 1);
    duk_put_global_string(ctx, CMD_MS_GOTO_AND_PLAY);

    duk_push_c_function(ctx, js_execute_LogMsg, 1);
    duk_put_global_string(ctx, CMD_MS_LOG_MSG);

    duk_push_global_object(ctx);
    duk_push_pointer(ctx, this);
    duk_put_prop_literal(ctx, -2, DUK_HIDDEN_SYMBOL("interpreter_obj"));

    return ctx;
}

// see https://datatracker.ietf.org/doc/draft-ietf-cellar-chapter-codecs
//  for a description of existing commands
bool matroska_js_interpreter_c::Interpret( const binary * p_command, size_t i_size )
{
    bool success = false;
    std::string sz_command( reinterpret_cast<const char*> (p_command), i_size );
    vlc_timer_t timer;

    vlc_debug( l, "command input : %s", sz_command.c_str() );
    try {
        if (vlc_timer_create(&timer, [](void *p) {
            auto p_this = static_cast<matroska_js_interpreter_c *>(p);
            p_this->on_timeout();
            }, this))
        {
            vlc_debug(l, "Timer initialization failed");
            return false;
        }
        timed_out = false;
        vlc_timer_schedule(timer, false, INTERPRETER_TIMEOUT, VLC_TIMER_FIRE_ONCE);
        success = !duk_peval_lstring(ctx, sz_command.c_str(), sz_command.size());
        vlc_timer_destroy(timer);

        if (timed_out)
        {
            vlc_error(l, "Execution TimedOut!\n%s", duk_safe_to_stacktrace(ctx, -1));
            return false;
        }

        if (!success)
        {
            vlc_error(l, "duktape:: Evaluation Failed!\n%s", duk_safe_to_stacktrace(ctx, -1));
            return false;
        }
    } catch (std::exception e){
        vlc_error(l, "duktape:: Evaluation Failed!\n%s", duk_safe_to_stacktrace(ctx, -1));
        return false;
    }
    vlc_debug(l, "duktape:: Evaluation complete");

    return true;
}

} // namespace
