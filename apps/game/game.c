#include <stdio.h>
#include "vector.h"

#define MAX_ENTITIES 1000

typedef enum {
    NONE     = 0,        // 00000000
    POSITION = (1 << 0), // 00000001 (Decimal 1)
    PHYSICS  = (1 << 1) // 00000010 (Decimal 2)
} component_type;

typedef struct {
  vector coordinates;
} position_component;

typedef struct {
  vector velocity;
  vector acceleration;
  float mass;
} physics_component;

unsigned int entity_masks[MAX_ENTITIES] = {0}; // Initialize all to 0 (None)

position_component position_components[MAX_ENTITIES];
physics_component physics_components[MAX_ENTITIES];

int free_stack[MAX_ENTITIES];
int top = -1;

void init() {
  for (int i = MAX_ENTITIES - 1; i >= 0; i--) {
    free_stack[++top] = i;
  }
}

int create_entity() {
  if (top >= 0) {
    return free_stack[top--];
  }
  
  return -1;
}

void destroy_entity(int id) {
  entity_masks[id] = NONE;
  free_stack[++top] = id;
}

int main(char** argv, int argc) {
  vector v = {1, 1};
}
