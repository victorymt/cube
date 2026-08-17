#include "world/block_atlas.h"
#include "world/world.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint64_t EXPECTED_TILE_HASHES[] = {
    UINT64_C(0x4098031764733f2c), UINT64_C(0xf9e47e1b7a79d992),
    UINT64_C(0x0f6179a413939355), UINT64_C(0x12ee0b6c08104e80),
    UINT64_C(0x45aa05a5e8339248), UINT64_C(0xebdac5c7e392d198),
    UINT64_C(0xd347a00e1d636ee9), UINT64_C(0xdf55440e8f0dc25d),
    UINT64_C(0xd8532d4ae43487c2), UINT64_C(0x1216302429b9245e),
    UINT64_C(0x869517218157f419), UINT64_C(0x090d7e34963ad228),
    UINT64_C(0x50ada295f1b5dec0), UINT64_C(0xc61eb468db7e6dab),
    UINT64_C(0x026770a1b4ab87b6), UINT64_C(0x5279be976c16454a),
    UINT64_C(0xe8e48456ece2791a), UINT64_C(0xff3bb52db9244141),
    UINT64_C(0x2b49f23af67026d2), UINT64_C(0x2a3046ed1d33d7b7),
    UINT64_C(0xe4154c785ac056d2), UINT64_C(0x752419608e425ad6),
    UINT64_C(0xbcf23e7186607aef), UINT64_C(0x64cc968364341540),
    UINT64_C(0xf41530051a7fcf7d), UINT64_C(0x2eaf2a55a200e5b7),
    UINT64_C(0xc80fe5c801ac1afa), UINT64_C(0x7637c3c87bc16459),
    UINT64_C(0x7c5dc0e6cf9f611a), UINT64_C(0x3e9e25b34fa8188a),
    UINT64_C(0x26e17affcadcce3d), UINT64_C(0x19e1ed2b7b46f1a8),
    UINT64_C(0x73a580008f703915), UINT64_C(0x1f3fa389e745a7e3),
    UINT64_C(0x5472b93725eb2048), UINT64_C(0xbda11de7facecbdd),
    UINT64_C(0x8788d6df976a1582), UINT64_C(0x20dfb761f7059050),
    UINT64_C(0x2539f023e105f9e9), UINT64_C(0xf341aeed3457429e),
    UINT64_C(0x708bab96400b1562), UINT64_C(0x0757e978ccddbb7b),
    UINT64_C(0x35d169211a5a96ac), UINT64_C(0xaeabbd2268570d93),
    UINT64_C(0x30007a1c60fe5eba), UINT64_C(0x9565cf9038e94aee),
    UINT64_C(0x925552888c862af4), UINT64_C(0x405e55b43a42e5ce),
    UINT64_C(0xa466719cd7398102), UINT64_C(0x70eb7496a76241bd),
    UINT64_C(0x39b8ca9204ae12c1), UINT64_C(0x1177f9f4146a20d4),
    UINT64_C(0x61b41761ac6930b5), UINT64_C(0xed5b575053937708),
    UINT64_C(0x220405e8018b540d), UINT64_C(0xa0b34bcbb47870e7),
    UINT64_C(0xfc9145530921b932), UINT64_C(0x700764db14c9cc9f),
    UINT64_C(0x47df8a61ab478bda), UINT64_C(0x5de967e2d55e4687),
    UINT64_C(0xf07ca4559b9e24c1), UINT64_C(0x0ca0e2d2545e7ead),
    UINT64_C(0x3f3578bb0a91d283), UINT64_C(0x197efafd757cfec7),
    UINT64_C(0x4f20991cf8928497), UINT64_C(0x62db17ebb75d1230),
    UINT64_C(0x8f043be04f849566), UINT64_C(0x954e4872494eca34),
    UINT64_C(0xbd9d516474a0ea77), UINT64_C(0x20fb39843eeb0a33),
    UINT64_C(0x8d3bb02844fa4024), UINT64_C(0x44ed0bb0174a422a),
    UINT64_C(0x02ac969d4b3ddf2f), UINT64_C(0x5f9b2c2dcdfeeb63),
    UINT64_C(0x4ce92a922005c3f8), UINT64_C(0x5c294bd1a06cee42),
    UINT64_C(0x4abf355a83ce46f1), UINT64_C(0x1e804499499af077),
    UINT64_C(0xd0c61716e30924c3), UINT64_C(0xe5b7a0d314317057),
    UINT64_C(0x45cc26ec2e8ff8dd), UINT64_C(0xd7412f3399f711b6),
    UINT64_C(0xc5377d299e726853), UINT64_C(0x4b956247d470ff59),
    UINT64_C(0x77ecf106959105c2), UINT64_C(0x0c63406d06f4744b),
    UINT64_C(0xe1c0e5b6f336dd0e), UINT64_C(0x94adb666708bd7b1),
    UINT64_C(0xc53279cf6ac4a3cd), UINT64_C(0xcaa2c0d3214f5e42),
    UINT64_C(0xceacfcb48eb68b0c), UINT64_C(0xb50ae4a84c13d998),
    UINT64_C(0x493e8e92a775afa5), UINT64_C(0x0ac98163540b6aab),
    UINT64_C(0x51fd4b8d225cabd6), UINT64_C(0x2a937b6804627574),
    UINT64_C(0xb8d299b6beb5efaf), UINT64_C(0xe4987a9ebbf83c98),
    UINT64_C(0x8e1cfd6831dc75bc), UINT64_C(0xfc1a5eb33c387f7b),
    UINT64_C(0xa333baef0376a36c), UINT64_C(0x8c8e140173acc7ce),
    UINT64_C(0x02cc45b2103bbeec), UINT64_C(0xfab28199d3ee63a2),
    UINT64_C(0x92ffdb0a61d138d1), UINT64_C(0x2d972e9e8b894582),
    UINT64_C(0x37988d430419c8ce), UINT64_C(0x73af89f2d9d10cb4),
    UINT64_C(0x6ecb7ea381872bc5), UINT64_C(0xc9d7be67f08ab4ac),
    UINT64_C(0x5fd0342bc118e6ad), UINT64_C(0xdc5f04ec83cf36fb),
    UINT64_C(0x20462631e3a0ab95), UINT64_C(0x6cef33e10fcdf84d),
    UINT64_C(0x3fd8723cb05783bc), UINT64_C(0x797e16fc591978b8),
    UINT64_C(0xb8b8a19561672b56), UINT64_C(0xfc0e19d3498aa633),
    UINT64_C(0x31e1b461d89ea42e), UINT64_C(0x4df4be7f89f21b3b),
    UINT64_C(0xfafd08c30eaf624d), UINT64_C(0x97a6a9e0c662db28),
    UINT64_C(0x5a976af624ae61a9), UINT64_C(0x8a9af393ed2a9021),
    UINT64_C(0x43bd0e7660f0d63f), UINT64_C(0x2f9d9d05ea16b7fb),
    UINT64_C(0xaf298b113315c814), UINT64_C(0x5f2c7089ce156214),
    UINT64_C(0x42b540aea2720822), UINT64_C(0x38ed4b028066996b),
    UINT64_C(0xd23a20eb2cc2370b), UINT64_C(0x4c556fe54e4d3fd4),
    UINT64_C(0xafd7310eb2ab190d), UINT64_C(0x1ca380128586a7f6),
    UINT64_C(0x9cc151178de778d5), UINT64_C(0xce5eec89e73a395c),
    UINT64_C(0x1b58c4c5114ca23e), UINT64_C(0xeaf588c013a96824),
    UINT64_C(0x79da3049a5b6bff8), UINT64_C(0x3b9b9a805f528ed3),
    UINT64_C(0x983a8a47232cc177), UINT64_C(0xdb25fb62c2397b4a),
    UINT64_C(0xfd16dec120fe8046), UINT64_C(0xf840265e75f514dd),
    UINT64_C(0xc1ae8289edcec431), UINT64_C(0x40e71ea2584d971b),
    UINT64_C(0x38fa772ddbfc95e9), UINT64_C(0x7cb960a9025ad6c1),
    UINT64_C(0xb3135b40df1fa00e), UINT64_C(0x7129ecc643c636c1),
    UINT64_C(0x25d8e9062ec2d54b), UINT64_C(0xb5da8d597bdab080),
    UINT64_C(0x80618350fb6ebfa5), UINT64_C(0x655dec9bb9eab7a3),
    UINT64_C(0x2dcb268bf985576e), UINT64_C(0x0d849932fe03ecf3),
    UINT64_C(0x41f7160dba921335), UINT64_C(0xe9779b13d1b0c50e),
    UINT64_C(0xe64350b3bb77e2de), UINT64_C(0xde75e653ed6f9a41),
    UINT64_C(0x2fd9983ac4895460), UINT64_C(0x7b93eec160112f63),
    UINT64_C(0x47619aef8bcdce51), UINT64_C(0xe83722715ca48ec3),
    UINT64_C(0xf5229c685eaee488), UINT64_C(0x9c0c5dcf5c0b6e2f),
    UINT64_C(0xaa84862994b1888e), UINT64_C(0xee1aae6bb170878a),
    UINT64_C(0x58e50ff1b8d532bb), UINT64_C(0xdb3e7b5003ca93df),
    UINT64_C(0x96b27f1d5d6c656f), UINT64_C(0x6c3270dc6cd6b634),
    UINT64_C(0x1deb26ee4dd49636), UINT64_C(0x78f4d3b846995815),
    UINT64_C(0x6af3b6015bc357da), UINT64_C(0xf15be4b5b4666a11),
    UINT64_C(0x7355cf644c1956a8), UINT64_C(0xbf187e688024d715),
    UINT64_C(0x0a7903b67317cf1e), UINT64_C(0x5588d48cbc8a593c),
    UINT64_C(0x1df7ec28e55df863), UINT64_C(0x24c2b3d6737a3546),
    UINT64_C(0x585b3571ff152878), UINT64_C(0xbe4b57d1210b0aed),
    UINT64_C(0xbe8b00e559b7bd49), UINT64_C(0x3ed6bfae81686548),
    UINT64_C(0xc0e4b6ce2e0d4503), UINT64_C(0x967a91996430672a),
    UINT64_C(0x20afdfd9af86c334), UINT64_C(0x61f45609dfd7e1c5),
    UINT64_C(0x7965e6a808d29dc3), UINT64_C(0x0691d5bc6dfa6d5b),
    UINT64_C(0x55894f6b56665b1e), UINT64_C(0x4af7378bc8003137),
    UINT64_C(0x4c535d1c70675089), UINT64_C(0x10fee3cbb1ccb6fc),
    UINT64_C(0x8b5716997a5a98ca), UINT64_C(0xd181b13d99b39827),
    UINT64_C(0xc4b962216a45fea2), UINT64_C(0x1d13cbc6b8691ef0),
    UINT64_C(0x76bc46765aa64cf4), UINT64_C(0xbd8fbe2402efb87c),
    UINT64_C(0x26dfa608941657d4), UINT64_C(0xf9d91b8cba80266b),
    UINT64_C(0x28c9d5507541d219), UINT64_C(0x98841122c7c91199),
    UINT64_C(0x8d6faa743be4e84a), UINT64_C(0x79f6b17231dbe606),
    UINT64_C(0xa88b974f36c38209), UINT64_C(0x78839503cbd7c101),
    UINT64_C(0xa0f671b3eda95c0c), UINT64_C(0xfdc0732de930b247),
    UINT64_C(0x14b72664466375ef), UINT64_C(0xfd557d5eb878a97e),
    UINT64_C(0xd333f0fe3ab72f52), UINT64_C(0x57267941283778f4),
    UINT64_C(0xcd35824387a6ae29), UINT64_C(0x5ddbd8dbe4227725),
    UINT64_C(0x3e3da89174b43920), UINT64_C(0xa851618bb7f0bc91),
    UINT64_C(0x5b8c97e2f8ac8ee7), UINT64_C(0x8aa16e7ad55a5ba4),
    UINT64_C(0x792f42a6de4ce6ce), UINT64_C(0x5aa10683249e2de5),
    UINT64_C(0x4e9a505b2ae38ebd), UINT64_C(0x8212325f264ba111),
    UINT64_C(0xcbe542a9e782da28), UINT64_C(0xa18bc091bad56d19),
    UINT64_C(0x838f5c0b105157f8), UINT64_C(0x1a01583ac19ba96f),
    UINT64_C(0xedc6fde1a294f33e), UINT64_C(0x060eaa31638381aa),
    UINT64_C(0xa2e0c864c1f260d4), UINT64_C(0xbe6293a2672bebfd),
    UINT64_C(0x4804961c1b1b9564), UINT64_C(0x2fe4c981662baccf),
    UINT64_C(0x76ed5b40b64885d2), UINT64_C(0xd623593e7c236bc7),
    UINT64_C(0xbb8b6f5bd4d821f2), UINT64_C(0x57ae226eb12151dd),
    UINT64_C(0x665a7551598b0c36), UINT64_C(0xc513e71f37b73121),
    UINT64_C(0xfc97877fb87da576), UINT64_C(0x1bda09c888383e58),
    UINT64_C(0x044af5e9ee4bc6d9), UINT64_C(0xfbd09b6ee01937c3),
    UINT64_C(0x4de7e64e777339d1), UINT64_C(0x5c802d0aefc38297),
    UINT64_C(0xedc0e6049741a2dd), UINT64_C(0x2da96acf03e2cd95),
    UINT64_C(0xa26db4c7c9ef9972), UINT64_C(0x5577a865e053b660),
    UINT64_C(0x94f7f7226e9e4d49), UINT64_C(0x4b36024c9b2665e0),
    UINT64_C(0xfd30f25f954a5113), UINT64_C(0x0feff35c46e1078f),
    UINT64_C(0x73ca117f9e591a6d), UINT64_C(0x909f44cff4fca6be),
    UINT64_C(0x751e8499c3754484), UINT64_C(0x72cb343cbf9f42b9),
    UINT64_C(0xbd7b208fcfa868e6), UINT64_C(0x428153fce25842fa),
    UINT64_C(0x3c71602176fff5a1), UINT64_C(0xbaf44f686413ebd8),
    UINT64_C(0x4efdb51891a865d3), UINT64_C(0x3487610f95ac5bea),
    UINT64_C(0x4e98ac5eceb519c8), UINT64_C(0x7780ce8962901594),
    UINT64_C(0xafd9a1ed153e486e), UINT64_C(0xa5cfd515c3723cbc),
    UINT64_C(0x99746c708a3be974), UINT64_C(0x817010c8766808d8),
    UINT64_C(0x4524ace78ef1c424), UINT64_C(0x73caaa56fdca05b9),
    UINT64_C(0x821856f9e736d644), UINT64_C(0x3aee0af59c055687),
    UINT64_C(0x34353dcf18fd3bfb), UINT64_C(0x25f5d17b640e80dc),
    UINT64_C(0x69d494ebd2d2b071), UINT64_C(0x1cd6c0194d962581),
    UINT64_C(0x604995f0c33aaffd), UINT64_C(0x040dce23cb7c55f5),
    UINT64_C(0x03708f73a43dba2d), UINT64_C(0x9c230436547f0306),
    UINT64_C(0x5c75a7993ffc38ac), UINT64_C(0x72d23e23cb54c9b5),
    UINT64_C(0xa46e87654ba91929), UINT64_C(0x5eb42c7e41f408ff),
    UINT64_C(0xf1076b608b7fd272), UINT64_C(0xcaad4ffa064bdbbf),
    UINT64_C(0x27fb2b435ae5272d), UINT64_C(0xb46629a1e88717f9),
    UINT64_C(0x126daad49b98bac2), UINT64_C(0x48fb131c74478a87),
    UINT64_C(0x9ce97ef2ea626c8a), UINT64_C(0x5f2803f54e6872fb),
    UINT64_C(0xc857e536ece1e6d8), UINT64_C(0x384a0cb2f5d362a9),
    UINT64_C(0xc58e0a1974b6bcb9), UINT64_C(0x850ece32684ccbac),
    UINT64_C(0x5fd231348381f2ac), UINT64_C(0x319d3e47b608d147),
    UINT64_C(0xe7023197954ed7df), UINT64_C(0x0da5d621992d63c6),
    UINT64_C(0x4323eb7394071e16), UINT64_C(0x2eee4c1e1f7b29e7),
    UINT64_C(0x69a6d955590181af), UINT64_C(0x9af32e2c23636a45),
    UINT64_C(0x9f27afe7ef0acfef),
    UINT64_C(0xf6391e5e7d71dbe4), UINT64_C(0xebb232b5ce43d7f2),
    UINT64_C(0xbd283f90b2b797d7), UINT64_C(0x9a35ed39f98c128d),
    UINT64_C(0xf2fc9320537028bb), UINT64_C(0xb1a379f2060abff5),
    UINT64_C(0xbb6d0595bf0e67b9), UINT64_C(0xb8359a06a2e459f9),
    UINT64_C(0xc1f215fba22bc5c2), UINT64_C(0x2495b5fbe7f4da56),
    UINT64_C(0x5a63afcfc7b1339f), UINT64_C(0xbab23cb047322244),
    UINT64_C(0x55eb24c82745247c), UINT64_C(0x93409ea0dd64f45b),
    UINT64_C(0x8f40964fa1b32f95), UINT64_C(0x82265c28c7fb6564),
    UINT64_C(0x79ec777cdff62840), UINT64_C(0xfdfe007771ef12f1),
    UINT64_C(0x8399944af2a12177), UINT64_C(0x28d3a81935ef6881)
};

