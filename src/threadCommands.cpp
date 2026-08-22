#include "threadCommands.h"

#if defined(GAACE_THREAD_CMDS)

#include <ctype.h>
#include <string.h>

// =============================================================================
//  Module-level state
//
//  Every command callback in this file is a plain C function stored as a
//  CMDfunction pointer in the Command table, so shared state lives in
//  file-scope statics rather than class members.  This mirrors debug.cpp.
//
//  LIMITATION: only one threadCommands instance may exist at a time.  A second
//  constructor call overwrites `cp` / `rootTC` and the first instance's
//  callbacks silently operate on the wrong objects.
// =============================================================================

static commandProcessor *cp     = NULL;
static ThreadController *rootTC = NULL;

static Thread *protectedList[THREAD_CMDS_MAX_PROTECTED];
static int     numProtected = 0;

// =============================================================================
//  Small helpers
// =============================================================================

/** Case-insensitive string equality (strcasecmp is not portable everywhere). */
static bool ieq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0')
    {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

/** True if @p s is non-empty and consists only of decimal digits. */
static bool allDigits(const char *s)
{
    if (s == NULL || *s == '\0') return false;
    for (; *s != '\0'; s++)
        if (!isdigit((unsigned char)*s)) return false;
    return true;
}

/**
 * Print an unsigned long through the commandProcessor.
 *
 * IMPORTANT: commandProcessor::print() has no `unsigned long` overload.  The
 * candidate set is int / uint32_t / float / double / uint8_t / bool, and where
 * uint32_t is not a typedef for unsigned long the call is ambiguous and fails
 * to compile.  Every unsigned long must be cast explicitly, so it is done in
 * exactly one place here.
 */
static void pu(unsigned long v)
{
    cp->print((uint32_t)v);
}

/** True if @p t was registered with threadCommands::protect(). */
static bool isProtected(Thread *t)
{
    for (int i = 0; i < numProtected; i++)
        if (protectedList[i] == t) return true;
    return false;
}

/**
 * Read the next comma-delimited token into a fixed buffer.
 *
 * getValue() hands back a charAllocate block; it is copied out and released
 * immediately so callers can hold the value across further parsing without
 * worrying about the scratch allocator.
 */
static bool nextToken(char *dst, int dstLen)
{
    char *p = NULL;
    if (!cp->getValue(&p)) return false;
    if (p == NULL) return false;
    strncpy(dst, p, dstLen - 1);
    dst[dstLen - 1] = '\0';
    cp->ca->free(p);
    return true;
}

// =============================================================================
//  Thread-pool traversal
//
//  ThreadController extends Thread, so a nested controller sits in the parent's
//  pool as a plain Thread*.  Thread::isController() is the discriminator that
//  lets the walk recurse without RTTI.
// =============================================================================

typedef bool (*threadVisitor)(Thread *t, ThreadController *owner,
                              int idx, int depth, void *ctx);

/** @return false as soon as a visitor asks to stop; true if the walk finished. */
static bool walk(ThreadController *tc, int depth, int *idx,
                 threadVisitor fn, void *ctx)
{
    if (tc == NULL) return true;
    if (depth > THREAD_CMDS_MAX_DEPTH) return true;   // cycle / runaway guard

    int n = tc->size();
    for (int i = 0; i < n; i++)
    {
        Thread *t = tc->get(i);
        if (t == NULL) continue;

        if (!fn(t, tc, *idx, depth, ctx)) return false;
        (*idx)++;

        if (t->isController())
        {
            ThreadController *sub = (ThreadController *)t;
            if (!walk(sub, depth + 1, idx, fn, ctx)) return false;
        }
    }
    return true;
}

/** Walk the whole tree from the root controller. */
static void walkAll(threadVisitor fn, void *ctx)
{
    int idx = 0;
    walk(rootTC, 0, &idx, fn, ctx);
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

typedef struct
{
    const char       *want;      ///< Name being searched for (NULL when by index)
    int               wantIdx;   ///< Index being searched for (-1 when by name)
    bool              fold;      ///< Case-insensitive name compare
    Thread           *found;
    ThreadController *owner;
} findCtx;

static bool findVisitor(Thread *t, ThreadController *owner,
                        int idx, int depth, void *ctx)
{
    (void)depth;
    findCtx *f = (findCtx *)ctx;

    if (f->want != NULL)
    {
        const char *n = t->getName();     // never NULL on either branch
        bool hit = f->fold ? ieq(n, f->want) : (strcmp(n, f->want) == 0);
        // An unnamed thread has getName() == "", which must never match.
        if (hit && n[0] != '\0')
        {
            f->found = t;
            f->owner = owner;
            return false;                 // stop the walk
        }
    }
    else if (idx == f->wantIdx)
    {
        f->found = t;
        f->owner = owner;
        return false;
    }
    return true;
}

Thread *threadCommands::find(const char *token, ThreadController **owner)
{
    if (token == NULL || rootTC == NULL) return NULL;

    findCtx f;

    // Pass 1: exact name match.
    f.want = token; f.wantIdx = -1; f.fold = false;
    f.found = NULL; f.owner = NULL;
    walkAll(findVisitor, &f);

    // Pass 2: case-insensitive name match.  Command tokens are normally
    // case-folded by the processor, so a host typing "LOCKIN" for a thread
    // named "lockin" should still resolve.
    if (f.found == NULL)
    {
        f.want = token; f.wantIdx = -1; f.fold = true;
        walkAll(findVisitor, &f);
    }

    // Pass 3: TLIST index.  Only attempted when the token is all digits AND
    // matched no name, so a thread literally named "1" still wins.
    if (f.found == NULL && allDigits(token))
    {
        f.want = NULL; f.wantIdx = (int)strtol(token, NULL, 10); f.fold = false;
        walkAll(findVisitor, &f);
    }

    if (owner != NULL) *owner = f.owner;
    return f.found;
}

/** Read one token and resolve it, NAKing on failure.  NULL means "already NAKed". */
static Thread *takeThread(ThreadController **owner)
{
    char token[MAXTOKEN];

    if (rootTC == NULL)          { cp->sendNAK(ERR_CMD); return NULL; }
    if (!nextToken(token, MAXTOKEN)) { cp->sendNAK(ERR_ARG); return NULL; }

    Thread *t = threadCommands::find(token, owner);
    if (t == NULL) { cp->sendNAK(ERR_ARG); return NULL; }
    return t;
}

// =============================================================================
//  Command callbacks - enumeration
// =============================================================================

/**
 * TLIST - dump the whole thread tree, one CSV row per thread.
 *
 * Columns: idx, depth, type, name, id, enabled, interval, lastRun, runTime, nextIn
 *
 * The header line is prefixed with '#' so a host parser can skip it.  `nextIn`
 * is signed milliseconds until the next scheduled run and goes negative when a
 * thread is overdue.  A thread with an empty name column can only be addressed
 * by its index.
 */
static bool listVisitor(Thread *t, ThreadController *owner,
                        int idx, int depth, void *ctx)
{
    (void)owner; (void)ctx;

    // Unsigned difference then a signed cast: wraps correctly across the
    // millis() rollover for any sane interval.
    long nextIn = (long)(t->getNextRunTime() - millis());

    cp->print(idx);                              cp->print(",");
    cp->print(depth);                            cp->print(",");
    cp->print(t->isController() ? "CTRL" : "TASK"); cp->print(",");
    cp->print(t->getName());                     cp->print(",");
    cp->print(t->getID());                       cp->print(",");
    cp->print(t->isEnabled());                   cp->print(",");
    pu(t->getInterval());                        cp->print(",");
    pu(t->getLastRunTime());                     cp->print(",");
    pu(t->runTimeMs());                          cp->print(",");
    cp->println((int)nextIn);
    return true;
}

static void cmdList(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    cp->sendACK(false);
    cp->println("#idx,depth,type,name,id,enabled,interval,lastRun,runTime,nextIn");
    walkAll(listVisitor, NULL);
}

static bool countVisitor(Thread *t, ThreadController *owner,
                         int idx, int depth, void *ctx)
{
    (void)t; (void)owner; (void)idx; (void)depth;
    (*(int *)ctx)++;
    return true;
}

/** GTCOUNT - total thread count, including nested controllers themselves. */
static void cmdCount(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    int n = 0;
    walkAll(countVisitor, &n);
    cp->sendACK(false);
    cp->println(n);
}

static bool namesVisitor(Thread *t, ThreadController *owner,
                         int idx, int depth, void *ctx)
{
    (void)owner; (void)idx; (void)depth;
    int *n = (int *)ctx;
    if (t->getName()[0] == '\0') return true;    // unnamed: not addressable
    if ((*n)++ > 0) cp->print(",");
    cp->print(t->getName());
    return true;
}

/** GTNAMES - comma-separated list of every named thread. */
static void cmdNames(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    int n = 0;
    cp->sendACK(false);
    walkAll(namesVisitor, &n);
    cp->print();                                  // terminating newline
}

/** GTPROT - names of threads excluded from TSTOPALL / TREM. */
static void cmdProtected(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }

    cp->sendACK(false);
    for (int i = 0; i < numProtected; i++)
    {
        if (i > 0) cp->print(",");
        cp->print(protectedList[i]->getName());
    }
    cp->print();
}

// =============================================================================
//  Command callbacks - per-thread get/set
// =============================================================================

/** True when the token that matched a '?' entry arrived with an 'S' prefix. */
static bool isSet(void)
{
    return toupper((unsigned char)cp->getCMD()[0]) == 'S';
}

/**
 * GTENA,<name>            -> TRUE | FALSE
 * STENA,<name>,TRUE|FALSE -> enable or disable one thread
 *
 * Re-enabling a thread that has been disabled for longer than its interval
 * leaves its cached next-run time in the past, so it fires once on the very
 * next scheduler tick and then resumes its normal cadence.  That is a single
 * catch-up run, not a burst, because runned() re-bases from dispatch time.
 */
static void cmdEnable(void)
{
    bool set = isSet();
    if (!cp->checkExpectedArgs(set ? 2 : 1)) return;

    Thread *t = takeThread(NULL);
    if (t == NULL) return;

    if (!set)
    {
        cp->sendACK(false);
        cp->println(t->isEnabled());
        return;
    }

    char v[MAXTOKEN];
    if (!nextToken(v, MAXTOKEN)) { cp->sendNAK(ERR_ARG); return; }

    if      (ieq(v, "TRUE"))  t->setEnabled(true);
    else if (ieq(v, "FALSE")) t->setEnabled(false);
    else { cp->sendNAK(ERR_ARG); return; }

    cp->sendACK();
}

/**
 * GTINT,<name>      -> interval in milliseconds
 * STINT,<name>,<ms> -> set the interval
 *
 * setInterval() re-bases the next run against the last run, so shortening an
 * interval can make a thread due immediately.  An interval of 0 means "run on
 * every scheduler tick".
 */
static void cmdInterval(void)
{
    bool set = isSet();
    if (!cp->checkExpectedArgs(set ? 2 : 1)) return;

    Thread *t = takeThread(NULL);
    if (t == NULL) return;

    if (!set)
    {
        cp->sendACK(false);
        pu(t->getInterval());
        cp->print();
        return;
    }

    uint32_t ms;
    if (!cp->getValue(&ms)) { cp->sendNAK(ERR_ARG); return; }

    t->setInterval((unsigned long)ms);
    cp->sendACK();
}

/** GTRUN,<name> - duration of this thread's most recent run, milliseconds. */
static void cmdRunTime(void)
{
    if (!cp->checkExpectedArgs(1)) return;

    Thread *t = takeThread(NULL);
    if (t == NULL) return;

    cp->sendACK(false);
    pu(t->runTimeMs());
    cp->print();
}

// =============================================================================
//  Command callbacks - scheduling
// =============================================================================

/**
 * TTRIG,<name> - make a thread due on the next scheduler tick.
 *
 * Has no visible effect on a disabled thread: shouldRun() also tests `enabled`.
 * The thread will fire as soon as it is re-enabled.
 */
static void cmdTrigger(void)
{
    if (!cp->checkExpectedArgs(1)) return;

    Thread *t = takeThread(NULL);
    if (t == NULL) return;

    t->setNextRunTime(millis());
    cp->sendACK();
}

/**
 * TDELAY,<name>,<ms> - push a thread's next run out by <ms> from now.
 *
 * One-shot: the thread returns to its configured interval after that run.
 * Useful for opening a quiet window around a sensitive measurement.
 */
static void cmdDelay(void)
{
    if (!cp->checkExpectedArgs(2)) return;

    Thread *t = takeThread(NULL);
    if (t == NULL) return;

    uint32_t ms;
    if (!cp->getValue(&ms)) { cp->sendNAK(ERR_ARG); return; }

    t->setNextRunTime(millis() + (unsigned long)ms);
    cp->sendACK();
}

typedef struct { bool state; int changed; } setAllCtx;

static bool setAllVisitor(Thread *t, ThreadController *owner,
                          int idx, int depth, void *ctx)
{
    (void)owner; (void)idx; (void)depth;
    setAllCtx *s = (setAllCtx *)ctx;

    // Never disable a controller: that would stop everything beneath it in one
    // stroke, including protected threads, and the leaves are being handled
    // individually anyway.
    if (t->isController()) return true;

    // Protected threads are exempt from the stop, but a start is always safe.
    if (!s->state && isProtected(t)) return true;

    if (t->isEnabled() != s->state) { t->setEnabled(s->state); s->changed++; }
    return true;
}

static void setAll(bool state)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    setAllCtx s; s.state = state; s.changed = 0;
    walkAll(setAllVisitor, &s);

    cp->sendACK(false);
    cp->println(s.changed);      // number of threads whose state actually changed
}

