/*
 * Copyright (c) 2012-2016 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 */

#include <yaul.h>

#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   240

#define STATE_ZOOM_MOVE_INVALID         (-1)
#define STATE_ZOOM_MOVE_ORIGIN          (0)
#define STATE_ZOOM_WAIT                 (1)
#define STATE_ZOOM_MOVE_ANCHOR          (2)
#define STATE_ZOOM_RELEASE_BUTTONS      (3)
#define STATE_ZOOM_SELECT_ANCHOR        (4)

#define ZOOM_TEX_PATH   "ZOOM.TEX"
#define ZOOM_PAL_PATH   "ZOOM.PAL"

#define ZOOM_POINT_WIDTH                (64)
#define ZOOM_POINT_HEIGHT               (102)
#define ZOOM_POINT_POINTER_SIZE         (3)
#define ZOOM_POINT_COLOR_SELECT         COLOR_RGB1555(1,  0,  0, 31)
#define ZOOM_POINT_COLOR_WAIT           COLOR_RGB1555(1, 31,  0,  0)
#define ZOOM_POINT_COLOR_HIGHLIGHT      COLOR_RGB1555(1,  0, 31,  0)

#define ORDER_SYSTEM_CLIP_COORDS_INDEX      0
#define ORDER_CLEAR_LOCAL_COORDS_INDEX      1
#define ORDER_CLEAR_POLYGON_INDEX           2
#define ORDER_LOCAL_COORDS_INDEX            3
#define ORDER_SPRITE_INDEX                  4
#define ORDER_POLYGON_POINTER_INDEX         5
#define ORDER_DRAW_END_INDEX                6
#define ORDER_COUNT                         7

#define ANIMATION_FRAME_COUNT    (14)
#define ANIMATION_FRAME_DURATION (3)

extern uint8_t root_romdisk[];

/* Zoom state */
static int32_t _state_zoom = STATE_ZOOM_MOVE_ORIGIN;
static int16_vec2_t _display = INT16_VEC2_INITIALIZER(ZOOM_POINT_WIDTH, ZOOM_POINT_HEIGHT);
static int16_vec2_t _zoom_point = INT16_VEC2_INITIALIZER(0, 0);
static uint16_t _zoom_point_value = VDP1_CMDT_ZOOM_POINT_CENTER;
static uint32_t _delay_frames = 0;
static smpc_peripheral_digital_t _digital;

static struct {
        int16_vec2_t position;
        color_rgb1555_t color;

        vdp1_cmdt_t *cmdt;
} _polygon_pointer;

static struct {
        uint16_t anim_frame;
        uint16_t anim_counter;

        uint16_t *tex_base;
        uint16_t *pal_base;

        vdp1_cmdt_t *cmdt;
} _sprite;

static void *_romdisk = NULL;

static vdp1_cmdt_list_t *_cmdt_list = NULL;
static vdp1_vram_partitions_t _vdp1_vram_partitions;

static volatile uint32_t _frt_count = 0;

static inline bool __always_inline
_digital_dirs_pressed(void)
{
        return (_digital.pressed.raw & PERIPHERAL_DIGITAL_DIRECTIONS) != 0x0000;
}

static void _init(void);

static void _cmdt_list_init(void);

static void _state_zoom_move_origin(void);
static void _state_zoom_wait(void);
static void _state_zoom_move_anchor(void);
static void _state_zoom_release_buttons(void);
static void _state_zoom_select_anchor(void);

static void _sprite_init(void);
static void _sprite_config(void);
static void _polygon_pointer_init(void);
static void _polygon_pointer_config(void);

static void _dma_upload(void *, void *, size_t, uint8_t);

static uint32_t _frame_time_calculate(void);

static void _vblank_out_handler(void *);
static void _cpu_frt_ovi_handler(void);

