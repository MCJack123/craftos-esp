#include <math.h>
#include <string.h>
#include <lauxlib.h>
#include <esp_crc.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include "../event.h"

#define TABLE_END 0x20
#define MODEM_ADDR 0xCCC28001UL

struct modem_80211_pkt {
    // 802.11
    uint16_t control;
    uint16_t duration;
    uint32_t dest_hi;
    uint16_t channel;
    uint8_t src[6];
    uint32_t filt_hi;
    uint16_t reply_channel;
    uint16_t seq;
    // message
    uint32_t id;
    uint32_t offset;
    uint32_t total_size;
    uint16_t size;
    uint8_t total_frags;
    int8_t tx_power;
    uint8_t data[];
} __packed;

struct modem_message_buf {
    uint32_t id;
    uint32_t size;
    struct modem_message_buf* next;
    uint8_t received_frags;
    uint8_t data[];
};

static EXT_RAM_ATTR uint16_t open_channels[128] = {0};
static uint8_t num_open_channels = 0;
static struct modem_message_buf* modem_message_list = NULL;

static void check_size(uint8_t** start, uint8_t** end, uint8_t** ptr, size_t cap) {
    if (*ptr + cap >= *end) {
        uint8_t* newptr = heap_caps_realloc(*start, *end - *start + cap + 1024, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        *end = newptr + (*end - *start + cap + 1024);
        *ptr = newptr + (*ptr - *start);
        *start = newptr;
    }
}

static void encode_value(lua_State *L, uint8_t** start, uint8_t** end, uint8_t** ptr) {
    int t = lua_type(L, -1);
    *(*ptr)++ = t;
    switch (t) {
        case LUA_TBOOLEAN:
            check_size(start, end, ptr, 2);
            *(*ptr)++ = lua_toboolean(L, -1);
            break;
        case LUA_TNUMBER: {
            check_size(start, end, ptr, 9);
            union {
                double d;
                struct {
                    uint32_t a;
                    uint32_t b;
                } i;
            } num;
            num.d = lua_tonumber(L, -1);
            *(*ptr)++ = (num.i.a >> 0) & 0xFF;
            *(*ptr)++ = (num.i.a >> 8) & 0xFF;
            *(*ptr)++ = (num.i.a >> 16) & 0xFF;
            *(*ptr)++ = (num.i.a >> 24) & 0xFF;
            *(*ptr)++ = (num.i.b >> 0) & 0xFF;
            *(*ptr)++ = (num.i.b >> 8) & 0xFF;
            *(*ptr)++ = (num.i.b >> 16) & 0xFF;
            *(*ptr)++ = (num.i.b >> 24) & 0xFF;
            break;
        } case LUA_TSTRING: {
            size_t sz;
            const char* str = lua_tolstring(L, -1, &sz);
            check_size(start, end, ptr, sz + 5);
            *(*ptr)++ = (sz >> 0) & 0xFF;
            *(*ptr)++ = (sz >> 8) & 0xFF;
            *(*ptr)++ = (sz >> 16) & 0xFF;
            *(*ptr)++ = (sz >> 24) & 0xFF;
            memcpy(*ptr, str, sz);
            *ptr += sz;
            break;
        } case LUA_TTABLE:
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                lua_pushvalue(L, -2); // key
                encode_value(L, start, end, ptr); // consume key
                encode_value(L, start, end, ptr); // consume value
            }
            check_size(start, end, ptr, 1);
            *(*ptr)++ = TABLE_END;
            break;
    }
    lua_pop(L, 1);
}

