#include "sad.h"
#include <stdlib.h>

static int sad_char(const char* l, const char* r, int n) {
    int sum = 0;
    while (n --> 0) {
        sum += abs((int)*l++ - (int)*r++);
    }
    return sum;
}

int sad(const void* l, const void* r, int n) {
    return sad_char(l,r,n);
}