/** TSTOPALL - disable every leaf thread except those marked protected. */
static void cmdStopAll(void)  { setAll(false); }

/** TSTARTALL - enable every leaf thread. */
static void cmdStartAll(void) { setAll(true);  }

/**
 * TREM,<name> - remove a thread from the controller that owns it.
 *
 * Refused for protected threads and for controllers, since removing a
 * controller silently orphans everything registered under it.  The Thread
 * object itself is not destroyed; it can be re-added from application code.
 */
static void cmdRemove(void)
{
    if (!cp->checkExpectedArgs(1)) return;

    ThreadController *owner = NULL;
    Thread *t = takeThread(&owner);
    if (t == NULL) return;

    if (isProtected(t) || t->isController() || owner == NULL)
    {
        cp->sendNAK(ERR_ARG);
        return;
    }

    owner->remove(t);
    cp->sendACK();
}

// =============================================================================
//  Command callbacks - profiling (opt in with -D THREAD_STATS)
// =============================================================================

#if defined(THREAD_STATS)

static bool profVisitor(Thread *t, ThreadController *owner,
                        int idx, int depth, void *ctx)
{
    (void)owner; (void)depth; (void)ctx;

    cp->print(idx);              cp->print(",");
    cp->print(t->getName());     cp->print(",");
    pu(t->runCount);             cp->print(",");
    pu(t->runTimeMs());          cp->print(",");
    pu(t->minRun);               cp->print(",");
    pu(t->maxRun);               cp->print(",");
    pu(t->avgRunMs());           cp->print(",");
    pu(t->overruns);             cp->print();
    return true;
}

