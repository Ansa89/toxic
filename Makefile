TOP=  .
include ${TOP}/Makefile.config
  
PROG= toxic
SRCS= src/*
  
CFLAGS+= ${TOXAV_CFLAGS} ${TOXCORE_CFLAGS} ${SODIUM_CFLAGS} ${OPENAL_CFLAGS} ${NCURSES_CFLAGS}
  
include ${TOP}/mk/build.prog.mk