int
main(void)
{
        static void (*const _state_zoom_funcs[])(void) = {
                _state_zoom_move_origin,
                _state_zoom_wait,
                _state_zoom_move_anchor,
                _state_zoom_release_buttons,
                _state_zoom_select_anchor
        };

        _init();

        _sprite_config();
        _polygon_pointer_config();

        cpu_frt_count_set(0);

        while (true) {
                smpc_peripheral_process();
                smpc_peripheral_digital_port(1, &_digital);

                _state_zoom_funcs[_state_zoom]();

                _sprite_config();
                _polygon_pointer_config();

                vdp1_sync_cmdt_list_put(_cmdt_list, 0, NULL, NULL);

                dbgio_flush();

                vdp_sync();

                uint32_t result;
                result = _frame_time_calculate();

                char fixed[16];
                fix16_str(result, fixed, 7);

                dbgio_printf("[H[2J%sms\n", fixed);
        }

        return 0;
}

void
user_init(void)
{
        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A,
            VDP2_TVMD_VERT_240);

        vdp2_scrn_back_screen_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            COLOR_RGB1555(1, 0, 3, 15));

        vdp2_sprite_priority_set(0, 6);

        /* Setup default VDP1 environment */
        vdp1_env_default_set();

        cpu_intc_mask_set(0);

        vdp2_tvmd_display_set();

        vdp_sync_vblank_out_set(_vblank_out_handler);

        cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
        cpu_frt_ovi_set(_cpu_frt_ovi_handler);

        vdp1_vram_partitions_get(&_vdp1_vram_partitions);
}

static void
_init(void)
{
        dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
        dbgio_dev_font_load();
        dbgio_dev_font_load_wait();

        romdisk_init();

        _romdisk = romdisk_mount("/", root_romdisk);
        assert(_romdisk != NULL);

        _cmdt_list_init();
}

static void
_cmdt_list_init(void)
{
        static const int16_vec2_t system_clip_coord =
            INT16_VEC2_INITIALIZER(SCREEN_WIDTH - 1,
                                     SCREEN_HEIGHT - 1);

        static const int16_vec2_t local_coord_ul =
            INT16_VEC2_INITIALIZER(0,
                                      0);

        static const int16_vec2_t local_coord_center =
            INT16_VEC2_INITIALIZER(SCREEN_WIDTH / 2,
                                      SCREEN_HEIGHT / 2);

        static const vdp1_cmdt_draw_mode_t polygon_draw_mode = {
                .raw = 0x0000,
                .bits.pre_clipping_disable = true
        };

        static const int16_vec2_t polygon_points[] = {
                INT16_VEC2_INITIALIZER(0, SCREEN_HEIGHT - 1),
                INT16_VEC2_INITIALIZER(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1),
                INT16_VEC2_INITIALIZER(SCREEN_WIDTH - 1,                 0),
                INT16_VEC2_INITIALIZER(               0,                 0)
        };

        _cmdt_list = vdp1_cmdt_list_alloc(ORDER_COUNT);

        (void)memset(&_cmdt_list->cmdts[0], 0x00, sizeof(vdp1_cmdt_t) * ORDER_COUNT);

        _cmdt_list->count = ORDER_COUNT;

        vdp1_cmdt_t *cmdts;
        cmdts = &_cmdt_list->cmdts[0];

        _sprite.cmdt = &cmdts[ORDER_SPRITE_INDEX];
        _polygon_pointer.cmdt = &cmdts[ORDER_POLYGON_POINTER_INDEX];

        _polygon_pointer_init();
        _sprite_init();

        vdp1_cmdt_system_clip_coord_set(&cmdts[ORDER_SYSTEM_CLIP_COORDS_INDEX]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_SYSTEM_CLIP_COORDS_INDEX], CMDT_VTX_SYSTEM_CLIP, &system_clip_coord);

        vdp1_cmdt_local_coord_set(&cmdts[ORDER_CLEAR_LOCAL_COORDS_INDEX]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_CLEAR_LOCAL_COORDS_INDEX], CMDT_VTX_LOCAL_COORD, &local_coord_ul);

        vdp1_cmdt_polygon_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX]);
        vdp1_cmdt_param_draw_mode_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX], polygon_draw_mode);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX], CMDT_VTX_POLYGON_A, &polygon_points[0]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX], CMDT_VTX_POLYGON_B, &polygon_points[1]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX], CMDT_VTX_POLYGON_C, &polygon_points[2]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_CLEAR_POLYGON_INDEX], CMDT_VTX_POLYGON_D, &polygon_points[3]);
        vdp1_cmdt_jump_skip_next(&cmdts[ORDER_CLEAR_POLYGON_INDEX]);

        vdp1_cmdt_local_coord_set(&cmdts[ORDER_LOCAL_COORDS_INDEX]);
        vdp1_cmdt_param_vertex_set(&cmdts[ORDER_LOCAL_COORDS_INDEX], CMDT_VTX_LOCAL_COORD, &local_coord_center);

        vdp1_cmdt_end_set(&cmdts[ORDER_DRAW_END_INDEX]);
}

