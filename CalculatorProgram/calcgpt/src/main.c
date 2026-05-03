// calcGPT - graphics-mode UI for the TI-84 Plus CE.
//
// Renders a chat-app style conversation log on the 320x240 framebuffer
// using the graphx library. Word-wraps each turn, paginates with the
// up/down arrow keys, and shows a composed input box when the user
// presses 2nd. Streaming reply chunks come in over USB serial.

#include <srldrvce.h>
#include <keypadc.h>
#include <graphx.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <tice.h>
#include <ti/screen.h>
#include <ti/getcsc.h>
#include <time.h>

// -----------------------------------------------------------------------------
// Layout
// -----------------------------------------------------------------------------
#define SCREEN_W       320
#define SCREEN_H       240
#define HEADER_H       16
#define FOOTER_H       16
#define BODY_TOP       HEADER_H
#define BODY_BOTTOM    (SCREEN_H - FOOTER_H)
#define BODY_H         (BODY_BOTTOM - BODY_TOP)
#define MARGIN         4
#define LINE_H         10
#define CHAR_W         8
#define CHAR_H         8

#define BODY_TEXT_LEFT   MARGIN
#define BODY_TEXT_RIGHT  (SCREEN_W - MARGIN)
#define BODY_TEXT_WIDTH  (BODY_TEXT_RIGHT - BODY_TEXT_LEFT)

#define INPUT_MAX        256

// -----------------------------------------------------------------------------
// Palette (small custom palette laid down at boot)
// -----------------------------------------------------------------------------
#define COL_BG          0
#define COL_FG          1
#define COL_HEADER_BG   2
#define COL_HEADER_FG   3
#define COL_BORDER      4
#define COL_PROMPT      5
#define COL_SPINNER     6
#define COL_DIM         7
#define COL_INPUT_BG    8

// Pack 8-bit RGB into the 1-5-5-5 palette format the LCD uses.
// Bit 15 carries the LSB of the 6-bit green channel for extra green precision;
// everything else is just the top 5 bits of each channel.
#define RGB1555(r, g, b) ((uint16_t)( \
    ((((g) >> 2) & 1) << 15) |        \
    ((((r) >> 3) & 0x1F) << 10) |     \
    ((((g) >> 3) & 0x1F) << 5)  |     \
     (((b) >> 3) & 0x1F)))

typedef enum { THEME_LIGHT = 0, THEME_DARK = 1 } theme_t;

static theme_t current_theme = THEME_LIGHT;

static void apply_theme(theme_t t)
{
    uint16_t p[9];
    if (t == THEME_DARK) {
        p[COL_BG]        = RGB1555( 24,  24,  32);
        p[COL_FG]        = RGB1555(232, 232, 232);
        p[COL_HEADER_BG] = RGB1555( 16,  48,  96);
        p[COL_HEADER_FG] = RGB1555(248, 248, 248);
        p[COL_BORDER]    = RGB1555( 80,  80,  96);
        p[COL_PROMPT]    = RGB1555(120, 224, 160);
        p[COL_SPINNER]   = RGB1555(255, 176,  64);
        p[COL_DIM]       = RGB1555(160, 160, 160);
        p[COL_INPUT_BG]  = RGB1555( 40,  40,  56);
    } else {
        p[COL_BG]        = RGB1555(248, 248, 240);
        p[COL_FG]        = RGB1555( 16,  16,  16);
        p[COL_HEADER_BG] = RGB1555( 32,  72, 144);
        p[COL_HEADER_FG] = RGB1555(248, 248, 248);
        p[COL_BORDER]    = RGB1555(160, 160, 160);
        p[COL_PROMPT]    = RGB1555( 24, 112,  56);
        p[COL_SPINNER]   = RGB1555(208, 112,  16);
        p[COL_DIM]       = RGB1555(112, 112, 112);
        p[COL_INPUT_BG]  = RGB1555(232, 232, 224);
    }
    gfx_SetPalette(p, sizeof(p), 0);
    current_theme = t;
}

