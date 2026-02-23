/* Copyright Olivier Quirant2026
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

#include "app/snake.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

#define SNAKE_CELL_SIZE      4
#define SNAKE_GRID_W         (LCD_WIDTH / SNAKE_CELL_SIZE) 
#define SNAKE_GRID_H         ((LCD_HEIGHT - 8) / SNAKE_CELL_SIZE)
#define SNAKE_MAX_LEN        (SNAKE_GRID_W * SNAKE_GRID_H)
#define SNAKE_START_LEN      5
#define SNAKE_BASE_DELAY_MS  150
#define SNAKE_MIN_DELAY_MS   60

static uint32_t randSeed = 1;

static bool isInitialized = false;
static bool isPaused = false;
static bool isGameOver = false;
static bool isBeep = false;

static uint16_t score = 0;
static uint16_t snake_len = 0;

static int8_t dir_x = 1;
static int8_t dir_y = 0;

static uint8_t snake_x[SNAKE_MAX_LEN];
static uint8_t snake_y[SNAKE_MAX_LEN];
static uint8_t food_x = 0;
static uint8_t food_y = 0;

static uint16_t tone = 0;

static char str[16];

static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

// Initialise seed
static void srand_custom(uint32_t seed) {
    randSeed = seed;
}

// Return pseudo-random from 0 to RAND_MAX (here 32767)
static int rand_custom(void) {
    randSeed = randSeed * 1103515245 + 12345;
    return (randSeed >> 16) & 0x7FFF; // 15 bits
}

// Return integer from min to max include
static uint8_t randInt(uint8_t min, uint8_t max) {
    return (uint8_t)(min + (rand_custom() % (max - min + 1)));
}

static void playBeep(uint16_t tone)
{
    BK4819_PlayTone(tone, true);
    AUDIO_AudioPathOn();
    BK4819_ExitTxMute();
    SYSTEM_DelayMs(100);
    BK4819_EnterTxMute();
    AUDIO_AudioPathOff();
}

static void drawScore(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));

    sprintf(str, "SCORE %03u", score);
    GUI_DisplaySmallest(str, 0, 1, true, true);

    sprintf(str, "LEN %03u", snake_len);
    GUI_DisplaySmallest(str, 72, 1, true, true);
}

static void drawCell(uint8_t gx, uint8_t gy, bool on)
{
    uint8_t x0 = gx * SNAKE_CELL_SIZE;
    uint8_t y0 = gy * SNAKE_CELL_SIZE;

    for (uint8_t y = 0; y < SNAKE_CELL_SIZE; y++) {
        UI_DrawLineBuffer(gFrameBuffer, x0, y0 + y, x0 + SNAKE_CELL_SIZE - 1, y0 + y, on);
    }
}

static void drawFood(uint8_t gx, uint8_t gy, bool on)
{
    uint8_t x0 = gx * SNAKE_CELL_SIZE + 1;
    uint8_t y0 = gy * SNAKE_CELL_SIZE + 1;

    for (uint8_t y = 0; y < 2; y++) {
        for (uint8_t x = 0; x < 2; x++) {
            UI_DrawPixelBuffer(gFrameBuffer, x0 + x, y0 + y, on);
        }
    }
}

static bool cellOnSnake(uint8_t gx, uint8_t gy, uint16_t limit)
{
    for (uint16_t i = 0; i < limit; i++) {
        if (snake_x[i] == gx && snake_y[i] == gy) {
            return true;
        }
    }
    return false;
}

static void placeFood(void)
{
    if (snake_len >= SNAKE_MAX_LEN) {
        return;
    }

    do {
        food_x = randInt(0, SNAKE_GRID_W - 1);
        food_y = randInt(0, SNAKE_GRID_H - 1);
    } while (cellOnSnake(food_x, food_y, snake_len));

    drawFood(food_x, food_y, true);
}

static void resetGame(void)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    score = 0;
    snake_len = SNAKE_START_LEN;
    dir_x = 1;
    dir_y = 0;

    uint8_t start_x = SNAKE_GRID_W / 2;
    uint8_t start_y = SNAKE_GRID_H / 2;

    for (uint16_t i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
        drawCell(snake_x[i], snake_y[i], true);
    }

    placeFood();

    isPaused = false;
    isGameOver = false;
}

static void setDirection(int8_t dx, int8_t dy)
{
    if (dx == -dir_x && dy == -dir_y) {
        return; // no reverse
    }

    dir_x = dx;
    dir_y = dy;
}

static bool stepSnake(void)
{
    int16_t next_x = (int16_t)snake_x[0] + dir_x;
    int16_t next_y = (int16_t)snake_y[0] + dir_y;

    if (next_x < 0 || next_x >= SNAKE_GRID_W || next_y < 0 || next_y >= SNAKE_GRID_H) {
        return false;
    }

    bool grow = ((uint8_t)next_x == food_x && (uint8_t)next_y == food_y);
    uint16_t limit = grow ? snake_len : (snake_len > 0 ? snake_len - 1 : 0);

    if (cellOnSnake((uint8_t)next_x, (uint8_t)next_y, limit)) {
        return false;
    }

    if (!grow && snake_len > 0) {
        drawCell(snake_x[snake_len - 1], snake_y[snake_len - 1], false);
    }

    if (grow && snake_len < SNAKE_MAX_LEN) {
        snake_len++;
    }

    for (uint16_t i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }

    snake_x[0] = (uint8_t)next_x;
    snake_y[0] = (uint8_t)next_y;

    drawCell(snake_x[0], snake_y[0], true);

    if (grow) {
        score++;
        drawFood(food_x, food_y, false);
        placeFood();
        isBeep = true;
        tone = 600;
    }

    return true;
}

static void showPause(bool show)
{
    if (show) {
        UI_PrintStringSmallBold("PAUSE", 0, 128, 4);
        return;
    }

    for (uint8_t i = 0; i < 8; i++) {
        UI_DrawLineBuffer(gFrameBuffer, 32, 32 + i, 96, 32 + i, false);
    }
}

static void showGameOver(void)
{
    UI_PrintStringSmallBold("GAME", 32, 0, 4);
    UI_PrintStringSmallBold("OVER", 66, 0, 4);
}

static void OnKeyDown(uint8_t key)
{
    bool wasPaused = isPaused;

    switch (key) {
    case KEY_2:
    case KEY_UP:
        if (!isPaused) {
            setDirection(0, -1);
        }
        break;
    case KEY_8:
    case KEY_DOWN:
        if (!isPaused) {
            setDirection(0, 1);
        }
        break;
    case KEY_4:
        if (!isPaused) {
            setDirection(-1, 0);
        }
        break;
    case KEY_0:
    case KEY_6:
        if (!isPaused) {
            setDirection(1, 0);
        }
        break;
    case KEY_MENU:
        if (isGameOver) {
            resetGame();
        } else {
            isPaused = !isPaused;
            showPause(isPaused);
        }
        kbd.counter = 0;
        SYSTEM_DelayMs(200);
        break;
    case KEY_EXIT:
        isPaused = false;
        isInitialized = false;
        break;
    default:
        break;
    }

    if (wasPaused == true && isPaused == false) {
        showPause(false);
    }
}

static KEY_Code_t GetKey(void)
{
    KEY_Code_t btn = KEYBOARD_Poll();
    if (btn == KEY_INVALID && GPIO_IsPttPressed()) {
        btn = KEY_PTT;
    }
    return btn;
}

static void HandleUserInput(void)
{
    kbd.prev = kbd.current;
    kbd.current = GetKey();

    if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
        kbd.counter = 1;
    } else {
        kbd.counter = 0;
    }

    if (kbd.counter == 1) {
        OnKeyDown(kbd.current);
    }
}

static void Tick(void)
{
    HandleUserInput();
    HandleUserInput();
}

void APP_RunSnake(void)
{
    uint8_t frame = 0;

    srand_custom((BK4819_ReadRegister(BK4819_REG_67) & 0x01FF) * gBatteryVoltageAverage * gEeprom.VfoInfo[0].pRX->Frequency);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    resetGame();
    isInitialized = true;

    while (isInitialized) {
        Tick();

        if (!isPaused) {
            if (!stepSnake()) {
                isGameOver = true;
                isPaused = true;
                showGameOver();
            }

            if (isBeep) {
                playBeep(tone);
                isBeep = false;
            }

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            if ((frame++ & 0x03) == 0) {
                getScreenShot(false);
            }
#endif
        }

        drawScore();

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();

        if (!isPaused) {
            uint16_t speedup = MIN(score * 3, (SNAKE_BASE_DELAY_MS - SNAKE_MIN_DELAY_MS));
            uint16_t delay = SNAKE_BASE_DELAY_MS - speedup;
            if (delay < SNAKE_MIN_DELAY_MS) {
                delay = SNAKE_MIN_DELAY_MS;
            }
            SYSTEM_DelayMs(delay);
        } else {
            SYSTEM_DelayMs(30);
        }
    }
}
