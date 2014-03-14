# Toxic

Toxic is an ncurses based instant messaging client for [Tox](http://tox.im) which formerly resided in the [Tox core repository](https://github.com/irungentoo/ProjectTox-Core) and is now available as a standalone program. It looks like [this](http://i.imgur.com/hL7WhVl.png).
## Installation

* Install the build requirements, these are
```ncurses, toxcore, toxav, and openal```

* Execute the configure script with ```./configure``` (you may need to pass it the location of your dependency libraries, i.e.):
```./configure --prefix=/where/to/install --with-libtoxcore=/usr --enable-debug --disable-av```

* Compile and install the program with ```make depend all && sudo make install```

#### Notes
If your default prefix is /usr/local and you get the error: "error while loading shared libraries: libtoxcore.so.0: cannot open shared object file: No such file or directory", then you can try running ```sudo ldconfig```. If that doesn't fix it, run:
```
echo '/usr/local/lib/' | sudo tee -a /etc/ld.so.conf.d/locallib.conf
sudo ldconfig
