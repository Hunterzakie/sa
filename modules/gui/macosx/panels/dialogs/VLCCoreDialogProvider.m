/*****************************************************************************
 * VLCCoreDialogProvider.m: Mac OS X Core Dialogs
 *****************************************************************************
 * Copyright (C) 2005-2019 VLC authors and VideoLAN
 *
 * Authors: Derk-Jan Hartman <hartman at videolan dot org>
 *          Felix Paul Kühne <fkuehne at videolan dot org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#import "VLCCoreDialogProvider.h"

#import "extensions/NSString+Helpers.h"
#import "main/VLCMain.h"
#import "windows/VLCErrorWindowController.h"

#import <vlc_common.h>
#import <vlc_dialog.h>

@interface VLCCoreDialogProvider ()
{
    VLCErrorWindowController *_errorPanel;
    vlc_dialog_id *_currentModalDialogID;
    BOOL _invalidated;
}

- (void)displayErrorWithTitle:(NSString *)title
                         text:(NSString *)text;

- (void)displayLoginDialog:(vlc_dialog_id *)dialogID
                     title:(NSString *)title
                      text:(NSString *)text
                  username:(NSString *)username
                askToStore:(BOOL)askToStore;

- (void)displayQuestion:(vlc_dialog_id *)dialogID
                  title:(NSString *)title
                   text:(NSString *)text
                   type:(vlc_dialog_question_type)questionType
             cancelText:(NSString *)cancelText
            action1Text:(NSString *)action1Text
            action2Text:(NSString *)action2Text;

- (void)displayProgressDialog:(vlc_dialog_id *)dialogID
                        title:(NSString *)title
                         text:(NSString *)text
                indeterminate:(BOOL)indeterminate
                     position:(float)position
                  cancelTitle:(NSString *)cancelTitle;

- (void)updateDisplayedProgressDialog:(vlc_dialog_id *)dialogID
                             position:(float)position
                                 text:(NSString *)text;

- (void)updateDisplayedProgressDialogWithInfo:(NSDictionary *)info;

- (void)cancelDialog:(NSValue *)dialogIDValue;

- (void)restoreModalDialog:(vlc_dialog_id *)previousModalDialogID;

- (BOOL)dismissIfCancelledDialog:(vlc_dialog_id *)dialogID;

@end

static void displayErrorCallback(void *p_data,
                                 const char *psz_title,
                                 const char *psz_text)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge VLCCoreDialogProvider *)p_data;
        NSString *title = toNSStr(psz_title);
        NSString *text = toNSStr(psz_text);
        dispatch_async(dispatch_get_main_queue(), ^{
            [dialogProvider displayErrorWithTitle:title text:text];
        });
    }
}

static void displayLoginCallback(void *p_data,
                                 vlc_dialog_id *p_id,
                                 const char *psz_title,
                                 const char *psz_text,
                                 const char *psz_default_username,
                                 bool b_ask_store)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge VLCCoreDialogProvider *)p_data;
        NSString *title = toNSStr(psz_title);
        NSString *text = toNSStr(psz_text);
        NSString *defaultUsername = toNSStr(psz_default_username);
        dispatch_async(dispatch_get_main_queue(), ^{
            [dialogProvider displayLoginDialog:p_id
                                         title:title
                                          text:text
                                      username:defaultUsername
                                    askToStore:b_ask_store];
        });
    }
}

static void displayQuestionCallback(void *p_data,
                                    vlc_dialog_id *p_id,
                                    const char *psz_title,
                                    const char *psz_text,
                                    vlc_dialog_question_type i_type,
                                    const char *psz_cancel,
                                    const char *psz_action1,
                                    const char *psz_action2)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge  VLCCoreDialogProvider *)p_data;
        NSString *title = toNSStr(psz_title);
        NSString *text = toNSStr(psz_text);
        NSString *cancelText = toNSStr(psz_cancel);
        NSString *action1Text = toNSStr(psz_action1);
        NSString *action2Text = toNSStr(psz_action2);
        dispatch_async(dispatch_get_main_queue(), ^{
            [dialogProvider displayQuestion:p_id
                                      title:title
                                       text:text
                                       type:i_type
                                 cancelText:cancelText
                                action1Text:action1Text
                                action2Text:action2Text];
        });
    }
}

static void displayProgressCallback(void *p_data,
                                    vlc_dialog_id *p_id,
                                    const char *psz_title,
                                    const char *psz_text,
                                    bool b_indeterminate,
                                    float f_position,
                                    const char *psz_cancel)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge VLCCoreDialogProvider *)p_data;
        NSString *title = toNSStr(psz_title);
        NSString *text = toNSStr(psz_text);
        NSString *cancelTitle = toNSStr(psz_cancel);
        dispatch_async(dispatch_get_main_queue(), ^{
            [dialogProvider displayProgressDialog:p_id
                                            title:title
                                             text:text
                                    indeterminate:b_indeterminate
                                         position:f_position
                                      cancelTitle:cancelTitle];
        });
    }
}

static void cancelCallback(void * __unused p_data,
                           vlc_dialog_id * __unused p_id)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge VLCCoreDialogProvider *)p_data;
        /* NSModalPanelRunLoopMode: also deliver while runModalForWindow: is up. */
        NSArray *modes = @[NSDefaultRunLoopMode, NSModalPanelRunLoopMode];
        [dialogProvider performSelectorOnMainThread:@selector(cancelDialog:)
                                         withObject:[NSValue valueWithPointer:p_id]
                                      waitUntilDone:NO
                                              modes:modes];
    }
}