/**
 * TPROF - run-time statistics per thread.
 *
 * Columns: idx, name, runs, last, min, max, avg, overruns  (all milliseconds)
 *
 * A non-zero overrun count means the callback took longer than its own
 * scheduling period, so that thread can never keep up.  Note the 1 ms
 * resolution: sub-millisecond tasks will report 0 for min/avg and are better
 * measured with a cycle counter.
 */
static void cmdProfile(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    cp->sendACK(false);
    cp->println("#idx,name,runs,last,min,max,avg,overruns");
    walkAll(profVisitor, NULL);
}

static bool resetStatsVisitor(Thread *t, ThreadController *owner,
                              int idx, int depth, void *ctx)
{
    (void)owner; (void)idx; (void)depth; (void)ctx;
    t->resetStats();
    return true;
}

/** TRESETSTATS - zero every thread's counters. */
static void cmdResetStats(void)
{
    if (cp->getNumArgs() != 0) { cp->sendNAK(ERR_ARG); return; }
    if (rootTC == NULL)        { cp->sendNAK(ERR_CMD); return; }

    walkAll(resetStatsVisitor, NULL);
    cp->sendACK();
}

#endif // THREAD_STATS

// =============================================================================
//  Command table
//
//  Defined at file scope so the constructor can hand out a stable pointer.
//  All entries are CMDfunction with nargs = -1: the handlers validate their own
//  argument counts, which is also what lets a single '?' handler serve both the
//  G and S forms (processCommand() skips the G/S argument check for functions).
// =============================================================================

