idiff
=====

Barebones Go image differ.

Installation:

    $ go install github.com/mtklein/idiff

Usage:

    $ idiff <left> <right> [diff.html]

Suggested workflow:

    $ <generate known good images in good/>
    $ while working ...
        $ <generate new images in bad/>
        $ idiff good bad && {start,open,xdg-open} diff.html