static void init_palette(void)
{
    apply_theme(THEME_LIGHT);
}

// -----------------------------------------------------------------------------
// Conversation model: ring of (role, text) messages, dynamically grown.
// -----------------------------------------------------------------------------
typedef enum { ROLE_USER, ROLE_ASSISTANT } role_t;

typedef struct {
    role_t role;
    char *text;
    int   len;
    int   cap;
} message_t;

#define MAX_MESSAGES 16

static message_t conversation[MAX_MESSAGES];
static int conv_head = 0;
static int conv_count = 0;
static bool last_role_was_user = false;

static void conv_drop_oldest(void)
{
    free(conversation[conv_head].text);
    conversation[conv_head].text = NULL;
    conv_head = (conv_head + 1) % MAX_MESSAGES;
    conv_count--;
}

static int conv_add(role_t role, const char *initial)
{
    if (conv_count == MAX_MESSAGES) conv_drop_oldest();
    int idx = (conv_head + conv_count) % MAX_MESSAGES;
    int len = (int)strlen(initial);
    int cap = len + 64;
    char *buf = (char*)malloc(cap + 1);
    if (!buf) return -1;
    memcpy(buf, initial, len);
    buf[len] = 0;
    conversation[idx].role = role;
    conversation[idx].text = buf;
    conversation[idx].len  = len;
    conversation[idx].cap  = cap;
    conv_count++;
    return idx;
}

static void conv_append_last(const char *chunk)
{
    if (conv_count == 0) return;
    int idx = (conv_head + conv_count - 1) % MAX_MESSAGES;
    message_t *m = &conversation[idx];
    int add = (int)strlen(chunk);
    int new_len = m->len + add;
    if (new_len + 1 > m->cap) {
        int new_cap = new_len + 64;
        char *grown = (char*)realloc(m->text, new_cap + 1);
        if (!grown) return;
        m->text = grown;
        m->cap = new_cap;
    }
    memcpy(m->text + m->len, chunk, add);
    m->len = new_len;
    m->text[new_len] = 0;
}

// Strip trailing CR/LF before storing the user prompt.
static void conv_handle_user_prompt(const char *raw)
{
    int n = (int)strlen(raw);
    while (n > 0 && (raw[n-1] == '\n' || raw[n-1] == '\r')) n--;
    char tmp[INPUT_MAX + 1];
    if (n > INPUT_MAX) n = INPUT_MAX;
    memcpy(tmp, raw, n);
    tmp[n] = 0;
    conv_add(ROLE_USER, tmp);
    last_role_was_user = true;
}

static void conv_handle_chunk(const char *chunk)
{
    if (last_role_was_user) {
        conv_add(ROLE_ASSISTANT, chunk);
        last_role_was_user = false;
    } else if (conv_count > 0) {
        int idx = (conv_head + conv_count - 1) % MAX_MESSAGES;
        if (conversation[idx].role == ROLE_ASSISTANT) {
            conv_append_last(chunk);
        } else {
            conv_add(ROLE_ASSISTANT, chunk);
        }
    } else {
        conv_add(ROLE_ASSISTANT, chunk);
    }
}

static void conv_clear(void)
{
    while (conv_count > 0) conv_drop_oldest();
    last_role_was_user = false;
}

