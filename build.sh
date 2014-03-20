TOXCORE=${TOXCORE_FILES:-"-ltoxcore"}
TOXAV=${TOXAV_FILES:-"-ltoxav"}
CRYPTO_LIB=${CRYPTO_LIB_FILES:-"-lsodium"}
PREFIX=${PREFIX:-"/usr/local"}

(gcc -o toxic -D HAVE_WIDECHAR -D _SUPPORT_AUDIO ./src/*.c  -L"$PREFIX/lib/" -I"$PREFIX/include/" -lpthread -lopenal $TOXCORE $TOXAV $CRYPTO_LIB -lvpx -lncursesw -lopus && echo build with audio and widechar suceeded) || (gcc -o toxic -D _SUPPORT_AUDIO ./src/*.c  -L"$PREFIX/lib/" -I"$PREFIX/include/" -lpthread -lopenal $TOXCORE $TOXAV_FILES $CRYPTO_LIB -lvpx -lncurses -lopus && echo build with audio suceeded) || (gcc -o toxic -D HAVE_WIDECHAR ./src/{c,e,f,g,l,m,p,t,w}*.c  -L"$PREFIX/lib/" -I"$PREFIX/include/" -lpthread $TOXCORE $CRYPTO_LIB -lncursesw && echo build with widechar suceeded) || (gcc -o toxic ./src/{c,e,f,g,l,m,p,t,w}*.c  -L"$PREFIX/lib/" -I"$PREFIX/include/" -lpthread $TOXCORE $CRYPTO_LIB -lncurses && echo build suceeded) || echo build failed
