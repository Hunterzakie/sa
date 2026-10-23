/*****************************************************************************
 * VLCPlayQueueDataSource.m: MacOS X interface module
 *****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * Authors: Felix Paul Kühne <fkuehne # videolan -dot- org>
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

#import "VLCPlayQueueDataSource.h"

#import "extensions/NSPasteboardItem+VLCAdditions.h"
#import "extensions/NSString+Helpers.h"
#import "extensions/NSView+VLCAdditions.h"
#import "main/VLCMain.h"
#import "playqueue/VLCPlayQueueController.h"
#import "playqueue/VLCPlayQueueTableCellView.h"
#import "playqueue/VLCPlayQueueItem.h"
#import "playqueue/VLCPlayQueueModel.h"
#import "library/VLCInputItem.h"
#import "library/VLCLibraryDataTypes.h"
#import "views/VLCDragDropView.h"
#import "views/VLCFileDragRecognisingView.h"

static NSString *VLCPlayQueueCellIdentifier = @"VLCPlayQueueCellIdentifier";

@interface VLCPlayQueueDataSource ()
{
    VLCPlayQueueModel *_playQueueModel;
    NSArray<NSNumber *> *_filteredIndexes;
}

@property (readonly, nonatomic) BOOL filtering;

@end

@implementation VLCPlayQueueDataSource

- (void)setPlayQueueController:(VLCPlayQueueController *)playQueueController
{
    _playQueueController = playQueueController;
    _playQueueModel = _playQueueController.playQueueModel;
    [self updateFilteredIndexes];
}

- (void)setCounterTextField:(NSTextField *)counterTextField
{
    _counterTextField = counterTextField;
    [self updateCounterTextField];
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _filterString = @"";
        _filteredIndexes = @[];
    }
    return self;
}

- (void)prepareForUse
{
    [_tableView registerForDraggedTypes:@[
        VLCMediaLibraryMediaItemPasteboardType,
        VLCMediaLibraryMediaItemUTI,
        VLCPlaylistItemPasteboardType,
        NSPasteboardTypeURL,
        NSPasteboardTypeFileURL,
        NSPasteboardTypeString,
        NSFilenamesPboardType
    ]];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    return self.filtering ? _filteredIndexes.count : _playQueueModel.numberOfPlayQueueItems;
}

- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    VLCPlayQueueTableCellView *cellView =
        [tableView makeViewWithIdentifier:VLCPlayQueueCellIdentifier owner:self];

    if (cellView == nil) {
        cellView = [VLCPlayQueueTableCellView fromNibWithOwner:self];
        if (cellView == nil) {
            msg_Err(getIntf(), "Failed to load nib file to show playlist items");
            return nil;
        }
        cellView.identifier = VLCPlayQueueCellIdentifier;
    }

    VLCPlayQueueItem * const item = [self playQueueItemAtRow:row];
    if (!item) {
        msg_Err(getIntf(), "playlist model did not return an item for representation");
        return cellView;
    }

    cellView.representedPlayQueueItem = item;
    cellView.representsCurrentPlayQueueItem =
        _playQueueController.currentPlayQueueIndex == (size_t)[self modelIndexForRow:row];

    return cellView;
}

- (void)playQueueUpdated
{
    [self updateFilteredIndexes];

    self.dragDropView.hidden = _playQueueModel.numberOfPlayQueueItems > 0 ? YES : NO;
    [self updateCounterTextField];
    [_tableView reloadData];
}

- (void)playQueueItemsInsertedAtIndex:(NSUInteger)index count:(NSUInteger)count
{
    if (self.filtering) {
        [self playQueueUpdated];
        return;
    }

    const NSUInteger numberOfPlayQueueItems = _playQueueModel.numberOfPlayQueueItems;
    self.dragDropView.hidden = numberOfPlayQueueItems > 0 ? YES : NO;
    self.counterTextField.stringValue = [NSString stringWithFormat:@"%lu", numberOfPlayQueueItems];

    if (count == 0) {
        return;
    }

    NSAssert(index <= numberOfPlayQueueItems - count,
             @"Inserted play queue rows must be within the model bounds");
    NSIndexSet * const indexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(index, count)];
    [_tableView insertRowsAtIndexes:indexes withAnimation:NSTableViewAnimationEffectNone];
}

- (void)playQueueItemsUpdatedAtIndex:(NSUInteger)index count:(NSUInteger)count
{
    if (count == 0) {
        return;
    }

    if (self.filtering) {
        [self playQueueUpdated];
        return;
    }

    const NSUInteger numberOfPlayQueueItems = _playQueueModel.numberOfPlayQueueItems;
    NSAssert(index <= numberOfPlayQueueItems - count,
             @"Updated play queue rows must be within the model bounds");
    NSIndexSet * const indexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(index, count)];
    [_tableView reloadDataForRowIndexes:indexes columnIndexes:[NSIndexSet indexSetWithIndex:0]];
}

- (id<NSPasteboardWriting>)tableView:(NSTableView *)tableView pasteboardWriterForRow:(NSInteger)row
{
    NSPasteboardItem * const pboardItem = [[NSPasteboardItem alloc] init];
    VLCPlayQueueItem * const playlistItem = [self playQueueItemAtRow:row];
    [pboardItem setString:[@(playlistItem.uniqueID) stringValue] forType:VLCPlaylistItemPasteboardType];
    return pboardItem;
}

- (NSDragOperation)tableView:(NSTableView *)tableView
                validateDrop:(id<NSDraggingInfo>)info
                 proposedRow:(NSInteger)row
       proposedDropOperation:(NSTableViewDropOperation)dropOperation
{
    if (self.filtering &&
        [info.draggingPasteboard availableTypeFromArray:@[VLCPlaylistItemPasteboardType]] != nil) {
        return NSDragOperationNone;
    }

    // Make drops at the end of the table go to the end.
    if (row == -1) {
        row = tableView.numberOfRows;
        dropOperation = NSTableViewDropAbove;
        [tableView setDropRow:row dropOperation:dropOperation];
    }

    // We don't ever want to drop onto a row, only between rows.
    if (dropOperation == NSTableViewDropOn) {
        [tableView setDropRow:row + 1 dropOperation:NSTableViewDropAbove];
    }

    return NSDragOperationCopy;
}

- (BOOL)tableView:(NSTableView *)tableView
       acceptDrop:(id<NSDraggingInfo>)info
              row:(NSInteger)row
    dropOperation:(NSTableViewDropOperation)dropOperation
{
    NSString *encodedIDtoMove = [info.draggingPasteboard stringForType:VLCPlaylistItemPasteboardType];
    const NSInteger targetIndex = [self modelInsertionIndexForRow:row];
    if (encodedIDtoMove != nil) {
        int64_t uniqueID = [encodedIDtoMove integerValue];
        [_playQueueController moveItemWithID:uniqueID toPosition:targetIndex];
        return YES;
    }

    /* Collect library media items from all pasteboard items.
     * Table view drags create one NSPasteboardItem per selected row, so we
     * must iterate all of them to capture every dragged item. */
    NSMutableArray<VLCMediaLibraryMediaItem *> * const allMediaItems = [NSMutableArray array];

    for (NSPasteboardItem * const pboardItem in info.draggingPasteboard.pasteboardItems) {
        NSData *itemData = [pboardItem dataForType:VLCMediaLibraryMediaItemPasteboardType];
        if (!itemData) {
            itemData = [pboardItem dataForType:VLCMediaLibraryMediaItemUTI];
        }
        if (!itemData) {
            continue;
        }

        NSArray<VLCMediaLibraryMediaItem *> * const items = [VLCMediaLibraryMediaItem mediaItemsFromPasteboardData:itemData];
        if (items) {
            [allMediaItems addObjectsFromArray:items];
        }
    }

    /* Collection view drags write a single aggregated payload directly on the
     * dragging pasteboard instead of creating one NSPasteboardItem per item. */
    if (allMediaItems.count == 0) {
        NSData *itemData = [info.draggingPasteboard dataForType:VLCMediaLibraryMediaItemPasteboardType];
        if (!itemData) {
            itemData = [info.draggingPasteboard dataForType:VLCMediaLibraryMediaItemUTI];
        }

        if (itemData) {
            NSArray<VLCMediaLibraryMediaItem *> * const items = [VLCMediaLibraryMediaItem mediaItemsFromPasteboardData:itemData];
            if (items) {
                [allMediaItems addObjectsFromArray:items];
            }
        }
    }

    if (allMediaItems.count > 0) {
        NSInteger insertionIndex = targetIndex;
        for (VLCMediaLibraryMediaItem * const mediaItem in allMediaItems) {
            [_playQueueController addInputItem:mediaItem.inputItem.vlcInputItem
                                    atPosition:insertionIndex
                                 startPlayback:NO];
            insertionIndex++;
        }
        return YES;
    }

    return [VLCFileDragRecognisingView
        handlePasteboardFromDragSessionAsPlayQueueItems:info.draggingPasteboard];
}

