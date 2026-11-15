/*
 * SDL1 add-on examples: SDL 1.2's own test programs, as they are.
 * Public domain (CC0), as the tests themselves (SDL 1.2's
 * test/COPYING).
 *
 * testsprite.c and graywin.c beside this file are SDL 1.2's, byte for
 * byte: nothing in them knows of STDL or an Atari. They take their
 * screen's size from the command line (640x480 without one), and a TOS
 * program started from the desktop has no command line, so this file
 * is what the add-on's make rule compiles: it includes the test named
 * by SDL1_TEST with its main renamed, and calls that with SDL1_ARGS.
 * That an unchanged SDL 1.2 program builds and runs is the point of
 * the add-on, and these two are its acceptance test: testsprite's
 * blits, colour key and SDL_DisplayFormat, and graywin's writes to the
 * screen surface's own pixels, which no planar port can keep.
 *
 * Two more ways to build it, for a run nobody watches (make ...
 * SDL1_EXTRA="-DSDL1_RUN=15 -DSDL1_CHECK"): SDL1_RUN=N sends the test
 * a key after N seconds, and SDL1_CHECK asks for the screen check and
 * says at the end what it found, every page that went up against the
 * updates that made it. With the check, what the test prints goes to
 * SDL1OUT.TXT: text printed after the mode is set is drawn by TOS onto
 * whichever page it has, as on any ST, and the check then finds that
 * page unlike its updates every time it comes round (a third of
 * testsprite's flips on a TT, for four lines of text in the band above
 * its picture).
 */

#define main sdl1_test_main
#include SDL1_TEST
#undef main

#ifdef SDL1_RUN
static Uint32 SDLCALL enough(Uint32 interval, void *param)
{
    SDL_Event e;
    (void)interval; (void)param;
    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&e);
    return 0;
}
#endif

int main(void)
{
    static char *argv[] = { "TEST", SDL1_ARGS, NULL };
    int ret;
#ifdef SDL1_CHECK
    Uint32 flips = 0, bad = 0;
    freopen("SDL1OUT.TXT", "w", stdout);
    STDL_SDL1_SetHint(STDL_SDL1_HINT_CHECK, "1");
#endif
#ifdef SDL1_RUN
    /* the clock is STDL's from SDL_Init on: the test's own call then
     * finds it done */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_AddTimer(SDL1_RUN * 1000, enough, NULL);
#endif
    ret = sdl1_test_main((int)(sizeof(argv) / sizeof(argv[0])) - 1, argv);
#ifdef SDL1_CHECK
    fclose(stdout);
    if (STDL_SDL1_GetCheck(&flips, &bad) < 0) {
        fprintf(stderr, "SDL1 check: no screen\n");
    } else {
        fprintf(stderr, "SDL1 check: %lu flips, %lu unlike their updates\n",
                (unsigned long)flips, (unsigned long)bad);
    }
#endif
#ifdef SDL1_RUN
    fflush(stdout);
    fprintf(stderr, "SDL1:done\n");
#endif
    return ret;
}
