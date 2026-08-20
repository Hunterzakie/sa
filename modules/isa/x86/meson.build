vlc_modules += {
     'name' : 'deinterlace_x86',
     'sources' : files('deinterlace.c'),
     'enabled' : have_sse2,
}
vlc_modules += {
     'name' : 'yadif_x86',
     'sources' : files('yadif.c','yadif_x86.asm'),
     'include_directories' : include_directories('../../../extras/include/x86'),
     'enabled' : host_machine.cpu_family() in ['x86', 'x86_64'] and cdata.has('HAVE_X86ASM'),
 }
