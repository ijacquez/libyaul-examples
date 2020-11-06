/*
 * Copyright (c) 2012-2020 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 */

#include <yaul.h>
#include <sega3d.h>

#include <assert.h>
#include <stdlib.h>

#define RESOLUTION_WIDTH    (320)
#define RESOLUTION_HEIGHT   (224)

#define SCREEN_WIDTH    (320)
#define SCREEN_HEIGHT   (224)

#define VDP1_VRAM_CMDT_COUNT    (8192)
#define VDP1_VRAM_TEXTURE_SIZE  (0x3BFE0)
#define VDP1_VRAM_GOURAUD_COUNT (1024)
#define VDP1_VRAM_CLUT_COUNT    (256)

#define ORDER_SYSTEM_CLIP_COORDS_INDEX  (0)
#define ORDER_LOCAL_COORDS_INDEX        (1)
#define ORDER_SEGA3D_INDEX              (2)
#define ORDER_BASE_COUNT                (3)

extern PDATA PD_PLANE1[];
extern PDATA PD_CUBE1[];
extern PDATA PD_SONIC[];
extern PDATA PD_QUAKE[];
extern PDATA PD_TORUS[];

extern Uint16 GR_SMS[];
extern PDATA PD_SMS3[];

extern TEXTURE TEX_SAMPLE[];
extern PICTURE PIC_SAMPLE[];

static void _vblank_out_handler(void *);

static void _assets_copy(const sega3d_object_t *object);

static smpc_peripheral_digital_t _digital;

static vdp1_cmdt_list_t *_cmdt_list;

int
main(void)
{
        sega3d_init();

        /* Set up global command table list */
        _cmdt_list = vdp1_cmdt_list_alloc(VDP1_VRAM_CMDT_COUNT);
        assert(_cmdt_list != NULL);
        /* Set up the first few command tables */
        int16_vec2_t p __unused;
        int16_vec2_zero(&p);
        vdp1_env_preamble_populate(&_cmdt_list->cmdts[0], NULL);

        sega3d_object_t object;

        object.pdata = PD_QUAKE;
        object.cmdts = &_cmdt_list->cmdts[0];
        object.offset = ORDER_SEGA3D_INDEX;
        object.flags = SEGA3D_OBJECT_FLAGS_WIREFRAME;
        object.iterate_fn = NULL;
        object.data = NULL;

        /* sega3d_tlist_set(TEX_SAMPLE, 2); */

        sega3d_object_prepare(&object);

        _assets_copy(&object);

        ANGLE z;
        z = 0;

        FIXED zz = FIX16(100.0f);

        while (true) {
                smpc_peripheral_process();
                smpc_peripheral_digital_port(1, &_digital);

                dbgio_printf("[H[2J");

                sega3d_matrix_push(MATRIX_TYPE_PUSH); {
                        /* sega3d_matrix_rotate_z(z); */
                        /* sega3d_matrix_rotate_y(z); */

                        sega3d_matrix_translate(toFIXED(0.0f), toFIXED(0.0f), zz);
                        sega3d_object_transform(&object);
                } sega3d_matrix_pop();

                sega3d_object_iterate(&object);

                if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_UP) != 0) {
                        z += DEGtoANG(1.0f);
                        zz += FIX16(1.0f);
                } else if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_DOWN) != 0) {
                        z -= DEGtoANG(1.0f);
                        zz -= FIX16(1.0f);
                }

                /* Be sure to terminate list */
                _cmdt_list->count = ORDER_BASE_COUNT + object.count;

                vdp1_cmdt_end_set(&_cmdt_list->cmdts[_cmdt_list->count - 1]);

                vdp1_sync_cmdt_list_put(_cmdt_list, NULL, NULL);

                dbgio_flush();
                vdp_sync();
        }
}

void
user_init(void)
{
        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A,
            VDP2_TVMD_VERT_224);

        vdp2_scrn_back_screen_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            COLOR_RGB1555(1, 0, 3, 15));

        vdp_sync_vblank_out_set(_vblank_out_handler);

        vdp1_vram_partitions_set(VDP1_VRAM_CMDT_COUNT,
            VDP1_VRAM_TEXTURE_SIZE,
            VDP1_VRAM_GOURAUD_COUNT,
            VDP1_VRAM_CLUT_COUNT);

        vdp1_env_default_set();
        vdp2_sprite_priority_set(0, 6);

        cpu_intc_mask_set(0);

        dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
        dbgio_dev_font_load();
        dbgio_dev_font_load_wait();

        vdp2_tvmd_display_set();
}

static void
_vblank_out_handler(void *work __unused)
{
        smpc_peripheral_intback_issue();
}

static void
_assets_copy(const sega3d_object_t *object)
{
        const uint16_t polygon_count =
            sega3d_object_polycount_get(object);

        (void)memcpy((uint16_t *)VDP1_VRAM(0x2BFE0),
            GR_SMS,
            sizeof(vdp1_gouraud_table_t) * polygon_count);

        vdp1_vram_partitions_t vdp1_vram_parts;
        vdp1_vram_partitions_get(&vdp1_vram_parts);
        
        for (uint32_t i = 0; i < 2; i++) {
                const PICTURE *picture;
                picture = &PIC_SAMPLE[i];
                const TEXTURE *texture;
                texture = &TEX_SAMPLE[picture->texno];

                const uint32_t vram_ptr = VDP1_VRAM(texture->CGadr << 3);

                const uint32_t size =
                    (texture->Hsize * texture->Vsize * 4) >> picture->cmode;

                (void)memcpy((void *)vram_ptr, picture->pcsrc, size);
        }
}