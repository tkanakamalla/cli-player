project('gst-apps', 'c')
gst_dep = dependency('gstreamer-1.0', method : 'pkg-config')
executable('cli-player', 'cli-player.c', 
    dependencies : [gst_dep],
    )
