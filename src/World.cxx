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

#include "World.hxx"
#include "Log.hxx"
#include "Bridge.hxx"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
is_parent(const Item *item, uint32_t parent_serial)
{
    switch (item->socket.cmd) {
    case PCK_ContainerUpdate:
        return item->socket.container.item.parent_serial == parent_serial;

    case PCK_Equip:
        return item->socket.mobile.parent_serial == parent_serial;

    default:
        return false;
    }
}

Item *
world_find_item(World *world,
                uint32_t serial)
{
    Item *item;

    list_for_each_entry(item, &world->items, siblings) {
        if (item->serial == serial)
            return item;
    }

    return nullptr;
}

static Item *
make_item(World *world, uint32_t serial)
{
    Item *i = world_find_item(world, serial);
    if (i != nullptr)
        return i;

    i = new Item(serial);
    list_add(&i->siblings, &world->items);

    return i;
}

void
world_world_item(World *world,
                 const struct uo_packet_world_item *p)
{
    assert(p->cmd == PCK_WorldItem);
    assert(ntohs(p->length) <= sizeof(*p));

    Item *i = make_item(world, p->serial & htonl(0x7fffffff));
    if (i == nullptr) {
        log_oom();
        return;
    }

    world_item_to_7(&i->socket.ground, p);
}

void
world_world_item_7(World *world,
                   const struct uo_packet_world_item_7 *p)
{
    assert(p->cmd == PCK_WorldItem7);

    Item *i = make_item(world, p->serial);
    if (i == nullptr) {
        log_oom();
        return;
    }

    i->socket.ground = *p;
}

void
world_equip(World *world,
            const struct uo_packet_equip *p)
{
    assert(p->cmd == PCK_Equip);

    Item *i = make_item(world, p->serial);
    if (i == nullptr) {
        log_oom();
        return;
    }

    i->socket.mobile = *p;
}

void
world_container_open(World *world,
                     const struct uo_packet_container_open *p)
{
    assert(p->cmd == PCK_ContainerOpen);

    Item *i = make_item(world, p->serial);
    if (i == nullptr) {
        log_oom();
        return;
    }

    i->packet_container_open = *p;
}

static void
world_sweep_container_update_inside(World *world,
                                    uint32_t parent_serial)
{
    const unsigned attach_sequence = world->item_attach_sequence;

    Item *item, *n;
    list_for_each_entry_safe(item, n, &world->items, siblings)
        if (is_parent(item, parent_serial) &&
            item->attach_sequence != attach_sequence)
            world_remove_item(item);
}

void
world_container_open_7(World *world,
                       const struct uo_packet_container_open_7 *p)
{
    assert(p->base.cmd == PCK_ContainerOpen);

    world_container_open(world, &p->base);
}

void
world_container_update(World *world,
                       const struct uo_packet_container_update_6 *p)
{
    assert(p->cmd == PCK_ContainerUpdate);

    Item *i = make_item(world, p->item.serial);
    if (i == nullptr) {
        log_oom();
        return;
    }

    i->socket.container = *p;
}

void
world_container_content(World *world,
                        const struct uo_packet_container_content_6 *p)
{
    assert(p->cmd == PCK_ContainerContent);

    const unsigned attach_sequence = ++world->item_attach_sequence;

    const struct uo_packet_fragment_container_item_6 *pi = p->items,
        *const end = pi + ntohs(p->num);

    for (; pi != end; ++pi) {
        Item *i = make_item(world, pi->serial);
        if (i == nullptr) {
            log_oom();
            return;
        }

        i->socket.container.cmd = PCK_ContainerUpdate;
        i->socket.container.item = *pi;
        i->attach_sequence = attach_sequence;
    }

    /* delete obsolete items; assuming that all parent_serials are the
       same, we use only the first one */
    if (p->num != 0)
        world_sweep_container_update_inside(world, p->items[0].parent_serial);
}

void
world_remove_item(Item *item) {
    assert(item != nullptr);
    assert(!list_empty(&item->siblings));

    list_del(&item->siblings);
    delete item;
}

/** deep-delete all items contained in the specified serial */
static void
remove_item_tree(World *world, uint32_t parent_serial) {
    Item *i, *n;
    struct list_head temp;

    INIT_LIST_HEAD(&temp);

    /* move all direct children to the temporary list */
    list_for_each_entry_safe(i, n, &world->items, siblings) {
        if (is_parent(i, parent_serial)) {
            /* move to temp list */
            list_del(&i->siblings);
            list_add(&i->siblings, &temp);
        }
    }

    /* delete these, and recursively delete their children */
    list_for_each_entry_safe(i, n, &temp, siblings) {
        remove_item_tree(world, i->serial);

        world_remove_item(i);
    }
}

