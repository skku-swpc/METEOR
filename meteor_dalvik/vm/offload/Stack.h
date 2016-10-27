struct Thread;
struct FifoBuffer;

void offPushAllStacks(struct FifoBuffer* fb);

void offPullAllStacks(struct FifoBuffer* fb);

void offPushStack(struct FifoBuffer* fb, struct Thread* thread);

struct Thread* offPullStack(struct FifoBuffer* fb, u4 tid);

#ifdef DEBUG
bool offCheckBreakFrames();

int offDebugStack(const struct Thread* thread);
#endif

#define CHECK_BREAK_FRAMES() assert(offCheckBreakFrames())