- (NSArray<NSTableViewRowAction *> *)tableView:(NSTableView *)tableView
                              rowActionsForRow:(NSInteger)row
                                          edge:(NSTableRowActionEdge)edge
{
    if (edge == NSTableRowActionEdgeTrailing) {
        VLCPlayQueueItem * const item = [self playQueueItemAtRow:row];
        if (item == nil) {
            return @[];
        }

        NSTableViewRowAction * const removeAction =
            [NSTableViewRowAction rowActionWithStyle:NSTableViewRowActionStyleDestructive
                                                title:_NS("Remove from Play Queue")
                                               handler:^(NSTableViewRowAction * const __unused action, const NSInteger row) {
                NSIndexSet * const indices = [self modelIndexSetForRowIndexes:[NSIndexSet indexSetWithIndex:row]];
                [_playQueueController removeItemsAtIndexes:indices];
            }];
        return @[removeAction];
    }
    return @[];
}

- (void)scrollToCurrentPlayQueueItem
{
    NSInteger filteredRow = [self rowForModelIndex:_playQueueController.currentPlayQueueIndex];
    if (filteredRow >= 0) {
        [_tableView scrollRowToVisible:filteredRow];
    }
}

- (void)setFilterString:(NSString *)filterString
{
    if (filterString == nil) {
        filterString = @"";
    }

    if ([_filterString isEqualToString:filterString]) {
        return;
    }

    _filterString = [filterString copy];
    [self playQueueUpdated];
}