static void
world_remove_item_serial(World *world, uint32_t serial)
{
    /* remove this entity */
    Item *i = world_find_item(world, serial);
    if (i != nullptr)
        world_remove_item(i);

    /* remove equipped items */
    remove_item_tree(world, serial);
}

Mobile::~Mobile() noexcept
{
    free(packet_mobile_status);
    free(packet_mobile_incoming);
}

static Mobile *
find_mobile(World *world, uint32_t serial)
{
    Mobile *mobile;

    list_for_each_entry(mobile, &world->mobiles, siblings) {
        if (mobile->serial == serial)
            return mobile;
    }

    return nullptr;
}

static Mobile *
add_mobile(World *world, uint32_t serial) {
    Mobile *m = find_mobile(world, serial);
    if (m != nullptr)
        return m;

    m = new Mobile(serial);

    list_add(&m->siblings, &world->mobiles);

    return m;
}

static void replace_packet(void **destp, const void *src,
                           size_t length) {
    assert(length == get_packet_length(PROTOCOL_6, src, length));

    if (*destp != nullptr)
        free(*destp);

    *destp = malloc(length);
    if (*destp == nullptr) {
        log_oom();
        return;
    }

    memcpy(*destp, src, length);
}

static void
read_equipped(World *world,
              const struct uo_packet_mobile_incoming *p)
{
    struct uo_packet_equip equip = {
        .cmd = PCK_Equip,
        .parent_serial = p->serial,
    };

    const char *const p0 = (const char*)(const void*)p;
    const char *const end = p0 + ntohs(p->length);
    const struct uo_packet_fragment_mobile_item *item = p->items;
    const char *i = (const char*)(const void*)item;

    while ((const char*)(const void*)(item + 1) <= end) {
        if (item->serial == 0)
            break;
        equip.serial = item->serial;
        equip.item_id = item->item_id & htons(0x3fff);
        equip.layer = item->layer;
        if (item->item_id & htons(0x8000)) {
            equip.hue = item->hue;
            item++;
            i = (const char*)(const void*)item;
        } else {
            equip.hue = 0;
            i += sizeof(*item) - sizeof(item->hue);
            item = (const struct uo_packet_fragment_mobile_item*)i;
        }

        world_equip(world, &equip);
    }
}

void
world_mobile_incoming(World *world,
                      const struct uo_packet_mobile_incoming *p)
{
    assert(p->cmd == PCK_MobileIncoming);

    if (p->serial == world->packet_start.serial) {
        /* update player's mobile */
        world->packet_start.body = p->body;
        world->packet_start.x = p->x;
        world->packet_start.y = p->y;
        world->packet_start.z = htons(p->z);
        world->packet_start.direction = p->direction;

        world->packet_mobile_update.body = p->body;
        world->packet_mobile_update.hue = p->hue;
        world->packet_mobile_update.flags = p->flags;
        world->packet_mobile_update.x = p->x;
        world->packet_mobile_update.y = p->y;
        world->packet_mobile_update.direction = p->direction;
        world->packet_mobile_update.z = p->z;
    }

    Mobile *m = add_mobile(world, p->serial);
    if (m == nullptr)
        return;

    replace_packet((void**)&m->packet_mobile_incoming,
                   p, ntohs(p->length));

    read_equipped(world, p);
}

void
world_mobile_status(World *world,
                    const struct uo_packet_mobile_status *p)
{
    assert(p->cmd == PCK_MobileStatus);

    Mobile *m = add_mobile(world, p->serial);
    if (m == nullptr)
        return;

    /* XXX: check if p->flags is available */
    if (m->packet_mobile_status == nullptr ||
        m->packet_mobile_status->flags <= p->flags)
        replace_packet((void**)&m->packet_mobile_status,
                       p, ntohs(p->length));
}

void
world_mobile_update(World *world,
                    const struct uo_packet_mobile_update *p)
{
    if (world->packet_start.serial == p->serial) {
        /* update player's mobile */
        world->packet_mobile_update = *p;

        world->packet_start.body = p->body;
        world->packet_start.x = p->x;
        world->packet_start.y = p->y;
        world->packet_start.z = htons(p->z);
        world->packet_start.direction = p->direction;
    }

    Mobile *m = find_mobile(world, p->serial);
    if (m == nullptr) {
        LogFormat(3, "warning in connection_mobile_update: no such mobile 0x%x\n",
                  (unsigned)ntohl(p->serial));
        return;
    }

    /* copy values to m->packet_mobile_incoming */
    if (m->packet_mobile_incoming != nullptr) {
        m->packet_mobile_incoming->body = p->body;
        m->packet_mobile_incoming->x = p->x;
        m->packet_mobile_incoming->y = p->y;
        m->packet_mobile_incoming->z = p->z;
        m->packet_mobile_incoming->direction = p->direction;
        m->packet_mobile_incoming->hue = p->hue;
        m->packet_mobile_incoming->flags = p->flags;
    }
}

