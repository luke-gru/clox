#include "common.h"
#include "tracer.h"
#include "vm.h"
#include "memory.h"

typedef struct LoopSeen {
    uint8_t *pc;
    uint8_t *pcEnd;
    int seenTimes;
    struct LoopSeen *next;
} LoopSeen;

static LoopSeen *loops;
static bool _inTrace = false;
static LoopSeen *_loopIn;

static void addLoopSeen(uint8_t *pc, uint8_t *pcEnd) {
    ASSERT(pc);
    ASSERT(pcEnd);
    LoopSeen *loop = loops;
    LoopSeen *prevLoop = NULL;
    while (loop) {
        prevLoop = loop;
        if (loop->pc == pc) {
            loop->seenTimes++;
            return;
        }
        loop = loop->next;
    }
    LoopSeen *next = ALLOCATE(LoopSeen, 1);
    next->pc = pc;
    next->pcEnd = pcEnd;
    next->seenTimes = 1;
    next->next = NULL;
    if (prevLoop) {
        prevLoop->next = next;
    } else {
        loops = next;
    }
}

static LoopSeen *getLoopSeen(uint8_t *pc) {
    LoopSeen *loop = loops;
    while (loop) {
        if (loop->pc == pc) {
            return loop;
        }
        loop = loop->next;
    }
    return NULL;
}

void loopHeaderRegisterSeen(uint8_t *pc, uint8_t *pcEnd) {
    addLoopSeen(pc, pcEnd);
}

LoopSeen *loopNeedsTrace(uint8_t *pc) {
    LoopSeen *seen = getLoopSeen(pc);
    if (!seen) return NULL;
    return seen->seenTimes >= 2 ? seen : NULL;
}

void beginTrace(LoopSeen *seen) {
    ASSERT(!_inTrace);
    fprintf(stderr, "begin trace\n");
    _inTrace = true;
    _loopIn = seen;
}

void endTrace() {
    ASSERT(_inTrace);
    fprintf(stderr, "end trace\n");
    _inTrace = false;
    _loopIn = NULL;
}

bool shouldEndTrace(uint8_t *pcEnd) {
    if (!_inTrace) return false;
    LoopSeen *cur = _loopIn;
    ASSERT(cur);
    if (cur->pcEnd <= pcEnd) {
        return true;
    } else {
        return false;
    }
}

bool inTrace() {
    return _inTrace;
}
