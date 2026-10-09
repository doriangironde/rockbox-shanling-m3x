/* Host-only SDL event injection for a silent, bounded startup smoke test. */
#include <pthread.h>
#include <stdlib.h>
#include <SDL.h>
static void screenshot(void)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_F5;
    SDL_PushEvent(&event); SDL_Delay(100);
    event.type = SDL_KEYUP; SDL_PushEvent(&event);
}
static void click(int x, int y)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x; event.button.y = y;
    SDL_PushEvent(&event); SDL_Delay(100);
    event.type = SDL_MOUSEBUTTONUP; SDL_PushEvent(&event);
    SDL_Delay(700);
}
static void swipe_up(void)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 384; event.button.y = 1050;
    SDL_PushEvent(&event); SDL_Delay(150);
    for (int y = 1000; y >= 300; y -= 50)
    {
        event.type = SDL_MOUSEMOTION;
        event.motion.state = SDL_BUTTON_LMASK;
        event.motion.x = 384; event.motion.y = y;
        SDL_PushEvent(&event); SDL_Delay(60);
    }
    SDL_Delay(400);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 384; event.button.y = 300;
    SDL_PushEvent(&event); SDL_Delay(1000);
}
static void *run(void *unused)
{
    (void)unused;
    SDL_Delay(3000);
    screenshot();
    SDL_Delay(1000);
    if (getenv("M3X_SMOKE_IPOD")) {
        click(200, 500); /* Settings, fourth full-screen menu row. */
        screenshot();
        click(80, 96);   /* Back to the main menu. */
        click(200, 188); /* Files. */
        click(200, 188); /* Music folder. */
        screenshot();
        swipe_up();
        screenshot();
        click(200, 188); /* A song in the scrolled list. */
        SDL_Delay(3000);
        screenshot();
        click(384, 1128); /* Pause. */
        screenshot();
        click(384, 1128); /* Resume. */
        click(80, 96);    /* Home, keeping music playing. */
        screenshot();
    } else if (getenv("M3X_SMOKE_PLAYBACK")) {
        click(80, 60); /* Files */
        click(80, 60); /* Music folder (sorted before screen dumps) */
        click(80, 60); /* Song */
        SDL_Delay(4000);
        screenshot(); SDL_Delay(1000);
    }
    SDL_Event event = {0};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    return NULL;
}
__attribute__((constructor)) static void start(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, run, NULL) == 0) pthread_detach(thread);
}
