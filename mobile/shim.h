/*
 * The phone (web) build of CalcAll compiles the desktop app's src/gui.c
 * unchanged, with this header included first (-include mobile/shim.h).
 *
 * It swaps a handful of raylib calls for versions in shim.c that fit a
 * phone screen: the app still lays itself out for a window about 480
 * pixels wide, and the shim scales that up to the phone's real pixels, so
 * everything stays sharp. It also adds pinch-to-zoom, typing through the
 * phone's own keyboard, and keeping your files in the browser's storage.
 */
#ifndef CALCALL_SHIM_H
#define CALCALL_SHIM_H

#include "raylib.h"

void mob_SetConfigFlags(unsigned int flags);
void mob_InitWindow(int width, int height, const char *title);
void mob_SetTargetFPS(int fps);
void mob_BeginDrawing(void);
void mob_EndDrawing(void);
int mob_GetScreenWidth(void);
int mob_GetScreenHeight(void);
void mob_SetWindowSize(int width, int height);
void mob_SetWindowPosition(int x, int y);
void mob_SetWindowMinSize(int width, int height);
void mob_BeginScissorMode(int x, int y, int width, int height);
float mob_GetMouseWheelMove(void);
int mob_GetCharPressed(void);
int mob_GetKeyPressed(void);

#define SetConfigFlags mob_SetConfigFlags
#define InitWindow mob_InitWindow
#define SetTargetFPS mob_SetTargetFPS
#define BeginDrawing mob_BeginDrawing
#define EndDrawing mob_EndDrawing
#define GetScreenWidth mob_GetScreenWidth
#define GetScreenHeight mob_GetScreenHeight
#define SetWindowSize mob_SetWindowSize
#define SetWindowPosition mob_SetWindowPosition
#define SetWindowMinSize mob_SetWindowMinSize
#define BeginScissorMode mob_BeginScissorMode
#define GetMouseWheelMove mob_GetMouseWheelMove
#define GetCharPressed mob_GetCharPressed
#define GetKeyPressed mob_GetKeyPressed

#endif