static void decode_value(lua_State *L, uint8_t** ptr) {
    int t = *(*ptr)++;
    switch (t) {
        case LUA_TBOOLEAN:
            lua_pushboolean(L, *(*ptr)++);
            break;
        case LUA_TNUMBER: {
            union {
                double d;
                struct {
                    uint32_t a;
                    uint32_t b;
                } i;
            } num;
            num.i.a = (*ptr)[0] | ((*ptr)[1] << 8) | ((*ptr)[2] << 16) | ((*ptr)[3] << 24);
            num.i.b = (*ptr)[4] | ((*ptr)[5] << 8) | ((*ptr)[6] << 16) | ((*ptr)[7] << 24);
            lua_pushnumber(L, num.d);
            *ptr += 8;
            break;
        } case LUA_TSTRING: {
            size_t sz = (*ptr)[0] | ((*ptr)[1] << 8) | ((*ptr)[2] << 16) | ((*ptr)[3] << 24);
            lua_pushlstring(L, (char*)*ptr + 4, sz);
            *ptr += sz + 4;
            break;
        } case LUA_TTABLE:
            lua_newtable(L);
            while (**ptr != TABLE_END) {
                decode_value(L, ptr); // key
                decode_value(L, ptr); // value
                lua_settable(L, -3);
            }
            (*ptr)++;
            break;
        default: lua_pushnil(L); break;
    }
}

static inline float get_distance(int8_t tx_power, int8_t rx_power, uint8_t channel) {
    float mwtx = pow10f(tx_power / 40.0f);
    float mwrx = pow10f(rx_power / 10.0f);
    float db = 10.0f * log10f(mwrx / mwtx);
    float freq = 2407.0f + channel * 5.0f;
    return pow10f((27.55f - 20.0f * log10f(freq) - db) / 20.0f);
}

static void modem_message_event(lua_State *L, void* arg) {
    struct modem_message_buf* buf = arg;
    uint8_t* ptr = buf->data;
    decode_value(L, &ptr);
    heap_caps_free(buf);
}

