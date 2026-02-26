#ifndef INPUT_H
#define INPUT_H

/**
 * Input events captured during a frame.
 */
typedef struct {
    int quit;      /* Window close or Escape pressed */
    int reset;     /* R key pressed */
    int zoom_in;    /* + key pressed (now: decrease distance) */
    int zoom_out;   /* - key pressed (now: increase distance) */
    int speed_up;   /* F key pressed */
    int speed_down; /* S key pressed */
    int pan_up;     /* Up arrow pressed (now: rotate elevation up) */
    int pan_down;   /* Down arrow pressed (now: rotate elevation down) */
    int pan_left;   /* Left arrow pressed (now: rotate azimuth left) */
    int pan_right;  /* Right arrow pressed (now: rotate azimuth right) */
} input_events;

/**
 * Poll for input events.
 * Fills the events struct with what happened this frame.
 */
void input_poll(input_events* events);

#endif /* INPUT_H */
