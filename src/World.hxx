/*
 * uoproxy
 *
 * Copyright 2005-2020 Max Kellermann <max.kellermann@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __UOPROXY_WORLD_H
#define __UOPROXY_WORLD_H

#include "packets.h"
#include "list.h"

struct Item {
    struct list_head siblings;

    const uint32_t serial;

    union {
        uint8_t cmd;

        /**
         * Item on the ground.
         */
        struct uo_packet_world_item_7 ground;

        /**
         * Item inside a container item.
         */
        struct uo_packet_container_update_6 container;

        /**
         * Item equipped by a mobile.
         */
        struct uo_packet_equip mobile;
    } socket;

    struct uo_packet_container_open packet_container_open{};
    unsigned attach_sequence = 0;

    explicit Item(uint32_t _serial) noexcept
        :serial(_serial)
    {
        socket.cmd = 0;
    }

    Item(const Item &) = delete;
    Item &operator=(const Item &) = delete;
};

struct Mobile {
    struct list_head siblings;

    const uint32_t serial;
    struct uo_packet_mobile_incoming *packet_mobile_incoming = nullptr;
    struct uo_packet_mobile_status *packet_mobile_status = nullptr;

    explicit Mobile(uint32_t _serial) noexcept
        :serial(_serial)
    {
    }

    ~Mobile() noexcept;

    Mobile(const Mobile &) = delete;
    Mobile &operator=(const Mobile &) = delete;
};

struct World {
    /* a lot of packets needed to attach a client*/

    struct uo_packet_start packet_start;
    struct uo_packet_map_change packet_map_change;
    struct uo_packet_map_patches packet_map_patches;
    struct uo_packet_season packet_season;
    struct uo_packet_mobile_update packet_mobile_update;
    struct uo_packet_global_light_level packet_global_light_level;
    struct uo_packet_personal_light_level packet_personal_light_level;
    struct uo_packet_war_mode packet_war_mode;
    struct uo_packet_target packet_target;

    /* mobiles in the world */

    struct list_head mobiles;

    /* items in the world */

    struct list_head items;
    unsigned item_attach_sequence;
};

Item *
world_find_item(World *world, uint32_t serial);

void
world_world_item(World *world,
                 const struct uo_packet_world_item *p);

void
world_world_item_7(World *world,
                   const struct uo_packet_world_item_7 *p);

void
world_equip(World *world,
            const struct uo_packet_equip *p);

void
world_container_open(World *world,
                     const struct uo_packet_container_open *p);

void
world_container_open_7(World *world,
                       const struct uo_packet_container_open_7 *p);

void
world_container_update(World *world,
                       const struct uo_packet_container_update_6 *p);

void
world_container_content(World *world,
                        const struct uo_packet_container_content_6 *p);

void
world_remove_item(Item *item);

void
world_mobile_incoming(World *world,
                      const struct uo_packet_mobile_incoming *p);

void
world_mobile_status(World *world,
                    const struct uo_packet_mobile_status *p);

void
world_mobile_update(World *world,
                    const struct uo_packet_mobile_update *p);

void
world_mobile_moving(World *world,
                    const struct uo_packet_mobile_moving *p);

void
world_mobile_zone(World *world,
                  const struct uo_packet_zone_change *p);

void
world_remove_mobile(Mobile *mobile);

void
world_remove_serial(World *world, uint32_t serial);

void
world_walked(World *world, uint16_t x, uint16_t y,
             uint8_t direction, uint8_t notoriety);

void
world_walk_cancel(World *world, uint16_t x, uint16_t y,
                  uint8_t direction);

#endif
