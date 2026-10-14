// Entry point. SDL's main callbacks drive the app the same way on desktop
// and in the browser (where the loop runs on requestAnimationFrame).

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "app/App.hpp"

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    auto* app = new gridfin::App(gridfin::AppOptions::parse(argc, argv));
    *appstate = app;
    return app->init();
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    return static_cast<gridfin::App*>(appstate)->handleEvent(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    return static_cast<gridfin::App*>(appstate)->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult)
{
    delete static_cast<gridfin::App*>(appstate);
}
