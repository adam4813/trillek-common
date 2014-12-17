#ifndef OS_EVENT_HPP_INCLUDED
#define OS_EVENT_HPP_INCLUDED

#include "trillek.hpp"
#include "type-id.hpp"

namespace trillek {

struct KeyboardEvent {
    enum KEY_ACTION { KEY_DOWN, KEY_UP, KEY_REPEAT, KEY_CHAR };
    int key;
    int scancode;
    KEY_ACTION action;
    int mods;
};

struct MouseBtnEvent {
    enum MOUSE_BTN_ACTION { DOWN, UP };
    enum MOUSE_BTN { LEFT, RIGHT, MIDDLE, EX1, EX2 };
    MOUSE_BTN_ACTION action;
    MOUSE_BTN button;
};

struct MouseMoveEvent {
    double norm_x, norm_y; // Resolution independent new x, y (0-1) from upper-left to lower-right.
    int old_x, old_y; // Client space old x, y.
    int new_x, new_y; // Client space new x, y.
};

struct MouseScrollEvent {
    double scroll_x, scroll_y;
};

namespace reflection {

TRILLEK_MAKE_IDTYPE(KeyboardEvent, 5000);
TRILLEK_MAKE_IDTYPE(MouseBtnEvent, 5001);
TRILLEK_MAKE_IDTYPE(MouseMoveEvent, 5002);
TRILLEK_MAKE_IDTYPE(MouseScrollEvent, 5003);

} // namespace reflection
} // namespace trillek

#endif