// -----------------------------------------------------------------------------
// Word-wrapped text rendering. Returns the y position immediately after the
// last drawn line. Lines that fall outside [BODY_TOP, BODY_BOTTOM] are skipped.
// -----------------------------------------------------------------------------
static int wrap_draw(int x, int y, int max_chars, const char *text)
{
    char line[64];
    int line_len = 0;
    int i = 0;
    while (text[i]) {
        char c = text[i];
        if (c == '\n') {
            line[line_len] = 0;
            if (y + CHAR_H > BODY_TOP && y < BODY_BOTTOM) {
                gfx_PrintStringXY(line, x, y);
            }
            y += LINE_H;
            line_len = 0;
            i++;
            continue;
        }
        if (c == ' ') {
            int j = i + 1;
            while (text[j] && text[j] != ' ' && text[j] != '\n') j++;
            int word_len = j - i - 1;
            if (line_len > 0 && line_len + 1 + word_len > max_chars) {
                line[line_len] = 0;
                if (y + CHAR_H > BODY_TOP && y < BODY_BOTTOM) {
                    gfx_PrintStringXY(line, x, y);
                }
                y += LINE_H;
                line_len = 0;
                i++;
                continue;
            }
            if (line_len == 0) { i++; continue; }
            line[line_len++] = ' ';
            i++;
            continue;
        }
        if (line_len < (int)sizeof(line) - 1) {
            line[line_len++] = c;
        }
        if (line_len >= max_chars) {
            line[line_len] = 0;
            if (y + CHAR_H > BODY_TOP && y < BODY_BOTTOM) {
                gfx_PrintStringXY(line, x, y);
            }
            y += LINE_H;
            line_len = 0;
        }
        i++;
    }
    if (line_len > 0) {
        line[line_len] = 0;
        if (y + CHAR_H > BODY_TOP && y < BODY_BOTTOM) {
            gfx_PrintStringXY(line, x, y);
        }
        y += LINE_H;
    }
    return y;
}

// Same line-counting logic as wrap_draw, but no drawing - used for layout.
static int wrap_height(int max_chars, const char *text)
{
    int lines = 0;
    int line_len = 0;
    int i = 0;
    while (text[i]) {
        char c = text[i];
        if (c == '\n') { lines++; line_len = 0; i++; continue; }
        if (c == ' ') {
            int j = i + 1;
            while (text[j] && text[j] != ' ' && text[j] != '\n') j++;
            int word_len = j - i - 1;
            if (line_len > 0 && line_len + 1 + word_len > max_chars) {
                lines++; line_len = 0; i++; continue;
            }
            if (line_len == 0) { i++; continue; }
            line_len++; i++; continue;
        }
        line_len++;
        if (line_len >= max_chars) { lines++; line_len = 0; }
        i++;
    }
    if (line_len > 0) lines++;
    return lines * LINE_H;
}

#define PREFIX_W (3 * CHAR_W)  // "Q: " or "A: "
#define MSG_TEXT_LEFT (BODY_TEXT_LEFT + PREFIX_W)
#define MSG_TEXT_CHARS ((BODY_TEXT_WIDTH - PREFIX_W) / CHAR_W)

static int draw_message(message_t *m, int y)
{
    const char *prefix = (m->role == ROLE_USER) ? "Q: " : "A: ";
    uint8_t prefix_color = (m->role == ROLE_USER) ? COL_PROMPT : COL_FG;

    if (y + CHAR_H > BODY_TOP && y < BODY_BOTTOM) {
        gfx_SetTextFGColor(prefix_color);
        gfx_SetTextBGColor(COL_BG);
        gfx_PrintStringXY(prefix, BODY_TEXT_LEFT, y);
    }
    gfx_SetTextFGColor(COL_FG);
    gfx_SetTextBGColor(COL_BG);
    int new_y = wrap_draw(MSG_TEXT_LEFT, y, MSG_TEXT_CHARS, m->text);
    return new_y + 4;
}

static int message_height(message_t *m)
{
    return wrap_height(MSG_TEXT_CHARS, m->text) + 4;
}

static int total_height(void)
{
    int h = 0;
    for (int i = 0; i < conv_count; i++) {
        int idx = (conv_head + i) % MAX_MESSAGES;
        h += message_height(&conversation[idx]);
    }
    return h;
}

// -----------------------------------------------------------------------------
// Hardcoded quick-prompt presets (F1-F5 in idle).
// Each must end with '\n' since that's what the host expects as message
// terminator on the serial link.
// -----------------------------------------------------------------------------
#define QUICK_PROMPT_COUNT 5

