#pragma once

struct FmKeyLoc_t {
	int row;
	int bitpos;
};

struct KeyMap_t {
	SDL_Keycode key;
	FmKeyLoc_t Loc;
};


KeyMap_t gKeyMap[] = {
{SDLK_HOME, {8, 0}},
{SDLK_UP, {8,1} },
{ SDLK_RIGHT, {8, 2} },
{SDLK_LEFT, {8,3}},
{SDLK_DOWN, {8,4}},
{SDLK_SPACE, {8,5}},
{ SDLK_BACKSPACE, {8,6} },
{ SDLK_INSERT, {8,7} },
	{SDLK_F1, {7, 0}},
	{SDLK_ESCAPE, {7, 1}},
	{SDLK_q, {7, 2} },
{ SDLK_LCTRL, {7,3} },
{ SDLK_LSHIFT, {7,4} },
{ SDLK_LALT, {7, 5} },
{ SDLK_1, {7, 6} },
{ SDLK_2, {7,7} },
{ SDLK_F2, {6, 0} },
{ SDLK_w, {6, 1} },
{ SDLK_s, {6, 2} },
{ SDLK_a, {6,3} },
{ SDLK_x, {6,4} },
{ SDLK_z, {6,5} },
{ SDLK_e, {6,6} },
{ SDLK_3, {6,7} },
{ SDLK_F3, {5,0} },
{ SDLK_t, {5,1} },
{ SDLK_r, {5,2} },
{ SDLK_d, {5,3} },
{ SDLK_f, {5,4} },
{ SDLK_c, {5,5} },
{ SDLK_5, {5,6} },
{ SDLK_4, {5,7} },
{ SDLK_F4, {4,0} },
{ SDLK_y, {4,1} },
{ SDLK_g, {4,2} },
{ SDLK_h, {4,3} },
{ SDLK_b, {4,4} },
{ SDLK_v, {4,5} },
{ SDLK_7, {4,6} },
{ SDLK_6, {4,7} },
{ SDLK_F5, {3,0} },
{ SDLK_i, {3,1} },
{ SDLK_u, {3,2} },
{ SDLK_j, {3,3} },
{ SDLK_m, {3,4} },
{ SDLK_n, {3,5} },
{ SDLK_9, {3,6} },
{ SDLK_8, {3,7} },
{ SDLK_F6, {2,0} },
{ SDLK_o, {2,1} },
{ SDLK_l, {2,2} },
{ SDLK_k, {2,3} },
{ SDLK_COMMA, {2,5} },
{ SDLK_PERIOD, {2,4} },
{ SDLK_p, {2,6} },
{ SDLK_0, {2,7} },
{ SDLK_F7, {1,0} },
{ SDLK_AT, {1,1} },
{ SDLK_SEMICOLON, {1,2} },
{ SDLK_COLON, {1,3} },
{ SDLK_UNDERSCORE, {1,4} },
{ SDLK_SLASH, {1,5} },
{ SDLK_MINUS, {1,6} },
{ SDLK_CARET, {1,7} },
{ SDLK_F8, {0,0} },
{ SDLK_RETURN, {0,1} },
{ SDLK_LEFTBRACKET, {0,2} },
{ SDLK_RIGHTBRACKET, {0,3} },
{ SDLK_RALT, {0,4} },
{ SDLK_RSHIFT, {0,5} },
{ SDLK_BACKSLASH, {0,6} },
{ SDLK_PAUSE, {0,7} },
{ SDLK_F12, {0,7} },
};