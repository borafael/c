#ifndef INPUT_H
#define INPUT_H

/**
 * Input events captured during a frame.
 */
typedef struct {
    int quit;      /* Window close or Escape pressed */
    int reset;     /* R key pressed */
    int zoom_in;   /* + key pressed */
    int zoom_out;   /* - key pressed */
    int speed_up;   /* F key pressed */
    int speed_down; /* S key pressed */
} input_events;

/**
 * Poll for input events.
 * Fills the events struct with what happened this frame.
 */
void input_poll(input_events* events);

#endif /* INPUT_H */
