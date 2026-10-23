// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * whisper_translation.h: 
 *****************************************************************************
 * Copyright © 2026 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_WHISPER_TRANSLATION_H
#define VLC_WHISPER_TRANSLATION_H

#include <stddef.h>
#include <stdint.h>

#include <vlc_frame.h>

#include "whisper_internal.h"

struct madlad_400_language {
    const char *name;
    const char *iso_639;
};

#define MADLAD_400_LANGUAGES(X) \
    X("No translation",             "") \
    X("Achinese",                   "ace") \
    X("Acoli",                      "ach") \
    X("Adangme",                    "ada") \
    X("Adhola",                     "adh") \
    X("Adyghe",                     "ady") \
    X("Afar",                       "aa") \
    X("Afrikaans",                  "af") \
    X("Aguaruna",                   "agr") \
    X("Agusan Manobo",              "msm") \
    X("Akha",                       "ahk") \
    X("Albanian",                   "sq") \
    X("Alur",                       "alz") \
    X("Ambulas",                    "abt") \
    X("Amharic",                    "am") \
    X("Ancient Greek",              "grc") \
    X("Arabic",                     "ar") \
    X("Armenian",                   "hy") \
    X("Arpitan",                    "frp") \
    X("Asháninka",                  "cni") \
    X("Assamese",                   "as") \
    X("Avar",                       "av") \
    X("Awa-Cuaiquer",               "kwi") \
    X("Awadhi",                     "awa") \
    X("Ayacucho Quechua",           "quy") \
    X("Aymara",                     "ay") \
    X("Azerbaijani",                "az") \
    X("Balinese",                   "ban") \
    X("Bambara",                    "bm") \
    X("Banggai",                    "bgz") \
    X("Baoulé",                     "bci") \
    X("Basa (Cameroon)",            "bas") \
    X("Bashkir",                    "ba") \
    X("Basque",                     "eu") \
    X("Batak Angkola",              "akb") \
    X("Batak Karo",                 "btx") \
    X("Batak Simalungun",           "bts") \
    X("Batak Toba",                 "bbc") \
    X("Belarusian",                 "be") \
    X("Belize Kriol English",       "bzj") \
    X("Bengali",                    "bn") \
    X("Betawi",                     "bew") \
    X("Bhojpuri",                   "bho") \
    X("Bimoba",                     "bim") \
    X("Bislama",                    "bi") \
    X("Bodo (India)",               "brx") \
    X("Boko (Benin)",               "bqc") \
    X("Bokobaru",                   "bus") \
    X("Bosnian",                    "bs") \
    X("Breton",                     "br") \
    X("Bukiyip",                    "ape") \
    X("Bulgarian",                  "bg") \
    X("Bulu",                       "bum") \
    X("Buryat",                     "bua") \
    X("Cajamarca Quechua",          "qvc") \
    X("Cantonese",                  "yue") \
    X("Caribbean Javanese",         "jvn") \
    X("Carpathian Romani",          "rmc") \
    X("Catalan",                    "ca") \
    X("Cañar H. Quichua",           "qxr") \
    X("Cebuano",                    "ceb") \
    X("Central Bikol",              "bik") \
    X("Central Mazahua",            "maz") \
    X("Central Melanau",            "mel") \
    X("Chamorro",                   "ch") \
    X("Chavacano",                  "cbk") \
    X("Chechen",                    "ce") \
    X("Cherokee",                   "chr") \
    X("Chhattisgarhi",              "hne") \
    X("Chichewa",                   "ny") \
    X("Chokwe",                     "cjk") \
    X("Chol",                       "ctu") \
    X("Chopi",                      "cce") \
    X("Chuj",                       "cac") \
    X("Chuukese",                   "chk") \
    X("Chuvash",                    "cv") \
    X("Cornish",                    "kw") \
    X("Corsican",                   "co") \
    X("Crimean Tatar",              "crh") \
    X("Croatian",                   "hr") \
    X("Czech",                      "cs") \
    X("Dadibi",                     "mps") \
    X("Danish",                     "da") \
    X("Dawro",                      "dwr") \
    X("Dhivehi",                    "dv") \
    X("Dinka",                      "din") \
    X("Ditammari",                  "tbz") \
    X("Dombe",                      "dov") \
    X("Dutch",                      "nl") \
    X("Dyula",                      "dyu") \
    X("Dzongkha",                   "dz") \
    X("E. Baluchi",                 "bgp") \
    X("E. Bolivian Guaraní",        "gui") \
    X("E. Bru",                     "bru") \
    X("E. Huasteca Nahuatl",        "nhe") \
    X("E. Maroon Creole",           "djk") \
    X("E. Tamang",                  "taj") \
    X("Enga",                       "enq") \
    X("English",                    "en") \
    X("Epena",                      "sja") \
    X("Erzya",                      "myv") \
    X("Esperanto",                  "eo") \
    X("Estonian",                   "et") \
    X("Ewe",                        "ee") \
    X("Falam Chin",                 "cfm") \
    X("Faroese",                    "fo") \
    X("Fiji Hindi",                 "hif") \
    X("Fijian",                     "fj") \
    X("Filipino",                   "fil") \
    X("Finnish",                    "fi") \
    X("Fipa",                       "fip") \
    X("Fon",                        "fon") \
    X("French",                     "fr") \
    X("Fulfulde",                   "ff") \
    X("Gagauz",                     "gag") \
    X("Galician",                   "gl") \
    X("Garhwali",                   "gbm") \
    X("Garifuna",                   "cab") \
    X("Georgian",                   "ka") \
    X("German",                     "de") \
    X("Gilbertese",                 "gil") \
    X("Goan Konkani",               "gom") \
    X("Gofa",                       "gof") \
    X("Gorontalo",                  "gor") \
    X("Greek",                      "el") \
    X("Guahibo",                    "guh") \
    X("Guajajára",                  "gub") \
    X("Guarani",                    "gn") \
    X("Guerrero Amuzgo",            "amu") \
    X("Guerrero Nahuatl",           "ngu") \
    X("Gujarati",                   "gu") \
    X("Gulay",                      "gvl") \
    X("Guyanese Creole English",    "gyn") \
    X("Haitian Creole",             "ht") \
    X("Hakha Chin",                 "cnh") \
    X("Hassaniyya",                 "mey") \
    X("Hausa",                      "ha") \
    X("Hawaiian",                   "haw") \
    X("Hebrew",                     "he") \
    X("Hiligaynon",                 "hil") \
    X("Hill Mari",                  "mrj") \
    X("Hindi",                      "hi") \
    X("Hiri Motu",                  "ho") \
    X("Hmong",                      "hmn") \
    X("Huallaga Huánuco Quechua",   "qub") \
    X("Huastec",                    "hus") \
    X("Huli",                       "hui") \
    X("Hungarian",                  "hu") \
    X("Iban",                       "iba") \
    X("Ibibio",                     "ibb") \
    X("Icelandic",                  "is") \
    X("Igbo",                       "ig") \
    X("Ilocano",                    "ilo") \
    X("Imbabura H. Quichua",        "qvi") \
    X("Indonesian",                 "id") \
    X("Inga",                       "inb") \
    X("Inuktitut",                  "iu") \
    X("Irish",                      "ga") \
    X("Isoko",                      "iso") \
    X("Italian",                    "it") \
    X("Iu Mien",                    "ium") \
    X("Izii",                       "izz") \
    X("Jamaican Creole English",    "jam") \
    X("Jambi Malay",                "jax") \
    X("Japanese",                   "ja") \
    X("Javanese",                   "jv") \
    X("Kabardian",                  "kbd") \
    X("Kabiyè",                     "kbp") \
    X("Kachin",                     "kac") \
    X("Kadazan Dusun",              "dtp") \
    X("Kalaallisut",                "kl") \
    X("Kalmyk",                     "xal") \
    X("Kanauji",                    "bjj") \
    X("Kannada",                    "kn") \
    X("Kaqchikel",                  "cak") \
    X("Kara-Kalpak (Latn)",         "kaa-Latn") \
    X("Kara-Kalpak",                "kaa") \
    X("Karachay-Balkar",            "krc") \
    X("Kashmiri",                   "ks") \
    X("Kazakh",                     "kk") \
    X("Kedah Malay",                "meo") \
    X("Kekchí",                     "kek") \
    X("Keley-I Kallahan",           "ify") \
    X("Khakas",                     "kjh") \
    X("Khasi",                      "kha") \
    X("Khmer",                      "km") \
    X("Khmu",                       "kjg") \
    X("Kimbundu",                   "kmb") \
    X("Kinaray-A",                  "krj") \
    X("Kinyarwanda",                "rw") \
    X("Kituba (DRC)",               "ktu") \
    X("Klingon",                    "tlh") \
    X("Kok Borok",                  "trp") \
    X("Komi",                       "kv") \
    X("Komi-Permyak",               "koi") \
    X("Kongo",                      "kg") \
    X("Korean",                     "ko") \
    X("Kosraean",                   "kos") \
    X("Krio",                       "kri") \
    X("Kuanua",                     "ksd") \
    X("Kuanyama",                   "kj") \
    X("Kumyk",                      "kum") \
    X("Kupang Malay",               "mkn") \
    X("Kurdish (Kurmanji)",         "ku") \
    X("Kurdish (Sorani)",           "ckb") \
    X("Kyrghyz",                    "ky") \
    X("K’iche’",                    "quc") \
    X("Lahu",                       "lhu") \
    X("Lambayeque Quechua",         "quf") \
    X("Lango (Uganda)",             "laj") \
    X("Lao",                        "lo") \
    X("Latgalian",                  "ltg") \
    X("Latin",                      "la") \
    X("Latvian",                    "lv") \
    X("Lingala",                    "ln") \
    X("Lithuanian",                 "lt") \
    X("Luba-Katanga",               "lu") \
    X("Luganda",                    "lg") \
    X("Luxembourgish",              "lb") \
    X("Maasina Fulfulde",           "ffm") \
    X("Macedonian",                 "mk") \
    X("Madurese",                   "mad") \
    X("Magahi",                     "mag") \
    X("Maithili",                   "mai") \
    X("Makasar",                    "mak") \
    X("Makhuwa-Meetto",             "mgh") \
    X("Malagasy",                   "mg") \
    X("Malay",                      "ms") \
    X("Malayalam",                  "ml") \
    X("Maltese",                    "mt") \
    X("Mam",                        "mam") \
    X("Manado Malay",               "xmm") \
    X("Mandarin Chinese",           "zh") \
    X("Manggarai",                  "mqy") \
    X("Manx",                       "gv") \
    X("Maori",                      "mi") \
    X("Mapudungun",                 "arn") \
    X("Maranao",                    "mrw") \
    X("Marathi",                    "mr") \
    X("Marshallese",                "mh") \
    X("Masai",                      "mas") \
    X("Masbatenyo",                 "msb") \
    X("Matigsalug Manobo",          "mbt") \
    X("Meadow Mari",                "chm") \
    X("Meiteilon (Manipuri)",       "mni") \
    X("Mewari",                     "mtr") \
    X("Minangkabau",                "min") \
    X("Mizo",                       "lus") \
    X("Moksha",                     "mdf") \
    X("Mon",                        "mnw") \
    X("Mongolian",                  "mn") \
    X("Morisien",                   "mfe") \
    X("Motu",                       "meu") \
    X("Mutu",                       "tuc") \
    X("Myanmar (Burmese)",          "my") \
    X("Mískito",                    "miq") \
    X("N. Emberá",                  "emp") \
    X("N. Luri",                    "lrc") \
    X("N. Pastaza Quichua",         "qvz") \
    X("N. Sami",                    "se") \
    X("Nande",                      "nnb") \
    X("Nandi",                      "niq") \
    X("Navajo",                     "nv") \
    X("Nepali",                     "ne") \
    X("Newari",                     "new") \
    X("Ngaju",                      "nij") \
    X("Ngäbere",                    "gym") \
    X("Nias",                       "nia") \
    X("Nimadi",                     "noe") \
    X("Nogai",                      "nog") \
    X("Noise",                      "zxx") \
    X("Norwegian",                  "no") \
    X("Nung (Viet Nam)",            "nut") \
    X("Nyungwe",                    "nyu") \
    X("Nzima",                      "nzi") \
    X("Obolo",                      "ann") \
    X("Occitan",                    "oc") \
    X("Odia (Oriya)",               "or") \
    X("Ojibwa",                     "oj") \
    X("Old English",                "ang") \
    X("Oromo",                      "om") \
    X("Ossetian",                   "os") \
    X("Paite Chin",                 "pck") \
    X("Palauan",                    "pau") \
    X("Pampanga",                   "pam") \
    X("Pangasinan",                 "pag") \
    X("Papiamento",                 "pap") \
    X("Parauk",                     "prk") \
    X("Pashto",                     "ps") \
    X("Persian",                    "fa") \
    X("Pijin",                      "pis") \
    X("Pohnpeian",                  "pon") \
    X("Polish",                     "pl") \
    X("Popti’",                     "jac") \
    X("Portuguese",                 "pt") \
    X("Punjabi",                    "pa") \
    X("Quechua",                    "qu") \
    X("Querétaro Otomi",            "otq") \
    X("Rajasthani",                 "raj") \
    X("Rakhine",                    "rki") \
    X("Rawa",                       "rwo") \
    X("Romani",                     "rom") \
    X("Romanian",                   "ro") \
    X("Romansh",                    "rm") \
    X("Rundi",                      "rn") \
    X("Russian",                    "ru") \
    X("Réunion Creole French",      "rcf") \
    X("S. Altai",                   "alt") \
    X("S. Bolivian Quechua",        "quh") \
    X("S. Luri",                    "luz") \
    X("S. Pastaza Quechua",         "qup") \
    X("Sabah Malay",                "msi") \
    X("Sabu",                       "hvn") \
    X("Samoan",                     "sm") \
    X("San Blas Kuna",              "cuk") \
    X("Sangir",                     "sxn") \
    X("Sango",                      "sg") \
    X("Sanskrit",                   "sa") \
    X("Saraiki",                    "skr") \
    X("Saramaccan",                 "srm") \
    X("Saterfriesisch",             "stq") \
    X("Scottish Gaelic",            "gd") \
    X("Sena",                       "seh") \
    X("Sepedi",                     "nso") \
    X("Serbian",                    "sr") \
    X("Serer",                      "srr") \
    X("Seselwa Creole French",      "crs") \
    X("Sesotho",                    "st") \
    X("Shan",                       "shn") \
    X("Shipibo-Conibo",             "shp") \
    X("Shona",                      "sn") \
    X("Shuar",                      "jiv") \
    X("Simte",                      "smt") \
    X("Sindhi",                     "sd") \
    X("Sinhala",                    "si") \
    X("Slovak",                     "sk") \
    X("Slovenian",                  "sl") \
    X("Somali",                     "so") \
    X("South Ndebele",              "nr") \
    X("Spanish",                    "es") \
    X("Sranan Tongo",               "srn") \
    X("St Lucian Creole French",    "acf") \
    X("Sundanese",                  "su") \
    X("Sunwar",                     "suz") \
    X("Supyire Senoufo",            "spp") \
    X("Surjapuri",                  "sjp") \
    X("Susu",                       "sus") \
    X("Swahili",                    "sw") \
    X("Swati",                      "ss") \
    X("Swedish",                    "sv") \
    X("Swiss German",               "gsw") \
    X("Sylheti",                    "syl") \
    X("Syriac",                     "syr") \
    X("S’gaw Karen",                "ksw") \
    X("Tabassaran",                 "tab") \
    X("Tajik",                      "tg") \
    X("Takestani",                  "tks") \
    X("Tamazight (Tfng)",           "ber") \
    X("Tamil",                      "ta") \
    X("Tandroy-Mahafaly Malagasy",  "tdx") \
    X("Tatar",                      "tt") \
    X("Tausug",                     "tsg") \
    X("Telugu",                     "te") \
    X("Termanu",                    "twu") \
    X("Teso",                       "teo") \
    X("Tetela",                     "tll") \
    X("Tetum",                      "tet") \
    X("Thai",                       "th") \
    X("Tibetan",                    "bo") \
    X("Ticuna",                     "tca") \
    X("Tigrinya",                   "ti") \
    X("Tiv",                        "tiv") \
    X("Tojolabal",                  "toj") \
    X("Tonga (Tonga Islands)",      "to") \
    X("Toraja-Sa’dan",              "sda") \
    X("Tsonga",                     "ts") \
    X("Tswa",                       "tsc") \
    X("Tswana",                     "tn") \
    X("Tulu",                       "tcy") \
    X("Turkish",                    "tr") \
    X("Turkmen",                    "tk") \
    X("Tuvalu",                     "tvl") \
    X("Tuvinian",                   "tyv") \
    X("Twi",                        "ak") \
    X("Tzeltal",                    "tzh") \
    X("Tzotzil",                    "tzo") \
    X("Tz’utujil",                  "tzj") \
    X("Tày",                        "tyz") \
    X("Udmurt",                     "udm") \
    X("Ukrainian",                  "uk") \
    X("Uma",                        "ppk") \
    X("Umbu-Ungu",                  "ubu") \
    X("Urdu",                       "ur") \
    X("Uyghur",                     "ug") \
    X("Uzbek",                      "uz") \
    X("Venda",                      "ve") \
    X("Venetian",                   "vec") \
    X("Vietnamese",                 "vi") \
    X("W. Frisian",                 "fy") \
    X("W. Kanjobal",                "knj") \
    X("Walloon",                    "wa") \
    X("Waray (Philippines)",        "war") \
    X("Wayuu",                      "guc") \
    X("Welsh",                      "cy") \
    X("Wolaytta",                   "wal") \
    X("Wolof",                      "wo") \
    X("Woun Meu",                   "noa") \
    X("Xhosa",                      "xh") \
    X("Yakut",                      "sah") \
    X("Yapese",                     "yap") \
    X("Yiddish",                    "yi") \
    X("Yoruba",                     "yo") \
    X("Yucateco",                   "yua") \
    X("Zande",                      "zne") \
    X("Zapotec",                    "zap") \
    X("Zarma",                      "dje") \
    X("Zaza",                       "zza") \
    X("Zhuang",                     "za") \
    X("Zulu",                       "zu")