static void
_dma_upload(void *dst, void *src, size_t len, uint8_t tag)
{
        const scu_dma_level_cfg_t scu_dma_level_cfg = {
                .mode = SCU_DMA_MODE_DIRECT,
                .stride = SCU_DMA_STRIDE_2_BYTES,
                .update = SCU_DMA_UPDATE_NONE,
                .xfer.direct.len = len,
                .xfer.direct.dst = (uint32_t)dst,
                .xfer.direct.src = CPU_CACHE_THROUGH | (uint32_t)src
        };

        scu_dma_handle_t handle;
        scu_dma_config_buffer(&handle, &scu_dma_level_cfg);

        int8_t ret __unused;
        ret = dma_queue_enqueue(&handle, tag, NULL, NULL);
        assert(ret == 0);
}

static uint32_t
_frame_time_calculate(void)
{
        uint16_t frt;
        frt = cpu_frt_count_get();

        cpu_frt_count_set(0);

        uint32_t delta_fix;
        delta_fix = frt << 4;

        uint32_t divisor_fix;
        divisor_fix = CPU_FRT_NTSC_320_32_COUNT_1MS << 4;

        cpu_divu_32_32_set(delta_fix << 4, divisor_fix);

        /* Remember to wait ~40 cycles */
        for (uint32_t i = 0; i < 8; i++) {
                cpu_instr_nop();
                cpu_instr_nop();
                cpu_instr_nop();
                cpu_instr_nop();
                cpu_instr_nop();
        }

        uint32_t result;
        result = cpu_divu_quotient_get();
        result <<= 12;

        return result;
}

static void
_sprite_init(void)
{
        _sprite.anim_frame = 0;
        _sprite.anim_counter = 0;

        _sprite.tex_base = _vdp1_vram_partitions.texture_base;
        _sprite.pal_base = (void *)VDP2_CRAM_MODE_1_OFFSET(1, 0, 0x0000);

        const vdp1_cmdt_draw_mode_t draw_mode = {
                .bits.trans_pixel_disable = true,
                .bits.pre_clipping_disable = true,
                .bits.end_code_disable = true
        };

        const vdp1_cmdt_color_bank_t color_bank = {
                .type_0.data.dc = 0x0100
        };

        vdp1_cmdt_scaled_sprite_set(_sprite.cmdt);
        vdp1_cmdt_param_draw_mode_set(_sprite.cmdt, draw_mode);
        vdp1_cmdt_param_color_mode4_set(_sprite.cmdt, color_bank);

        vdp1_cmdt_param_size_set(_sprite.cmdt, ZOOM_POINT_WIDTH, ZOOM_POINT_HEIGHT);

        void *fh[2];
        void *p;
        size_t len;

        fh[0] = romdisk_open(_romdisk, ZOOM_TEX_PATH);
        assert(fh[0] != NULL);
        p = romdisk_direct(fh[0]);
        len = romdisk_total(fh[0]);
        _dma_upload(_sprite.tex_base, p, len, DMA_QUEUE_TAG_IMMEDIATE);

        fh[1] = romdisk_open(_romdisk, ZOOM_PAL_PATH);
        assert(fh[1] != NULL);
        p = romdisk_direct(fh[1]);
        len = romdisk_total(fh[1]);
        _dma_upload(_sprite.pal_base, p, len, DMA_QUEUE_TAG_IMMEDIATE);

        dma_queue_flush(DMA_QUEUE_TAG_IMMEDIATE);
        dma_queue_flush_wait();

        romdisk_close(fh[0]);
        romdisk_close(fh[1]);
}