static const char *quick_prompts[QUICK_PROMPT_COUNT] = {
    "Explain step by step.\n",
    "Give an example.\n",
    "Summarize.\n",
    "What's the formula?\n",
    "Show me how to solve this.\n",
};

static const char *quick_prompt_labels[QUICK_PROMPT_COUNT] = {
    "Y=     Explain step by step",
    "WIN    Give an example",
    "ZOOM   Summarize",
    "TRACE  What's the formula?",
    "GRAPH  Solve this",
};

// -----------------------------------------------------------------------------
// Top-level rendering
// -----------------------------------------------------------------------------
static int  scroll_offset = 0;
static bool waiting       = false;
static bool dirty         = true;
static const char *header_status = "Connecting...";
static uint8_t spin_idx = 0;
static clock_t last_spin_tick = 0;

static void clamp_scroll(void)
{
    int total = total_height();
    int max_scroll = total - BODY_H;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;
    if (scroll_offset < 0) scroll_offset = 0;
}

static void scroll_to_bottom(void)
{
    int total = total_height();
    scroll_offset = total > BODY_H ? total - BODY_H : 0;
}

static void draw_bar(int x, int y, int w, int h, uint8_t bg)
{
    gfx_SetColor(bg);
    gfx_FillRectangle_NoClip(x, y, w, h);
}

static void draw_header(void)
{
    draw_bar(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);
    gfx_SetTextFGColor(COL_HEADER_FG);
    gfx_SetTextBGColor(COL_HEADER_BG);
    gfx_PrintStringXY("calcGPT by Bryce Joseph", MARGIN, MARGIN);
    if (header_status) {
        unsigned int sw = gfx_GetStringWidth(header_status);
        gfx_PrintStringXY(header_status, SCREEN_W - MARGIN - (int)sw, MARGIN);
    }
}

static void draw_footer(const char *hints)
{
    draw_bar(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COL_HEADER_BG);
    gfx_SetTextFGColor(COL_HEADER_FG);
    gfx_SetTextBGColor(COL_HEADER_BG);
    gfx_PrintStringXY(hints, MARGIN, SCREEN_H - FOOTER_H + MARGIN);
}

static void draw_idle(void)
{
    draw_bar(0, BODY_TOP, SCREEN_W, BODY_H, COL_BG);
    draw_header();
    draw_footer("2nd:ask  Fkeys:preset  MODE:theme");

    if (conv_count == 0) {
        gfx_SetTextFGColor(COL_FG);
        gfx_SetTextBGColor(COL_BG);
        int x = MARGIN * 3;
        int y = BODY_TOP + 12;
        gfx_PrintStringXY("Press 2nd to compose a prompt.", x, y);
        y += 18;
        gfx_SetTextFGColor(COL_DIM);
        gfx_PrintStringXY("Quick prompts:", x, y);
        y += 14;
        for (int i = 0; i < QUICK_PROMPT_COUNT; i++) {
            gfx_PrintStringXY(quick_prompt_labels[i], x + 8, y);
            y += 12;
        }
    } else {
        clamp_scroll();
        int y = BODY_TOP + MARGIN - scroll_offset;
        for (int i = 0; i < conv_count; i++) {
            int idx = (conv_head + i) % MAX_MESSAGES;
            y = draw_message(&conversation[idx], y);
            if (y > BODY_BOTTOM) break;
        }
    }

    if (waiting) {
        static const char frames[] = { '|', '/', '-', '\\' };
        char s[2] = { frames[spin_idx & 3], 0 };
        gfx_SetTextFGColor(COL_SPINNER);
        gfx_SetTextBGColor(COL_BG);
        gfx_PrintStringXY(s, SCREEN_W - MARGIN - CHAR_W,
                          BODY_BOTTOM - CHAR_H - MARGIN);
    }
}

// -----------------------------------------------------------------------------
// Compose / on-screen keyboard
// -----------------------------------------------------------------------------
typedef enum { MODE_LOWER = 0, MODE_UPPER = 1, MODE_NUM = 2, MODE_SYM = 3 } input_mode_t;
#define MODE_COUNT 4

