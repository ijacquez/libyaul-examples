/*
 * Copyright (c) 2012-2016 Israel Jacquez
 * See LICENSE for details.
 *
 * Israel Jacquez <mrkotfw@gmail.com>
 */

#ifndef ENGINE_COIN_MGR_H
#define ENGINE_COIN_MGR_H

#include "blue.h"

struct coin_mgr {
        COMPONENT_DECLARATIONS

        uint32_t coins;

        struct {
                void (*m_spawn)(struct component *, fix16_t, fix16_t, int16_t);
        } functions;

        struct {
                uint32_t m_coin;
        } private_data;
} __aligned (64);

extern void component_coin_mgr_on_init(struct component *);
extern void component_coin_mgr_on_update(struct component *);
extern void component_coin_mgr_on_draw(struct component *);
extern void component_coin_mgr_on_destroy(struct component *);

extern void component_coin_mgr_spawn(struct component *, fix16_t, fix16_t,
    int16_t);

#endif /* !ENGINE_COIN_MGR_H */