Command threadCmds[] =
{
    // cmd          type         nargs  pointer                options  help
    {"TLIST",       CMDfunction, -1, (void *)cmdList,      NULL, "List all scheduler threads"},
    {"GTCOUNT",     CMDfunction, -1, (void *)cmdCount,     NULL, "Returns the number of scheduler threads"},
    {"GTNAMES",     CMDfunction, -1, (void *)cmdNames,     NULL, "Returns a comma separated list of thread names"},
    {"GTPROT",      CMDfunction, -1, (void *)cmdProtected, NULL, "Returns the names of protected threads"},
    {"?TENA",       CMDfunction, -1, (void *)cmdEnable,    NULL, "Thread enabled state, name[,TRUE|FALSE]"},
    {"?TINT",       CMDfunction, -1, (void *)cmdInterval,  NULL, "Thread interval in mS, name[,mS]"},
    {"GTRUN",       CMDfunction, -1, (void *)cmdRunTime,   NULL, "Returns a thread's last run time in mS, name"},
    {"TTRIG",       CMDfunction, -1, (void *)cmdTrigger,   NULL, "Schedule a thread to run on the next tick, name"},
    {"TDELAY",      CMDfunction, -1, (void *)cmdDelay,     NULL, "Delay a thread's next run, name,mS"},
    {"TSTOPALL",    CMDfunction, -1, (void *)cmdStopAll,   NULL, "Disable all threads except protected ones"},
    {"TSTARTALL",   CMDfunction, -1, (void *)cmdStartAll,  NULL, "Enable all threads"},
    {"TREM",        CMDfunction, -1, (void *)cmdRemove,    NULL, "Remove a thread from its controller, name"},
#if defined(THREAD_STATS)
    {"TPROF",       CMDfunction, -1, (void *)cmdProfile,   NULL, "List thread run time statistics"},
    {"TRESETSTATS", CMDfunction, -1, (void *)cmdResetStats,NULL, "Zero all thread run time statistics"},
#endif
    {NULL}  // sentinel
};

CommandList threadCmdsList = {threadCmds, NULL};

// =============================================================================
//  threadCommands
// =============================================================================

threadCommands::threadCommands(commandProcessor *cmdP, ThreadController *root)
{
    cp     = cmdP;
    rootTC = root;

    numProtected = 0;
    for (int i = 0; i < THREAD_CMDS_MAX_PROTECTED; i++) protectedList[i] = NULL;
}

CommandList *threadCommands::threadCmdList(void)
{
    return &threadCmdsList;
}

void threadCommands::setRoot(ThreadController *root)
{
    rootTC = root;
}

bool threadCommands::protect(Thread *t)
{
    if (t == NULL) return false;
    if (isProtected(t)) return true;
    if (numProtected >= THREAD_CMDS_MAX_PROTECTED) return false;

    protectedList[numProtected++] = t;
    return true;
}

#endif // GAACE_THREAD_CMDS