static void updateProgressCallback(void *p_data,
                                   vlc_dialog_id *p_id,
                                   float f_value,
                                   const char *psz_text)
{
    @autoreleasepool {
        VLCCoreDialogProvider *dialogProvider = (__bridge VLCCoreDialogProvider *)p_data;
        /* NSModalPanelRunLoopMode: also deliver while runModalForWindow: is up. */
        NSDictionary *info = @{
            @"dialogID" : [NSValue valueWithPointer:p_id],
            @"position" : @(f_value),
            @"text"     : toNSStr(psz_text)
        };
        NSArray *modes = @[NSDefaultRunLoopMode, NSModalPanelRunLoopMode];
        [dialogProvider performSelectorOnMainThread:@selector(updateDisplayedProgressDialogWithInfo:)
                                         withObject:info
                                      waitUntilDone:NO
                                              modes:modes];
    }
}

@implementation VLCCoreDialogProvider

- (instancetype)init
{
    self = [super init];

    if (self) {
        msg_Dbg(getIntf(), "Register dialog provider");
        [[NSBundle mainBundle] loadNibNamed:@"CoreDialogs" owner:self topLevelObjects:nil];

        intf_thread_t *p_intf = getIntf();
        /* subscribe to various interactive dialogues */

        const vlc_dialog_cbs cbs = {
            displayLoginCallback,
            displayQuestionCallback,
            displayProgressCallback,
            cancelCallback,
            updateProgressCallback
        };

        vlc_dialog_provider_set_error_callback(p_intf, displayErrorCallback, (__bridge void *)self);
        vlc_dialog_provider_set_callbacks(p_intf, &cbs, (__bridge void *)self);
    }

    return self;
}

/* Must run before the provider starts deallocating. */
- (void)invalidate
{
    if (_invalidated)
        return;
    _invalidated = YES;

    msg_Dbg(getIntf(), "Deinitializing dialog provider");

    intf_thread_t *p_intf = getIntf();
    vlc_dialog_provider_set_callbacks(p_intf, NULL, NULL);
    vlc_dialog_provider_set_error_callback(p_intf, NULL, NULL);
}

- (void)dealloc
{
    /* Last resort: leaving the callbacks registered would be worse. */
    [self invalidate];
}

-(void)awakeFromNib
{
    [_authenticationLoginLabel setStringValue: _NS("Username")];
    [_authenticationPasswordLabel setStringValue: _NS("Password")];
    [_authenticationCancelButton setTitle: _NS("Cancel")];
    [_authenticationOkButton setTitle: _NS("OK")];
    [_authenticationStorePasswordCheckbox setTitle:_NS("Remember")];

    [_progressCancelButton setTitle: _NS("Cancel")];
    [_progressIndicator setUsesThreadedAnimation: YES];
}

- (void)displayErrorWithTitle:(NSString *)title text:(NSString *)text
{
    if (!_errorPanel) {
        _errorPanel = [[VLCErrorWindowController alloc] init];
    }
    [_errorPanel showWindow:nil];
    [_errorPanel addError:title withMsg:text];
}

- (void)displayLoginDialog:(vlc_dialog_id *)dialogID
                     title:(NSString *)title
                      text:(NSString *)text
                  username:(NSString *)username
                askToStore:(BOOL)askToStore
{
    if ([self dismissIfCancelledDialog:dialogID])
        return;

    [_authenticationTitleLabel setStringValue:title];
    _authenticationWindow.title = title;
    [_authenticationDescriptionLabel setStringValue:text];

    [_authenticationLoginTextField setStringValue:username];
    [_authenticationPasswordTextField setStringValue:@""];

    _authenticationStorePasswordCheckbox.hidden = !askToStore;
    _authenticationStorePasswordCheckbox.state = NSOffState;

    [_authenticationWindow center];
    vlc_dialog_id * const previousModalDialogID = _currentModalDialogID;
    _currentModalDialogID = dialogID;
    NSInteger returnValue = [NSApp runModalForWindow:_authenticationWindow];
    [self restoreModalDialog:previousModalDialogID];
    [_authenticationWindow close];

    username = _authenticationLoginTextField.stringValue;
    NSString *password = _authenticationPasswordTextField.stringValue;
    if (returnValue == 0) {
        vlc_dialog_id_dismiss(dialogID);
    } else {
        vlc_dialog_id_post_login(dialogID,
                                 username ? [username UTF8String] : NULL,
                                 password ? [password UTF8String] : NULL,
                                 _authenticationStorePasswordCheckbox.state == NSOnState);
    }
}