#define KBD_ROWS    3
#define KBD_COLS    10
#define KBD_CELL_W  30
#define KBD_CELL_H  24
#define KBD_GAP     1
#define KBD_W       (KBD_CELL_W * KBD_COLS + KBD_GAP * (KBD_COLS - 1))
#define KBD_X       ((SCREEN_W - KBD_W) / 2)
#define KBD_Y       128

// 30 cells per mode (3 rows of 10). Last cell is space - rendered as "_".
static const char kbd_chars[MODE_COUNT][KBD_ROWS * KBD_COLS] = {
    {  // MODE_LOWER
        'a','b','c','d','e','f','g','h','i','j',
        'k','l','m','n','o','p','q','r','s','t',
        'u','v','w','x','y','z','.',',','?',' '
    },
    {  // MODE_UPPER
        'A','B','C','D','E','F','G','H','I','J',
        'K','L','M','N','O','P','Q','R','S','T',
        'U','V','W','X','Y','Z','.',',','?',' '
    },
    {  // MODE_NUM
        '0','1','2','3','4','5','6','7','8','9',
        '+','-','*','/','^','(',')','=','.',',',
        '<','>','[',']','"','\'','?','!','_',' '
    },
    {  // MODE_SYM - punctuation and shell-style symbols
        '$','%','&','@','#','!','?',':',';',',',
        '.','"','\'','`','~','|','\\','_','-','+',
        '(',')','[',']','{','}','<','>','=',' '
    },
};

static const char *mode_label(input_mode_t m)
{
    switch (m) {
        case MODE_UPPER: return "[ABC]";
        case MODE_NUM:   return "[123]";
        case MODE_SYM:   return "[sym]";
        case MODE_LOWER:
        default:         return "[abc]";
    }
}

static void draw_keyboard(input_mode_t mode, int sel_row, int sel_col)
{
    for (int r = 0; r < KBD_ROWS; r++) {
        for (int c = 0; c < KBD_COLS; c++) {
            int x = KBD_X + c * (KBD_CELL_W + KBD_GAP);
            int y = KBD_Y + r * (KBD_CELL_H + KBD_GAP);
            char ch = kbd_chars[mode][r * KBD_COLS + c];

            bool selected = (r == sel_row && c == sel_col);
            uint8_t cell_bg = selected ? COL_HEADER_BG : COL_INPUT_BG;
            uint8_t cell_fg = selected ? COL_HEADER_FG : COL_FG;

            gfx_SetColor(cell_bg);
            gfx_FillRectangle_NoClip(x, y, KBD_CELL_W, KBD_CELL_H);
            gfx_SetColor(COL_BORDER);
            gfx_Rectangle_NoClip(x, y, KBD_CELL_W, KBD_CELL_H);

            // Render space as a visible "_" so the cell isn't blank.
            char display[2];
            display[0] = (ch == ' ') ? '_' : ch;
            display[1] = 0;

            int tx = x + (KBD_CELL_W - CHAR_W) / 2;
            int ty = y + (KBD_CELL_H - CHAR_H) / 2;
            gfx_SetTextFGColor(cell_fg);
            gfx_SetTextBGColor(cell_bg);
            gfx_PrintStringXY(display, tx, ty);
        }
    }
}