static void
_sprite_config(void)
{
        const uint32_t offset =
            _sprite.anim_frame * (ZOOM_POINT_WIDTH * ZOOM_POINT_HEIGHT);

        vdp1_cmdt_param_char_base_set(_sprite.cmdt, (uint32_t)_sprite.tex_base + offset);

        vdp1_cmdt_param_zoom_set(_sprite.cmdt, _zoom_point_value);
        vdp1_cmdt_param_vertex_set(_sprite.cmdt, CMDT_VTX_ZOOM_SPRITE_POINT, &_zoom_point);
        vdp1_cmdt_param_vertex_set(_sprite.cmdt, CMDT_VTX_ZOOM_SPRITE_DISPLAY, &_display);

        if (_sprite.anim_counter == 0) {
                _sprite.anim_counter = ANIMATION_FRAME_DURATION;

                _sprite.anim_frame++;
                if (_sprite.anim_frame >= ANIMATION_FRAME_COUNT) {
                        _sprite.anim_frame = 0;
                }
        } else {
                _sprite.anim_counter--;
        }
}

static void
_polygon_pointer_init(void)
{
        static const vdp1_cmdt_draw_mode_t draw_mode = {
                .raw = 0x0000,
                .bits.pre_clipping_disable = true
        };

        vdp1_cmdt_polygon_set(_polygon_pointer.cmdt);
        vdp1_cmdt_param_draw_mode_set(_polygon_pointer.cmdt, draw_mode);
}

static void
_polygon_pointer_config(void)
{
        int16_vec2_t points[4];

        points[0].x = ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.x - 1;
        points[0].y = -ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.y;
        points[1].x = ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.x - 1;
        points[1].y = ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.y - 1;
        points[2].x = -ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.x;
        points[2].y = ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.y - 1;
        points[3].x = -ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.x;
        points[3].y = -ZOOM_POINT_POINTER_SIZE + _polygon_pointer.position.y;

        vdp1_cmdt_param_color_set(_polygon_pointer.cmdt, _polygon_pointer.color);

        vdp1_cmdt_param_vertex_set(_polygon_pointer.cmdt, CMDT_VTX_POLYGON_A, &points[0]);
        vdp1_cmdt_param_vertex_set(_polygon_pointer.cmdt, CMDT_VTX_POLYGON_B, &points[1]);
        vdp1_cmdt_param_vertex_set(_polygon_pointer.cmdt, CMDT_VTX_POLYGON_C, &points[2]);
        vdp1_cmdt_param_vertex_set(_polygon_pointer.cmdt, CMDT_VTX_POLYGON_D, &points[3]);
}

static void
_state_zoom_move_origin(void)
{
        _polygon_pointer.position.x = 0;
        _polygon_pointer.position.y = 0;

        _display.x = ZOOM_POINT_WIDTH - 1;
        _display.y = ZOOM_POINT_HEIGHT - 1;

        _zoom_point_value = VDP1_CMDT_ZOOM_POINT_CENTER;
        _zoom_point.x = 0;
        _zoom_point.y = 0;

        _polygon_pointer.color = ZOOM_POINT_COLOR_SELECT;

        _delay_frames = 0;

        if (_digital_dirs_pressed()) {
                _state_zoom = STATE_ZOOM_WAIT;
        } else if ((_digital.held.button.a) != 0) {
                _state_zoom = STATE_ZOOM_RELEASE_BUTTONS;
        }
}

