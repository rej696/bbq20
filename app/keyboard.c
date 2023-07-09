#include "app_config.h"
#include "fifo.h"
#include "keyboard.h"
#include "reg.h"

#include <pico/stdlib.h>

#define LIST_SIZE	10 // size of the list keeping track of all the pressed keys

// Keys are defined as entries, with three possible values
struct entry
{
	char chr;
	char alt;
	enum key_mod mod;
};

// list_item represents a keypress, giving a start time, corresponding key
// (entry) and state, array of applied mods, and an "effective_key" which is the
// key that is resolved after applying the mods
struct list_item
{
	const struct entry *p_entry;
	uint32_t hold_start_time;
	enum key_state state;
	bool mods[KEY_MOD_ID_LAST];
	char effective_key;
};

// Array of the pin numbers for each row
static const uint8_t row_pins[NUM_OF_ROWS] =
{
	PINS_ROWS
};

// Array of the pin numbers for each column
static const uint8_t col_pins[NUM_OF_COLS] =
{
	PINS_COLS
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// kbd_entries is effectively a keymap using a 2d array (rows x columns) of the
// entry data structure. It can be seen that the rows and columns don't match
// the rows and columns of the keyboard, so it isn't an ideal abstraction for mapping keys.
static const struct entry kbd_entries[][NUM_OF_COLS] =
{
	{ { KEY_JOY_CENTER },  { 'W', '1' },              { 'G', '/' },              { 'S', '4' },              { 'L', '"'  },  { 'H' , ':' } },
	{ { },                 { 'Q', '#' },              { 'R', '3' },              { 'E', '2' },              { 'O', '+'  },  { 'U', '_'  } },
	{ { KEY_BTN_LEFT1 },   { '~', '0' },              { 'F', '6' },              { .mod = KEY_MOD_ID_SHL }, { 'K', '\''  }, { 'J', ';'  } },
	{ { },                 { ' ', '\t' },             { 'C', '9' },              { 'Z', '7' },              { 'M', '.'  },  { 'N', ','  } },
	{ { KEY_BTN_LEFT2 },   { .mod = KEY_MOD_ID_SYM }, { 'T', '(' },              { 'D', '5' },              { 'I', '-'  },  { 'Y', ')'  } },
	{ { KEY_BTN_RIGHT1 },  { .mod = KEY_MOD_ID_ALT }, { 'V', '?' },              { 'X', '8' },              { '$', '`'  },  { 'B', '!'  } },
	{ { },                 { 'A', '*' },              { .mod = KEY_MOD_ID_SHR }, { 'P', '@' },              { '\b' },       { '\n', '|' } },
};

static const char button_map[4][4] = {
    { 0x1B, '&', '=', '\\' },
    { '*', '%', '[', ']' },
    { '<', '>', '{', '}' },
    { KEY_JOY_LEFT, KEY_JOY_DOWN, KEY_JOY_UP, KEY_JOY_RIGHT }
};

// additional datastructure handled similarly to kbd_entries but for buttons?
// uncertain what buttons these are, as the top row buttons (e.g. blackberry
// button, end call button) seem to be handled in kbd_entries.
#if NUM_OF_BTNS > 0
static const struct entry btn_entries[NUM_OF_BTNS] =
{
	BTN_KEYS
};

static const uint8_t btn_pins[NUM_OF_BTNS] =
{
	PINS_BTNS
};
#endif

#pragma GCC diagnostic pop

// global persistant data for the keyboard (i.e. self)
static struct
{
        // linked lists of callbacks for different modules to handle events
        // (i.e. usb, interrupts)
        struct key_lock_callback *lock_callbacks;
	struct key_callback *key_callbacks;

        // list of keypress events
	struct list_item list[LIST_SIZE];

        // bitfield of modifiers currently applied to the keyboard (if sending
        // the modifiers over usb, may want to remove / modify this)
        bool mods[KEY_MOD_ID_LAST];

        // flags for handling locks
	bool capslock_changed;
	bool capslock;

	bool numlock_changed;
	bool numlock;
} self;

static void transition_to(struct list_item * const p_item, const enum key_state next_state)
{
	const struct entry * const p_entry = p_item->p_entry;

	p_item->state = next_state;

	if (!p_entry)
		return;

        // resolve the "effective_key" after applying modifiers
	if (p_item->effective_key == '\0') {
		char key = p_entry->chr;
                // send a modifier key if configured to report mods
		switch (p_entry->mod) {
			case KEY_MOD_ID_ALT:
				if (reg_is_bit_set(REG_ID_CFG, CFG_REPORT_MODS))
					key = KEY_MOD_ALT;
				break;

			case KEY_MOD_ID_SHL:
				if (reg_is_bit_set(REG_ID_CFG, CFG_REPORT_MODS))
					key = KEY_MOD_SHL;
				break;

			case KEY_MOD_ID_SHR:
				if (reg_is_bit_set(REG_ID_CFG, CFG_REPORT_MODS))
					key = KEY_MOD_SHR;
				break;

			case KEY_MOD_ID_SYM:
				if (reg_is_bit_set(REG_ID_CFG, CFG_REPORT_MODS))
					key = KEY_MOD_SYM;
				break;

			default:
			{
				if (reg_is_bit_set(REG_ID_CFG, CFG_USE_MODS)) {
                                        // check which modifiers are currently applied/active
					const bool shift = (self.mods[KEY_MOD_ID_SHL] || self.mods[KEY_MOD_ID_SHR]) | self.capslock;
					const bool alt = self.mods[KEY_MOD_ID_ALT] | self.numlock;
					/* const bool is_button = (key <= KEY_BTN_RIGHT1) || ((key >= KEY_BTN_LEFT2) && (key <= KEY_BTN_RIGHT2)); */
					const bool is_button = ((key == KEY_BTN_RIGHT1)
                                            || (key == KEY_BTN_RIGHT2)
                                            || (key == KEY_BTN_LEFT1)
                                            || (key == KEY_BTN_LEFT2));
                                        const bool ctrl = self.mods[KEY_MOD_ID_SYM];

                                        if (is_button) {
                                            int idx = 0;
                                            if (alt) idx = 1;
                                            if (shift) idx = 2;
                                            if (ctrl) idx = 3;

                                            int btn = 0;
                                            switch (key) {
                                                case KEY_BTN_LEFT1:
                                                    btn = 0;
                                                    break;
                                                case KEY_BTN_LEFT2:
                                                    btn = 1;
                                                    break;
                                                case KEY_BTN_RIGHT1:
                                                    btn = 2;
                                                    break;
                                                case KEY_BTN_RIGHT2:
                                                    btn = 3;
                                                    break;
                                                default:
                                                    // unreachable
                                                    break;
                                            }

                                            key = button_map[idx][btn];
                                        }

                                        // "apply" modifiers
					if (alt && !is_button) {
						key = p_entry->alt;
					} else if ((key >= 'A') && (key <= 'Z')) {
                                            if (ctrl) {
                                                // send control key by subtracting 0x40
                                                key = key - 0x40;
                                            } else if (!shift) {
                                                // set non-shifted letters to lowercase by adding a space (0x32)
						key = (key + ' ');
                                            } else {
                                                // uppercase do nothing
                                            }
					}
				}

				break;
			}
		}

                // set the "effective key" to the resulting key
		p_item->effective_key = key;
	}

	if (p_item->effective_key == '\0')
		return;

        // inject the keypress event into the public fifo and send over usb
	keyboard_inject_event(p_item->effective_key, next_state);
}

static void next_item_state(struct list_item * const p_item, const bool pressed)
{
        // State machine for keypress events (i.e. hold, pressed, etc.). The
        // state is propagated with the key for use in the logic of the callback
        switch (p_item->state) {
		case KEY_STATE_IDLE:
			if (pressed) {
                                // TODO may be able to handle layer toggling in here?
                                // if the key is a modifier, set the mods bitfield accordingly
				if (p_item->p_entry->mod != KEY_MOD_ID_NONE)
					self.mods[p_item->p_entry->mod] = true;

                                // handle capslock enable
				if (!self.capslock_changed && self.mods[KEY_MOD_ID_SHR] && self.mods[KEY_MOD_ID_ALT]) {
					self.capslock = true;
					self.capslock_changed = true;
				}

                                // handle numlock enable
				if (!self.numlock_changed && self.mods[KEY_MOD_ID_SHL] && self.mods[KEY_MOD_ID_ALT]) {
					self.numlock = true;
					self.numlock_changed = true;
				}

                                // handle capslock disable
				if (!self.capslock_changed && (self.mods[KEY_MOD_ID_SHL] || self.mods[KEY_MOD_ID_SHR])) {
					self.capslock = false;
					self.capslock_changed = true;
				}

                                // handle numlock disable
				if (!self.numlock_changed && (self.mods[KEY_MOD_ID_SHL] || self.mods[KEY_MOD_ID_SHR])) {
					self.numlock = false;
					self.numlock_changed = true;
				}

                                // set "changed" flags to false when alt key nolonger held
				if (!self.mods[KEY_MOD_ID_ALT]) {
					self.capslock_changed = false;
					self.numlock_changed = false;
				}

                                // if either of the lock events are changed,
                                // handle all the "key_lock" callbacks. note
                                // there is no usb callback for the key_lock, so
                                // nothing is sent to the computer
                                if (self.lock_callbacks && (self.capslock_changed || self.numlock_changed)) {
					struct key_lock_callback *cb = self.lock_callbacks;
					while (cb) {
						cb->func(self.capslock_changed, self.numlock_changed);

						cb = cb->next;
					}
				}

                                // transition the state machine and handle the keypress
				transition_to(p_item, KEY_STATE_PRESSED);

				p_item->hold_start_time = to_ms_since_boot(get_absolute_time());
			}
			break;

		case KEY_STATE_PRESSED:
			if ((to_ms_since_boot(get_absolute_time()) - p_item->hold_start_time) > (reg_get_value(REG_ID_HLD) * 10)) {
				transition_to(p_item, KEY_STATE_HOLD);
			 } else if(!pressed) {
				transition_to(p_item, KEY_STATE_RELEASED);
			}
			break;

		case KEY_STATE_HOLD:
			if (!pressed)
				transition_to(p_item, KEY_STATE_RELEASED);
			break;

		case KEY_STATE_RELEASED:
		{
			if (p_item->p_entry->mod != KEY_MOD_ID_NONE)
				self.mods[p_item->p_entry->mod] = false;

			p_item->p_entry = NULL;
			p_item->effective_key = '\0';
			transition_to(p_item, KEY_STATE_IDLE);
			break;
		}
	}
}

// Button matrix key scanning, (i.e. also where the magic happens)
// will scan the matrix periodically according to the timer
static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;

        // Loop over all columns
	for (uint32_t c = 0; c < NUM_OF_COLS; ++c) {
                // set up the gpio for the column
		gpio_pull_up(col_pins[c]);
		gpio_put(col_pins[c], 0);
		gpio_set_dir(col_pins[c], GPIO_OUT);


                // Loop over the rows in the column
		for (uint32_t r = 0; r < NUM_OF_ROWS; ++r) {
                        // Check if the specific button is pressed
			const bool pressed = (gpio_get(row_pins[r]) == 0);
                        // Get the overall index of the key
			const int32_t key_idx = (int32_t)((r * NUM_OF_COLS) + c);

                        // if the key is already in the list (i.e. it
                        // has already been pressed) find the index of the item
                        // in the list, and use that list_item to update the state machine
                        int32_t list_idx = -1;
			for (int32_t i = 0; i < LIST_SIZE; ++i) {
				if (self.list[i].p_entry != &((const struct entry*)kbd_entries)[key_idx])
					continue;

				list_idx = i;
				break;
			}

                        // update the state machine for the list item if one was found
			if (list_idx > -1) {
				next_item_state(&self.list[list_idx], pressed);
				continue;
			}

                        // if the button is not pressed do nothing
			if (!pressed)
				continue;

                        // if the button is pressed, find the first available
                        // empty slot in the list, and populate that list item
                        // with the data for the button. If there is no avaiable
                        // space in the list, then the keypress is ignored.
                        for (uint32_t i = 0 ; i < LIST_SIZE; ++i) {
				if (self.list[i].p_entry != NULL)
					continue;

				self.list[i].p_entry = &((const struct entry*)kbd_entries)[key_idx];
				self.list[i].effective_key = '\0';
				self.list[i].state = KEY_STATE_IDLE;
				next_item_state(&self.list[i], pressed);

				break;
			}
		}

                // turn of the gpio for the column
		gpio_put(col_pins[c], 1);
		gpio_disable_pulls(col_pins[c]);
		gpio_set_dir(col_pins[c], GPIO_IN);
	}

        // Do the same as above, but for buttons
#if NUM_OF_BTNS > 0
	for (uint32_t b = 0; b < NUM_OF_BTNS; ++b) {
		const bool pressed = (gpio_get(btn_pins[b]) == 0);

		int32_t list_idx = -1;
		for (int32_t i = 0; i < LIST_SIZE; ++i) {
			if (self.list[i].p_entry != &((const struct entry*)btn_entries)[b])
				continue;

			list_idx = i;
			break;
		}

		if (list_idx > -1) {
			next_item_state(&self.list[list_idx], pressed);
			continue;
		}

		if (!pressed)
			continue;

		for (uint32_t i = 0 ; i < LIST_SIZE; ++i) {
			if (self.list[i].p_entry != NULL)
				continue;

			self.list[i].p_entry = &((const struct entry*)btn_entries)[b];
			self.list[i].effective_key = '\0';
			self.list[i].state = KEY_STATE_IDLE;
			next_item_state(&self.list[i], pressed);

			break;
		}
	}
#endif

	// negative value means interval since last alarm time
	return -(reg_get_value(REG_ID_FRQ) * 1000);
}

