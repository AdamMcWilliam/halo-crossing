/* Writes Animal Crossing message-window text (m_font.h's character set and
 * 0x7F control codes) so the Halo layer can put its own lines in an NPC's
 * mouth. ASCII letters, digits and common punctuation map 1:1; '\n' starts
 * a new line. The window shows four lines of about 22 characters. */
#ifndef HC_AC_TEXT_H
#define HC_AC_TEXT_H

typedef struct HcAcText {
    unsigned char* buf;
    int cap;
    int len;
    int overflow;
} HcAcText;

void hc_ac_text_begin(HcAcText* t, unsigned char* buf, int cap);
void hc_ac_text_put(HcAcText* t, const char* ascii);
/* One line in big coloured letters (scale in 32nds: 32 = normal). The
 * letters overlap the line below, so leave it empty. The voice gets louder too. */
void hc_ac_text_shout(HcAcText* t, const char* ascii, int scale, unsigned int rgb);
void hc_ac_text_player_name(HcAcText* t);
/* Wait for A, then start a fresh page. */
void hc_ac_text_page(HcAcText* t);
/* Ends the message; returns its length, or 0 if it didn't fit. */
int hc_ac_text_end(HcAcText* t);

#endif
