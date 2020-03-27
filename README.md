idiff
=====

Barebones image differs.

Build steps, C version:

    $ ninja

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
