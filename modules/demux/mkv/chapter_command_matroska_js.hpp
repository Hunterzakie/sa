// Copyright (C) 2024 VLC authors and VideoLAN
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// chapter_command_matroska_js.hpp : MatroskaJS implementation for Matroska Chapter Codecs
// Author: Khalid Masum <khalid.masum.92@gmail.com>

#ifndef VLC_MKV_CHAPTER_COMMAND_MATROSKA_JS_HPP_
#define VLC_MKV_CHAPTER_COMMAND_MATROSKA_JS_HPP_

#include "chapter_command_script_common.hpp"

typedef struct duk_hthread duk_context;
typedef int duk_small_int_t;
typedef unsigned int duk_small_uint_t;
typedef duk_small_int_t duk_ret_t;

namespace mkv {

class matroska_js_interpreter_c : public matroska_script_interpreter_common_c
{
private:
    bool gotoChapter(chapter_uid i_chapter_uid);

    //JavaScript access functions
    static duk_ret_t js_execute_GotoAndPlay(duk_context *ctx);
    static duk_ret_t js_execute_LogMsg(duk_context *ctx);

    //Command executors
    bool execute_GotoAndPlay(const std::string &arg);
    bool execute_LogMsg(const std::string &arg);

    duk_context *ctx;

    static void execution_timeout_checker(void *data);
    static void fatal_error_handler(void *udata, const char* msg);
    duk_context* ms_setup();
    void on_timeout();
public:
    std::atomic_bool timed_out;

    matroska_js_interpreter_c( struct vlc_logger *log, chapter_codec_vm & vm_ )
    :matroska_script_interpreter_common_c( log, vm_ )
    {
        ctx = ms_setup();
    }

    bool Interpret( const binary * p_command, size_t i_size ) override;

    // MatroskaJS commands
    static const char* CMD_MS_GOTO_AND_PLAY;
    static const char* CMD_MS_LOG_MSG;
};

class matroska_js_codec_c : public matroska_script_codec_common_c
{
public:
    matroska_js_codec_c( struct vlc_logger *log, chapter_codec_vm & vm_, matroska_js_interpreter_c & interpreter_)
    :matroska_script_codec_common_c( log, vm_, MATROSKA_CHAPTER_CODEC_MATROSKA_JS)
    ,interpreter( interpreter_ )
    {}

    matroska_script_interpreter_common_c & get_interpreter() override
    {
        return interpreter;
    }

protected:
    matroska_js_interpreter_c & interpreter;
};

} // namespace

#endif // VLC_MKV_CHAPTER_COMMAND_SCRIPT_HPP_