_Static_assert(sizeof(EXPECTED_TILE_HASHES) /
                       sizeof(EXPECTED_TILE_HASHES[0]) ==
                   TEX_COUNT,
               "atlas digest table must cover every texture");

static bool ColorsEqual(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static uint64_t HashColor(uint64_t hash, Color color)
{
    const unsigned char channels[4] = { color.r, color.g, color.b, color.a };
    for (int channel = 0; channel < 4; channel++) {
        hash ^= channels[channel];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t HashRegion(Image image, int left, int top, int width,
                           int height)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            hash = HashColor(hash, GetImageColor(image, left + x, top + y));
        }
    }
    return hash;
}

static Image GenerateAtlas(void)
{
    Image image = GenImageColor(ATLAS_CELL_SIZE * ATLAS_COLUMNS,
                                ATLAS_CELL_SIZE * ATLAS_ROWS, BLANK);
    assert(IsImageValid(image));
    for (int texture = 0; texture < TEX_COUNT; texture++) {
        DrawAtlasTile(&image, (BlockTexture)texture);
    }
    return image;
}

static void AssertAtlasCoordinates(void)
{
    const float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    const float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    assert(ATLAS_CELL_SIZE == 32);
    assert(ATLAS_TILE_PADDING >= ATLAS_TILE_SIZE / 2);

    for (int i = 0; i < TEX_COUNT; i++) {
        BlockTexture texture = (BlockTexture)i;
        int column = i % ATLAS_COLUMNS;
        int row = i / ATLAS_COLUMNS;
        Rectangle source = AtlasSourceRect(texture);
        assert(source.x == column * ATLAS_CELL_SIZE + ATLAS_TILE_PADDING);
        assert(source.y == row * ATLAS_CELL_SIZE + ATLAS_TILE_PADDING);
        assert(source.width == ATLAS_TILE_SIZE);
        assert(source.height == ATLAS_TILE_SIZE);

        Vector2 uvs[6];
        AtlasUVs(texture, uvs);
        float minU = (source.x + 0.25f) / atlasWidth;
        float maxU = (source.x + source.width - 0.25f) / atlasWidth;
        float minV = (source.y + 0.25f) / atlasHeight;
        float maxV = (source.y + source.height - 0.25f) / atlasHeight;
        for (int vertex = 0; vertex < 6; vertex++) {
            assert(fabsf(uvs[vertex].x - minU) < 0.000001f ||
                   fabsf(uvs[vertex].x - maxU) < 0.000001f);
            assert(fabsf(uvs[vertex].y - minV) < 0.000001f ||
                   fabsf(uvs[vertex].y - maxV) < 0.000001f);
        }
    }
}