static IRAM_ATTR void modem_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    assert(type == WIFI_PKT_DATA);
    wifi_promiscuous_pkt_t* pkt_buf = buf;
    struct modem_80211_pkt* pkt = pkt_buf->payload;
    if ((pkt->control & 0xFBFF) != 0x0208 || pkt->dest_hi != MODEM_ADDR || pkt->filt_hi != MODEM_ADDR) return;
    printf("Received potential message on channel %hu\n", pkt->channel);
    bool found = false;
    for (uint8_t i = 0; i < num_open_channels; i++) if (open_channels[i] == pkt->channel) {found = true; break;}
    if (!found) return;
    if (pkt->total_frags == 1) {
        // Skip list traversal; this is the only fragment
        printf("Received single message on channel %hu\n", pkt->channel);
        struct modem_message_buf* buf = heap_caps_malloc(sizeof(struct modem_message_buf) + pkt->size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        buf->size = pkt->size;
        memcpy(buf->data, pkt->data, pkt->size);
        event_t event;
        event.type = EVENT_TYPE_MODEM_MESSAGE;
        event.modem.channel = pkt->channel;
        event.modem.replyChannel = pkt->reply_channel;
        event.modem.distance = get_distance(pkt->tx_power, pkt_buf->rx_ctrl.rssi, pkt_buf->rx_ctrl.channel);
        event.modem.message_fn = modem_message_event;
        event.modem.message_arg = buf;
        event_push(&event);
        return;
    }
    struct modem_message_buf* list = modem_message_list;
    struct modem_message_buf** last_ptr = &modem_message_list;
    while (list) {
        if (list->id == pkt->id) {
            memcpy(list->data + pkt->offset, pkt->data, pkt->size);
            if (++list->received_frags == pkt->total_frags) {
                printf("Queueing message on channel %hu\n", pkt->channel);
                *last_ptr = list->next;
                event_t event;
                event.type = EVENT_TYPE_MODEM_MESSAGE;
                event.modem.channel = pkt->channel;
                event.modem.replyChannel = pkt->reply_channel;
                event.modem.distance = get_distance(pkt->tx_power, pkt_buf->rx_ctrl.rssi, pkt_buf->rx_ctrl.channel);
                event.modem.message_fn = modem_message_event;
                event.modem.message_arg = list;
                event_push(&event);
                return;
            }
        }
        last_ptr = &list->next;
        list = list->next;
    }
    printf("Creating message on channel %hu with %hhu fragments\n", pkt->channel, pkt->total_frags);
    list = heap_caps_malloc(sizeof(struct modem_message_buf) + pkt->total_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    list->id = pkt->id;
    list->size = pkt->total_size;
    list->received_frags = 1;
    memcpy(list->data + pkt->offset, pkt->data, pkt->size);
    list->next = modem_message_list;
    modem_message_list = list;
}

static int modem_open(lua_State *L) {
    uint16_t channel = luaL_checkinteger(L, 1);
    if (num_open_channels == 128) return luaL_error(L, "Too many open channels");
    open_channels[num_open_channels] = channel;
    if (num_open_channels++ == 0) {
        wifi_promiscuous_filter_t filter;
        filter.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
        esp_wifi_set_promiscuous_filter(&filter);
        esp_wifi_set_promiscuous_rx_cb(modem_cb);
        esp_wifi_set_promiscuous(true);
    }
    return 0;
}

static int modem_isOpen(lua_State *L) {
    uint16_t channel = luaL_checkinteger(L, 1);
    for (int i = 0; i < num_open_channels; i++) {
        if (open_channels[i] == channel) {
            lua_pushboolean(L, true);
            return 1;
        }
    }
    lua_pushboolean(L, false);
    return 1;
}

static int modem_close(lua_State *L) {
    uint16_t channel = luaL_checkinteger(L, 1);
    for (int i = 0; i < num_open_channels; i++) {
        if (open_channels[i] == channel) {
            for (int j = i + 1; j < num_open_channels; j++) open_channels[j-1] = open_channels[j];
            if (--num_open_channels == 0) esp_wifi_set_promiscuous(false);
            return 0;
        }
    }
    return 0;
}

static int modem_closeAll(lua_State *L) {
    num_open_channels = 0;
    esp_wifi_set_promiscuous(false);
    return 0;
}

static int modem_transmit(lua_State *L) {
    static EXT_RAM_ATTR uint8_t modem_buf[1450];
    static struct modem_80211_pkt* pkt = modem_buf;
    uint16_t channel = luaL_checkinteger(L, 1);
    uint16_t replyChannel = luaL_checkinteger(L, 2);
    luaL_checkany(L, 3);
    lua_settop(L, 3);
    uint8_t* enc = heap_caps_malloc(1024, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    uint8_t* end = enc + 1024;
    uint8_t* ptr = enc;
    encode_value(L, &enc, &end, &ptr);
    end = ptr;
    int frag = 0;
    uint32_t id = esp_random();
    for (ptr = enc; ptr < end; ptr += 1400, frag++) {
        pkt->control = ptr + 1400 >= end ? 0x0208 : 0x0608;
        pkt->duration = 0x0000;
        pkt->dest_hi = MODEM_ADDR;
        pkt->channel = channel;
        esp_wifi_get_mac(WIFI_IF_STA, pkt->src);
        pkt->filt_hi = MODEM_ADDR;
        pkt->reply_channel = replyChannel;
        pkt->seq = frag & 0xF; // autoset
        pkt->id = id;
        pkt->offset = ptr - enc;
        pkt->total_size = end - enc;
        pkt->total_frags = pkt->total_size / 1400 + (pkt->total_size % 1400 ? 1 : 0);
        esp_wifi_get_max_tx_power(&pkt->tx_power);
        size_t sz = ptr + 1400 >= end ? end - ptr : 1400;
        pkt->size = sz;
        memcpy(pkt->data, ptr, sz);
        ESP_ERROR_CHECK(esp_wifi_80211_tx(WIFI_IF_STA, modem_buf, sizeof(struct modem_80211_pkt) + sz, true));
    }
    free(enc);
    return 0;
}

static int modem_isWireless(lua_State *L) {
    lua_pushboolean(L, true);
    return 1;
}

const luaL_Reg modem_methods[] = {
    {"open", modem_open},
    {"isOpen", modem_isOpen},
    {"close", modem_close},
    {"closeAll", modem_closeAll},
    {"transmit", modem_transmit},
    {"isWireless", modem_isWireless},
    {NULL, NULL}
};