- (IBAction)authenticationDialogAction:(id)sender
{
    if ([[sender title] isEqualToString: _NS("OK")])
        [NSApp stopModalWithCode: 1];
    else
        [NSApp stopModalWithCode: 0];
}

- (void)displayQuestion:(vlc_dialog_id *)dialogID
                  title:(NSString *)title
                   text:(NSString *)text
                   type:(vlc_dialog_question_type)questionType
             cancelText:(NSString *)cancelText
            action1Text:(NSString *)action1Text
            action2Text:(NSString *)action2Text
{
    if ([self dismissIfCancelledDialog:dialogID])
        return;

    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:title];
    [alert setInformativeText:text];
    [alert addButtonWithTitle:action1Text];
    [alert addButtonWithTitle:action2Text];
    [alert addButtonWithTitle:cancelText];
    [alert.buttons.lastObject setKeyEquivalent:[NSString stringWithFormat:@"%C", 0x1b]];

    switch (questionType) {
        case VLC_DIALOG_QUESTION_WARNING:
            [alert setAlertStyle:NSAlertStyleWarning];
            break;
        case VLC_DIALOG_QUESTION_CRITICAL:
            [alert setAlertStyle:NSAlertStyleCritical];
            break;
        default:
            [alert setAlertStyle:NSAlertStyleInformational];
            break;
    }

    vlc_dialog_id * const previousModalDialogID = _currentModalDialogID;
    _currentModalDialogID = dialogID;
    NSInteger returnValue = [alert runModal];
    [self restoreModalDialog:previousModalDialogID];
    switch (returnValue) {
        case NSAlertFirstButtonReturn:
            vlc_dialog_id_post_action(dialogID, 1);
            break;

        case NSAlertSecondButtonReturn:
            vlc_dialog_id_post_action(dialogID, 2);
            break;

        case NSAlertThirdButtonReturn:
        default:
            vlc_dialog_id_dismiss(dialogID);
    }

}

- (BOOL)dismissIfCancelledDialog:(vlc_dialog_id *)dialogID
{
    if (!vlc_dialog_is_cancelled(getIntf(), dialogID))
        return NO;

    vlc_dialog_id_dismiss(dialogID);
    return YES;
}

/* The incoming id may already be freed and its address reused, so only compare
 * it and let the core confirm the cancellation. */
- (void)cancelDialog:(NSValue *)dialogIDValue
{
    if (_currentModalDialogID != [dialogIDValue pointerValue])
        return;

    if (vlc_dialog_is_cancelled(getIntf(), _currentModalDialogID))
        [NSApp stopModalWithCode: 0];
}

/* The enclosing dialog may have been cancelled while this one was up. */
- (void)restoreModalDialog:(vlc_dialog_id *)previousModalDialogID
{
    _currentModalDialogID = previousModalDialogID;

    if (_currentModalDialogID != NULL &&
        vlc_dialog_is_cancelled(getIntf(), _currentModalDialogID))
        [NSApp stopModalWithCode: 0];
}

- (void)displayProgressDialog:(vlc_dialog_id *)dialogID
                        title:(NSString *)title
                         text:(NSString *)text
                indeterminate:(BOOL)indeterminate
                     position:(float)position
                  cancelTitle:(NSString *)cancelTitle
{
    if ([self dismissIfCancelledDialog:dialogID])
        return;

    _progressTitleLabel.stringValue = title;
    _progressWindow.title = title;

    _progressDescriptionLabel.stringValue = text;

    _progressIndicator.indeterminate = indeterminate;
    _progressIndicator.doubleValue = position;

    if ([cancelTitle length] > 0) {
        _progressCancelButton.title = cancelTitle;
        _progressCancelButton.enabled = YES;
    } else {
        _progressCancelButton.title = _NS("Cancel");
        _progressCancelButton.enabled = NO;
    }

    [_progressIndicator startAnimation:self];

    [_progressWindow center];
    vlc_dialog_id * const previousModalDialogID = _currentModalDialogID;
    _currentModalDialogID = dialogID;
    [NSApp runModalForWindow:_progressWindow];
    [self restoreModalDialog:previousModalDialogID];
    [_progressWindow close];

    [_progressIndicator stopAnimation:self];

    vlc_dialog_id_dismiss(dialogID);
}

- (void)updateDisplayedProgressDialogWithInfo:(NSDictionary *)info
{
    [self updateDisplayedProgressDialog:[info[@"dialogID"] pointerValue]
                               position:[info[@"position"] floatValue]
                                   text:info[@"text"]];
}

- (void)updateDisplayedProgressDialog:(vlc_dialog_id *)dialogID
                             position:(float)position
                                 text:(NSString *)text
{
    if (_currentModalDialogID != dialogID) {
        return;
    }

    if (!_progressIndicator.indeterminate) {
        _progressIndicator.doubleValue = position;
        _progressDescriptionLabel.stringValue = text;
    }
}

- (VLCErrorWindowController *)errorPanel
{
    if (!_errorPanel) {
        _errorPanel = [[VLCErrorWindowController alloc] init];
    }

    return _errorPanel;
}

- (IBAction)progressDialogAction:(id)sender
{
    [NSApp stopModalWithCode: -1];
}

@end
