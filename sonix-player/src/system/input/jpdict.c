/*
 * jpdict.c: the lookup of hiragana readings in jpdict_data.c.
 *
 * The way a reading is matched (an entry's key exactly, or a stem and one kana
 * of its okurigana, searched from the last entry back) follows lv_ime_jp_lite.c
 * (https://github.com/fukuen/lv_port_pc_vscode, feat/ime-jp).
 * Copyright (c) 2026 fukuen.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3. It is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details (<http://www.gnu.org/licenses/>).
 */

#include "jpdict.h"

#include "jpdict_data.h"

#include <stdint.h>
#include <string.h>

// The kana an okurigana consonant stands for, indexed by the letter: the key
// "あk" is あ followed by one of か き く け こ. Same table as SKK_EXP in
// tools/gen_jpdict.py.
static const char *const OKURI[26] = {
	"あ", "ばびぶべぼ", "", "だぢづでど", "え", "", "がぎぐげご", "はひふへほ", "い",
	"", "かきくけこ", "", "まみむめも", "なにぬねの", "お", "ぱぴぷぺぽ", "", "らりるれろ",
	"さしすせそ", "たちつてとっ", "う", "", "わ", "", "やゆよ", "ざじずぜぞ",
};

// The code point of the three-byte character at `s`, or 0 when it is not one.
static unsigned head_cp(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	if ((p[0] & 0xF0) != 0xE0 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) {
		return 0;
	}
	return ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (unsigned)(p[2] & 0x3F);
}

// The run of entries that start with this kana, found by bisection.
static const jpdict_head_t *find_head(unsigned cp) {
	int lo = 0, hi = JPDICT_HEAD_COUNT - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (JPDICT_HEADS[mid].cp == cp) {
			return &JPDICT_HEADS[mid];
		}
		if (JPDICT_HEADS[mid].cp < cp) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return NULL;
}

// True when the three bytes at `tail` are one of the kana in `set`.
static bool in_set(const char *tail, const char *set) {
	for (; *set; set += 3) {
		if (memcmp(tail, set, 3) == 0) {
			return true;
		}
	}
	return false;
}

static int count_cands(const char *cands) {
	int count = 1;
	for (; *cands; cands++) {
		if (*cands == '/') {
			count++;
		}
	}
	return count;
}

bool jpdict_lookup(const char *reading, jpdict_hit_t *hit) {
	if (!reading || !hit) {
		return false;
	}
	size_t len = strlen(reading);
	if (len < 3) {
		return false;
	}
	unsigned cp = head_cp(reading);
	const jpdict_head_t *head = cp ? find_head(cp) : NULL;
	if (!head) {
		return false;
	}

	// From the last entry back, as lv_ime_jp_lite does: where two entries both
	// match, the later one is the one that is answered.
	for (int i = head->first + head->count - 1; i >= head->first; i--) {
		const char *key = JPDICT_KEYS + JPDICT_ENTRIES[i].key;
		size_t klen = strlen(key);
		size_t consumed;

		if (len == klen) {
			if (memcmp(reading, key, len) != 0) {
				continue;
			}
			consumed = len;
		} else if (len == klen + 2 && key[klen - 1] >= 'a' && key[klen - 1] <= 'z') {
			// A stem and one kana: the key's last letter is a consonant (3 bytes
			// of kana in the reading take the place of its 1 byte).
			if (memcmp(reading, key, klen - 1) != 0 || !in_set(reading + klen - 1, OKURI[key[klen - 1] - 'a'])) {
				continue;
			}
			consumed = klen - 1;
		} else {
			continue;
		}

		hit->cands = JPDICT_VALS + JPDICT_ENTRIES[i].val;
		hit->consumed = consumed;
		hit->count = count_cands(hit->cands);
		return true;
	}
	return false;
}

size_t jpdict_candidate(const jpdict_hit_t *hit, int index, char *out, size_t cap) {
	if (cap) {
		out[0] = '\0';
	}
	if (!hit || !hit->cands || index < 0 || index >= hit->count || !cap) {
		return 0;
	}
	const char *p = hit->cands;
	for (int i = 0; i < index; i++) {
		p = strchr(p, '/') + 1; // index < count, so there is a "/" to find
	}
	const char *end = strchr(p, '/');
	size_t len = end ? (size_t)(end - p) : strlen(p);
	if (len + 1 > cap) {
		return 0;
	}
	memcpy(out, p, len);
	out[len] = '\0';
	return len;
}

// True for the 3-byte hiragana at `s` (U+3041 .. U+3096), the letters a reading
// is made of.
static bool is_hiragana(const char *s) {
	unsigned cp = head_cp(s);
	return cp >= 0x3041 && cp <= 0x3096;
}

bool jpdict_lookup_before(const char *text, unsigned cursor, unsigned max_chars, jpdict_hit_t *hit, char *reading,
						  size_t cap, unsigned *chars) {
	if (!text || !hit || !reading || !chars || max_chars == 0 || cap < 3 * (size_t)max_chars + 1) {
		return false;
	}

	// The byte at which the caret stands, found by stepping over `cursor`
	// characters. A cut-off sequence ends the walk rather than running on.
	size_t at = 0;
	for (unsigned seen = 0; seen < cursor; seen++) {
		unsigned char c = (unsigned char)text[at];
		if (!c) {
			return false; // the caret is past the end
		}
		size_t step = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
		for (size_t i = 1; i < step; i++) {
			if (!text[at + i]) {
				return false;
			}
		}
		at += step;
	}

	// The run of hiragana before it, three bytes a character, newest last.
	unsigned run = 0;
	while (run < max_chars && at >= 3 * (size_t)(run + 1) && is_hiragana(text + at - 3 * (run + 1))) {
		run++;
	}

	// The longest piece of it that is in the dictionary: the whole run first,
	// then without its first character, and so on down to the last one.
	for (unsigned n = run; n >= 1; n--) {
		const char *from = text + at - 3 * (size_t)n;
		memcpy(reading, from, 3 * (size_t)n);
		reading[3 * (size_t)n] = '\0';
		if (jpdict_lookup(reading, hit)) {
			*chars = n;
			return true;
		}
	}
	return false;
}

int jpdict_entry_count(void) { return JPDICT_ENTRY_COUNT; }
