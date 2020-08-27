#include <assert.h>
#include <ftw.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static struct Side {
    const char* root;
    char*       path;
    size_t  enc_size;
    int           fd;
    int       UNUSED;
    void*        enc;
    size_t  dec_size;
    uint16_t*    dec;
} A,B;


static void cleanup_except_path(struct Side* s) {
    if (s->dec   ) { free  (s->dec);              s->dec = NULL; }
    if (s->enc   ) { munmap(s->enc, s->enc_size); s->enc = NULL; }
    if (s->fd > 0) { close (s->fd);               s->fd  =   -1; }
}

static void cleanup(struct Side* s) {
    cleanup_except_path(s);
    if (s->path) { free(s->path); s->path = NULL; }
}

#if !defined(__has_include)
    #define  __has_include(x) 0
#endif

#if 1 && __has_include(<spng.h>)
    #include <spng.h>
    static void decode(struct Side* s) {
        spng_ctx* ctx = spng_ctx_new(0);
        spng_set_png_buffer(ctx, s->enc, s->enc_size);
        spng_decoded_image_size(ctx, SPNG_FMT_RGBA16, &s->dec_size);
        s->dec = malloc(s->dec_size);
        spng_decode_image(ctx, s->dec, s->dec_size, SPNG_FMT_RGBA16, 0);
        spng_ctx_free(ctx);
    }
#elif 1 && __has_include(<png.h>)
    #include <png.h>
    static void decode(struct Side* s) {
        png_image img = {
            .version = PNG_IMAGE_VERSION,
            .format  = PNG_FORMAT_LINEAR_RGB_ALPHA,
        };
        png_image_begin_read_from_memory(&img, s->enc, s->enc_size);

        s->dec_size = PNG_IMAGE_SIZE(img);
        s->dec      = malloc(s->dec_size);

        png_image_finish_read(&img, /*background=*/NULL,
                              s->dec, (int)PNG_IMAGE_ROW_STRIDE(img), /*colormap=*/NULL);
        png_image_free(&img);
    }
#else
    #define STB_IMAGE_IMPLEMENTATION
    #define STBI_ONLY_PNG
    #include "ext/stb/stb_image.h"
    static void decode(struct Side* s) {
        int w,h,ch;
        s->dec = stbi_load_16_from_memory(s->enc,s->enc_size, &w,&h, &ch,4);
        s->dec_size = sizeof(uint16_t)*w*h;
    }
#endif

struct Diff {
    struct Side a,b;
    double diff;
};

static void* decode_and_diff(void* arg) {
    struct Diff* d = arg;
    struct Side *a = &d->a,
                *b = &d->b;

    decode(a);
    decode(b);

    if (a->dec_size == b->dec_size) {
        const size_t nchannels = a->dec_size / sizeof(uint16_t);
        size_t diff = 0;
        for (size_t i = 0; i < nchannels; i++) {
            diff += (size_t)abs(a->dec[i] - b->dec[i]);
        }
        d->diff = diff / (1.0 * nchannels * (uint16_t)~0);
    } else {
        d->diff = 1.0;
    }

    cleanup_except_path(a);
    cleanup_except_path(b);
    return NULL;
}

static pthread_t thread[1<<20], *next_thread = thread;
static struct Diff diff[1<<20], *next_diff   = diff;

static int total = 0,
           pairs = 0;

