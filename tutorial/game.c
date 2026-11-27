#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "printf.h"
#include "wordle.h"

uintptr_t uart_base_vaddr;

#define UART_IRQ_CH 0
#define WORDLE_SERVER_CH 1
#define WORDLE_SERVER_TCB_SLOT 1
#define GAME_TCB_SLOT 2
#define GAME_PRIORITY 254
#define WORDLE_SERVER_PRIORITY 102

#define RHR_MASK 0b111111111
#define UARTDR 0x000
#define UARTFR 0x018
#define UARTIMSC 0x038
#define UARTICR 0x044
#define PL011_UARTFR_TXFF (1 << 5)
#define PL011_UARTFR_RXFE (1 << 4)

#define REG_PTR(base, offset) ((volatile uint32_t *)((base) + (offset)))

#define MOVE_CURSOR_UP "\033[5A"
#define CLEAR_TERMINAL_BELOW_CURSOR "\033[0J"
#define GREEN "\033[32;1;40m"
#define YELLOW "\033[39;103m"
#define DEFAULT_COLOUR "\033[0m"
#define INVALID_CHAR (-1)

struct wordle_char {
    int ch;
    enum character_state state;
};

static struct wordle_char table[NUM_TRIES][WORD_LENGTH];
static int curr_row = 0;
static int curr_letter = 0;

static void uart_init(void) {
    *REG_PTR(uart_base_vaddr, UARTIMSC) = 0x50;
}

static int uart_get_char(void) {
    int ch = 0;

    if ((*REG_PTR(uart_base_vaddr, UARTFR) & PL011_UARTFR_RXFE) == 0) {
        ch = *REG_PTR(uart_base_vaddr, UARTDR) & RHR_MASK;
    }

    switch (ch) {
    case '\n':
        ch = '\r';
        break;
    case 8:
        ch = 0x7f;
        break;
    }
    return ch;
}

static void uart_put_char(int ch) {
    while ((*REG_PTR(uart_base_vaddr, UARTFR) & PL011_UARTFR_TXFF) != 0);

    *REG_PTR(uart_base_vaddr, UARTDR) = ch;
    if (ch == '\r') {
        uart_put_char('\n');
    }
}

static void uart_handle_irq(void) {
    *REG_PTR(uart_base_vaddr, UARTICR) = 0x7f0;
}

static void uart_put_str(const char *str) {
    while (*str) {
        uart_put_char(*str);
        str++;
    }
}

static void set_priorities(void) {
    seL4_CPtr wordle_tcb = microkit_cspace_root_slot_to_cptr(WORDLE_SERVER_TCB_SLOT);
    seL4_CPtr game_tcb = microkit_cspace_root_slot_to_cptr(GAME_TCB_SLOT);

    seL4_Error err = seL4_TCB_SetPriority(game_tcb, wordle_tcb, GAME_PRIORITY);
    if (err != seL4_NoError) {
        printf("GAME|ERROR: raising own priority failed: %d\n", err);
    }

    err = seL4_TCB_SetPriority(wordle_tcb, game_tcb, WORDLE_SERVER_PRIORITY);
    if (err != seL4_NoError) {
        printf("GAME|ERROR: lowering wordle_server priority failed: %d\n", err);
    }
}

static void wordle_server_check(void) {
    for (int i = 0; i < WORD_LENGTH; i++) {
        seL4_SetMR(i, table[curr_row][i].ch);
    }

    seL4_MessageInfo_t reply = seL4_Call(BASE_ENDPOINT_CAP + WORDLE_SERVER_CH,
                                         seL4_MessageInfo_new(0, 0, 0, WORD_LENGTH));

    if (seL4_MessageInfo_get_length(reply) != WORD_LENGTH) {
        printf("GAME|ERROR: unexpected reply length %lu\n", seL4_MessageInfo_get_length(reply));
        return;
    }

    for (int i = 0; i < WORD_LENGTH; i++) {
        table[curr_row][i].state = seL4_GetMR(i);
    }
}

static void print_table(bool clear_terminal) {
    if (clear_terminal) {
        uart_put_str(MOVE_CURSOR_UP);
        uart_put_str(CLEAR_TERMINAL_BELOW_CURSOR);
    }

    for (int row = 0; row < NUM_TRIES; row++) {
        for (int letter = 0; letter < WORD_LENGTH; letter++) {
            uart_put_str("[");
            enum character_state state = table[row][letter].state;
            int ch = table[row][letter].ch;
            if (ch != INVALID_CHAR) {
                switch (state) {
                case INCORRECT:
                    break;
                case CORRECT_PLACEMENT:
                    uart_put_str(GREEN);
                    break;
                case INCORRECT_PLACEMENT:
                    uart_put_str(YELLOW);
                    break;
                default:
                    microkit_dbg_puts("GAME|ERROR: unexpected character state\n");
                }
                char ch_str[] = { ch, '\0' };
                uart_put_str(ch_str);
                uart_put_str(DEFAULT_COLOUR);
            } else {
                uart_put_str(" ");
            }
            uart_put_str("] ");
        }
        uart_put_str("\n");
    }
}

static void init_table(void) {
    for (int row = 0; row < NUM_TRIES; row++) {
        for (int letter = 0; letter < WORD_LENGTH; letter++) {
            table[row][letter].ch = INVALID_CHAR;
            table[row][letter].state = INCORRECT;
        }
    }
}

static bool char_is_backspace(int ch) {
    return ch == 0x7f;
}

static bool char_is_letter(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static void add_char_to_table(int c) {
    if (curr_row >= NUM_TRIES) {
        return;
    }

    if (char_is_backspace(c)) {
        if (curr_letter > 0) {
            curr_letter--;
            table[curr_row][curr_letter].ch = INVALID_CHAR;
        }
    } else if (char_is_letter(c) && curr_letter != WORD_LENGTH) {
        table[curr_row][curr_letter].ch = c;
        curr_letter++;
    }

    if (c == '\r' && curr_letter == WORD_LENGTH) {
        wordle_server_check();
        curr_row++;
        curr_letter = 0;
    }
}

void init(void) {
    set_priorities();
    uart_init();
    uart_put_str("GAME: starting\n");
    uart_put_str("Welcome to the Wordle client!\n");
    init_table();
    print_table(false);
}

void notified(microkit_channel channel) {
    if (channel != UART_IRQ_CH) {
        return;
    }

    uart_handle_irq();

    bool got_input = false;
    int ch;
    while ((ch = uart_get_char()) != 0) {
        add_char_to_table(ch);
        got_input = true;
    }

    microkit_irq_ack(channel);

    if (got_input) {
        print_table(true);
    }
}
