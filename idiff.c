#include <assert.h>
#include <ftw.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <png.h>

static struct Side {
    const char* root;
    char*       path;
    size_t  enc_size;
    int           fd;
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

struct Diff {
    struct Side a,b;
    double diff;
};

static void* diff(void* arg) {
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

static pthread_t threads[1<<20], *next_thread = threads;
static struct Diff diffs[1<<20], *next_diff   = diffs;

static void start_diff(struct Side a, struct Side b) {
    *next_diff = (struct Diff){a,b, 0.0};
    if (0 == pthread_create(next_thread, NULL, diff, next_diff)) {
        next_thread++;
    } else {
        diff(next_diff);
    }
    next_diff++;
}

static int total = 0,
           pairs = 0;

static int walk(const char* path, const struct stat* st, int flag) {
    if (flag == FTW_F && 0 == strcmp(".png", strrchr(path, '.'))) {
        struct Side a = A,
                    b = B;
        do {
            ++total;

            asprintf(&a.path, "%s/%s", a.root, path + strlen(b.root));
            assert(a.path);
            b.path = strdup(path);

            struct stat ast;
            if (0 != stat(a.path, &ast)) {
                fprintf(stderr, "No pair for %s.\n", path);
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

            if (a.enc_size == b.enc_size && 0 == memcmp(a.enc, b.enc, a.enc_size)) {
                break;
            }

            // Responsibility for cleanup(&a) and cleanup(&b) passes to
            // start_diff(), then to diff() which calls cleanup_except_path(),
            // and ultimately back to main() where we finally cleanup() the paths.
            start_diff(a,b);
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

    const int nopenfd = 1024;
    ftw(B.root, walk, nopenfd);

    for (pthread_t* th = threads; th != next_thread; th++) {
        pthread_join(*th, NULL);
    }

    const size_t ndiffs = (size_t)(next_diff - diffs);
    qsort(diffs, ndiffs, sizeof(struct Diff), sort_by_descending_diff);

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

    for (size_t i = 0; i < ndiffs; i++) {
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
            diffs[i].b.path, diffs[i].a.path,
            diffs[i].b.path, diffs[i].a.path,
            diffs[i].b.path, diffs[i].b.path,
            diffs[i].a.path, diffs[i].a.path);

        cleanup(&diffs[i].a);
        cleanup(&diffs[i].b);
    }

    printf("%d .pngs in %s\n%d pairs in %s\n%zu diffs\n", total,B.root, pairs,A.root, ndiffs);
    return ndiffs ? 0 : 1;
}