static void AssertPixelContract(Image image)
{
    bool matched = true;
    for (int texture = 0; texture < TEX_COUNT; texture++) {
        int cellX = (texture % ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
        int cellY = (texture / ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
        uint64_t actual = HashRegion(image, cellX, cellY, ATLAS_CELL_SIZE,
                                     ATLAS_CELL_SIZE);
        if (actual != EXPECTED_TILE_HASHES[texture]) {
            fprintf(stderr,
                    "texture %d digest: expected 0x%016llx, got 0x%016llx\n",
                    texture,
                    (unsigned long long)EXPECTED_TILE_HASHES[texture],
                    (unsigned long long)actual);
            matched = false;
        }
    }
    uint64_t atlas = HashRegion(image, 0, 0, image.width, image.height);
    if (atlas != UINT64_C(0x0f79d2cb1ff919a5)) {
        fprintf(stderr, "atlas digest: got 0x%016llx\n",
                (unsigned long long)atlas);
        matched = false;
    }
    assert(matched);
}

static void AssertMipSafePadding(Image image)
{
    for (int texture = 0; texture < TEX_COUNT; texture++) {
        Rectangle source = AtlasSourceRect((BlockTexture)texture);
        int cellLeft = (texture % ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
        int cellTop = (texture / ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
        for (int y = 0; y < ATLAS_CELL_SIZE; y++) {
            int sourceY = y - ATLAS_TILE_PADDING;
            if (sourceY < 0) sourceY = 0;
            if (sourceY >= ATLAS_TILE_SIZE) sourceY = ATLAS_TILE_SIZE - 1;
            for (int x = 0; x < ATLAS_CELL_SIZE; x++) {
                int sourceX = x - ATLAS_TILE_PADDING;
                if (sourceX < 0) sourceX = 0;
                if (sourceX >= ATLAS_TILE_SIZE) sourceX = ATLAS_TILE_SIZE - 1;
                Color actual = GetImageColor(image, cellLeft + x, cellTop + y);
                Color expected = GetImageColor(image, (int)source.x + sourceX,
                                               (int)source.y + sourceY);
                assert(ColorsEqual(actual, expected));
            }
        }
    }
}

static void AssertTransparentArtwork(Image image)
{
    const BlockTexture textures[] = { TEX_FLOWER, TEX_MUSHROOM };
    for (int index = 0; index < 2; index++) {
        Rectangle source = AtlasSourceRect(textures[index]);
        assert(GetImageColor(image, (int)source.x, (int)source.y).a == 0);
        assert(GetImageColor(image, (int)source.x + 7,
                             (int)source.y + 7).a == 255);
    }
}

static void AssertTextureMapping(void)
{
    assert(TextureForBlockFace(BLOCK_GRASS, 2) == TEX_GRASS_TOP);
    assert(TextureForBlockFace(BLOCK_GRASS, 3) == TEX_DIRT);
    assert(TextureForBlockFace(BLOCK_GRASS, 0) == TEX_GRASS_SIDE);
    assert(TextureForBlockFace(BLOCK_WOOD, 2) == TEX_WOOD_TOP);
    assert(TextureForBlockFace(BLOCK_WOOD, 0) == TEX_WOOD_SIDE);
    assert(TextureForBlockFace(BLOCK_AIR, 0) == TEX_DIRT);
    static const BlockType geologyBlocks[] = {
        BLOCK_GRAVEL, BLOCK_CLAY, BLOCK_MUD, BLOCK_MOSSY_STONE,
        BLOCK_RED_SAND, BLOCK_BASALT, BLOCK_COPPER_ORE, BLOCK_CRYSTAL,
        BLOCK_GRANITE, BLOCK_LIMESTONE, BLOCK_SHALE, BLOCK_MARBLE,
        BLOCK_PEAT, BLOCK_PERMAFROST, BLOCK_ROCK_SALT,
        BLOCK_VOLCANIC_ASH, BLOCK_PUMICE, BLOCK_SULFUR_ORE,
        BLOCK_PACKED_ICE, BLOCK_QUARTZ_ORE
    };
    for (size_t index = 0;
         index < sizeof(geologyBlocks) / sizeof(geologyBlocks[0]); index++) {
        assert(TextureForBlockFace(geologyBlocks[index], 0) ==
               (BlockTexture)(TEX_GRAVEL + index));
    }
    for (int index = 0; index < COLOR_BLOCK_COUNT; index++) {
        BlockType block = (BlockType)(BLOCK_COLOR_START + index);
        assert(TextureForBlockFace(block, 0) ==
               (BlockTexture)(TEX_COLOR_START + index));
    }
}

static void AssertNaturalBlockContract(void)
{
    static const char *names[] = {
        "Gravel", "Clay", "Mud", "Mossy Stone",
        "Red Sand", "Basalt", "Copper Ore", "Crystal",
        "Granite", "Limestone", "Shale", "Marble", "Peat",
        "Permafrost", "Rock Salt", "Volcanic Ash", "Pumice",
        "Sulfur Ore", "Packed Ice", "Quartz Ore"
    };
    assert(BLOCK_NATURAL_END - BLOCK_NATURAL_START + 1 ==
           (int)(sizeof(names) / sizeof(names[0])));
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        BlockType type = (BlockType)(BLOCK_NATURAL_START + index);
        assert(IsValidBlockType(type));
        assert(strcmp(BlockName(type), names[index]) == 0);
        assert(BlockCollisionHeight(type) == 1.0f);
        assert(!IsTranslucentBlock(type));
        assert(BlockBaseColor(type).a == 255);
    }
}

static void AssertBlockCatalogCompleteness(void)
{
    for (int value = BLOCK_AIR; value <= BLOCK_NATURAL_END; value++) {
        BlockType type = (BlockType)value;
        const char *name = BlockName(type);
        assert(IsValidBlockType(type));
        assert(name != NULL && name[0] != '\0');
        assert(type == BLOCK_AIR || strcmp(name, "Air") != 0);
        assert(BlockBaseColor(type).a == 255);
        for (int face = 0; face < 6; face++) {
            BlockTexture texture = TextureForBlockFace(type, face);
            assert(texture >= 0 && texture < TEX_COUNT);
        }
    }

    BlockType invalid = (BlockType)(BLOCK_NATURAL_END + 1);
    Color fallback = BlockBaseColor(invalid);
    assert(strcmp(BlockName(invalid), "Air") == 0);
    assert(fallback.r == 118 && fallback.g == 122 &&
           fallback.b == 124 && fallback.a == 255);
    assert(TextureForBlockFace(invalid, 0) == TEX_DIRT);
}

int main(void)
{
    AssertAtlasCoordinates();
    AssertTextureMapping();
    AssertNaturalBlockContract();
    AssertBlockCatalogCompleteness();

    Image first = GenerateAtlas();
    Image second = GenerateAtlas();
    AssertPixelContract(first);
    AssertPixelContract(second);
    AssertMipSafePadding(first);
    AssertTransparentArtwork(first);
    assert(HashRegion(first, 0, 0, first.width, first.height) ==
           HashRegion(second, 0, 0, second.width, second.height));
    UnloadImage(second);
    UnloadImage(first);
    puts("block atlas tests passed");
    return 0;
}