void
world_mobile_moving(World *world,
                    const struct uo_packet_mobile_moving *p)
{
    if (world->packet_start.serial == p->serial) {
        /* update player's mobile */
        world->packet_start.body = p->body;
        world->packet_start.x = p->x;
        world->packet_start.y = p->y;
        world->packet_start.z = htons(p->z);
        world->packet_start.direction = p->direction;

        world->packet_mobile_update.body = p->body;
        world->packet_mobile_update.hue = p->hue;
        world->packet_mobile_update.flags = p->flags;
        world->packet_mobile_update.x = p->x;
        world->packet_mobile_update.y = p->y;
        world->packet_mobile_update.direction = p->direction;
        world->packet_mobile_update.z = p->z;
    }

    Mobile *m = find_mobile(world, p->serial);
    if (m == nullptr) {
        LogFormat(3, "warning in connection_mobile_moving: no such mobile 0x%x\n",
                  (unsigned)ntohl(p->serial));
        return;
    }

    /* copy values to m->packet_mobile_incoming */
    if (m->packet_mobile_incoming != nullptr) {
        m->packet_mobile_incoming->body = p->body;
        m->packet_mobile_incoming->x = p->x;
        m->packet_mobile_incoming->y = p->y;
        m->packet_mobile_incoming->z = p->z;
        m->packet_mobile_incoming->direction = p->direction;
        m->packet_mobile_incoming->hue = p->hue;
        m->packet_mobile_incoming->flags = p->flags;
        m->packet_mobile_incoming->notoriety = p->notoriety;
    }
}

void
world_mobile_zone(World *world,
                  const struct uo_packet_zone_change *p)
{
    world->packet_start.x = p->x;
    world->packet_start.y = p->y;
    world->packet_start.z = p->z;

    world->packet_mobile_update.x = p->x;
    world->packet_mobile_update.y = p->y;
    world->packet_mobile_update.z = ntohs(p->z);
}

void
world_remove_mobile(Mobile *mobile) {
    assert(mobile != nullptr);
    assert(!list_empty(&mobile->siblings));

    list_del(&mobile->siblings);
    delete mobile;
}

static void
world_remove_mobile_serial(World *world, uint32_t serial) {
    /* remove this entity */
    Mobile *m = find_mobile(world, serial);
    if (m != nullptr)
        world_remove_mobile(m);

    /* remove equipped items */
    remove_item_tree(world, serial);
}

void
world_remove_serial(World *world, uint32_t serial) {
    uint32_t host_serial = ntohl(serial);

    if (host_serial < 0x40000000)
        world_remove_mobile_serial(world, serial);
    else if (host_serial < 0x80000000)
        world_remove_item_serial(world, serial);
}

void
world_walked(World *world, uint16_t x, uint16_t y,
             uint8_t direction, uint8_t notoriety) {
    world->packet_start.x = x;
    world->packet_start.y = y;
    world->packet_start.direction = direction;

    world->packet_mobile_update.x = x;
    world->packet_mobile_update.y = y;
    world->packet_mobile_update.direction = direction;

    Mobile *m = find_mobile(world, world->packet_start.serial);
    if (m != nullptr && m->packet_mobile_incoming != nullptr) {
        m->packet_mobile_incoming->x = x;
        m->packet_mobile_incoming->y = y;
        m->packet_mobile_incoming->direction = direction;
        m->packet_mobile_incoming->notoriety = notoriety;
    }
}

void
world_walk_cancel(World *world, uint16_t x, uint16_t y,
                  uint8_t direction)
{
    world->packet_start.x = x;
    world->packet_start.y = y;
    world->packet_start.direction = direction;

    world->packet_mobile_update.x = x;
    world->packet_mobile_update.y = y;
    world->packet_mobile_update.direction = direction;

    Mobile *m = find_mobile(world, world->packet_start.serial);
    if (m != nullptr && m->packet_mobile_incoming != nullptr) {
        m->packet_mobile_incoming->x = x;
        m->packet_mobile_incoming->y = y;
        m->packet_mobile_incoming->direction = direction;
    }
}
