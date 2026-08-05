/*****************************************************************************
 * vlc_services_discovery.h : Services Discover functions
 *****************************************************************************
 * Copyright (C) 1999-2004 VLC authors and VideoLAN
 *
 * Authors: Pierre d'Herbemont <pdherbemont # videolan.org>
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

#ifndef VLC_SERVICES_DISCOVERY_H_
#define VLC_SERVICES_DISCOVERY_H_

#include <vlc_input.h>
#include <vlc_probe.h>

/**
 * \file
 * This file lists functions and structures for service discovery (SD) in vlc
 */

# ifdef __cplusplus
extern "C" {
# endif

/**
 * @{
 */

struct services_discovery_callbacks
{
    void (*item_added)(struct services_discovery_t *sd, input_item_t *parent,
                       input_item_t *item);
    void (*item_removed)(struct services_discovery_t *sd, input_item_t *item);
};

struct services_discovery_owner_t
{
    const struct services_discovery_callbacks *cbs;
    void *sys; /**< Private data for the owner callbacks */
};

struct vlc_sd_config_operations;

/**
 * Main service discovery structure to build a SD module
 */
struct services_discovery_t
{
    struct vlc_object_t obj;
    module_t *          p_module;             /**< Loaded module */

    char *psz_name;                           /**< Main name of the SD */
    config_chain_t *p_cfg;                    /**< Configuration for the SD */

    const char *description; /**< Human-readable name */

    /** Control function
     * \see services_discovery_command_e
     */
    int ( *pf_control ) ( services_discovery_t *, int, va_list );

    /** Configurable SD callbacks, should be set by an SD that advertises SD_CAP_CONFIGURABLE 
     * \see SD_CAP_CONFIGURABLE
     */
    const struct vlc_sd_config_operations *config_ops;

    void *p_sys;                              /**< Custom private data */

    struct services_discovery_owner_t owner; /**< Owner callbacks */
};

/**
 * Service discovery categories
 * \see vlc_sd_probe_Add
 */
enum services_discovery_category_e
{
    SD_CAT_DEVICES = 1,           /**< Devices, like portable music players */
    SD_CAT_LAN,                   /**< LAN/WAN services, like Upnp or SAP */
    SD_CAT_INTERNET,              /**< Internet or Website channels services */
    SD_CAT_MYCOMPUTER             /**< Computer services, like Discs or Apps */
};

/**
 * Service discovery control commands
 */
enum services_discovery_command_e
{
    SD_CMD_SEARCH = 1,          /**< arg1 = query */
    SD_CMD_DESCRIPTOR           /**< arg1 = services_discovery_descriptor_t* */
};

/**
 * Service discovery capabilities
 */
enum services_discovery_capability_e
{
    SD_CAP_SEARCH = 1,          /**< One can search in the SD */
    SD_CAP_CONFIGURABLE = 2     /**< The SD is configurable */
};

/**
 * Service discovery descriptor
 * \see services_discovery_command_e
 */
typedef struct
{
    char *psz_short_desc;       /**< The short description, human-readable */
    char *psz_icon_url;         /**< URL to the icon that represents it */
    char *psz_url;              /**< URL for the service */
    int   i_capabilities;       /**< \see services_discovery_capability_e */
} services_discovery_descriptor_t;

/**
 * Type of a configurable SD entry field. It selects the editor widget shown in
 * the configuration UI and how the value is persisted.
 * \see vlc_sd_field_desc_t
 */
enum vlc_sd_field_type_e
{
    SD_FIELD_TEXT = 0,  /**< Free single-line text.                            */
    SD_FIELD_SECRET,    /**< Secret text; stored in the keystore, never in the
                             plaintext configuration.                          */
    SD_FIELD_CHOICE,    /**< Integer picked out of \ref vlc_sd_field_desc_t.choices.    */
    SD_FIELD_FILE,      /**< Local file or URL; the editor offers a browser.   */
    SD_FIELD_INTEGER,
    SD_FIELD_BOOLEAN,
};

/**
 * One selectable value of a SD_FIELD_CHOICE field: a machine value (an enum
 * defined by the SD) and a human-readable label shown in the UI.
 */
typedef struct
{
    int value;          /**< Stored value (an SD-defined enum).  */
    const char *label;  /**< Display text for the UI (gettext).  */
} vlc_sd_field_choice_t;

/**
 * Description of one field making up a configurable SD entry.
 *
 * An entry schema is an array of these, it is returned by
 * vlc_sd_config_operations.describe() along with its length.
 */
typedef struct
{
    const char *key;              /**< Machine name, e.g. "uri" (never NULL).  */
    const char *label;            /**< Human-readable label for the UI.         */
    enum vlc_sd_field_type_e type;
    bool required;                /**< An entry missing this field (empty or
                                       absent) is rejected.              */
    const vlc_sd_field_choice_t *choices;  /**< SD_FIELD_CHOICE only.               */
    size_t choice_count;               /**< How many choices \ref choices holds. */
} vlc_sd_field_desc_t;


typedef struct
{
    char *key;
    enum vlc_sd_field_type_e type;   /**< determines what union member to use */
    union {
        char *value;      /**< SD_FIELD_TEXT, SD_FIELD_SECRET, SD_FIELD_FILE */
        int64_t integer;  /**< SD_FIELD_INTEGER, SD_FIELD_CHOICE            */
        bool boolean;     /**< SD_FIELD_BOOLEAN                             */
    };
} vlc_sd_field_t;

/**
 * Input entry of a configurable SD.
 *
 * An entry is identified by its \ref id, assigned by the SD; it is only valid
 * for as long as the SD is open (it is not persisted) and is always strictly
 * positive. 0 means "no id", which is what a UI leaves when adding an entry.
 *
 * \ref fields holds \ref field_count fields, and may be NULL when the entry has
 * none.
 */
typedef struct vlc_sd_input
{
    int id;
    vlc_sd_field_t *fields;
    size_t field_count;
} vlc_sd_input_t;

/**
 * Entry management operations of a configurable services discovery.
 */
struct vlc_sd_config_operations
{
    /**
     * Describe what one entry is made of.
     *
     * \param count [out] number of fields in the returned schema
     * \return the schema of an entry, owned by the SD and valid for as long as
     *         it is open
     */
    const vlc_sd_field_desc_t *(*describe)(services_discovery_t *sd, size_t *count);

    /**
     * List the current entries.
     *
     * \param count [out] how many entries the returned array holds.
     * \return the entries, to be released with vlc_sd_input_ListFree(), or NULL
     *         when the list is empty or on error
     */
    vlc_sd_input_t *(*list)(services_discovery_t *sd, size_t *count);

    /**
     * Add an entry.
     *
     * \param entry the entry to add; its id is ignored, the SD assigns one.
     * \return the id assigned to the new entry, which is strictly positive, or
     *         a negative error code
     */
    int (*add)(services_discovery_t *sd, const vlc_sd_input_t *entry);

    /**
     * Remove the entry designated by \p id.
     *
     * \return VLC_SUCCESS on success
     */
    int (*remove)(services_discovery_t *sd, int id);
};


/***********************************************************************
 * Service Discovery
 ***********************************************************************/

/**
 * Ask for a research in the SD
 * @param p_sd: the Service Discovery
 * @param i_control: the command to issue
 * @param args: the argument list
 * @return VLC_SUCCESS in case of success, the error code otherwise
 */
static inline int vlc_sd_control( services_discovery_t *p_sd, int i_control, va_list args )
{
    if( p_sd->pf_control )
        return p_sd->pf_control( p_sd, i_control, args );
    else
        return VLC_EGENERIC;
}

static inline void vlc_sd_input_free(vlc_sd_input_t *input)
{
    for (size_t i = 0; i < input->field_count; i++)
    {
        vlc_sd_field_t *f = &input->fields[i];

        free(f->key);
        if (f->type == SD_FIELD_TEXT || f->type == SD_FIELD_SECRET
            || f->type == SD_FIELD_FILE)
            free(f->value);
    }
    free(input->fields);
    input->id = 0;
    input->fields = NULL;
    input->field_count = 0;
}

/**
 * Free \p count entries, then the array holding them.
 *
 * Entries are only released up to \p count, so a list whose construction failed
 * half-way can be released by passing the number of entries reached: an entry
 * that was zero-initialised, or whose field_count does not cover its whole
 * fields array yet, is released without touching what is not built.
 */
static inline void vlc_sd_input_ListFree(vlc_sd_input_t *list, size_t count)
{
    if (list == NULL)
        return;
    for (size_t i = 0; i < count; i++)
        vlc_sd_input_free(&list[i]);
    free(list);
}

/* Get the services discovery modules names to use in Create(), in a null
 * terminated string array. Array and string must be freed after use. */
VLC_API char ** vlc_sd_GetNames( vlc_object_t *, char ***, int **, int ** ) VLC_USED;
#define vlc_sd_GetNames(obj, pln, pcat, pcaps ) \
        vlc_sd_GetNames(VLC_OBJECT(obj), pln, pcat, pcaps)

/**
 * Creates a services discoverer.
 */
VLC_API services_discovery_t *vlc_sd_Create(vlc_object_t *parent,
    const char *chain, const struct services_discovery_owner_t *owner)
VLC_USED;
#define vlc_sd_Create( obj, a, b ) \
        vlc_sd_Create( VLC_OBJECT( obj ), a, b )

VLC_API void vlc_sd_Destroy( services_discovery_t * );

/**
 * Added top-level service callback.
 *
 * This is a convenience wrapper for services_discovery_AddSubItem().
 * It covers the most common case wherby the added item is a top-level service,
 * i.e. it has no parent node.
 */
static inline void services_discovery_AddItem(services_discovery_t *sd,
                                              input_item_t *item)
{
    sd->owner.cbs->item_added(sd, NULL, item);
}

/**
 * Added service callback.
 *
 * A services discovery module invokes this function when it "discovers" a new
 * service, i.e. a new input item.
 *
 * @note This callback does not take ownership of the input item; it might
 * however (and most probably will) add one of more references to the item.
 *
 * @note Each input item must be reported at most once: do not pass the same
 * item to this function again, even under a different parent.
 *
 * The caller is responsible for releasing its own reference(s) eventually.
 * Keeping a reference is necessary to call services_discovery_RemoveItem() or
 * to alter the item later. However, if the caller will never remove nor alter
 * the item, it can drop its reference(s) immediately.
 *
 * @param sd the service discovery instance exposing the item
 * @param parent the parent to attach the item to
 * @param item input item to add
 */
static inline void services_discovery_AddSubItem(services_discovery_t *sd,
                                                 input_item_t *parent,
                                                 input_item_t *item)
{
    sd->owner.cbs->item_added(sd, parent, item);
}

/**
 * Removed service callback.
 *
 * A services discovery module invokes this function when it senses that a
 * service is no longer available.
 */
static inline void services_discovery_RemoveItem(services_discovery_t *sd,
                                                 input_item_t *item)
{
    sd->owner.cbs->item_removed(sd, item);
}

/* SD probing */

VLC_API int vlc_sd_probe_Add(vlc_probe_t *, const char *, const char *, int category);

VLC_API int vlc_sd_probe_AddCap(vlc_probe_t *, const char *, const char *,
                                int category, int capabilities);

#define VLC_SD_PROBE_SUBMODULE \
    add_submodule() \
        set_capability( "services probe", 100 ) \
        set_callback( vlc_sd_probe_Open )

#define VLC_SD_PROBE_HELPER(name, longname, cat) \
static int vlc_sd_probe_Open (vlc_object_t *obj) \
{ \
    return vlc_sd_probe_Add ((struct vlc_probe_t *)obj, name, \
                             longname, cat); \
}

/* Same as VLC_SD_PROBE_HELPER but for configurable SDs */
#define VLC_SD_PROBE_HELPER_CAP(name, longname, cat, caps) \
static int vlc_sd_probe_Open (vlc_object_t *obj) \
{ \
    return vlc_sd_probe_AddCap ((struct vlc_probe_t *)obj, name, \
                                longname, cat, caps); \
}

/** @} */
# ifdef __cplusplus
}
# endif

#endif