- (BOOL)filtering
{
    return _filterString.length > 0;
}

- (NSInteger)modelIndexForRow:(NSInteger)row
{
    if (row < 0) {
        return -1;
    }

    if (!self.filtering) {
        return row < (NSInteger)_playQueueModel.numberOfPlayQueueItems ? row : -1;
    }

    return row < (NSInteger)_filteredIndexes.count ? _filteredIndexes[row].integerValue : -1;
}

- (NSIndexSet *)modelIndexSetForRowIndexes:(NSIndexSet *)rowIndexes
{
    NSMutableIndexSet * const modelIndexes = [NSMutableIndexSet indexSet];
    [rowIndexes enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL * const __unused stop) {
        const NSInteger modelIndex = [self modelIndexForRow:(NSInteger)idx];
        if (modelIndex >= 0) {
            [modelIndexes addIndex:(NSUInteger)modelIndex];
        }
    }];
    return modelIndexes.copy;
}

- (VLCPlayQueueItem *)playQueueItemAtRow:(NSInteger)row
{
    const NSInteger modelIndex = [self modelIndexForRow:row];
    if (modelIndex < 0) {
        return nil;
    }
    return [_playQueueModel playQueueItemAtIndex:modelIndex];
}

- (void)updateFilteredIndexes
{
    if (!self.filtering) {
        _filteredIndexes = @[];
        return;
    }

    NSMutableArray<NSNumber *> * const matchingIndexes = [NSMutableArray array];
    const NSStringCompareOptions options = NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch;

    for (NSUInteger idx = 0; idx < _playQueueModel.numberOfPlayQueueItems; idx++) {
        VLCPlayQueueItem * const item = [_playQueueModel playQueueItemAtIndex:(NSInteger)idx];
        if (item == nil) {
            continue;
        }

        NSArray<NSString *> * const candidateStrings = @[
            item.title ?: @"",
            item.artistName ?: @"",
            item.albumName ?: @"",
            item.path ?: @"",
            item.url.absoluteString ?: @""
        ];

        for (NSString * const candidate in candidateStrings) {
            if ([candidate rangeOfString:self.filterString options:options].location != NSNotFound) {
                [matchingIndexes addObject:@(idx)];
                break;
            }
        }
    }

    _filteredIndexes = matchingIndexes.copy;
}

- (NSInteger)rowForModelIndex:(NSUInteger)modelIndex
{
    if (!self.filtering) {
        return (NSInteger)modelIndex;
    }

    const NSUInteger row = [_filteredIndexes indexOfObject:@(modelIndex)];
    return row == NSNotFound ? -1 : (NSInteger)row;
}

- (NSInteger)modelInsertionIndexForRow:(NSInteger)row
{
    if (!self.filtering) {
        return row;
    }

    if (row < 0 || row >= (NSInteger)_filteredIndexes.count) {
        return (NSInteger)_playQueueModel.numberOfPlayQueueItems;
    }

    return [self modelIndexForRow:row];
}

- (void)updateCounterTextField
{
    if (self.counterTextField == nil) {
        return;
    }

    if (self.filtering) {
        self.counterTextField.stringValue =
            [NSString stringWithFormat:@"%lu/%lu",
             [self numberOfRowsInTableView:self.tableView],
             _playQueueModel.numberOfPlayQueueItems];
    } else {
        self.counterTextField.stringValue =
            [NSString stringWithFormat:@"%lu", _playQueueModel.numberOfPlayQueueItems];
    }
}

@end
