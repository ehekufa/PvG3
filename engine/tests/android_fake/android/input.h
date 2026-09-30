#ifndef OG_FAKE_ANDROID_INPUT_H
#define OG_FAKE_ANDROID_INPUT_H

#include <stdint.h>

enum {
    AINPUT_EVENT_TYPE_MOTION = 1,
    AMOTION_EVENT_ACTION_DOWN = 0,
    AMOTION_EVENT_ACTION_UP = 1,
    AMOTION_EVENT_ACTION_MOVE = 2,
    AMOTION_EVENT_ACTION_CANCEL = 3,
    AMOTION_EVENT_ACTION_MASK = 0xFF
};

typedef struct AInputEvent AInputEvent;

int AInputEvent_getType(const AInputEvent *ev);
int AMotionEvent_getAction(const AInputEvent *ev);
float AMotionEvent_getX(const AInputEvent *ev, int index);
float AMotionEvent_getY(const AInputEvent *ev, int index);

/* Очередь событий для теста: задаётся извне, разбирается хостом. */
void og_fake_input_push(int action, float x, float y);
int og_fake_input_count(void);
const AInputEvent *og_fake_input_next(void);
void og_fake_input_clear(void);

#endif
