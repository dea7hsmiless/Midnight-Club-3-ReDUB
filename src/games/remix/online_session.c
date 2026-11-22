/* online_session.c -- the Online session: enter, leave, and put everything back.
 *
 * MC3_ONLINE_SESSION=1 turns it on. Off by default, like every other optional
 * behaviour in this port.
 *
 * WHAT A SESSION IS
 *
 * The game's main menu has a "Networking" item (the Xbox Live / System Link
 * screen). Entering it in a session instead takes the player into the cruise
 * the way Arcade does, but with:
 *
 *   - 2,000,000 in the career manager's money, so the garage can buy and tune;
 *   - every car, part, rim, paint and performance level unlocked through the
 *     game's own flags;
 *   - nothing written to the memory card while the session runs.
 *
 * A session is only safe if everything it overwrites is saved first and put
 * back exactly when it ends. The money and the garage blob are the career's,
 * and a session that leaked would leave a career save with 2,000,000 and
 * everything unlocked. So:
 *
 *   - online_enter() saves each field before it touches it;
 *   - online_leave() restores by name, not by asking what changed;
 *   - the keep array is only valid while s_sess.on, and leave clears it.
 *
 * THE GARAGE BLOB
 *
 * The garage is 0x18340 bytes at 0x0053F308. It is raw save data - no host or
 * guest pointers inside - which is exactly why a plain memcpy is sound and
 * why the upstream could treat it the same way. Bounds are asserted against
 * the .data section so a future edit cannot quietly move it out of range.
 *
 * WHY THIS IS OUR OWN FILE
 *
 * The reference implementation (mc3-web up to its MIT commit) spread this over
 * recomp_manual.c with file-scope globals and a lot of browser #ifdefs. This
 * port has no web target, so those branches would be dead weight; and keeping
 * the whole lifecycle in one small module makes "can a session leak?" a
 * question you answer by reading one file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The career manager -> progress object -> money. Measured: [0x557F0C] is the
 * career manager, +0xC its progress, and the money is +0xACC into that. */
#define OS_MGR_VA       0x00557F0Cu
#define OS_MGR_PROG     0x0Cu
#define OS_MONEY_OFF    0xACCu

/* The garage blob and the unlock flags (see unlock_all.c, which writes the
 * same four flags for MC3_UNLOCK_ALL). */
#define OS_GARAGE_VA    0x0053F308u
#define OS_GARAGE_SIZE  0x18340u
#define OS_UNLOCK_RACES  0x0053F2ECu
#define OS_UNLOCK_CITY   0x0053F2EDu
#define OS_UNLOCK_CARS   0x0053F2EEu
#define OS_UNLOCK_GARAGE 0x0053F2F2u

/* .data holds the garage blob; assert it stays inside, so a wrong address is a
 * compile error rather than a write into something else. */
#define OS_DATA_LO      0x004A8140u
#define OS_DATA_HI      0x0056DE60u
typedef char os_garage_fits[(OS_GARAGE_VA >= OS_DATA_LO &&
                             OS_GARAGE_VA + OS_GARAGE_SIZE <= OS_DATA_HI) ? 1 : -1];

#define OS_MONEY        2000000u

static struct {
    int      on;
    int      money_ok;
    uint32_t money;
    uint8_t  garage[OS_GARAGE_SIZE];
    uint8_t  unlock[4];
    unsigned enters, leaves;
} s_sess;

static uint32_t s_os_flag[4] = {
    OS_UNLOCK_RACES, OS_UNLOCK_CITY, OS_UNLOCK_CARS, OS_UNLOCK_GARAGE
};

static int os_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_ONLINE_SESSION");
        on = e && e[0] == '1';
        if (on)
            fprintf(stderr, "[ONLINE] session on (MC3_ONLINE_SESSION): money and the garage are"
                    " saved on entry and restored on the way out\n");
    }
    return on;
}

/* Is this address readable guest RAM? Same rule the rest of the port uses. */
static int os_ram(uint32_t va)
{
    return (va >= 0x00010000u && va < 0x04000000u) ||
           (va >= 0x80000000u && va < 0x84000000u);
}

/* The same test for callers that run before roam_compat.h is included (the
 * main-menu reorder, whose override sits earlier in recomp_manual.c). */