void keyboard_inject_event(char key, enum key_state state)
{
	const struct fifo_item item = { key, state };
        // add the keypress event to the fifo
	if (!fifo_enqueue(item)) {
		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_INT))
			reg_set_bit(REG_ID_INT, INT_OVERFLOW);

		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_ON))
			fifo_enqueue_force(item);
	}

        // go through the callback linked list and call all appendend callbacks
        // One of these callbacks is the usb callback, which handles sending the
        // right scancode over usb
        struct key_callback *cb = self.key_callbacks;
	while (cb) {
		cb->func(key, state);

		cb = cb->next;
	}
}

// function to check if any key is currently pressed
bool keyboard_is_key_down(char key)
{
	for (int32_t i = 0; i < LIST_SIZE; ++i) {
		struct list_item *item = &self.list[i];

		if (item->p_entry == NULL)
			continue;

		if ((item->state != KEY_STATE_PRESSED) && (item->state != KEY_STATE_HOLD))
			continue;

		if (item->effective_key != key)
			continue;

		return true;
	}

	return false;
}

// function to check if any modifiers are currently applied
bool keyboard_is_mod_on(enum key_mod mod)
{
	return self.mods[mod];
}

void keyboard_add_key_callback(struct key_callback *callback)
{
	// first callback
	if (!self.key_callbacks) {
		self.key_callbacks = callback;
		return;
	}

	// find last and insert after
	struct key_callback *cb = self.key_callbacks;
	while (cb->next)
		cb = cb->next;

	cb->next = callback;
}

