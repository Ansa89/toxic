TOP=  .
include ${TOP}/Makefile.config
  
PROG= toxic
SRCS= src/chat.c src/chat_commands.c \
	src/configdir.c src/execute.c \
	src/file_senders.c src/friendlist.c \
	src/global_commands.c src/groupchat.c \
	src/log.c src/main.c \
	src/misc_tools.c src/prompt.c \
	src/toxic_strings.c src/windows.c \
	${AV_SRCS}

#Note: Only build audio_call.c if ENABLE_AV or something like that is defined
  
CFLAGS+= ${OPT_CFLAGS} ${TOXAV_CFLAGS} ${TOXCORE_CFLAGS} ${SODIUM_CFLAGS} \
	${OPENAL_CFLAGS} ${NCURSES_CFLAGS} ${PTRHEADS_CFLAGS} '-DTOXICVER="${TOXIC_VER}"'

LIBS+= ${TOXAV_LIBS} ${TOXCORE_LIBS} ${SODIUM_LIBS} ${OPENAL_LIBS} ${NCURSES_LIBS} ${PTHREADS_LIBS}

all: config-ok ${PROG}

config-ok:
	@if [ "${CONFIGURE_OK}" != "yes" ]; then \
	    echo "Please run ./configure first"; \
	    exit 1; \
	fi
  
include ${TOP}/mk/build.prog.mk
