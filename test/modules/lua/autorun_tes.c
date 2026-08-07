#include "../../modules/lua/autorun.h"
#include "../../libvlc/test.h"

static char const * psz_test_json = "{"
"\"autorun\": true,"
"\"capabilites\": 5,"
"\"name\": \"somepath\","
"\"title\": \"Dummy\","
"\"author\": \"Bob\","
"\"version\": \"0.1\","
"\"url\": \"https://www.opensubtitles.org/\","
"\"description\": \"a description\","
"\"shortdescription\": \"a short description\","
"\"icondata\": \"(null)\","
"\"icondatasize\": 0,"
"\"timestamp\": 2147483648" // INT_MAX +1
"}";

int main(void){
    test_init();
    libvlc_instance_t *vlc = libvlc_new(0, NULL);
    loadExtensionsIntoCache(vlc, psz_test_json);
    assert(extensions_cache.extensions.i_size == 1);

    struct extension_t* pext = extensions_cache.extensions.p_elems[0];
    assert(pext);
    assert(strcmp(pext->psz_name, "somepath") == 0);
    assert(strcmp(pext->psz_title, "Dummy") == 0);
    assert(strcmp(pext->psz_author, "Bob") == 0);
    assert(strcmp(pext->psz_version, "0.1") == 0);
    assert(strcmp(pext->psz_url, "https://www.opensubtitles.org/") == 0);
    assert(strcmp(pext->psz_description, "a description") == 0);
    assert(strcmp(pext->psz_shortdescription, "a short description") == 0);

    struct lua_extension* plua_ext = extensions_cache.extensions.p_elems[0]->p_sys;
    assert(plua_ext->b_autorun);
    assert(plua_ext->last_saved == 2147483648);

    return 0;
};