void keyboard_add_lock_callback(struct key_lock_callback *callback)
{
	// first callback
	if (!self.lock_callbacks) {
		self.lock_callbacks = callback;
		return;
	}

	// find last and insert after
	struct key_lock_callback *cb = self.lock_callbacks;
	while (cb->next)
		cb = cb->next;

	cb->next = callback;
}

bool keyboard_get_capslock(void)
{
	return self.capslock;
}

bool keyboard_get_numlock(void)
{
	return self.numlock;
}

void keyboard_init(void)
{
	for (int i = 0; i < KEY_MOD_ID_LAST; ++i)
		self.mods[i] = false;

	// rows
	for (uint32_t i = 0; i < NUM_OF_ROWS; ++i) {
		gpio_init(row_pins[i]);
		gpio_pull_up(row_pins[i]);
		gpio_set_dir(row_pins[i], GPIO_IN);
	}

	// cols
	for(uint32_t i = 0; i < NUM_OF_COLS; ++i) {
		gpio_init(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
	}

	// btns
#if NUM_OF_BTNS > 0
	for(uint32_t i = 0; i < NUM_OF_BTNS; ++i) {
		gpio_init(btn_pins[i]);
		gpio_pull_up(btn_pins[i]);
		gpio_set_dir(btn_pins[i], GPIO_IN);
	}
#endif

        // setup the timer_task to be triggered periodically accoriding to the
        // configured value in the register
        add_alarm_in_ms(reg_get_value(REG_ID_FRQ), timer_task, NULL, true);
}