static void
_state_zoom_wait(void)
{
        _delay_frames++;

        if (_delay_frames > 9) {
                _delay_frames = 0;
                _state_zoom = STATE_ZOOM_MOVE_ANCHOR;
        } else if (!(_digital_dirs_pressed())) {
                _delay_frames = 0;
                _state_zoom = STATE_ZOOM_MOVE_ORIGIN;
        }
}

static void
_state_zoom_move_anchor(void)
{
        _polygon_pointer.position.x = 0;
        _polygon_pointer.position.y = 0;

        if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_LEFT) != 0) {
                _polygon_pointer.position.x = -_display.x / 2;
        }

        if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_RIGHT) != 0) {
                _polygon_pointer.position.x = _display.x / 2;
        }

        if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_UP) != 0) {
                _polygon_pointer.position.y = -_display.y / 2;
        }

        if ((_digital.pressed.raw & PERIPHERAL_DIGITAL_DOWN) != 0) {
                _polygon_pointer.position.y = _display.y / 2;
        }

        /* Determine the zoom point */
        bool x_center;
        x_center = _polygon_pointer.position.x == 0;
        bool x_left;
        x_left = _polygon_pointer.position.x < 0;
        bool x_right;
        x_right = _polygon_pointer.position.x > 0;

        bool y_center;
        y_center = _polygon_pointer.position.y == 0;
        bool y_up;
        y_up = _polygon_pointer.position.y < 0;
        bool y_down;
        y_down = _polygon_pointer.position.y > 0;

        if (x_center && y_up) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_UPPER_CENTER;
                _zoom_point.x = 0;
                _zoom_point.y = -((ZOOM_POINT_HEIGHT / 2) - 1);
        } else if (x_center && y_down) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_LOWER_CENTER;
                _zoom_point.x = 0;
                _zoom_point.y = ZOOM_POINT_HEIGHT / 2;
        } else if (y_center && x_left) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_CENTER_LEFT;
                _zoom_point.x = -((ZOOM_POINT_WIDTH / 2) - 1);
                _zoom_point.y = 0;
        } else if (y_center && x_right) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_CENTER_RIGHT;
                _zoom_point.x = ZOOM_POINT_WIDTH / 2;
                _zoom_point.y = 0;
        } else if (y_up && x_left) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_UPPER_LEFT;
                _zoom_point.x = -((ZOOM_POINT_WIDTH / 2) - 1);
                _zoom_point.y = -((ZOOM_POINT_HEIGHT / 2) - 1);
        } else if (y_up && x_right) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_UPPER_RIGHT;
                _zoom_point.x = ZOOM_POINT_WIDTH / 2;
                _zoom_point.y = -((ZOOM_POINT_HEIGHT / 2) - 1);
        } else if (y_down && x_left) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_LOWER_LEFT;
                _zoom_point.x = -((ZOOM_POINT_WIDTH / 2) - 1);
                _zoom_point.y = ZOOM_POINT_HEIGHT / 2;
        } else if (y_down && x_right) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_LOWER_RIGHT;
                _zoom_point.x = ZOOM_POINT_WIDTH / 2;
                _zoom_point.y = ZOOM_POINT_HEIGHT / 2;
        } else if (x_center && y_center) {
                _zoom_point_value = VDP1_CMDT_ZOOM_POINT_CENTER;
                _zoom_point.x = 0;
                _zoom_point.y = 0;
        }

        if (!(_digital_dirs_pressed())) {
                _state_zoom = STATE_ZOOM_MOVE_ORIGIN;
        } else if ((_digital.held.button.a) != 0) {
                _state_zoom = STATE_ZOOM_RELEASE_BUTTONS;
        }
}

