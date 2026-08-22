#ifndef THREADCOMMANDS_H_
#define THREADCOMMANDS_H_

/**
 * @file threadCommands.h
 * @brief Serial command module for the ArduinoThread cooperative scheduler.
 *
 * Registers a block of commands with a `commandProcessor` that let a host
 * inspect and control the running thread pool: list tasks, enable/disable
 * them individually or en masse, change scheduling intervals, force or delay
 * the next run, and (optionally) read back run-time profiling data.
 *
 * Usage
 * -----
 *   ThreadController  tasks;
 *   commandProcessor  cp;
 *   threadCommands    tcmds(&cp, &tasks);
 *
 *   cp.registerCommands(tcmds.threadCmdList());
 *   tcmds.protect(&commsThread);   // never stopped by TSTOPALL / TREM
 *
 * Build configuration
 * -------------------
 *   -D GAACE_THREAD_CMDS     Required.  Without it this module compiles to
 *                            nothing, so GAACE_Core still builds for projects
 *                            that do not link ArduinoThread.
 *   -D THREAD_STATS          Optional.  Enables TPROF / TRESETSTATS and the
 *                            per-thread counters they report.  Costs roughly
 *                            20 bytes per Thread.  Must be set globally so
 *                            that ArduinoThread and this module agree on the
 *                            layout of Thread.
 *
 * platformio.ini:
 *
 *   lib_deps =
 *       https://github.com/GordonAnderson/GAACE_Core.git#stm32
 *       https://github.com/GordonAnderson/ArduinoThread.git#stm32
 *   build_flags =
 *       -D GAACE_THREAD_CMDS
 *       -D THREAD_STATS
 *
 * Portability note
 * ----------------
 * This file and threadCommands.cpp are deliberately identical on the `main`
 * and `stm32` branches of GAACE_Core.  They touch no part of the API that
 * differs between them: the Stream -> GStream change is confined to
 * registerStream()/selectStream()/serial, none of which are used here, and
 * Thread::getName() returns `const char *` on both ArduinoThread branches.
 * No Arduino String is used.  Keep it that way so the file cherry-picks
 * cleanly in both directions.
 *
 * Limitations
 * -----------
 *  - Only one threadCommands instance may exist at a time.  Like `debug`, the
 *    callbacks are plain C functions sharing file-scope statics, and each
 *    constructor call overwrites them.
 *  - Thread names are addressed over the wire as a single command token, so
 *    they are limited to MAXTOKEN-1 (19) characters.
 *  - Nested controllers are walked to THREAD_CMDS_MAX_DEPTH levels.
 */

#if defined(GAACE_THREAD_CMDS)

#include "commandProcessor.h"
#include "Thread.h"
#include "ThreadController.h"

// ---------------------------------------------------------------------------
// Compile-time limits
// ---------------------------------------------------------------------------

/** Maximum number of threads that can be marked protected. */
#ifndef THREAD_CMDS_MAX_PROTECTED
#define THREAD_CMDS_MAX_PROTECTED 6
#endif

/** Maximum nesting depth walked when enumerating sub-controllers. */
#ifndef THREAD_CMDS_MAX_DEPTH
#define THREAD_CMDS_MAX_DEPTH 4
#endif

// ---------------------------------------------------------------------------
// threadCommands
// ---------------------------------------------------------------------------

class threadCommands
{
public:
    /**
     * @brief Construct the module and bind it to a processor and a root
     *        controller.
     *
     * @param cmdP  commandProcessor that will own these commands.  Must
     *              outlive this object.
     * @param root  Top-level ThreadController.  All enumeration starts here
     *              and recurses into any nested controllers found in its
     *              pool.  May be NULL and set later with setRoot().
     */
    threadCommands(commandProcessor *cmdP, ThreadController *root);

    /**
     * @brief Return the CommandList node holding all thread commands.
     *        Pass to commandProcessor::registerCommands().
     */
    CommandList *threadCmdList(void);

    /**
     * @brief Change the controller that enumeration starts from.
     */
    void setRoot(ThreadController *root);

    /**
     * @brief Mark a thread as protected.
     *
     * Protected threads are skipped by TSTOPALL and refused by TREM.  Use
     * this on anything whose failure would cost you the command link itself
     * (the thread that calls processStreams()/processCommands()), the
     * watchdog kicker, and any safety interlock task.
     *
     * @return true if recorded; false if the protect table is full.
     */
    bool protect(Thread *t);

    /**
     * @brief Look up a thread by name, or by list index if @p token is all
     *        digits and matches no name.
     *
     * @param token  Name or decimal index.
     * @param owner  If non-NULL, receives the controller the thread was
     *               found in.
     * @return Thread pointer, or NULL if not found.
     */
    static Thread *find(const char *token, ThreadController **owner = NULL);
};

#else  // !GAACE_THREAD_CMDS

// Keep the translation unit non-empty and give a clearer error than a wall of
// missing-symbol messages if someone includes this without the build flag.
typedef int threadCommands_disabled_t;

#endif // GAACE_THREAD_CMDS

#endif // THREADCOMMANDS_H_
