#include "hc_ac_text.h"

#include <string.h>

#include "m_font.h"

#define RESERVE_END 2

static void put_byte(HcAcText* t, int b) {
    if (t->len >= t->cap - RESERVE_END) {
        t->overflow = 1;
        return;
    }
    t->buf[t->len++] = (unsigned char)b;
}

static void put_code(HcAcText* t, const unsigned char* code, int n) {
    if (t->len + n > t->cap - RESERVE_END) {
        t->overflow = 1;
        return;
    }
    memcpy(t->buf + t->len, code, (size_t)n);
    t->len += n;
}

static int ac_char(char c) {
    if (c == '\n') return CHAR_NEW_LINE;
    if (c == '/') return CHAR_FORWARD_SLASH;
    if (c == '#') return CHAR_HASHTAG;
    if (c == ';') return CHAR_SEMICOLON;
    if (c == '+') return CHAR_PLUS;
    if (c >= ' ' && c <= 'z' && !strchr("$*[\\]^`", c)) return c;
    return CHAR_QUESTIONMARK;
}

void hc_ac_text_begin(HcAcText* t, unsigned char* buf, int cap) {
    t->buf = buf;
    t->cap = cap;
    t->len = 0;
    t->overflow = cap < RESERVE_END;
}

void hc_ac_text_put(HcAcText* t, const char* ascii) {
    for (; *ascii; ascii++) put_byte(t, ac_char(*ascii));
}

void hc_ac_text_shout(HcAcText* t, const char* ascii, int scale, unsigned int rgb) {
    int n = 0;
    for (const char* s = ascii; *s && *s != '\n'; s++) n++;
    const unsigned char big[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_SET_LINE_SCALE, (unsigned char)scale };
    const unsigned char colour[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_SET_COLOR_CHAR, (unsigned char)(rgb >> 24),
                                     (unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)(n > 255 ? 255 : n) };
    put_code(t, big, sizeof(big));
    put_code(t, colour, sizeof(colour));
    hc_ac_text_put(t, ascii);
}

void hc_ac_text_player_name(HcAcText* t) {
    const unsigned char name[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_PUT_STRING_PLAYER_NAME };
    put_code(t, name, sizeof(name));
}

void hc_ac_text_page(HcAcText* t) {
    const unsigned char page[] = { CHAR_CONTROL_CODE, mFont_CONT_CODE_BUTTON, CHAR_CONTROL_CODE, mFont_CONT_CODE_CLEAR };
    put_code(t, page, sizeof(page));
}

int hc_ac_text_end(HcAcText* t) {
    if (t->overflow) return 0;
    t->buf[t->len++] = CHAR_CONTROL_CODE;
    t->buf[t->len++] = mFont_CONT_CODE_LAST;
    return t->len;
}