int online_session_ram(uint32_t va) { return os_ram(va); }

/* The money slot, or 0 while the career manager is not up yet. */
static uint32_t os_money_va(void)
{
    uint32_t mgr = MEM32(OS_MGR_VA), prog;
    if (!os_ram(mgr))
        return 0;
    prog = MEM32(mgr + OS_MGR_PROG);
    return os_ram(prog) ? prog + OS_MONEY_OFF : 0;
}

int online_session_on(void) { return s_sess.on; }

/* Is a session wanted at all (the setting), as opposed to running?
 *
 * The autostart needs this before any session exists: it steers the main menu
 * to the Networking item, and the session only turns on when that item is
 * pressed. Asking online_session_on() while steering would always read 0. */
int online_session_wanted(void) { return os_on(); }

/* Enter: save what we are about to overwrite, then overwrite it. */
void online_enter(const char *why)
{
    uint32_t m;
    int i;

    if (!os_on())
        return;
    if (s_sess.on) {
        /* Re-entering is normal (the menu can be revisited). Re-assert the
         * money/unlocks, but never re-save: the keep array must stay the
         * career's original values, not the session's. */
        if ((m = os_money_va()) != 0)
            MEM32(m) = OS_MONEY;
        for (i = 0; i < 4; i++)
            MEM8(s_os_flag[i]) = 1;
        return;
    }

    memset(&s_sess, 0, sizeof s_sess);

    m = os_money_va();
    s_sess.money_ok = m != 0;
    s_sess.money = m ? MEM32(m) : 0;
    memcpy(s_sess.garage, (const void *)XBOX_PTR(OS_GARAGE_VA), OS_GARAGE_SIZE);
    for (i = 0; i < 4; i++)
        s_sess.unlock[i] = MEM8(s_os_flag[i]);
    s_sess.on = 1;
    s_sess.enters++;

    if (m)
        MEM32(m) = OS_MONEY;
    for (i = 0; i < 4; i++)
        MEM8(s_os_flag[i]) = 1;

    fprintf(stderr, "[ONLINE] enter (%s): money %u -> %u, unlock flags %u/%u/%u/%u saved,"
            " all cars and parts unlocked\n",
            why ? why : "?", (unsigned)s_sess.money, (unsigned)OS_MONEY,
            (unsigned)s_sess.unlock[0], (unsigned)s_sess.unlock[1],
            (unsigned)s_sess.unlock[2], (unsigned)s_sess.unlock[3]);
}

/* Leave: put back every field we saved, whether or not it looks changed. */
void online_leave(const char *why)
{
    uint32_t m;
    int i;

    if (!s_sess.on)
        return;

    m = os_money_va();
    if (m && s_sess.money_ok)
        MEM32(m) = s_sess.money;
    memcpy((void *)XBOX_PTR(OS_GARAGE_VA), s_sess.garage, OS_GARAGE_SIZE);
    for (i = 0; i < 4; i++)
        MEM8(s_os_flag[i]) = s_sess.unlock[i];

    fprintf(stderr, "[ONLINE] leave (%s): money %u restored, unlock flags %u/%u/%u/%u restored,"
            " the garage is the career's again\n",
            why ? why : "?", (unsigned)s_sess.money,
            (unsigned)s_sess.unlock[0], (unsigned)s_sess.unlock[1],
            (unsigned)s_sess.unlock[2], (unsigned)s_sess.unlock[3]);

    memset(&s_sess, 0, sizeof s_sess);
    s_sess.leaves = 1;          /* count kept across sessions is not needed */
}

/* Once a frame while a session runs: re-assert the money and the flags. The
 * game's own profile load can clear the unlock block, and the garage can spend
 * money, so this is the same "write it back" discipline unlock_all.c uses. */
void online_session_pass(void)
{
    uint32_t m;
    int i;

    if (!s_sess.on)
        return;
    if ((m = os_money_va()) != 0 && MEM32(m) != OS_MONEY)
        MEM32(m) = OS_MONEY;
    for (i = 0; i < 4; i++)
        if (MEM8(s_os_flag[i]) != 1)
            MEM8(s_os_flag[i]) = 1;
}
