#include "gameplay/album.h"

#include <assert.h>
#include <stdio.h>

static void TestAlbumCollection(void)
{
    AlbumInit();
    assert(AlbumImageCount() == 0);
    assert(AlbumAddPath(NULL) == ALBUM_ADD_INVALID);
    assert(AlbumAddPath("") == ALBUM_ADD_INVALID);
    assert(AlbumAddPath("tests/test_album.c") == ALBUM_ADD_OK);
    assert(AlbumAddPath("tests/test_album.c") == ALBUM_ADD_DUPLICATE);
    assert(AlbumImageCount() == 1);
    assert(AlbumHasPath("tests/test_album.c"));
    assert(AlbumImagePathAt(-1) == NULL);
    assert(AlbumImagePathAt(1) == NULL);

    assert(AlbumRemoveAt(0));
    assert(!AlbumRemoveAt(0));
    assert(AlbumImageCount() == 0);

    char path[64];
    for (int index = 0; index < ALBUM_MAX_IMAGES; index++) {
        snprintf(path, sizeof(path), "image-%d.png", index);
        assert(AlbumAddPath(path) == ALBUM_ADD_OK);
    }
    assert(AlbumAddPath("overflow.png") == ALBUM_ADD_FULL);
}

static void TestAlbumPersistence(void)
{
    AlbumReset();
    assert(AlbumAddPath("tests/test_album.c") == ALBUM_ADD_OK);
    assert(AlbumAddPath("README.md") == ALBUM_ADD_OK);

    FILE *saved = tmpfile();
    assert(saved);
    assert(AlbumSave(saved));
    rewind(saved);
    AlbumReset();
    assert(AlbumLoad(saved));
    assert(AlbumImageCount() == 2);
    assert(AlbumHasPath("tests/test_album.c"));
    assert(AlbumHasPath("README.md"));
    fclose(saved);

    FILE *invalid = tmpfile();
    assert(invalid);
    assert(fprintf(invalid, "album %d\n", ALBUM_MAX_IMAGES + 1) > 0);
    rewind(invalid);
    assert(!AlbumLoad(invalid));
    assert(AlbumImageCount() == 2);
    fclose(invalid);

    FILE *filtered = tmpfile();
    assert(filtered);
    assert(fprintf(filtered,
                   "album 3\ntests/test_album.c\nmissing-image.png\n"
                   "tests/test_album.c\n") > 0);
    rewind(filtered);
    assert(AlbumLoad(filtered));
    assert(AlbumImageCount() == 1);
    assert(AlbumHasPath("tests/test_album.c"));
    fclose(filtered);
}

int main(void)
{
    TestAlbumCollection();
    TestAlbumPersistence();
    puts("album tests passed");
    return 0;
}