static void draw_compose(input_mode_t mode, const char *buf,
                         int sel_row, int sel_col, bool show_caret)
{
    gfx_FillScreen(COL_BG);

    // Header
    draw_bar(0, 0, SCREEN_W, HEADER_H, COL_HEADER_BG);
    gfx_SetTextFGColor(COL_HEADER_FG);
    gfx_SetTextBGColor(COL_HEADER_BG);
    gfx_PrintStringXY("calcGPT by Bryce Joseph", MARGIN, MARGIN);
    const char *lbl = mode_label(mode);
    unsigned int lw = gfx_GetStringWidth(lbl);
    gfx_PrintStringXY(lbl, SCREEN_W - MARGIN - (int)lw, MARGIN);

    // Input box (above the keyboard)
    int box_x = MARGIN * 2;
    int box_y = HEADER_H + 8;
    int box_w = SCREEN_W - 4 * MARGIN;
    int box_h = KBD_Y - box_y - 8;
    draw_bar(box_x, box_y, box_w, box_h, COL_INPUT_BG);
    gfx_SetColor(COL_BORDER);
    gfx_Rectangle_NoClip(box_x, box_y, box_w, box_h);

    // Typed text + blinking caret
    char with_caret[INPUT_MAX + 2];
    int blen = (int)strlen(buf);
    if (blen > INPUT_MAX) blen = INPUT_MAX;
    memcpy(with_caret, buf, blen);
    if (show_caret) with_caret[blen++] = '_';
    with_caret[blen] = 0;

    int max_chars_in_box = (box_w - 2 * MARGIN) / CHAR_W;
    gfx_SetTextFGColor(COL_FG);
    gfx_SetTextBGColor(COL_INPUT_BG);
    wrap_draw(box_x + MARGIN, box_y + MARGIN, max_chars_in_box, with_caret);

    // On-screen keyboard
    draw_keyboard(mode, sel_row, sel_col);

    // Mid-hint between keyboard and footer
    gfx_SetTextFGColor(COL_DIM);
    gfx_SetTextBGColor(COL_BG);
    gfx_PrintStringXY("ALPHA: cycle modes    MODE: cancel",
                      MARGIN,
                      KBD_Y + KBD_ROWS * (KBD_CELL_H + KBD_GAP) + 4);

    // Footer
    draw_footer("arrows:nav 2nd:type ENTER:send DEL:bksp");

    gfx_SwapDraw();
}

char* takeInput(void)
{
    input_mode_t mode = MODE_LOWER;
    int sel_row = 0, sel_col = 0;
    int len = 0;
    int cap = 64;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    buf[0] = 0;

    // Drain any pending key events so the 2nd press that brought us here
    // doesn't immediately register as "type the highlighted cell".
    while (os_GetCSC()) { /* spin */ }

    bool caret_on = true;
    bool dirty_kbd = true;
    clock_t last_blink = clock();

    draw_compose(mode, buf, sel_row, sel_col, caret_on);

    for (;;) {
        clock_t now = clock();
        if (now - last_blink > CLOCKS_PER_SEC / 2) {
            caret_on = !caret_on;
            last_blink = now;
            dirty_kbd = true;
        }
        if (dirty_kbd) {
            draw_compose(mode, buf, sel_row, sel_col, caret_on);
            dirty_kbd = false;
        }

        uint8_t key = os_GetCSC();
        if (!key) continue;

        // ENTER sends the prompt.
        if (key == sk_Enter) break;
        // MODE cancels.
        if (key == sk_Mode)  { free(buf); return NULL; }

        // Arrow keys navigate the on-screen keyboard.
        if (key == sk_Up    && sel_row > 0)             { sel_row--; dirty_kbd = true; continue; }
        if (key == sk_Down  && sel_row < KBD_ROWS - 1)  { sel_row++; dirty_kbd = true; continue; }
        if (key == sk_Left  && sel_col > 0)             { sel_col--; dirty_kbd = true; continue; }
        if (key == sk_Right && sel_col < KBD_COLS - 1)  { sel_col++; dirty_kbd = true; continue; }

        // ALPHA cycles abc -> ABC -> 123 -> sym -> abc.
        if (key == sk_Alpha) {
            mode = (input_mode_t)((mode + 1) % MODE_COUNT);
            dirty_kbd = true;
            continue;
        }

        // DEL/CLEAR backspace.
        if (key == sk_Del || key == sk_Clear) {
            if (len > 0) {
                len--;
                buf[len] = 0;
                dirty_kbd = true;
            }
            continue;
        }

        // 2nd types the highlighted character.
        if (key == sk_2nd) {
            char ch = kbd_chars[mode][sel_row * KBD_COLS + sel_col];
            if (!ch) continue;
            if (len + 3 >= cap) {
                int new_cap = (cap < INPUT_MAX) ? cap * 2 : cap;
                if (new_cap > INPUT_MAX) new_cap = INPUT_MAX;
                if (len + 3 >= new_cap) continue;
                char *grown = (char*)realloc(buf, new_cap);
                if (!grown) { free(buf); return NULL; }
                buf = grown;
                cap = new_cap;
            }
            buf[len++] = ch;
            buf[len] = 0;
            dirty_kbd = true;
            continue;
        }
    }

    if (len + 2 > cap) {
        char *grown = (char*)realloc(buf, len + 2);
        if (!grown) { free(buf); return NULL; }
        buf = grown;
    }
    buf[len] = '\n';
    buf[len + 1] = 0;
    return buf;
}

