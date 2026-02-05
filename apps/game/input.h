#ifndef INPUT_H
#define INPUT_H

/**
 * Input events captured during a frame.
 */
typedef struct {
    int quit;   /* Window close or Escape pressed */
    int reset;  /* R key pressed */
} input_events;

/**
 * Poll for input events.
 * Fills the events struct with what happened this frame.
 */
void input_poll(input_events* events);

#endif /* INPUT_H */
