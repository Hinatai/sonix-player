#ifndef JPDICT_H
#define JPDICT_H

#include <stdbool.h>
#include <stddef.h>

// Hiragana to kanji candidates, with no LVGL and no allocation, so it can be
// tested on a PC like kana.c. The dictionary is the one of lv_ime_jp_lite,
// kept in flash as plain const arrays (jpdict_data.c, made by
// tools/gen_jpdict.py); nothing is built at start-up and no RAM is used.
//
// It is a reading-to-kanji list, not a sentence converter: it answers for the
// whole reading it is given, not for the best split of a longer text.

// The longest candidate in the dictionary, in bytes with the NUL. Candidates
// are copied out into a buffer of this size or more.
#define JPDICT_CAND_MAX 64

typedef struct {
	const char *cands; // internal: the candidates of the entry, "/" between them
	size_t consumed;   // bytes at the start of the reading that a candidate replaces
	int count;         // how many candidates there are (at least 1)
} jpdict_hit_t;

// Look up a reading in hiragana, UTF-8. False when there is nothing for it.
//
// Two kinds of reading match:
//   - exactly an entry's key: "あいさつ" -> 挨拶;
//   - a verb or adjective stem followed by one kana of its okurigana: "あかい"
//     matches the key "あかi" and gives 赤. The candidate replaces only the stem,
//     so `consumed` is shorter than the reading: the い stays as typed.
bool jpdict_lookup(const char *reading, jpdict_hit_t *hit);

// Candidate `index` (0 .. hit->count-1) as NUL-terminated UTF-8 in `out`. The
// length is returned; 0, and an empty string, for a bad index or a `cap` that is
// too small.
size_t jpdict_candidate(const jpdict_hit_t *hit, int index, char *out, size_t cap);

// The hiragana just before the caret, looked up the way a phone does: the
// longest run of up to `max_chars` characters that has an entry. `text` is the
// whole field and `cursor` the caret counted in characters, as
// lv_textarea_get_cursor_pos() gives it. On a hit, `reading` receives the run
// that matched (NUL-terminated, `cap` bytes, which must hold 3 * max_chars + 1)
// and `chars` how many characters of the field it is. False when the text
// before the caret does not end in hiragana, or nothing is known for it.
//
// The caller replaces those `chars` characters with a candidate and puts back
// what `reading + hit->consumed` holds: the okurigana, if any.
bool jpdict_lookup_before(const char *text, unsigned cursor, unsigned max_chars, jpdict_hit_t *hit, char *reading,
						  size_t cap, unsigned *chars);

// How much the dictionary holds, for the about page and the tests.
int jpdict_entry_count(void);

#endif /* JPDICT_H */
