/*
================================================================================

    tinyMMO - an attempt to write a simple MMO-RPG in my spare time
    written by Sebastian Steinhauer <s.steinhauer@yahoo.de>

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or
    distribute this software, either in source code form or as a compiled
    binary, for any purpose, commercial or non-commercial, and by any
    means.

    In jurisdictions that recognize copyright laws, the author or authors
    of this software dedicate any and all copyright interest in the
    software to the public domain. We make this dedication for the benefit
    of the public at large and to the detriment of our heirs and
    successors. We intend this dedication to be an overt act of
    relinquishment in perpetuity of all present and future rights to this
    software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
    OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.

    For more information, please refer to <https://unlicense.org>

================================================================================
*/
/*==[[ Includes ]]============================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>


/*==[[ Defines / Enums ]]======================================================*/
enum {
    TICK_RATE                   = 20, // 20 ticks per second

    NETWORK_PORT                = 6502, // UDP port we want to open
    NETWORK_CLIENTS             = 1024, // maximum amount of clients we support
    NETWORK_TIMEOUT             = TICK_RATE * 10, // kick clients after 10s of silence

    VIDEO_COLS                  = 16, // tile columns
    VIDEO_ROWS                  = 16, // tile rows

    AUDIO_SOUNDS                = 32, // we have 32 sound effects
    AUDIO_TRACKS                = 8, // we have 8 music tracks
};

#define TICK_TIME               (1.0 / (double)TICK_RATE)

// button bit-masks
typedef enum {
    BUTTON_A                    = 1,
    BUTTON_B                    = 2,
    BUTTON_X                    = 4,
    BUTTON_Y                    = 8,
    BUTTON_UP                   = 16,
    BUTTON_DOWN                 = 32,
    BUTTON_LEFT                 = 64,
    BUTTON_RIGHT                = 128,
} button_t;


/*==[[ Types ]]===============================================================*/

// client structure
typedef struct client_t {
    bool                        connected; // flag if the client is connected

    // input system
    struct {
        uint8_t                 down; // buttons which are currently down
        uint8_t                 pressed; // buttons which were just pressed
    } input;
    // output system
    struct {
        uint8_t                 video[VIDEO_ROWS][VIDEO_COLS]; // screen content for the client
        uint32_t                audio; // sound effects which should play
        int8_t                  music; // which music should play
    } output;
    // network system
    struct {
        struct sockaddr_in      addr; // network address of this client
        uint64_t                last_tick; // last global tick we received data
        uint32_t                send_tick; // tick we are going to send
        uint32_t                recv_tick; // tick we have received from client
    } net;
} client_t;


/*==[[ Global State ]]========================================================*/

static struct state_t {
    bool                        running; // keep the client running
    uint64_t                    tick; // current global server tick

    struct {
        int                     udp; // UDP socket
        client_t                clients[NETWORK_CLIENTS]; // all our clients
        bool                    sorted; // is true when the client array is sorted
    } net;
} state;


/*==[[ Helper Functions ]]====================================================*/

// show error message and quit the client
static void panic(const char *fmt, ...) {
    char message[1024];
    va_list va;
    va_start(va, fmt); vsnprintf(message, sizeof(message), fmt, va); va_end(va);
    fprintf(stderr, "panic: %s\n", message);
    exit(EXIT_FAILURE);
}

// log message with timestamp
static void logger(const char *fmt, ...) {
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&(time_t){time(NULL)}));
    char message[1024];
    va_list va;
    va_start(va, fmt); vsnprintf(message, sizeof(message), fmt, va); va_end(va);
    printf("%s | %s\n", timestamp, message);
}


/*==[[ Core Game Functions ]]=================================================*/

// callback when game initializes
static void on_init(void) {
}

// callback when game should end
static void on_quit(void) {
}

// callback for global game tick
static void on_tick(void) {
}

// callback for new client
static void on_connect(client_t *client) {
    (void)client;
}

// callback when client left
static void on_disconnect(client_t *client) {
    (void)client;
}

// callback for every client tick
static void on_client(client_t *client) {
    (void)client;
}


/*==[[ Core Server Implementation ]]==========================================*/

// read 32-bit big-endian integer
static uint32_t read_uint32(const uint8_t *data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

// write 32-bit big-endian integer
static void write_uint32(uint8_t *data, const uint32_t x) {
    data[0] = (x >> 24) & 255; data[1] = (x >> 16) & 255; data[2] = (x >> 8) & 255; data[3] = x & 255;
}

// return the current time in seconds
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0);
}

// client comparator function which is used in qsort/bsearch
static int compare_clients(const void *a, const void *b) {
    return memcmp(&((const client_t*)a)->net.addr, &((const client_t*)b)->net.addr, sizeof(struct sockaddr_in));
}