// -----------------------------------------------------------------------------
// USB / serial setup (preserved from the original)
// -----------------------------------------------------------------------------
srl_device_t srl;
bool has_srl_device = false;
uint8_t srl_buf[512];

static usb_error_t handle_usb_event(usb_event_t event, void *event_data,
                                    usb_callback_data_t *callback_data __attribute__((unused)))
{
    usb_error_t err;
    if ((err = srl_UsbEventCallback(event, event_data, callback_data)) != USB_SUCCESS)
        return err;

    if (event == USB_DEVICE_CONNECTED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE)) {
        usb_device_t device = event_data;
        usb_ResetDevice(device);
    }

    if (event == USB_HOST_CONFIGURE_EVENT ||
        (event == USB_DEVICE_ENABLED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE))) {
        if (has_srl_device) return USB_SUCCESS;
        usb_device_t device;
        if (event == USB_HOST_CONFIGURE_EVENT) {
            device = usb_FindDevice(NULL, NULL, USB_SKIP_HUBS);
            if (device == NULL) return USB_SUCCESS;
        } else {
            device = event_data;
        }
        srl_error_t error = srl_Open(&srl, device, srl_buf, sizeof srl_buf,
                                     SRL_INTERFACE_ANY, 9600);
        if (error) {
            header_status = "srl err";
            dirty = true;
            return USB_SUCCESS;
        }
        has_srl_device = true;
        header_status = "Ready";
        dirty = true;
    }

    if (event == USB_DEVICE_DISCONNECTED_EVENT) {
        usb_device_t device = event_data;
        if (device == srl.dev) {
            srl_Close(&srl);
            has_srl_device = false;
            header_status = "Disconnected";
            dirty = true;
        }
    }
    return USB_SUCCESS;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
// Send a prompt as if the user had typed it. Used by both the 2nd-key compose
// path and the F-key quick-prompt path.
static void send_prompt_text(const char *text)
{
    if (!has_srl_device) return;
    conv_handle_user_prompt(text);
    srl_Write(&srl, text, strlen(text));
    waiting = true;
    last_spin_tick = clock();
    header_status = "Streaming";
    scroll_to_bottom();
    dirty = true;
}

