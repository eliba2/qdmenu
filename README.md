
# qdmenu
dmenu using qt.

## Description
[dmenu](https://tools.suckless.org/dmenu/) implementation using qt instead of X.


## Building

This project uses cmake. Follow these instructions:

```
mkdir build && cd build

cmake -DCMAKE_PREFIX_PATH="~/Qt/6.5.3/macos/lib/cmake" -DCMAKE_BUILD_TYPE=Release ..
```

Note to set your Qt path.

You can verify your installation with 

```
echo -e "first\nsecond\nthird" | ./qdmenu
```


## How to use
Man page availbe [here](https://man.archlinux.org/man/extra/dmenu/dmenu.1.en). Use --help for all available command line options.


### Not supported ###

There's no simple way getting other windows information under Mac so the *windowid* options is not available. It might be added under linux.

## License

Similar to dmenu.