// return human readable client address
static const char *client_address(const client_t *client) {
    static char buffer[128];
    char addr[128];
    inet_ntop(AF_INET, &client->net.addr.sin_addr, addr, sizeof(addr));
    snprintf(buffer, sizeof(buffer), "%s:%d", addr, ntohs(client->net.addr.sin_port));
    return buffer;
}

// create / find a client for the given address
static client_t *create_client(const struct sockaddr_in addr) {
    // make sure our clients are sorted
    if (!state.net.sorted) {
        qsort(state.net.clients, NETWORK_CLIENTS, sizeof(client_t), compare_clients);
        state.net.sorted = true;
    }
    // try to locate an existing client for this addr
    const client_t needle = { .net.addr = addr };
    client_t *client = bsearch(&needle, state.net.clients, NETWORK_CLIENTS, sizeof(client_t), compare_clients);
    if (client != NULL) return client;
    // client was not found in our list, create a new one
    // we can simply pick the first client of the array, because zero address will be sorted to the start
    client = &state.net.clients[0];
    if (client->connected) return NULL;
    *client = (client_t){ .connected = true, .net.addr = addr, .output.music = -1 };
    state.net.sorted = false;
    logger("Client %s connected", client_address(client));
    on_connect(client);
    return client;
}

// remove client from our server
static void destroy_client(client_t *client) {
    logger("Client %s disconnected", client_address(client));
    on_disconnect(client);
    *client = (client_t){0};
    state.net.sorted = false;
}

// handle a single client
static void handle_client(client_t *client) {
    // handle client logic
    on_client(client);
    // send update packet to client
    uint8_t data[1024];
    write_uint32(&data[0], ++client->net.send_tick);
    write_uint32(&data[4], client->output.audio);
    data[8] = client->output.music;
    memcpy(&data[9], client->output.video, sizeof(client->output.video));
    sendto(state.net.udp, data, sizeof(client->output.video) + 9, 0, (const struct sockaddr*)&client->net.addr, sizeof(client->net.addr));
    // reset audio and pressed state
    client->output.audio = 0;
    client->input.pressed = 0;
}

// receive UDP packets
static void receive_packets(void) {
    for (;;) {
        // receive next UDP packet if available
        uint8_t data[1024];
        struct sockaddr_in addr = {0};
        socklen_t addr_len = sizeof(addr);
        int received = recvfrom(state.net.udp, data, sizeof(data), 0, (struct sockaddr*)&addr, &addr_len);
        if (received <= 0) return;
        if (received < 5) continue;
        // find client for this packet
        client_t *client = create_client(addr);
        if (client == NULL) continue;
        // handle the client and receive the input
        const uint32_t tick = read_uint32(&data[0]);
        if (tick <= client->net.recv_tick) continue;
        client->net.last_tick = state.tick;
        client->net.recv_tick = tick;
        client->input.pressed = (~client->input.down) & data[4];
        client->input.down = data[4];
    }
}

// run server tick
static void run_tick(void) {
    // increase the global tick
    state.tick++;
    // handle the global game
    on_tick();
    // iterate over all clients
    for (int i = 0; i < NETWORK_CLIENTS; ++i) {
        client_t *client = &state.net.clients[i];
        if (!client->connected) {
            continue;
        } else if (state.tick - client->net.last_tick > NETWORK_TIMEOUT) {
            destroy_client(client);
        } else {
            handle_client(client);
        }
    }
}

// run the server
static void run_server(void) {
    on_init();
    double delta_time = 0.0;
    double last_time = get_time();
    while (state.running) {
        // advance in time
        double current_time = get_time();
        delta_time += current_time - last_time;
        last_time = current_time;
        // advance in ticks
        for (; delta_time >= TICK_TIME; delta_time -= TICK_TIME)
            run_tick();
        // receive UDP data and sleep
        receive_packets();
        usleep(10000);
    }
    on_quit();
}

// shutdown the whole server
static void quit_server(void) {
    logger("Stopping server ...");
}

// initialize the server
static void init_server(void) {
    // init state
    state = (struct state_t){ .running = true };
    atexit(quit_server);
    logger("Starting server ...");
    // open UDP server socket
    if ((state.net.udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        panic("socket() failed: %s", strerror(errno));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(NETWORK_PORT), .sin_addr.s_addr = INADDR_ANY };
    if (bind(state.net.udp, (const struct sockaddr*)&addr, sizeof(addr)))
        panic("bind() failed: %s", strerror(errno));
    if (fcntl(state.net.udp, F_SETFL, O_NONBLOCK, 1))
        panic("fcntl() failed: %s", strerror(errno));
}

// main entry point
int main(void) {
    init_server();
    run_server();
    return 0;
}


/*==[[  ]]====================================================================*/