#define WHISPER_LANGUAGES(X) \
    X("Auto-detect",                "auto") \
    X("English",                    "en") \
    X("Chinese",                    "zh") \
    X("German",                     "de") \
    X("Spanish",                    "es") \
    X("Russian",                    "ru") \
    X("Korean",                     "ko") \
    X("French",                     "fr") \
    X("Japanese",                   "ja") \
    X("Portuguese",                 "pt") \
    X("Turkish",                    "tr") \
    X("Polish",                     "pl") \
    X("Catalan",                    "ca") \
    X("Dutch",                      "nl") \
    X("Arabic",                     "ar") \
    X("Swedish",                    "sv") \
    X("Italian",                    "it") \
    X("Indonesian",                 "id") \
    X("Hindi",                      "hi") \
    X("Finnish",                    "fi") \
    X("Vietnamese",                 "vi") \
    X("Hebrew",                     "he") \
    X("Ukrainian",                  "uk") \
    X("Greek",                      "el") \
    X("Malay",                      "ms") \
    X("Czech",                      "cs") \
    X("Romanian",                   "ro") \
    X("Danish",                     "da") \
    X("Hungarian",                  "hu") \
    X("Tamil",                      "ta") \
    X("Norwegian",                  "no") \
    X("Thai",                       "th") \
    X("Urdu",                       "ur") \
    X("Croatian",                   "hr") \
    X("Bulgarian",                  "bg") \
    X("Lithuanian",                 "lt") \
    X("Latin",                      "la") \
    X("Maori",                      "mi") \
    X("Malayalam",                  "ml") \
    X("Welsh",                      "cy") \
    X("Slovak",                     "sk") \
    X("Telugu",                     "te") \
    X("Persian",                    "fa") \
    X("Latvian",                    "lv") \
    X("Bengali",                    "bn") \
    X("Serbian",                    "sr") \
    X("Azerbaijani",                "az") \
    X("Slovenian",                  "sl") \
    X("Kannada",                    "kn") \
    X("Estonian",                   "et") \
    X("Macedonian",                 "mk") \
    X("Breton",                     "br") \
    X("Basque",                     "eu") \
    X("Icelandic",                  "is") \
    X("Armenian",                   "hy") \
    X("Nepali",                     "ne") \
    X("Mongolian",                  "mn") \
    X("Bosnian",                    "bs") \
    X("Kazakh",                     "kk") \
    X("Albanian",                   "sq") \
    X("Swahili",                    "sw") \
    X("Galician",                   "gl") \
    X("Marathi",                    "mr") \
    X("Punjabi",                    "pa") \
    X("Sinhala",                    "si") \
    X("Khmer",                      "km") \
    X("Shona",                      "sn") \
    X("Yoruba",                     "yo") \
    X("Somali",                     "so") \
    X("Afrikaans",                  "af") \
    X("Occitan",                    "oc") \
    X("Georgian",                   "ka") \
    X("Belarusian",                 "be") \
    X("Tajik",                      "tg") \
    X("Sindhi",                     "sd") \
    X("Gujarati",                   "gu") \
    X("Amharic",                    "am") \
    X("Yiddish",                    "yi") \
    X("Lao",                        "lo") \
    X("Uzbek",                      "uz") \
    X("Faroese",                    "fo") \
    X("Haitian Creole",             "ht") \
    X("Pashto",                     "ps") \
    X("Turkmen",                    "tk") \
    X("Nynorsk",                    "nn") \
    X("Maltese",                    "mt") \
    X("Sanskrit",                   "sa") \
    X("Luxembourgish",              "lb") \
    X("Myanmar",                    "my") \
    X("Tibetan",                    "bo") \
    X("Tagalog",                    "tl") \
    X("Malagasy",                   "mg") \
    X("Assamese",                   "as") \
    X("Tatar",                      "tt") \
    X("Hawaiian",                   "haw") \
    X("Lingala",                    "ln") \
    X("Hausa",                      "ha") \
    X("Bashkir",                    "ba") \
    X("Javanese",                   "jw") \
    X("Sundanese",                  "su") \
    X("Cantonese",                  "yue")

#define MADLAD_LANG_STRUCT(name, iso) {name, iso},
static const struct madlad_400_language madlad_400_languages[] = {
    MADLAD_400_LANGUAGES(MADLAD_LANG_STRUCT)
};
#undef MADLAD_LANG_STRUCT

static const int madlad_400_languages_count =
            sizeof(madlad_400_languages) / sizeof(madlad_400_languages[0]);

#endif /* vlc_whisper_translation_h */
