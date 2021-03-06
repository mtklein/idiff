idiff
=====

Barebones image differs.

Build steps, C version:

    $ cc -g -O2 -march=native -pthread idiff.c -o idiff

idiff will use `libspng` if available (link `-lspng`),
falling back to `libpng` if available (link `-lpng`),
or `stb_image` from the `ext/stb` submodule as last resort.

Build steps, Go version:

    $ rm idiff.c
    $ go build

Usage:

    $ idiff <left> <right> [diff.html]

Suggested workflow:

    $ <generate known good images in good/>
    $ while working ...
        $ <generate new images in bad/>
        $ idiff good bad && {start,open,xdg-open} diff.html
