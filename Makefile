  TOP=  .
  include ${TOP}/Makefile.config
  
  PROG= toxic
  SRCS= src \
  
  CFLAGS+= ${CFLAGS}
  
  include ${TOP}/mk/build.prog.mk