static void
_state_zoom_release_buttons(void)
{
        _polygon_pointer.color = ZOOM_POINT_COLOR_WAIT;

        if (!(_digital_dirs_pressed())) {
                _state_zoom = STATE_ZOOM_SELECT_ANCHOR;
        }
}

static void
_state_zoom_select_anchor(void)
{
        static const struct zoom_point_boundary {
                int16_t w_dir; /* Display width direction */
                int16_t h_dir; /* Display height direction */
                int16_t x_max;
                int16_t y_max;
                int16_t x_min;
                int16_t y_min;
        } _zoom_point_boundaries[] = {
                { /* Upper left */
                        .w_dir = 1,
                        .h_dir = -1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }, { /* Upper center */
                        .w_dir = 1,
                        .h_dir = -1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = SCREEN_WIDTH,
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }, { /* Upper right */
                        .w_dir = -1,
                        .h_dir = -1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }, { /* Reserved */
                        0
                }, { /* Center left */
                        .w_dir = 1,
                        .h_dir = 1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = SCREEN_HEIGHT
                }, { /* Center */
                        .w_dir = 1,
                        .h_dir = -1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = SCREEN_WIDTH,
                        .y_max = SCREEN_HEIGHT
                }, { /* Center right */
                        .w_dir = -1,
                        .h_dir = -1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = SCREEN_HEIGHT
                }, { /* Reserved */
                        0
                }, { /* Lower left */
                        .w_dir = 1,
                        .h_dir = 1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }, { /* Lower center */
                        .w_dir = 1,
                        .h_dir = 1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = SCREEN_WIDTH,
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }, { /* Lower right */
                        .w_dir = -1,
                        .h_dir = 1,
                        .x_min = 0,
                        .y_min = 0,
                        .x_max = (SCREEN_WIDTH / 2) + (ZOOM_POINT_WIDTH / 2),
                        .y_max = ZOOM_POINT_HEIGHT + ((SCREEN_HEIGHT - ZOOM_POINT_HEIGHT) / 2)
                }
        };

        _polygon_pointer.color = ZOOM_POINT_COLOR_HIGHLIGHT;

        uint32_t zp_idx;
        zp_idx = _zoom_point_value - 5;

        const struct zoom_point_boundary *zp_boundary;
        zp_boundary = &_zoom_point_boundaries[zp_idx];

        int16_t dw_dir;
        dw_dir = zp_boundary->w_dir * 1;

        int16_t dh_dir;
        dh_dir = zp_boundary->h_dir * 1;

        if ((_digital.pressed.button.up) != 0) {
                if (((_display.y + dh_dir) >= zp_boundary->y_min) &&
                    ((_display.y + dh_dir) <= zp_boundary->y_max)) {
                        _display.y = _display.y + dh_dir;
                }
        }

        if ((_digital.pressed.button.down) != 0) {
                if (((_display.y - dh_dir) >= zp_boundary->y_min) &&
                    ((_display.y - dh_dir) <= zp_boundary->y_max)) {
                        _display.y = _display.y - dh_dir;
                }
        }

        if ((_digital.pressed.button.left) != 0) {
                if (((_display.x - dw_dir) >= zp_boundary->x_min) &&
                    ((_display.x - dw_dir) <= zp_boundary->x_max)) {
                        _display.x = _display.x - dw_dir;
                }
        }

        if ((_digital.pressed.button.right) != 0) {
                if (((_display.x + dw_dir) >= zp_boundary->x_min) &&
                    ((_display.x + dw_dir) <= zp_boundary->x_max)) {
                        _display.x = _display.x + dw_dir;
                }
        }

        if ((_digital.held.button.b) != 0) {
                _state_zoom = STATE_ZOOM_MOVE_ORIGIN;
        }
}

static void
_vblank_out_handler(void *work __unused)
{
        smpc_peripheral_intback_issue();
}

static void
_cpu_frt_ovi_handler(void)
{
}
