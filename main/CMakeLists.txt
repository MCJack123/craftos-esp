idf_component_register(SRCS
    "craftos2-lua/src/lapi.c" "craftos2-lua/src/lauxlib.c" "craftos2-lua/src/lbaselib.c" "craftos2-lua/src/lbitlib.c" "craftos2-lua/src/lcode.c" "craftos2-lua/src/lcorolib.c"
    "craftos2-lua/src/lctype.c" "craftos2-lua/src/ldblib.c" "craftos2-lua/src/ldebug.c" "craftos2-lua/src/ldebug.c" "craftos2-lua/src/ldo.c" "craftos2-lua/src/ldump.c"
    "craftos2-lua/src/lfunc.c" "craftos2-lua/src/lgc.c" "craftos2-lua/src/linit.c" "craftos2-lua/src/liolib.c" "craftos2-lua/src/llex.c" "craftos2-lua/src/lmathlib.c"
    "craftos2-lua/src/lmem.c" "craftos2-lua/src/loadlib.c" "craftos2-lua/src/lobject.c" "craftos2-lua/src/lopcodes.c" "craftos2-lua/src/loslib.c" "craftos2-lua/src/lparser.c"
    "craftos2-lua/src/lstate.c" "craftos2-lua/src/lstring.c" "craftos2-lua/src/lstrlib.c" "craftos2-lua/src/ltable.c" "craftos2-lua/src/ltablib.c" "craftos2-lua/src/ltm.c"
    "craftos2-lua/src/lundump.c" "craftos2-lua/src/lutf8lib.c" "craftos2-lua/src/lvm.c" "craftos2-lua/src/lzio.c" "llock.c"
    "api/fs_handle.c" "api/fs.c" "api/http.c" "api/os.c" "api/peripheral.c" "api/redstone.c" "api/term.c" "api/wifi.c"
    "driver/audio.c" "driver/bootldr.c" "driver/hid.c" "driver/storage.c" "driver/vga.c" "driver/wifi.c"
    "module/redstone.c" "module/terminal.c"
    "peripheral/drive.c" "peripheral/modem.c" "peripheral/speaker.c"
    "common.c" "event.c" "machine.c" "main.c"
    INCLUDE_DIRS "." "craftos2-lua/include")