int main(void)
{
    bool prompt_key_held = false;
    bool up_held = false;
    bool down_held = false;
    bool mode_held = false;
    bool first_chunk_seen = false;
    bool fkey_held[QUICK_PROMPT_COUNT] = { false, false, false, false, false };
    static const uint8_t fkey_masks[QUICK_PROMPT_COUNT] = {
        kb_Yequ, kb_Window, kb_Zoom, kb_Trace, kb_Graph
    };

    // Set up graphics first so even early errors show on the new UI.
    gfx_Begin();
    init_palette();
    gfx_SetDrawBuffer();
    gfx_FillScreen(COL_BG);
    draw_header();
    draw_footer("Connecting...");
    gfx_SwapDraw();

    const usb_standard_descriptors_t *desc = srl_GetCDCStandardDescriptors();
    usb_error_t usb_error = usb_Init(handle_usb_event, NULL, desc, USB_DEFAULT_INIT_FLAGS);
    if (usb_error) {
        usb_Cleanup();
        gfx_End();
        os_ClrHome();
        printf("usb init error %u\n", usb_error);
        do kb_Scan(); while (!kb_IsDown(kb_KeyClear));
        return 1;
    }

    do {
        kb_Scan();
        usb_HandleEvents();

        if (has_srl_device) {
            char in_buf[65];
            memset(in_buf, 0, sizeof(in_buf));
            size_t n = srl_Read(&srl, in_buf, 64);
            if (n > 0) {
                in_buf[n] = 0;
                if (!first_chunk_seen) {
                    // The host's "ack" handshake byte; don't store it.
                    first_chunk_seen = true;
                    header_status = "Connected";
                } else {
                    if (waiting) {
                        waiting = false;
                        header_status = "Connected";
                    }
                    conv_handle_chunk(in_buf);
                    scroll_to_bottom();
                }
                dirty = true;
            }
        }

        // Spinner tick at 4Hz while waiting.
        if (waiting) {
            clock_t now = clock();
            if (now - last_spin_tick > CLOCKS_PER_SEC / 4) {
                last_spin_tick = now;
                spin_idx++;
                dirty = true;
            }
        }

        // 2nd: open the compose screen.
        if ((kb_Data[1] & kb_2nd) && !prompt_key_held) {
            prompt_key_held = true;
            if (has_srl_device && first_chunk_seen && !waiting) {
                char *temp = takeInput();
                if (temp) {
                    send_prompt_text(temp);
                    free(temp);
                }
                dirty = true;
                // Suppress further key triggers until everything is released
                // so the MODE-cancel or 2nd-type press doesn't immediately
                // re-fire here as a theme toggle / new compose.
                mode_held = true;
                for (int i = 0; i < QUICK_PROMPT_COUNT; i++) fkey_held[i] = true;
            }
        } else if (!(kb_Data[1] & kb_2nd)) {
            prompt_key_held = false;
        }

        // F-keys (Y=, WINDOW, ZOOM, TRACE, GRAPH): send a hardcoded prompt.
        if (has_srl_device && first_chunk_seen && !waiting) {
            for (int i = 0; i < QUICK_PROMPT_COUNT; i++) {
                if ((kb_Data[1] & fkey_masks[i]) && !fkey_held[i]) {
                    fkey_held[i] = true;
                    send_prompt_text(quick_prompts[i]);
                } else if (!(kb_Data[1] & fkey_masks[i])) {
                    fkey_held[i] = false;
                }
            }
        } else {
            // Still need to release-detect or the held flag sticks.
            for (int i = 0; i < QUICK_PROMPT_COUNT; i++) {
                if (!(kb_Data[1] & fkey_masks[i])) fkey_held[i] = false;
            }
        }

        // MODE: toggle theme (light <-> dark).
        if ((kb_Data[1] & kb_Mode) && !mode_held) {
            mode_held = true;
            apply_theme(current_theme == THEME_LIGHT ? THEME_DARK : THEME_LIGHT);
            dirty = true;
        } else if (!(kb_Data[1] & kb_Mode)) {
            mode_held = false;
        }

        // Down arrow: scroll forward one half-page.
        if ((kb_Data[7] & kb_Down) && !down_held) {
            down_held = true;
            scroll_offset += BODY_H / 2;
            clamp_scroll();
            dirty = true;
        } else if (!(kb_Data[7] & kb_Down)) {
            down_held = false;
        }
        if ((kb_Data[7] & kb_Up) && !up_held) {
            up_held = true;
            scroll_offset -= BODY_H / 2;
            clamp_scroll();
            dirty = true;
        } else if (!(kb_Data[7] & kb_Up)) {
            up_held = false;
        }

        if (dirty) {
            draw_idle();
            gfx_SwapDraw();
            dirty = false;
        }

    } while (!kb_IsDown(kb_KeyClear));

    conv_clear();
    usb_Cleanup();
    gfx_End();
    return 0;
}
