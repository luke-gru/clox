#ifndef clox_tracer_h
#define clox_tracer_h

struct LoopSeen;

void loopHeaderRegisterSeen(uint8_t *loopHeaderPC, uint8_t *loopEndPC);
struct LoopSeen *loopNeedsTrace(uint8_t *loopHeaderPC);
bool inTrace(void);
void beginTrace(struct LoopSeen *seen);
void endTrace(void);
bool shouldEndTrace(uint8_t *pc);

#endif
