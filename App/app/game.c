/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "app/game.h"

#include "app/breakout.h"
#include "app/snake.h"

#include "app/keyboard_state.h"
#include "driver/keyboard.h"
#include "driver/gpio.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "ui/helper.h"
#include <string.h>

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

static KEY_Code_t GetKey(void)
{
    KEY_Code_t btn = KEYBOARD_Poll();
    if (btn == KEY_INVALID && GPIO_IsPttPressed()) {
        btn = KEY_PTT;
    }
    return btn;
}

static void DrawMenu(uint8_t selection)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    UI_PrintStringSmallBold("GAME", 44, 0, 1);

    if (selection == 0) {
        UI_PrintStringSmallBoldInverse("BREAKOUT", 16, 0, 3);
        UI_PrintStringSmallBold("SNAKE", 40, 0, 5);
    } else {
        UI_PrintStringSmallBold("BREAKOUT", 16, 0, 3);
        UI_PrintStringSmallBoldInverse("SNAKE", 40, 0, 5);
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

void APP_RunGame(void)
{
    uint8_t selection = 0;

    DrawMenu(selection);

    while (1) {
        KEY_Code_t key;

        kbd.prev = kbd.current;
        kbd.current = GetKey();

        if (kbd.current == KEY_INVALID || kbd.current == kbd.prev) {
            SYSTEM_DelayMs(30);
            continue;
        }

        key = kbd.current;

        switch (key) {
        case KEY_UP:
        case KEY_2:
            selection = 0;
            DrawMenu(selection);
            break;
        case KEY_DOWN:
        case KEY_8:
            selection = 1;
            DrawMenu(selection);
            break;
        case KEY_MENU:
            if (selection == 0) {
                APP_RunBreakout();
            } else {
                APP_RunSnake();
            }
            return;
        case KEY_EXIT:
            return;
        default:
            break;
        }

        SYSTEM_DelayMs(150);
    }
}