static int walk(const char* path, const struct stat* st, int flag) {
    if (flag == FTW_F && 0 == strcmp(".png", strrchr(path, '.'))) {
        struct Side a = A,
                    b = B;
        do {
            ++total;

            size_t len = strlen(path) - strlen(b.root) + strlen(a.root) + 2/*path divider and NUL*/;
            a.path = malloc(len);
            snprintf(a.path,len, "%s/%s", a.root, path + strlen(b.root));
            b.path = strdup(path);

            struct stat ast;
            if (0 != stat(a.path, &ast)) {
                fprintf(stderr, "No pair for %s at %s.\n", b.path, a.path);
                break;
            }
            a.enc_size = (size_t)ast.st_size;
            b.enc_size = (size_t)st->st_size;

            ++pairs;

            a.fd = open(a.path, O_RDONLY);
            b.fd = open(b.path, O_RDONLY);
            assert(a.fd > 0 && b.fd > 0);

            a.enc = mmap(NULL,a.enc_size, PROT_READ,MAP_PRIVATE, a.fd,0);
            b.enc = mmap(NULL,b.enc_size, PROT_READ,MAP_PRIVATE, b.fd,0);
            assert(a.enc != MAP_FAILED && b.enc != MAP_FAILED);

            if ((1) && a.enc_size == b.enc_size && 0 == memcmp(a.enc, b.enc, a.enc_size)) {
                break;
            }

            // Responsibility for cleanup(&a) and cleanup(&b) passes to decode_and_diff()
            // which calls cleanup_except_path(), and ultimately back to main()
            // where we finally cleanup() the paths.
            *next_diff = (struct Diff){a,b, 0.0};
            if (0 == pthread_create(next_thread, NULL, decode_and_diff, next_diff)) {
                next_thread++;
            } else {
                decode_and_diff(next_diff);
            }
            next_diff++;
            return 0;

        } while(0);

        cleanup(&a);
        cleanup(&b);
    }
    return 0;
}

static int sort_by_descending_diff(const void* x, const void* y) {
    const struct Diff *X = x,
                      *Y = y;

    return X->diff < Y->diff ? +1
         : X->diff > Y->diff ? -1
         : 0;
}

int main(int argc, char** argv) {
    B.root    =       argc > 1 ? argv[1] : "before";
    A.root    =       argc > 2 ? argv[2] :  "after";
    FILE* out = fopen(argc > 3 ? argv[3] : "diff.html", "w");

    const int max_open_fds = 1024;
    ftw(B.root, walk, max_open_fds);

    for (pthread_t* th = thread; th != next_thread; th++) {
        pthread_join(*th, NULL);
    }

    const size_t diffs = (size_t)(next_diff - diff);
    qsort(diff, diffs, sizeof(struct Diff), sort_by_descending_diff);

    const char* style =
        "body { background-size: 16px 16px;                                                   "
        "       background-color: rgb(230,230,230);                                           "
        "       background-image:                                                             "
        "   linear-gradient(45deg, rgba(255,255,255,.2) 25%, transparent 25%, transparent 50%,"
        "   rgba(255,255,255,.2) 50%, rgba(255,255,255,.2) 75%, transparent 75%, transparent) "
        "}                                                                                    "
        "div { position: relative; left: 0; top: 0 }                                          "
        "table { table-layout:fixed; width:100% }                                             "
        "img {max-width:100%; max-height:320; left: 0; top: 0 }                               ";
    fprintf(out, "<style>%s</style><table>\n", style);

    for (size_t i = 0; i < diffs; i++) {
        fprintf(out,
            "<tr><td><div style=\"filter: grayscale(1) brightness(256)\">                    "
            "            <img src=%s>                                                        "
            "            <img src=%s style=\"position:absolute; mix-blend-mode:difference\"> "
            "        </div>                                                                  "
            "    <td><div>                                                                   "
            "            <img src=%s>                                                        "
            "            <img src=%s style=\"position:absolute; mix-blend-mode:difference\"> "
            "        </div>                                                                  "
            "    <td><a href=%s><img src=%s></a>                                             "
            "    <td><a href=%s><img src=%s></a>\n                                           ",
            diff[i].b.path, diff[i].a.path,
            diff[i].b.path, diff[i].a.path,
            diff[i].b.path, diff[i].b.path,
            diff[i].a.path, diff[i].a.path);

        cleanup(&diff[i].a);
        cleanup(&diff[i].b);
    }

    printf("%d .pngs in %s\n%d pairs in %s\n%zu diffs\n", total,B.root, pairs,A.root, diffs);
    return diffs ? 0 : 1;
}
